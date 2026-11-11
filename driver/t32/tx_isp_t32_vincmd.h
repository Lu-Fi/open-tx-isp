/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 video-input proc command show of the vendor module:
 * video_input_cmd_show prints, for each of the 4 sensor slots, the slot
 * header and the slot's last command result text (d9460 + 128 * id); an
 * empty text is first set to "null" (sprintf "%s").
 * seq_printf / sprintf go through struct t32_vincmd_ops.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_vincmd_oracle).
 */
#ifndef TX_ISP_T32_VINCMD_H
#define TX_ISP_T32_VINCMD_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_VINCMD_SLOTS	4
#define T32_VINCMD_TEXT		128

struct t32_vincmd_ops {
	int (*seq_printf)(void *seq, const char *fmt, ...);
	int (*sprintf)(char *buf, const char *fmt, ...);
};

/* texts: d9460, 4 x 128 bytes */
static inline int t32_video_input_cmd_show(const struct t32_vincmd_ops *k, void *seq, char *texts)
{
	u32 i;

	for (i = 0; i < T32_VINCMD_SLOTS; i++) {
		char *t = texts + T32_VINCMD_TEXT * i;

		k->seq_printf(seq, "------------>> sensor id: %d <<------------\n", i);
		if (!strlen(t))
			k->sprintf(t, "%s", "null");
		k->seq_printf(seq, "%s\n", t);
	}
	return 0;
}

#endif /* TX_ISP_T32_VINCMD_H */
