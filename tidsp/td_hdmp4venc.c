/*
 * Copyright (C) 2009-2010 Felipe Contreras
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "dsp_bridge.h"
#include "dmm_buffer.h"

#include "gstdspbase.h"
#include "gstdspvenc.h"

#include "td_mp4venc_common.h"

struct create_args {
	uint32_t size;
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;

	uint32_t width;
	uint32_t height;
	uint32_t bitrate;
	uint32_t vbv_size;
	uint32_t gob_interval;

	uint8_t is_mpeg4;
	uint8_t color_format;
	uint8_t hec;
	uint8_t resync_marker;
	uint8_t data_part;
	uint8_t reversible_vlc;
	uint8_t unrestricted_mv;
	uint8_t framerate;
	uint8_t rate_control;
	uint8_t qp_first;
	uint8_t profile;
	uint8_t level;
};

static void create_args(GstDspBase *base, unsigned *profile_id, void **arg_data)
{
	GstDspVEnc *self = GST_DSP_VENC(base);
	gint vbv;

	struct create_args args = {
		.size = sizeof(args) - 4,
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.width = self->width,
		.height = self->height,
		.bitrate = self->bitrate,
		.gob_interval = self->keyframe_interval * self->framerate,
		.is_mpeg4 = 1,
		.color_format = (self->color_format == GST_MAKE_FOURCC('U','Y','V','Y') ? 1 : 0),
		.unrestricted_mv = 1,
		.framerate = self->framerate,
		.qp_first = 5,
	};

	if (self->mode == 0)
		args.rate_control = 3;
	else
		args.rate_control = 0;

	args.is_mpeg4 = base->alg == GSTDSP_HDMP4VENC ? 1 : 0;

	if (base->alg == GSTDSP_HDMP4VENC) {
		/* additional framerate and resolution checks for level-0 */
		if (self->level == 0 && (self->framerate > 15 || self->width > 176 || self->height > 144))
			self->level = 2;
		/* for qcif 15fps @ 128kpbs */
		if (self->level == 0 && self->bitrate > 64000)
			/* setting level=10 indicates level 0b*/
			self->level = 10;

		switch (self->level) {
		case 0:
		case 1:
			vbv = 10; break;
		case 10:
			vbv = 20; break;
		case 2:
		case 3:
			vbv = 40; break;
		case 4:
			vbv = 80; break;
		case 5:
			vbv = 112; break;
		case 6:
			vbv = 248; break;
		default:
			vbv = 0; break;
		}
	} else {
		/* additional framerate/resolution checks with each level
		 * are due to their restriction as per standard
		 */
		switch (self->level) {
		case 10:
		case 45:
			if (self->framerate > 15)
				self->level = 20;
			break;
		case 20:
			if (self->width > 176 && self->framerate > 15)
				self->level = 30;
			if (self->width <= 176 && self->framerate > 30)
				self->level = 50;
			break;
		case 30:
		case 40:
			if (self->framerate > 30)
				self->level = 60;
			break;
		case 50:
			if (self->width > 176 && self->framerate > 50)
				self->level = 60;
			break;
		default:
			break;
		}

		vbv = (self->level == 20) ? 16 : 32;
	}

	args.level = self->level;
	args.vbv_size = vbv;

	self->priv.mpeg4.vbv_size = vbv;

	*profile_id = 0;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

struct in_params {
	uint32_t frame_index;
	uint32_t framerate;
	uint32_t bitrate;
	uint32_t i_frame_interval;
	uint32_t generate_header;
	uint32_t force_i_frame;

	uint32_t resync_interval;
	uint32_t hec_interval;
	uint32_t air_rate;
	uint32_t mir_rate;
	uint32_t qp_intra;
	uint32_t f_code;
	uint32_t half_pel;
	uint32_t ac_pred;
	uint32_t mv;
};

struct out_params {
	uint32_t bitstream_size;
	uint8_t frame_type;
	uint8_t skip_frame;
	int32_t ext_error_code;
};

static void out_recv_cb(GstDspBase *base, struct td_buffer *tb)
{
	dmm_buffer_t *b = tb->data;
	struct out_params *param;
	param = tb->params->data;
	if (XDM_ERROR_IS_FATAL(param->ext_error_code)) {
		pr_err(base, "invalid i/p params or insufficient o/p buf size");
		g_atomic_int_set(&base->status, GST_FLOW_ERROR);
	}

	tb->keyframe = (param->frame_type == 1);
	if (base->alg == GSTDSP_HDMP4VENC)
		td_mp4venc_try_extract_extra_data(base, b);

	if (G_UNLIKELY(param->skip_frame))
		b->skip = TRUE;
}

static void in_send_cb(GstDspBase *base, struct td_buffer *tb)
{
	struct in_params *param;
	GstDspVEnc *self = GST_DSP_VENC(base);

	param = tb->params->data;
	param->frame_index = g_atomic_int_exchange_and_add(&self->frame_index, 1);
	g_mutex_lock(self->keyframe_mutex);
	param->force_i_frame = self->keyframe_event ? 1 : 0;
	if (self->keyframe_event) {
		gst_pad_push_event(base->srcpad, self->keyframe_event);
		self->keyframe_event = NULL;
	}
	g_mutex_unlock(self->keyframe_mutex);
}

static void setup_in_params(GstDspBase *base, dmm_buffer_t *tmp)
{
	struct in_params *in_param;
	GstDspVEnc *self = GST_DSP_VENC(base);

	in_param = tmp->data;
	in_param->framerate = self->framerate;
	in_param->bitrate = self->bitrate;
	in_param->i_frame_interval = 15;
	in_param->resync_interval = 1024;
	in_param->hec_interval = 3;
	in_param->air_rate = 10;
	in_param->qp_intra = 10;
	in_param->f_code = 5;
	in_param->half_pel = 1;
	in_param->mv = 1;
}

static void setup_params(GstDspBase *base)
{
	struct in_params *in_param;
	struct out_params *out_param;
	GstDspVEnc *self = GST_DSP_VENC(base);
	du_port_t *p;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), setup_in_params);
	p->send_cb = in_send_cb;

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
	p->recv_cb = out_recv_cb;

	base->output_buffer_size = (self->priv.mpeg4.vbv_size * 2048) +
				   (self->bitrate / self->framerate);
}

struct td_codec td_hdmp4venc_codec = {
	.uuid = &(const struct dsp_uuid) { 0x34f881de, 0xefad, 0x4a7e, 0x8b, 0xe0,
		{ 0xa6, 0x06, 0x8b, 0xdc, 0x9b, 0x1e } },
	.filename = "m4vhdenc_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
};
