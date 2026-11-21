#include "tx-isp-core-local.h"

/*
 * jxf23_t20 (and most other T20 sensors) never populate
 * attr->one_line_expr_in_us, so the vendor GetExpr handler always reports
 * line_us = 0 to userspace.  The sensor source isn't in this tree to patch
 * directly, so rename the vendor v4l2 entry point and wrap it: if the
 * active sensor left the field at 0, derive the line period from the
 * current timing instead (line time = 1e6 / (fps * vts) rounded to the
 * nearest us, t20_expr_fallback_line_us()) and patch just that field in
 * the copy already sent to userspace.  The setter (SetExpr MANUAL) divides
 * by the same integer line time (openimp #744), see tx-isp-expr-time.h.
 */
#include "tx-isp-expr-time.h"
#define isp_core_ops_g_ctrl isp_core_ops_g_ctrl_vendor
#include "source/apical-isp/tx-isp-core-tuning.c"
#undef isp_core_ops_g_ctrl

/* openimp #744: the same integer line time the setter divides by
 * (tx-isp-expr-time.h), so GetExpr lines x line_us round-trips exactly. */
static unsigned short t20_core_fallback_one_line_expr_in_us(struct tx_isp_core_device *core)
{
	struct tx_isp_sensor_attribute *attr = core->vin.attr;

	if (!attr)
		return 0;
	return (unsigned short)t20_expr_fallback_line_us(attr->total_height,
							 core->vin.fps);
}

int isp_core_ops_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct tx_isp_core_device *core = sd_to_tx_isp_core_device(sd);
	struct tx_isp_sensor_attribute *attr;
	unsigned short line_us;
	int ret;

	ret = isp_core_ops_g_ctrl_vendor(sd, ctrl);
	if (ret)
		return ret;

	if (ctrl->id != IMAGE_TUNING_CID_EXPR_ATTR)
		return ret;

	attr = core->vin.attr;
	if (attr && attr->one_line_expr_in_us == 0) {
		line_us = t20_core_fallback_one_line_expr_in_us(core);
		if (line_us) {
			union isp_core_expr_attr __user *uattr =
				(union isp_core_expr_attr __user *)ctrl->value;

			if (copy_to_user(&uattr->g_attr.one_line_expr_in_us,
					  &line_us, sizeof(line_us)))
				return -EFAULT;
		}
	}
	return ret;
}
