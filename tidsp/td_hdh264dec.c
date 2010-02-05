/*
 * Copyright (C) 2010 Nokia Corporation
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "dsp_bridge.h"
#include "dmm_buffer.h"
#include "td_h264dec_common.h"

#include "gstdspbase.h"
#include "gstdspvdec.h"
#include "gstdspparse.h"

struct create_args {
	uint32_t size;
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;

	uint16_t reserved;

	uint32_t max_width;
	uint32_t max_height;
	uint32_t color_format;
	uint32_t max_framerate;
	uint32_t max_bitrate;
	uint32_t endianness;
	uint32_t profile;
	int32_t max_level;
	uint32_t mode;
	int32_t preroll;
	uint32_t stream_format;
	uint32_t display_width;
	uint32_t ref_frames;
	uint32_t reordering;
};

static unsigned get_heap_profile(GstDspVDec *self)
{
	int heap_size, frame_size, dpb_frame_size, width, height;
	const int padding = 32 * 2; /* padding */

	width = ROUND_UP(self->width, 16);
	height = ROUND_UP(self->height, 16);

	frame_size = (width + padding) * (height + padding);
	if ((self->profile == 77 || self->profile == 100) &&
			self->priv.h264.initial_height) {
		height = ROUND_UP(self->priv.h264.initial_height, 16);
		frame_size = (width + padding) * (height + padding / 8);
	}
	heap_size = frame_size * (self->priv.h264.ref_frames + 1) * 3 / 2;

	/* add stream nal buffer */
	heap_size += 0x100000;
	/* dpb frames */
	dpb_frame_size = width * height * 3 / 2;
	/* add non-referable frames */
	heap_size += dpb_frame_size * 16;
	/* add motion information */
	heap_size += 160 * (width / 16) * (height / 16) *
		(self->priv.h264.ref_frames + 1);
	/* row level mb hdr info */
	heap_size += (width / 16) * 4 * 180;

	return heap_size / 0x400000;
}

static void create_args(GstDspBase *base, unsigned *profile_id, void **arg_data)
{
	GstDspVDec *self = GST_DSP_VDEC(base);

	struct create_args args = {
		.size = sizeof(args) - 4,
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.max_width = self->width,
		.max_height = self->height,
		.color_format = 1,
		.max_bitrate = -1,
		.endianness = 1,
		.max_level = 8,
		.ref_frames = self->priv.h264.ref_frames,
		.reordering = 1,
	};

	args.color_format = self->color_format == GST_MAKE_FOURCC('U', 'Y', 'V', 'Y') ? 1 : 0;

	/* disable frame reordering(dpb) in streaming mode */
	if (self->mode == 1)
		args.reordering = 0;

	if (self->profile == 77 || self->profile == 100) {
		if (self->priv.h264.initial_height)
			args.max_height = self->priv.h264.initial_height;
		if (!args.reordering)
			pr_warning(self, "streaming mode can cause out of order frames in MP/HP");
	} else {
		/* disable reordering on resolutions above WVGA */
		if (((self->width * self->height) >> 8) > 1590)
			args.reordering = 0;
	}

	*profile_id = get_heap_profile(self);

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

struct in_params {
	int32_t buff_count;
	uint32_t num_of_nalu;
	uint8_t stream_format;
	uint8_t length_of_length;
	uint32_t usr_arg;
	uint8_t qos;
};

struct out_params {
	uint32_t display_id;
	uint32_t bytes_consumed;
	int32_t error_code;
	uint32_t decoded_frame_type;
	uint32_t num_of_nalu_decoded;
	uint32_t frame_width;
	uint32_t frame_height;
	uint32_t usr_arg;
	int32_t skip_frame;
	int32_t ext_error_code;
};

static void out_recv_cb(GstDspBase *base, struct td_buffer *tb)
{
	dmm_buffer_t *b = tb->data;
	struct out_params *param;
	param = tb->params->data;

	if (param->error_code == -1) {
		pr_err(base, "buffer error");
		g_atomic_int_set(&base->status, GST_FLOW_ERROR);
	}

	if (G_UNLIKELY(param->skip_frame))
		b->skip = TRUE;

	tb->keyframe = (param->decoded_frame_type == 0);
}

static void in_send_cb(GstDspBase *base, struct td_buffer *tb)
{
	GstDspVDec *self;
	struct in_params *param;
	self = GST_DSP_VDEC(base);
	param = tb->params->data;
	param->qos = g_atomic_int_get(&base->qos);

	param->length_of_length = self->priv.h264.lol;

	param->stream_format = self->priv.h264.hd_h264_streamtype;
	param->buff_count = g_atomic_int_exchange_and_add(&self->frame_index, 1);
}

static void setup_params(GstDspBase *base)
{
	struct in_params *in_param;
	struct out_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), NULL);
	p->send_cb = in_send_cb;
	base->pre_process_buffer = td_h264dec_check_stream_params;

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
	p->recv_cb = out_recv_cb;
}

static void hdh264_flush_buffers(GstDspBase *base)
{
	gint i;

	for (i = 0; i < 15; i++) {
		struct td_buffer *tb;
		tb = async_queue_pop(base->ports[0]->queue);
		if (!tb)
			return;
		dmm_buffer_allocate(tb->data, 1);
		base->send_buffer(base, tb);
	}
}

static bool handle_extra_data(GstDspBase *base, GstBuffer *buf)
{
	bool res;

	GstDspVDec *self = GST_DSP_VDEC(base);

	buf = td_h264dec_transform_extra_data(self, buf);
	if (!buf) {
		gstdsp_got_error(base, 0, "invalid codec_data");
		return false;
	}
	self->priv.h264.hd_h264_streamtype = (self->priv.h264.is_avc == TRUE);
	res = gstdsp_send_codec_data(base, buf);
	gst_buffer_unref(buf);
	return res;
}

static unsigned get_latency(GstDspBase *base, unsigned frame_duration)
{
	GstDspVDec *vdec = GST_DSP_VDEC(base);

	/* 15 frames of buffering if poc_type != 2 */
	if (vdec->priv.h264.poc_type != 2)
		return 15 * frame_duration;

	return 0;
}

struct td_codec td_hdh264dec_bp_codec = {
	.uuid = &(const struct dsp_uuid) { 0x1d0e6707, 0x47da, 0x40eb, 0xa4, 0xb6,
		{ 0x25, 0xe9, 0x6a, 0x20, 0xd4, 0x34 } },
	.filename = "h264bphddec_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
	.flush_buffer = hdh264_flush_buffers,
	.handle_extra_data = handle_extra_data,
	.get_latency = get_latency,
};

struct td_codec td_hdh264dec_hp_codec = {
	.uuid = &(const struct dsp_uuid) { 0x27fc9b2a, 0xc769, 0x4c4a, 0xa7, 0xd7,
		{ 0x61, 0x68, 0xc1, 0x7e, 0x06, 0xdf } },
	.filename = "h264hpdec_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
	.flush_buffer = hdh264_flush_buffers,
	.handle_extra_data = handle_extra_data,
	.get_latency = get_latency,
};
