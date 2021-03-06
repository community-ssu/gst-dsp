/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef UTIL_H
#define UTIL_H

#include "dsp_bridge.h"
#include "dmm_buffer.h"

struct _GstBuffer;

bool gstdsp_register(int dsp_handle,
		     const struct dsp_uuid *uuid,
		     int type,
		     const char *filename);

bool gstdsp_map_buffer(void *self,
		struct _GstBuffer *buf,
		dmm_buffer_t *b);

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#define CODEC_DIR "/lib/dsp/"

static inline gboolean sn_exist(const char *filename)
{
	gboolean res;
	gchar  * codec_fname=g_build_filename(CODEC_DIR,filename,NULL);
	res = g_file_test(codec_fname,G_FILE_TEST_EXISTS);
	g_free(codec_fname);

	return res;
}

#endif /* UTIL_H */
