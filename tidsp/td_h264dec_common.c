/*
 * Copyright (C) 2009-2010 Felipe Contreras
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Authors:
 * Felipe Contreras <felipe.contreras@gmail.com>
 * Juha Alanen <juha.m.alanen@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "td_h264dec_common.h"
#include "gstdspparse.h"

void td_h264dec_check_stream_params(GstDspBase *self, GstBuffer *buf)
{
	GstDspVDec *vdec = GST_DSP_VDEC(self);

	/* Use a fake vdec to get width and height (if any) */
	GstDspVDec helper = *vdec;
	GstCaps *new_caps = gst_caps_copy(GST_PAD_CAPS(self->sinkpad));

	(GST_DSP_BASE(&helper))->tmp_caps = new_caps;

	if (vdec->width != 0 && vdec->height != 0 &&
			gst_dsp_h264_parse(GST_DSP_BASE(&helper), buf))
	{
		if (helper.width != vdec->width ||
				helper.height != vdec->height)
		{
			/* generate new caps */
			pr_debug(self, "frame size changed from %dx%d to %dx%d\n",
					vdec->width,
					vdec->height,
					helper.width,
					helper.height);

			gst_caps_set_simple(new_caps,
					"width", G_TYPE_INT, helper.width,
					"height", G_TYPE_INT, helper.height, NULL);

			gst_pad_set_caps(self->sinkpad, new_caps);
		}
	}
	gst_caps_unref(new_caps);
}

GstBuffer *td_h264dec_transform_extra_data(GstDspVDec *self, GstBuffer *buf)
{
	guint8 *data, *outdata;
	guint size, total_size = 0, len, num_sps, num_pps;
	guint lol;
	guint val;
	guint i;
	GstBuffer *new;

	/* extract some info and transform into codec expected format, which is:
	 * lol bytes (BE) SPS size, SPS, lol bytes (BE) PPS size, PPS */

	data = GST_BUFFER_DATA(buf);
	size = GST_BUFFER_SIZE(buf);

	if (size < 8)
		goto fail;

	val = GST_READ_UINT32_BE(data);
	if (val == 1 || (val >> 8) == 1) {
		pr_debug(self, "codec_data in byte-stream format not transformed");
		return gst_buffer_ref(buf);
	}

	lol = (data[4] & (0x3)) + 1;
	num_sps = data[5] & 0x1f;
	data += 6;
	size -= 6;
	for (i = 0; i < num_sps; i++) {
		len = GST_READ_UINT16_BE(data);
		total_size += len + lol;
		data += len + 2;
		if (size < len + 2)
			goto fail;
		size -= len + 2;
	}
	num_pps = data[0];
	data++;
	size++;
	for (i = 0; i < num_pps; i++) {
		len = GST_READ_UINT16_BE(data);
		total_size += len + lol;
		data += len + 2;
		if (size < len + 2)
			goto fail;
		size -= len + 2;
	}

	/* save original data */
	new = gst_buffer_new_and_alloc(total_size);
	data = GST_BUFFER_DATA(buf);
	outdata = GST_BUFFER_DATA(new);

	data += 6;
	for (i = 0; i < num_sps; ++i) {
		len = GST_READ_UINT16_BE(data);
		val = len << (8 * (4 - lol));
		GST_WRITE_UINT32_BE(outdata, val);
		memcpy(outdata + lol, data + 2, len);
		outdata += len + lol;
		data += 2 + len;
	}
	data += 1;
	for (i = 0; i < num_pps; ++i) {
		len = GST_READ_UINT16_BE(data);
		val = len << (8 * (4 - lol));
		GST_WRITE_UINT32_BE(outdata, val);
		memcpy(outdata + lol, data + 2, len);
		outdata += len + lol;
		data += 2 + len;
	}

	pr_debug(self, "lol: %d", lol);
	self->priv.h264.lol = lol;

	return new;

fail:
	pr_warning(self, "failed to transform h264 to codec format");
	return NULL;
}
