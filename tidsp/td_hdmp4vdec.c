/*
 * Copyright (C) 2010 Nokia Corporation
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "dsp_bridge.h"
#include "dmm_buffer.h"

#include "gstdspbase.h"
#include "gstdspvdec.h"

#include "td_hdcodec.h"

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
	uint32_t display_width;
	uint32_t is_h263;
};

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
		.color_format = 4,
		.max_bitrate = -1,
		.endianness = 1,
		.max_level = 8,
	};

	args.color_format = self->color_format == GST_MAKE_FOURCC('U', 'Y', 'V', 'Y') ? 4 : 1;
	args.is_h263 = (self->profile == 1) ? 1 : 0;

	*profile_id = 4;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

struct in_params {
	int32_t frame_index;
	uint32_t usr_arg;
};

struct out_params {
	uint32_t display_id;
	uint32_t bytes_consumed;
	int32_t error_code;
	uint32_t frame_type;
	uint32_t usr_arg;
	int32_t ext_error_code;
	int32_t skip_frame;
};

static void out_recv_cb(GstDspBase *base, struct td_buffer *tb)
{
	dmm_buffer_t *b = tb->data;
	struct out_params *param;
	param = tb->params->data;
	if (G_LIKELY(tb->user_data)) {
		GstBuffer *buffer = tb->user_data;
		GST_BUFFER_TIMESTAMP(buffer) = base->ts_array[param->usr_arg].time;
		GST_BUFFER_DURATION(buffer) = base->ts_array[param->usr_arg].duration;
	}

	handle_hdcodec_error(base, param->error_code, param->ext_error_code);

	if (G_UNLIKELY(param->skip_frame))
		b->skip = TRUE;
	else
		b->skip = FALSE;

	tb->keyframe = (param->frame_type == 0);
}

static void in_send_cb(GstDspBase *base, struct td_buffer *tb)
{
	struct in_params *param;
	param = tb->params->data;
	param->usr_arg = tb->data->ts_index;

	param->frame_index = g_atomic_int_exchange_and_add(&param->frame_index, 1);
}

static void setup_params(GstDspBase *base)
{
	struct in_params *in_param;
	struct out_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), NULL);
	p->send_cb = in_send_cb;

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
	p->recv_cb = out_recv_cb;
}

struct td_codec td_hdmp4vdec_codec = {
	.uuid = &(const struct dsp_uuid) { 0x4343bbdb, 0xd7c2, 0x4ac8, 0xb1, 0x19,
		{ 0xf1, 0x7a, 0x9e, 0x5a, 0x33, 0xee } },
	.filename = "m4vhddec_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
	.flush_buffer = gstdsp_base_flush_buffer,
	.ts_mode = TS_MODE_CHECK_IN,
};
