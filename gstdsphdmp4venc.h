/*
 * Copyright (C) 2009 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_HD_MP4VENC_H
#define GST_DSP_HD_MP4VENC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_HD_MP4VENC(obj) (GstHdDspMp4VEnc *)(obj)
#define GST_DSP_HD_MP4VENC_TYPE (gst_dsp_hd_mp4venc_get_type())
#define GST_DSP_HD_MP4VENC_CLASS(obj) (GstHdDspMp4VEncClass *)(obj)

typedef struct _GstDspHdMp4VEnc GstDspHdMp4VEnc;
typedef struct _GstDspHdMp4VEncClass GstDspHdMp4VEncClass;

#include "gstdspvenc.h"

struct _GstDspHdMp4VEnc {
	GstDspVEnc element;
};

struct _GstDspHdMp4VEncClass {
	GstDspVEncClass parent_class;
};

GType gst_dsp_hd_mp4venc_get_type(void);

G_END_DECLS

#endif /* GST_DSP_HD_MP4VENC_H */
