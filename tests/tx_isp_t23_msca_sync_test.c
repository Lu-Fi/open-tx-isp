/*
 * Host test for the T23 MSCA output lifetime rules
 * (driver/t23/tx_isp_t23_msca_sync.h, README "Output/channel restart hang").
 *
 * Besides the helpers it runs a small model of one output through the
 * sequences seen on cam-B (openimp on-demand wakes: REQBUFS, QBUF,
 * STREAMON, STREAMOFF, close + pool free; a timps process restart; a
 * second channel streaming on) with the driver's default parameters and
 * checks the invariants:
 *  - no DMA into a freed buffer (enabled output with frames flowing never
 *    holds a freed address, and nothing is enabled after its buffers went);
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

struct model {
	/* hardware */
	u32 d040;
	u32 d050;
	int fifo[8];		/* buffer ids, -1 none */
	int fifo_n;
	int last;		/* last address written, -1 = zero (cleared) */
	int flowing;		/* input running, frame-done IRQs */
	unsigned int d010;	/* update requests */
	unsigned int cfg_loads;
	unsigned int live_d040_writes;	/* d040 written mid-frame */
	/* driver state */
	struct t23_msca_pend pend;
	int ch_en;
	int kept;
	int live_valid;
	int cfg_equal;
	int buf_owned;
	/* userspace */
	int pool_valid[POOL];
	unsigned int bad_dma;
};

static void m_reset(struct model *m)
{
	memset(m, 0, sizeof(*m));
	m->cfg_equal = 1;
	m->last = -1;
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
		if (buf < 0 || !m->pool_valid[buf])
			m->bad_dma++;
		/* userspace DQBUF + QBUF while the channel streams */
		else if (m->ch_en && m->fifo_n < 8)
			m->fifo[m->fifo_n++] = buf;
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

static void m_release(struct model *m)
{
	if (m->ch_en)
		return;
	if (m->d040 & 1U) {
		m_d040_sync(m, 0, 1U);
		m_frame(m);
	}
	m->kept = 0;
	m_fifo_clear(m);
}

static void m_reqbufs(struct model *m)
{
	int i;

	m_release(m);
	for (i = 0; i < POOL; i++)
		m->pool_valid[i] = 1;
	m->buf_owned = 1;
}

static void m_qbuf_all(struct model *m)
{
	int i;

	for (i = 0; i < POOL; i++)
		m->fifo[m->fifo_n++] = i;
}

static void m_input(struct model *m, int on)
{
	m->flowing = on;
	if (!on)
		m_apply_pending(m, 0);
}

static void m_streamon(struct model *m)
{
	int input_was_off = !m->flowing;
	int reuse = t23_msca_restart_mode(1, m->live_valid, m->cfg_equal) ==
		    T23_MSCA_REUSE;

	if (t23_msca_fifo_clear_allowed(!!(m->d040 & 1U), m->flowing)) {
		/* rearm: clear and push the queued buffers again */
		m->fifo_n = 0;
		m_qbuf_all(m);
	}
	if (input_was_off && reuse)
		m_d040_sync(m, 1U, 0);
	if (input_was_off)
		m_input(m, 1);
	if (!(input_was_off && reuse)) {
		if (reuse) {
			if (!(m->d040 & 1U))
				m_d040_sync(m, 1U, 0);
		} else {
			if (m->flowing)
				m_frame(m);	/* boundary before the load */
			m->cfg_loads++;
			m->d010++;
			m_write_d040(m, m->d040 | 1U, 1);
			m->live_valid = 1;
			m->cfg_equal = 1;
		}
	}
	m->ch_en = 1;
	m->kept = 0;
}

/* last == the input stops with this STREAMOFF */
static void m_streamoff(struct model *m, int last, int keep_param)
{
	m->ch_en = 0;
	if (t23_msca_stop_keeps_bit(keep_param, last)) {
		m->kept = !!(m->d040 & 1U);
	} else if (m->d040 & 1U) {
		m_d040_sync(m, 0, 1U);
		m_frame(m);
	}
	if (last) {
		m_frame(m);	/* drain the frame in flight */
		m_input(m, 0);
	}
}

static void m_close_and_free(struct model *m)
{
	int i;

	if (m->buf_owned)
		m_release(m);
	m->buf_owned = 0;
	for (i = 0; i < POOL; i++)
		m->pool_valid[i] = 0;
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
	/* session start: tx-isp STREAMON runs the input, first channel start */
	m_input(&m, 1);
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
		CHECK(!(m.d040 & 1U));
		CHECK(m.fifo_n == 0);
		/* idle phase: the input is stopped */
		for (f = 0; f < 5; f++)
			m_frame(&m);
		/* next wake */
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

/* process restart: the old pool goes away, the new session starts */
static void test_process_restart(void)
{
	struct model m;
	int f;

	m_reset(&m);
	m_input(&m, 1);
	m_reqbufs(&m);
	m_qbuf_all(&m);
	m_streamon(&m);
	for (f = 0; f < 6; f++)
		m_frame(&m);
	/* timps killed: close without STREAMOFF -> stream off, release */
	m_streamoff(&m, 1, 1);
	m_close_and_free(&m);
	CHECK(!(m.d040 & 1U) && !m.kept);
	/* new process: tx-isp STREAMON before the channel has buffers */
	m_input(&m, 1);
	for (f = 0; f < 6; f++)
		m_frame(&m);
	m_reqbufs(&m);
	m_qbuf_all(&m);
	m_streamon(&m);
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
	m_input(&m, 1);
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
	CHECK(m.cfg_loads == 1);
}

/* msca_keep_enabled=2 (stock) without release would write freed memory */
static void test_keep_without_release_is_unsafe(void)
{
	struct model m;
	int f;

	m_reset(&m);
	m_input(&m, 1);
	m_reqbufs(&m);
	m_qbuf_all(&m);
	m_streamon(&m);
	m_streamoff(&m, 0, 2);
	CHECK(m.d040 & 1U);
	/* pool freed without the release (what d28a0177 did on close) */
	memset(m.pool_valid, 0, sizeof(m.pool_valid));
	for (f = 0; f < 8; f++)
		m_frame(&m);
	CHECK(m.bad_dma > 0);
}

int main(void)
{
	test_helpers();
	test_wakes();
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
