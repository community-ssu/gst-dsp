/*
 * Copyright (C) 2009 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_HD_H264ENC_H
#define GST_DSP_HD_H264ENC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_HD_H264ENC(obj) (GstDspHdH264Enc *)(obj)
#define GST_DSP_HD_H264ENC_TYPE (gst_dsp_hd_h264enc_get_type())
#define GST_DSP_HD_H264ENC_CLASS(obj) (GstDspHdH264EncClass *)(obj)

typedef struct _GstDspHdH264Enc GstDspHdH264Enc;
typedef struct _GstDspHdH264EncClass GstDspHdH264EncClass;

#include "gstdspvenc.h"

struct _GstDspHdH264Enc {
	GstDspVEnc element;
};

struct _GstDspHdH264EncClass {
	GstDspVEncClass parent_class;
};

GType gst_dsp_hd_h264enc_get_type(void);

G_END_DECLS

#endif /* GST_DSP_HD_H264ENC_H */
