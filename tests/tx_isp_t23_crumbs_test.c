/* Host test for the T23 hang crumb page layout (driver/t23/tx_isp_t23_crumbs.h). */
#include <stdio.h>
#include <string.h>

#include "../driver/t23/tx_isp_t23_crumbs.h"

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

int main(void)
{
	static u32 page[T23_CRUMB_PAGE_BYTES / 4];
	u32 word = 0;
	u32 when = 0;
	u32 i;

	/* layout fits the page */
	CHECK(T23_CRUMB_RING + T23_CRUMB_RING_LEN * 2 <= T23_CRUMB_PAGE_BYTES / 4);
	CHECK(T23_CRUMB_W_MASK < T23_CRUMB_RING);

	memset(page, 0xa5, sizeof(page));
	CHECK(!t23_crumb_valid(page));
	t23_crumb_reset(page, 1234);
	CHECK(t23_crumb_valid(page));
	CHECK(page[T23_CRUMB_W_SESSION] == 1234);
	CHECK(page[T23_CRUMB_W_SEQ] == 0);
	CHECK(page[T23_CRUMB_PAGE_BYTES / 4 - 1] == 0);
	CHECK(t23_crumb_entry(page, 0, &word, &when) == -1);

	t23_crumb_mark(page, T23C_CHAN_ON, 1, 100);
	CHECK(page[T23_CRUMB_W_SEQ] == 1);
	CHECK(page[T23_CRUMB_W_STEP] == (T23C_CHAN_ON | (1U << 16)));
	CHECK(page[T23_CRUMB_W_JIFFIES] == 100);
	CHECK(t23_crumb_entry(page, 0, &word, &when) == 0);
	CHECK(word == (T23C_CHAN_ON | (1U << 16)) && when == 100);
	CHECK(t23_crumb_entry(page, 1, &word, &when) == -1);

	/* wrap: the newest 64 steps stay, oldest first out */
	for (i = 0; i < 200; i++)
		t23_crumb_mark(page, T23C_FLIP_SKIP, i, 1000 + i);
	CHECK(page[T23_CRUMB_W_SEQ] == 201);
	CHECK(t23_crumb_entry(page, 0, &word, &when) == 0);
	CHECK(word == (T23C_FLIP_SKIP | (199U << 16)) && when == 1199);
	CHECK(t23_crumb_entry(page, 63, &word, &when) == 0);
	CHECK(word == (T23C_FLIP_SKIP | (136U << 16)) && when == 1136);
	CHECK(t23_crumb_entry(page, 64, &word, &when) == -1);
	/* the ring never touches the header or the end of the page */
	CHECK(page[T23_CRUMB_W_MAGIC] == T23_CRUMB_MAGIC);
	CHECK(page[T23_CRUMB_W_SESSION] == 1234);
	CHECK(page[T23_CRUMB_RING + T23_CRUMB_RING_LEN * 2] == 0);

	if (failures) {
		fprintf(stderr, "tx_isp_t23_crumbs_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("tx_isp_t23_crumbs_test: ok\n");
	return 0;
}
