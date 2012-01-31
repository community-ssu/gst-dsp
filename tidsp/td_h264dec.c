/*
 * Copyright (C) 2009-2010 Felipe Contreras
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Authors:
 * Felipe Contreras <felipe.contreras@gmail.com>
 * Juha Alanen <juha.m.alanen@nokia.com>
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
		.color_format = self->color_format == GST_MAKE_FOURCC('U', 'Y', 'V', 'Y') ? 1 : 0,
		.max_bitrate = -1,
		.endianness = 1,
		.max_level = -1,
	};

	if (self->width * self->height > 352 * 288)
		*profile_id = 3;
	else if (self->width * self->height > 176 * 144)
		*profile_id = 2;
	else
		*profile_id = 1;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

static void transform_nal_encoding(GstDspVDec *self, struct td_buffer *tb)
{
	guint8 *data;
	gint size;
	gint lol;
	guint val, nal;
	dmm_buffer_t *b = tb->data;

	data = b->data;
	size = b->len;
	lol = self->priv.h264.lol;

	nal = 0;
	while (size) {
		if (size < lol)
			goto fail;

		/* get NAL size encoded in BE lol bytes */
		val = GST_READ_UINT32_BE(data);
		val >>= ((4 - lol) << 3);
		if (lol == 4)
			/* blank size prefix with 00 00 00 01 */
			GST_WRITE_UINT32_BE(data, 0x01);
		else if (lol == 3)
			/* blank size prefix with 00 00 01 */
			GST_WRITE_UINT24_BE(data, 0x01);
		else
			nal++;
		data += lol + val;
		size -= lol + val;
	}

	if (lol < 3) {
		/* slower, but unlikely path; need to copy stuff to make room for sync */
		guint8 *odata, *alloc_data;
		gint osize;

		/* set up for next run */
		data = b->data;
		size = b->len;
		osize = size + nal * (4 - lol);
		/* save this so it is not free'd by subsequent allocate */
		alloc_data = b->allocated_data;
		b->allocated_data = NULL;
		dmm_buffer_allocate(b, osize);

		odata = b->data;
		while (size) {
			if (size < lol)
				goto fail;

			/* get NAL size encoded in BE lol bytes */
			val = GST_READ_UINT32_BE(data);
			val >>= ((4 - lol) << 3);
			GST_WRITE_UINT32_BE(odata, 0x01);
			odata += 4;
			data += lol;
			memcpy(odata, data, val);
			odata += val;
			data += val;
			size -= lol + val;
		}
		/* now release original data */
		if (tb->user_data) {
			gst_buffer_unref(tb->user_data);
			tb->user_data = NULL;
		}
		free(alloc_data);
	}
	return;

fail:
	pr_warning(self, "failed to transform h264 to codec format");
	return;
}

struct out_params {
	uint32_t display_id;
	uint32_t bytes_consumed;
	int32_t error_code;
	uint32_t frame_type;
	uint32_t num_of_nalu;
	int32_t mb_err_status_flag;
	int8_t mb_err_status_out[1620];
};

static void out_recv_cb(GstDspBase *base, struct td_buffer *tb)
{
	GstDspVDec *vdec = GST_DSP_VDEC(base);
	dmm_buffer_t *b = tb->data;
	struct out_params *param;
	param = tb->params->data;

	pr_debug(base, "receive %zu/%ld",
			b->len, base->output_buffer_size);
	pr_debug(base, "error: 0x%x, frame type: %d",
			param->error_code, param->frame_type);
	if (param->error_code & 0xffff)
		pr_err(base, "decode error");

	gstdsp_vdec_len_fixup(vdec, b);
}

static void in_send_cb(GstDspBase *base, struct td_buffer *tb)
{
	GstDspVDec *vdec = GST_DSP_VDEC(base);
	/* transform MP4 format to bytestream format */
	if (G_LIKELY(vdec->priv.h264.lol)) {
		pr_debug(base, "transforming H264 buffer data");
		/* intercept and transform into dsp expected format */
		transform_nal_encoding(vdec, tb);
	} else {
		/* no more need for callback */
		tb->port->send_cb = NULL;
	}
}

static void setup_params(GstDspBase *base)
{
	struct out_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	p->send_cb = in_send_cb;
	base->pre_process_buffer = td_h264dec_check_stream_params;

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
	p->recv_cb = out_recv_cb;
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
	res = gstdsp_send_codec_data(base, buf);
	gst_buffer_unref(buf);
	return res;
}

struct td_codec td_h264dec_codec = {
	.uuid = &(const struct dsp_uuid) { 0xCB1E9F0F, 0x9D5A, 0x4434, 0x84, 0x49,
		{ 0x1F, 0xED, 0x2F, 0x99, 0x2D, 0xF7 } },
	.filename = "h264vdec_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
	.handle_extra_data = handle_extra_data,
};
