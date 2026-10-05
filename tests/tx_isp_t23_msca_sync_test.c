/*
 * Host test for the T23 MSCA output lifetime rules
 * (driver/t23/tx_isp_t23_msca_sync.h, README "Output/channel restart hang").
 *
 * Besides the helpers it runs a small model of one output through the
 * sequences seen on cam-B (openimp on-demand wakes: REQBUFS, QBUF,
 * STREAMON, STREAMOFF, close + pool free; a timps process restart; a
 * second channel streaming on) with the driver's default parameters and
 * checks the invariants:
 *  - no DMA into a freed buffer (the input never starts under an enabled
 *    output whose FIFO still holds addresses of a freed pool);
 *  - the FIFO of an enabled output is never cleared while frames flow;
 *  - an unchanged restart issues no cfg load and no 0xd010 request;
 *  - 0xd040 changes while frames flow happen only at a frame boundary.
 */
#include <stdio.h>
#include <string.h>

#include "../driver/t23/tx_isp_t23_msca_sync.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

/* ---- model ------------------------------------------------------------ */

#define POOL 4
#define FIFO_MAX 32

struct model {
	/* hardware */
	u32 d040;
	u32 d050;
	int fifo[FIFO_MAX];	/* buffer ids (generation * POOL + index) */
	int fifo_n;
	int last;		/* last address written, -1 = zero (cleared) */
	int flowing;		/* input running, frame-done IRQs */
	unsigned int d010;	/* update requests */
	unsigned int cfg_loads;
	unsigned int live_d040_writes;	/* d040 written mid-frame */
	/* driver state */
	struct t23_msca_pend pend;
	int ch_en;
	int live_valid;
	int cfg_equal;
	int queued;		/* buffers of the current pool queued */
	int deferred;		/* start waits for the first QBUF */
	/* userspace */
	int gen;		/* pool generation */
	int pool_alive;
	unsigned int bad_dma;
};

static void m_reset(struct model *m)
{
	memset(m, 0, sizeof(*m));
	m->cfg_equal = 1;
	m->last = -1;
}

static int m_valid(const struct model *m, int buf)
{
	return buf >= 0 && m->pool_alive && buf / POOL == m->gen;
}

static void m_write_d040(struct model *m, u32 v, int at_boundary)
{
	if (m->flowing && !at_boundary && v != m->d040)
		m->live_d040_writes++;
	m->d040 = v;
}

static void m_apply_pending(struct model *m, int at_boundary)
{
	if (m->pend.set || m->pend.clr)
		m_write_d040(m, t23_msca_pend_take_d040(&m->pend, m->d040),
			     at_boundary);
	if (m->pend.flip) {
		m->pend.flip = 0;
		m->d050 = m->pend.flip_word;
		m->d010++;
	}
}

static void m_push(struct model *m, int buf)
{
	if (m->fifo_n < FIFO_MAX)
		m->fifo[m->fifo_n++] = buf;
}

/* one frame: the MSCA writes into the FIFO head (or its last address) */
static void m_frame(struct model *m)
{
	if (!m->flowing)
		return;
	if (m->d040 & 1U) {
		int buf = m->fifo_n ? m->fifo[0] : -1;

		if (m->fifo_n) {
			memmove(m->fifo, m->fifo + 1,
				(size_t)(m->fifo_n - 1) * sizeof(int));
			m->fifo_n--;
		}
		if (buf < 0)
			buf = m->last;	/* empty FIFO: the last address again */
		else
			m->last = buf;
		if (!m_valid(m, buf))
			m->bad_dma++;
		else if (m->ch_en)	/* DQBUF + QBUF while streaming */
			m_push(m, buf);
	}
	/* frame-done ISR */
	if (t23_msca_pend_any(&m->pend))
		m_apply_pending(m, 1);
}

static void m_d040_sync(struct model *m, u32 set, u32 clr)
{
	t23_msca_pend_bits(&m->pend, set, clr);
	if (!m->flowing)
		m_apply_pending(m, 0);
	else
		m_frame(m);	/* the caller waits for the boundary */
}

static void m_fifo_clear(struct model *m)
{
	CHECK(t23_msca_fifo_clear_allowed(!!(m->d040 & 1U), m->flowing));
	m->fifo_n = 0;
	m->last = -1;
}

/* regtrace_t23_msca_release() */
static void m_release(struct model *m)
{
	if (m->ch_en)
		return;
	if (m->d040 & 1U) {
		m_d040_sync(m, 0, 1U);
		m_frame(m);
	}
	m_fifo_clear(m);
}

static void m_input_on(struct model *m)
{
	CHECK(!(m->d040 & 1U) || m->fifo_n == 0 || m_valid(m, m->fifo[0]));
	m->flowing = 1;
}

static void m_input_off(struct model *m)
{
	m_frame(m);	/* drain the frame in flight */
	m->flowing = 0;
	m_apply_pending(m, 0);
}

/* tx-isp STREAMON of a new session */
static void m_session_start(struct model *m)
{
	if (!m->flowing) {
		if (m->d040 & 1U)
			m_release(m);
		m_input_on(m);
	}
}

static void m_reqbufs(struct model *m)
{
	if (m->flowing)
		m_release(m);
	m->gen++;
	m->pool_alive = 1;
	m->queued = 0;
}

static void m_msca_on(struct model *m);

static void m_qbuf_all(struct model *m)
{
	int i;

	for (i = 0; i < POOL; i++)
		m_push(m, m->gen * POOL + i);
	m->queued = POOL;
	if (m->deferred)
		m_msca_on(m);
}

/* regtrace_t23_set_msca_stream(enable) */
static void m_msca_on(struct model *m)
{
	m->deferred = 0;
	if (!(m->d040 & 1U) && m->fifo_n == 0 && m->last < 0) {
		m->deferred = 1;	/* empty FIFO: wait for QBUF */
		return;
	}
	if ((m->d040 & 1U) &&
	    t23_msca_restart_mode(1, m->live_valid, m->cfg_equal) ==
	    T23_MSCA_REUSE)
		return;		/* nothing written */
	if (m->d040 & 1U)
		m_d040_sync(m, 0, 1U);
	m->cfg_loads++;
	m->d010++;
	m->d040 |= 1U;		/* stock-exact start */
	m->live_valid = 1;
	m->cfg_equal = 1;
}

static void m_streamon(struct model *m)
{
	unsigned int pushed = 0;
	int i;

	/* rearm */
	if (m->queued &&
	    t23_msca_fifo_clear_allowed(!!(m->d040 & 1U), m->flowing)) {
		m->fifo_n = 0;
		for (i = 0; i < m->queued; i++)
			m_push(m, m->gen * POOL + i);
		pushed = (unsigned int)m->queued;
	}
	if (!m->flowing) {
		/* release_before_input */
		if ((m->d040 & 1U) && !pushed)
			m_release(m);
		m_input_on(m);
	}
	m->ch_en = 1;
	m_msca_on(m);
}

/* last == the input stops with this STREAMOFF */
static void m_streamoff(struct model *m, int last, int keep_param)
{
	m->ch_en = 0;
	if (!t23_msca_stop_keeps_bit(keep_param, last) && (m->d040 & 1U)) {
		m_d040_sync(m, 0, 1U);
		m_frame(m);
	}
	if (last)
		m_input_off(m);
}

static void m_close_and_free(struct model *m)
{
	if (m->flowing)
		m_release(m);
	m->pool_alive = 0;
	m->queued = 0;
}

static void m_flip(struct model *m, u32 word)
{
	if (t23_msca_flip_queue(&m->pend, m->d050, word, 1) && !m->flowing)
		m_apply_pending(m, 0);
}

/* ---- tests ------------------------------------------------------------ */

static void test_helpers(void)
{
	struct t23_msca_pend p;

	memset(&p, 0, sizeof(p));
	CHECK(!t23_msca_pend_any(&p));
	t23_msca_pend_bits(&p, 1U, 0);
	t23_msca_pend_bits(&p, 0, 1U);	/* newer request wins */
	CHECK(p.set == 0 && p.clr == 1U);
	t23_msca_pend_bits(&p, 2U, 0);
	CHECK(t23_msca_pend_take_d040(&p, 0x7U) == 0x6U);
	CHECK(!p.set && !p.clr);

	CHECK(t23_msca_flip_queue(&p, 0x10, 0x10, 1) == 0);
	CHECK(t23_msca_flip_queue(&p, 0x10, 0x10, 0) == 1);
	p.flip = 0;
	CHECK(t23_msca_flip_queue(&p, 0x10, 0x20, 1) == 1);
	CHECK(t23_msca_flip_effective(&p, 0x10) == 0x20);
	/* re-send of the pending word is a no-op too */
	CHECK(t23_msca_flip_queue(&p, 0x10, 0x20, 1) == 0);

	CHECK(!t23_msca_stop_keeps_bit(0, 1));
	CHECK(t23_msca_stop_keeps_bit(1, 1));
	CHECK(!t23_msca_stop_keeps_bit(1, 0));
	CHECK(t23_msca_stop_keeps_bit(2, 0));

	CHECK(t23_msca_restart_mode(1, 1, 1) == T23_MSCA_REUSE);
	CHECK(t23_msca_restart_mode(0, 1, 1) == T23_MSCA_RELOAD);
	CHECK(t23_msca_restart_mode(1, 0, 1) == T23_MSCA_RELOAD);
	CHECK(t23_msca_restart_mode(1, 1, 0) == T23_MSCA_RELOAD);

	CHECK(!t23_msca_fifo_clear_allowed(1, 1));
	CHECK(t23_msca_fifo_clear_allowed(1, 0));
	CHECK(t23_msca_fifo_clear_allowed(0, 1));

	CHECK(t23_msca_frames_since(5, 4, 1));
	CHECK(!t23_msca_frames_since(4, 4, 1));
	CHECK(t23_msca_frames_since(1, 0xffffffffU, 2));
}

/* timps on-demand wakes, openimp DisableChn = STREAMOFF + close + free */
static void test_wakes(void)
{
	struct model m;
	unsigned int loads, d010;
	int i, f;

	m_reset(&m);
	m_session_start(&m);
	m_reqbufs(&m);
	m_qbuf_all(&m);
	m_streamon(&m);
	CHECK(m.cfg_loads == 1);
	m_flip(&m, 0);		/* timps HVFLIP re-send, unchanged */
	for (f = 0; f < 10; f++)
		m_frame(&m);
	loads = m.cfg_loads;
	d010 = m.d010;

	for (i = 0; i < 30; i++) {
		m_streamoff(&m, 1, 1);
		m_close_and_free(&m);
		CHECK(m.d040 & 1U);	/* kept, input stopped */
		for (f = 0; f < 5; f++)
			m_frame(&m);
		m_reqbufs(&m);
		m_qbuf_all(&m);
		m_streamon(&m);
		m_flip(&m, 0);
		for (f = 0; f < 12; f++)
			m_frame(&m);
	}
	CHECK(m.cfg_loads == loads);
	CHECK(m.d010 == d010);
	CHECK(m.bad_dma == 0);
	CHECK(m.live_d040_writes == 0);
}

/* STREAMON without queued buffers after a kept stop: released, reloaded */
static void test_wake_without_buffers(void)
{
	struct model m;
	int f;

	m_reset(&m);
	m_session_start(&m);
	m_reqbufs(&m);
	m_qbuf_all(&m);
	m_streamon(&m);
	m_streamoff(&m, 1, 1);
	m_close_and_free(&m);
	m_reqbufs(&m);
	m_streamon(&m);		/* nothing queued yet: deferred */
	CHECK(m.deferred && !(m.d040 & 1U));
	for (f = 0; f < 3; f++)
		m_frame(&m);
	m_qbuf_all(&m);		/* first QBUF starts the output */
	CHECK(!m.deferred && (m.d040 & 1U));
	for (f = 0; f < 6; f++)
		m_frame(&m);
	CHECK(m.bad_dma == 0);
	CHECK(m.cfg_loads == 2);
}

/* process restart: the old pool goes away, the new session starts */
static void test_process_restart(void)
{
	struct model m;
	int f;

	m_reset(&m);
	m_session_start(&m);
	m_reqbufs(&m);
	m_qbuf_all(&m);
	m_streamon(&m);
	for (f = 0; f < 6; f++)
		m_frame(&m);
	/* timps killed: close -> stream off (input stops), pool freed */
	m_streamoff(&m, 1, 1);
	m_close_and_free(&m);
	CHECK(m.d040 & 1U);
	/* new process: tx-isp STREAMON before the channel has buffers */
	m_session_start(&m);
	CHECK(!(m.d040 & 1U));
	for (f = 0; f < 6; f++)
		m_frame(&m);
	m_reqbufs(&m);
	m_qbuf_all(&m);
	m_streamon(&m);		/* stock-exact load */
	CHECK(m.cfg_loads == 2);
	for (f = 0; f < 6; f++)
		m_frame(&m);
	CHECK(m.bad_dma == 0);
	CHECK(m.live_d040_writes == 0);
}

/* the input keeps running (another channel streams): live stop/start */
static void test_live_restart(void)
{
	struct model m;
	int i, f;

	m_reset(&m);
	m_session_start(&m);
	m_reqbufs(&m);
	m_qbuf_all(&m);
	m_streamon(&m);
	for (i = 0; i < 10; i++) {
		for (f = 0; f < 3; f++)
			m_frame(&m);
		m_streamoff(&m, 0, 1);
		CHECK(!(m.d040 & 1U));
		m_close_and_free(&m);
		for (f = 0; f < 3; f++)
			m_frame(&m);
		m_reqbufs(&m);
		m_qbuf_all(&m);
		m_streamon(&m);
	}
	m_flip(&m, 0x00070000U);	/* real change: at a boundary */
	CHECK(m.pend.flip);
	m_frame(&m);
	CHECK(!m.pend.flip && m.d050 == 0x00070000U);
	CHECK(m.bad_dma == 0);
	CHECK(m.live_d040_writes == 0);
}

/* d28a0177: kept output, new session input start without release */
static void test_keep_without_release_is_unsafe(void)
{
	struct model m;
	int f;

	m_reset(&m);
	m_session_start(&m);
	m_reqbufs(&m);
	m_qbuf_all(&m);
	m_streamon(&m);
	m_streamoff(&m, 1, 1);
	m_close_and_free(&m);
	m.flowing = 1;		/* input started, output not released */
	for (f = 0; f < 8; f++)
		m_frame(&m);
	CHECK(m.bad_dma > 0);
}

int main(void)
{
	test_helpers();
	test_wakes();
	test_wake_without_buffers();
	test_process_restart();
	test_live_restart();
	test_keep_without_release_is_unsafe();
	if (failures) {
		fprintf(stderr, "tx_isp_t23_msca_sync_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("tx_isp_t23_msca_sync_test: ok\n");
	return 0;
}
