/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "plugin.h"
#include "util.h"

#include "gstdspdummy.h"
#include "gstdspvdec.h"
#include "gstdspadec.h"
#include "gstdsph263enc.h"
#include "gstdspjpegenc.h"
#include "gstdsph264enc.h"
#include "gstdspvpp.h"
#include "gstdspipp.h"
#include "gstdsphdmp4venc.h"
#include "gstdspmp4venc.h"
#include "gstdsphdh264enc.h"

GstDebugCategory *gstdsp_debug;

static gboolean
plugin_init(GstPlugin *plugin)
{
#ifndef GST_DISABLE_GST_DEBUG
	gstdsp_debug = _gst_debug_category_new("dsp", 0, "DSP stuff");
#endif

	if (!gst_element_register(plugin, "dspdummy", GST_RANK_NONE, GST_DSP_DUMMY_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dspvdec", GST_RANK_PRIMARY + 1, GST_DSP_VDEC_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dsph263enc", GST_RANK_PRIMARY + 1, GST_DSP_H263ENC_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dspjpegenc", GST_RANK_PRIMARY + 1, GST_DSP_JPEGENC_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dsph264enc", GST_RANK_PRIMARY + 1, GST_DSP_H264ENC_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dspvpp", GST_RANK_PRIMARY, GST_DSP_VPP_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dspipp", GST_RANK_PRIMARY + 1, GST_DSP_IPP_TYPE))
		return FALSE;

	if(sn_exist("m4vhdenc_sn.dll64P"))
	{
		if (!gst_element_register(plugin, "dspmp4venc", GST_RANK_PRIMARY + 1, GST_DSP_HD_MP4VENC_TYPE))
			return FALSE;
	}
	else
	{
		if (!gst_element_register(plugin, "dspmp4venc", GST_RANK_PRIMARY + 1, GST_DSP_MP4VENC_TYPE))
			return FALSE;
	}

	if(sn_exist("h264bpenc_sn.dll64P"))
		if (!gst_element_register(plugin, "dsphdh264enc", GST_RANK_SECONDARY, GST_DSP_HD_H264ENC_TYPE))
			return FALSE;

	return TRUE;
}

GstPluginDesc gst_plugin_desc = {
	.major_version = 0,
	.minor_version = 10,
	.name = "dsp",
	.description = (gchar *) "Texas Instruments DSP elements",
	.plugin_init = plugin_init,
	.version = VERSION,
	.license = "LGPL",
	.source = "gst-dsp",
	.package = "none",
	.origin = "none",
};
