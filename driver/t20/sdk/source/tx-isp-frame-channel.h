/*
 * Driver-owned copy of external/ingenic-sdk/3.10.14/isp/t20/
 * tx-isp-frame-channel.h.  The frame channel sources in this directory
 * include it by a quoted name, so this copy wins over the SDK one; the
 * 3.10 layout is unchanged, only the >= 4.4 buffer layout differs.
 */
#ifndef __TX_ISP_FRAME_CHANNEL_H__
#define __TX_ISP_FRAME_CHANNEL_H__

#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-core.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
#include <media/videobuf2-v4l2.h>
#endif

#include <tx-isp-common.h>

struct frame_channel_format {
	unsigned char name[32];
	unsigned int fourcc;
	unsigned int depth;
	unsigned int priv;
};

struct tx_isp_frame_channel_video_device;

typedef struct tx_isp_frame_channel_video_device frame_chan_vdev_t;

struct frame_channel_attribute {
/* Private: internal use only */
	unsigned short max_width;
	unsigned short max_height;
	unsigned short min_width;
	unsigned short min_height;
	unsigned short step_width;
	unsigned short step_height;
	struct v4l2_rect bounds;
	struct frame_image_scalercap scalercap;

/* Public: user could set it through special ioctl */
	struct v4l2_rect crop;
	struct frame_image_scaler scaler;
	struct v4l2_format output;
	unsigned int crop_enable :1;
	unsigned int scaler_enable :1;
	unsigned int lineoffset;
	int frame_rate;
};

struct frame_channel_buffer {
	struct list_head entry;
	unsigned int addr;
	void *priv;
};

struct frame_channel_video_buffer{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	/*
	 * Since 4.4 the vb2 v4l2 layer treats every buffer as a struct
	 * vb2_v4l2_buffer (to_vb2_v4l2_buffer()) and stores flags, field,
	 * timecode, sequence and planes[] right behind the vb2_buffer.  Keep
	 * that room, or QBUF overwrites buf.entry (flags/field) and the
	 * "never link twice" check of openimp #467 sees every buffer as
	 * already queued.
	 */
	union {
		struct vb2_buffer vb;
		struct vb2_v4l2_buffer vb_v4l2;
	};
	struct frame_channel_buffer buf;
	/* 7.x frame channel: the v4l2_buffer view and sequence counter */
	struct v4l2_buffer v4l2_buf;
	u32 sequence;
#else
	struct vb2_buffer vb;
	struct frame_channel_buffer buf;
#endif
	struct list_head entry;
};

struct frame_channel_fh {
	enum v4l2_priority prio;
};
struct tx_isp_frame_channel_video_device {
	/* the paramters have been inited before it is registered */
	struct v4l2_subdev *parent;
	struct frame_vb2_manager *vbm;
	struct frame_channel_attribute attr;
	int index;
	int bypass;

	/* the parameters will be inited when it is registered */
	struct video_device *video;
	struct vb2_queue vbq;
	struct list_head active_list;
	spinlock_t slock;
	struct mutex mlock;
	atomic_t state;
	struct completion comp;
	int (*interrupt_service_routine)(frame_chan_vdev_t *chan, u32 status, bool *handled);
	unsigned int out_frames;
	unsigned int lose_frames;
	struct v4l2_fmtdesc *fmtdesc;
	struct v4l2_format *fmt;
	int reqbufs;
};

int tx_isp_frame_channel_device_register(frame_chan_vdev_t *chan);
void tx_isp_frame_channel_device_unregister(frame_chan_vdev_t *chan);

#endif/*__TX_ISP_FRAME_CHANNEL_H__*/
