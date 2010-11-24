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

#include "td_mp4venc_common.h"

void td_mp4venc_try_extract_extra_data(GstDspBase *base, dmm_buffer_t *b)
{
	GstDspVEnc *self = GST_DSP_VENC(base);
	guint8 gov[] = { 0x0, 0x0, 0x1, 0xB3 };
	guint8 vop[] = { 0x0, 0x0, 0x1, 0xB6 };
	guint8 *data;
	GstBuffer *codec_buf;

	if (G_LIKELY(self->priv.mpeg4.codec_data_done))
		return;

	if (!b->len)
		return;

	/* only mind codec-data for storage */
	if (self->mode)
		goto done;

	/*
	 * Codec data expected in first frame,
	 * and runs from VOSH to GOP (not including); so locate the latter one.
	 */
	data = memmem(b->data, b->len, gov, 4);

	if (!data) {
		/* maybe no GOP is in the stream, look for first VOP */
		data = memmem(b->data, b->len, vop, 4);
	}

	if (!data) {
		pr_err(self, "failed to extract mpeg4 codec-data");
		goto done;
	}

	codec_buf = gst_buffer_new_and_alloc(data - (guint8 *) b->data);
	memcpy(GST_BUFFER_DATA(codec_buf), b->data, GST_BUFFER_SIZE(codec_buf));
	gstdsp_set_codec_data_caps(base, codec_buf);
	gst_buffer_unref(codec_buf);
done:
	self->priv.mpeg4.codec_data_done = TRUE;
}
