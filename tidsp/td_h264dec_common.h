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

#ifndef TD_H264DEC_COMMON_H
#define TD_H264DEC_COMMON_H

#include "gstdspvdec.h"

void td_h264dec_check_stream_params(GstDspBase *self, GstBuffer *buf);
GstBuffer *td_h264dec_transform_extra_data(GstDspVDec *vdec, GstBuffer *buf);

#endif
