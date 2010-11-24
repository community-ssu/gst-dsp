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

#ifndef TD_MP4VENC_COMMON_H
#define TD_MP4VENC_COMMON_H

#include "gstdspvenc.h"

void td_mp4venc_try_extract_extra_data(GstDspBase *base, dmm_buffer_t *b);

#endif
