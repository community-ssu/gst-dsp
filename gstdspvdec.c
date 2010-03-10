/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Authors:
 * Felipe Contreras <felipe.contreras@gmail.com>
 * Juha Alanen <juha.m.alanen@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspvdec.h"
#include "gstdspparse.h"
#include "plugin.h"
#include "util.h"

#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

static GstDspBaseClass *parent_class;

#define td_jpegdec_codec td_fake_codec

enum {
	ARG_0,
	ARG_MODE,
};

#define DEFAULT_MODE 0

#define GST_TYPE_DSPVDEC_MODE gst_dspvdec_mode_get_type()
static GType
gst_dspvdec_mode_get_type(void)
{
	static GType gst_dspvdec_mode_type;

	if (!gst_dspvdec_mode_type) {
		static GEnumValue modes[] = {
			{0, "Playback", "playback"},
			{1, "Streaming", "streaming"},
			{0, NULL, NULL},
		};

		gst_dspvdec_mode_type = g_enum_register_static("GstDspVDecMode", modes);

	}

	return gst_dspvdec_mode_type;

}

static inline GstCaps *
generate_sink_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/mpeg",
				  "mpegversion", G_TYPE_INT, 4,
				  "systemstream", G_TYPE_BOOLEAN, FALSE,
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-divx",
				  "divxversion", GST_TYPE_INT_RANGE, 4, 5,
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-xvid",
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-3ivx",
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-h263",
				  "variant", G_TYPE_STRING, "itu",
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-h264", "alignment", G_TYPE_STRING, "au",
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-wmv",
				  "wmvversion", G_TYPE_INT, 3,
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

static inline GstCaps *
generate_src_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/x-raw-yuv",
				  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'),
				  NULL);

	gst_caps_append_structure(caps, struc);
	struc = gst_structure_new("video/x-raw-yuv",
				  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('I', '4', '2', '0'),
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

void gstdsp_vdec_len_fixup(GstDspVDec *self, dmm_buffer_t *b)
{
	/* let the videosink know the real size */
	if (self->color_format == GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'))
		b->len = self->crop_width * self->crop_height * 2;
	else
		b->len = self->crop_width * self->crop_height * 3 / 2;
}

static bool
check_force_codec(GstDspVDec *self)
{
	GstDspBase *base = GST_DSP_BASE(self);
	const char *codec;

	codec = g_getenv(CODEC_ENV_VAR);
	if (!codec)
		return false;

	pr_info(self, "codec choice %s", codec);
	if (strcmp(codec, "HD") == 0) {
		switch (base->alg) {
		case GSTDSP_MPEG4VDEC:
		case GSTDSP_H263DEC:
			base->alg = GSTDSP_HDMPEG4VDEC;
			base->codec = &td_hdmp4vdec_codec;
			break;
		case GSTDSP_H264DEC:
			base->alg = GSTDSP_HDH264VDEC;
			break;
		default:
			break;
		}
	} else if (strcmp(codec, "HP") == 0) {
		switch (base->alg) {
		case GSTDSP_H264DEC:
			base->alg = GSTDSP_HDH264VDEC;
			base->codec = &td_hdh264dec_hp_codec;
			break;
		default:
			break;
		}
	} else {
		switch (base->alg) {
		case GSTDSP_HDMPEG4VDEC:
			base->alg = GSTDSP_MPEG4VDEC;
			base->codec = &td_mp4vdec_codec;
			break;
		case GSTDSP_HDH264VDEC:
			base->alg = GSTDSP_H264DEC;
			base->codec = &td_h264dec_codec;
			break;
		default:
			break;
		}
	}

	return true;
}

static void
codec_fixup(GstDspVDec *self)
{
	GstDspBase *base = GST_DSP_BASE(self);

	switch (base->alg) {
	case GSTDSP_MPEG4VDEC:
		if (self->width * self->height > 864 * 480) {
			base->alg = GSTDSP_HDMPEG4VDEC;
			base->codec = &td_hdmp4vdec_codec;
		}
		break;
	case GSTDSP_H263DEC:
		if (self->profile == 1) {
			base->alg = GSTDSP_HDMPEG4VDEC;
			base->codec = &td_hdmp4vdec_codec;
		}
		break;
	default:
		break;
	}
}

static void *
create_node(GstDspBase *base)
{
	GstDspVDec *self;
	struct td_codec *codec;
	int dsp_handle;
	struct dsp_node *node;

	const struct dsp_uuid usn_uuid = { 0x79A3C8B3, 0x95F2, 0x403F, 0x9A, 0x4B,
		{ 0xCF, 0x80, 0x57, 0x73, 0x05, 0x41 } };

	const struct dsp_uuid ringio_uuid = { 0x47698bfb, 0xa7ee, 0x417e, 0xa6, 0x7a,
		{ 0x41, 0xc0, 0x27, 0x9e, 0xb8, 0x05 } };

	const struct dsp_uuid conversions_uuid = { 0x722DD0DA, 0xF532, 0x4238, 0xB8, 0x46,
		{ 0xAB, 0xFF, 0x5D, 0xA4, 0xBA, 0x02 } };

	self = GST_DSP_VDEC(base);
	dsp_handle = base->dsp_handle;

	if (!gstdsp_register(dsp_handle, &ringio_uuid, DSP_DCD_LIBRARYTYPE, "ringio.dll64P")) {
		pr_err(self, "failed to register ringio node library");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, &usn_uuid, DSP_DCD_LIBRARYTYPE, "usn.dll64P")) {
		pr_err(self, "failed to register usn node library");
		return NULL;
	}

	/* codec fixups */
	if (!check_force_codec(self))
		codec_fixup(self);

	/* profile selection */
	/* only if it hasn't been forced */
	if (!base->codec) {
		switch (base->alg) {
		case GSTDSP_HDH264VDEC:
			if (self->profile == 77 || self->profile == 100)
				base->codec = &td_hdh264dec_hp_codec;
			else
				base->codec = &td_hdh264dec_bp_codec;
			break;
		default:
			break;
		}
	}

	codec = base->codec;
	if (!codec) {
		pr_err(self, "unknown algorithm");
		return NULL;
	}

	pr_info(base, "algo=%s", codec->filename);

	/* Register conversions library only for TI codecs */
	if (!(base->alg == GSTDSP_HDMPEG4VDEC || base->alg == GSTDSP_HDH264VDEC)) {
		/* SN_API == 0 doesn't have it, so don't fail */
		(void) gstdsp_register(dsp_handle, &conversions_uuid, DSP_DCD_LIBRARYTYPE, "conversions.dll64P");
	}

	if (!gstdsp_register(dsp_handle, codec->uuid, DSP_DCD_LIBRARYTYPE, codec->filename)) {
		pr_err(self, "failed to register algo node library");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, codec->uuid, DSP_DCD_NODETYPE, codec->filename)) {
		pr_err(self, "failed to register algo node");
		return NULL;
	}

	{
		struct dsp_node_attr_in attrs = {
			.cb = sizeof(attrs),
			.priority = 5,
			.timeout = 1000,
		};
		void *arg_data;

		codec->create_args(base, &attrs.profile_id, &arg_data);

		if (!arg_data)
			return NULL;

		if (!dsp_node_allocate(dsp_handle, base->proc, codec->uuid, arg_data, &attrs, &node)) {
			pr_err(self, "dsp node allocate failed");
			free(arg_data);
			return NULL;
		}
		free(arg_data);
	}

	if (!dsp_node_create(dsp_handle, node)) {
		pr_err(self, "dsp node create failed");
		dsp_node_free(dsp_handle, node);
		return NULL;
	}

	pr_info(self, "dsp node created");

	if (codec->setup_params)
		codec->setup_params(base);

	if (codec->send_params)
		codec->send_params(base, node);

	base->flush_buffer = codec->flush_buffer;
	base->ts_mode = codec->ts_mode;

	return node;
}

static void save_codec_data(GstDspBase *base, GstStructure *in_struc)
{
	const GValue *codec_data;
	GstBuffer *buf;

	codec_data = gst_structure_get_value(in_struc, "codec_data");
	if (!codec_data)
		return;

	buf = gst_value_get_buffer(codec_data);
	if (!buf)
		return;

	gst_buffer_unref(base->codec_data);
	base->codec_data = gst_buffer_ref(buf);
}

static inline void
configure_caps(GstDspVDec *self,
	       GstCaps *in,
	       GstCaps *out)
{
	GstDspBase *base;
	GstCaps *allowed_caps;
	GstStructure *out_struc, *in_struc;
	const GValue *aspect_ratio;
	bool i420_is_valid = true;

	base = GST_DSP_BASE(self);

	in_struc = gst_caps_get_structure(in, 0);

	out_struc = gst_structure_new("video/x-raw-yuv",
				      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'),
				      NULL);

	gst_structure_get_int(in_struc, "width", &self->width);
	gst_structure_get_int(in_struc, "height", &self->height);

	self->crop_width = self->width;
	self->crop_height = self->height;

	gst_structure_set(out_struc, "width", G_TYPE_INT, self->crop_width, NULL);
	gst_structure_set(out_struc, "height", G_TYPE_INT, self->crop_height, NULL);

	aspect_ratio = gst_structure_get_value(in_struc, "pixel-aspect-ratio");
	if (aspect_ratio)
		gst_structure_set_value(out_struc, "pixel-aspect-ratio", aspect_ratio);

	/* estimate the real coded framesize */
	self->width = ROUND_UP(self->width, 16);
	self->height = ROUND_UP(self->height, 16);

	base->output_buffer_size = self->width * self->height * 2;
	self->color_format = GST_MAKE_FOURCC('U', 'Y', 'V', 'Y');

	/* in jpegdec I420 is only possible if the image has that chroma */
	if (base->alg == GSTDSP_JPEGDEC) {
		guint32 color_format;
		if (gst_structure_get_fourcc(in_struc, "format", &color_format))
			i420_is_valid = (color_format == GST_MAKE_FOURCC('I', '4', '2', '0'));
		else
			i420_is_valid = false; /* we don't know the chroma */
	}

	allowed_caps = gst_pad_get_allowed_caps(base->srcpad);
	if (allowed_caps) {
		if (gst_caps_get_size(allowed_caps) > 0) {
			GstStructure *s;
			guint32 color_format;
			s = gst_caps_get_structure(allowed_caps, 0);
			if (gst_structure_get_fourcc(s, "format", &color_format)) {
				if (color_format == GST_MAKE_FOURCC('I', '4', '2', '0')
				    && i420_is_valid) {
					self->color_format = color_format;
					base->output_buffer_size = self->width * self->height * 3 / 2;
					gst_structure_set(out_struc, "format", GST_TYPE_FOURCC,
							  GST_MAKE_FOURCC('I', '4', '2', '0'), NULL);
				}
			}
		}
		gst_caps_unref(allowed_caps);
	}

	{
		const GValue *framerate = NULL;
		framerate = gst_structure_get_value(in_struc, "framerate");
		if (framerate)
			gst_structure_set_value(out_struc, "framerate", framerate);
		else
			/* FIXME this is a workaround for xvimagesink */
			gst_structure_set(out_struc, "framerate",
					  GST_TYPE_FRACTION, 0, 1, NULL);
		base->default_duration = 0;
		if (framerate) {
			guint fps_num = 0, fps_den = 0;

			fps_num = gst_value_get_fraction_numerator(framerate);
			fps_den = gst_value_get_fraction_denominator(framerate);
			if (fps_num && fps_den)
				base->default_duration = gst_util_uint64_scale(GST_SECOND, fps_den, fps_num);
		}
	}

	gst_caps_append_structure(out, out_struc);
}

static inline gboolean need_node_reset(GstDspVDec *self, GstCaps *new_caps)
{
	gint width, height;
	GstStructure *struc;
	GstDspBase *base = GST_DSP_BASE(self);

	if (G_UNLIKELY(!base->node))
		return FALSE;

	struc = gst_caps_get_structure(new_caps, 0);
	gst_structure_get_int(struc, "width", &width);
	gst_structure_get_int(struc, "height", &height);

	if (self->width == width && self->height == height)
		return FALSE;

	return TRUE;
}

static gboolean
sink_setcaps(GstPad *pad,
	     GstCaps *caps)
{
	GstDspVDec *self;
	GstDspBase *base;
	GstStructure *in_struc;
	GstCaps *out_caps;
	const char *name;
	gboolean ret;
	struct td_codec *codec;

	self = GST_DSP_VDEC(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);

#ifdef DEBUG
	{
		gchar *str = gst_caps_to_string(caps);
		pr_info(self, "sink caps: %s", str);
		g_free(str);
	}
#endif

	if (need_node_reset(self, caps))
		gstdsp_reinit(base);

	in_struc = gst_caps_get_structure(caps, 0);

	if (base->node)
		goto skip_setup;

	base->parsed = false;
	self->profile = 0;

	name = gst_structure_get_name(in_struc);
	if (strcmp(name, "video/x-h264") == 0) {
		base->alg = GSTDSP_HDH264VDEC;
		self->priv.h264.lol = 0;
		self->priv.h264.hd_h264_streamtype = 0;
		self->priv.h264.ref_frames = 0;
		base->parse_func = gst_dsp_h264_parse;
		self->priv.h264.initial_height = 0;
	}
	else if (strcmp(name, "video/x-h263") == 0) {
		base->alg = GSTDSP_H263DEC;
		base->parse_func = gst_dsp_h263_parse;
	}
	else if (strcmp(name, "video/x-wmv") == 0) {
		guint32 fourcc;
		base->alg = GSTDSP_WMVDEC;

		if (gst_structure_get_fourcc(in_struc, "fourcc", &fourcc) ||
		    gst_structure_get_fourcc(in_struc, "format", &fourcc))
		{
			if (fourcc == GST_MAKE_FOURCC('W', 'V', 'C', '1'))
				self->wmv_is_vc1 = TRUE;
			else
				self->wmv_is_vc1 = FALSE;
		}
	}
	else if (strcmp(name, "image/jpeg") == 0) {
		base->alg = GSTDSP_JPEGDEC;
		gst_structure_get_boolean(in_struc, "interlaced",
					  &self->jpeg_is_interlaced);
	}
	else if (strcmp(name, "video/x-divx") == 0) {
		base->alg = GSTDSP_MPEG4VDEC;
		base->parse_func = gst_dsp_mpeg4_parse;
	}
	else {
		base->alg = GSTDSP_HDMPEG4VDEC;
		base->parse_func = gst_dsp_mpeg4_parse;
	}

	switch (base->alg) {
	case GSTDSP_MPEG4VDEC:
	case GSTDSP_H263DEC:
		codec = &td_mp4vdec_codec;
		break;
	case GSTDSP_H264DEC:
		codec = &td_h264dec_codec;
		break;
	case GSTDSP_WMVDEC:
		codec = &td_wmvdec_codec;
		break;
	case GSTDSP_JPEGDEC:
		codec = &td_jpegdec_codec;
		break;
	case GSTDSP_HDMPEG4VDEC:
		codec = &td_hdmp4vdec_codec;
		break;
	case GSTDSP_HDH264VDEC:
		codec = NULL; /* should be selected by profile */
		break;
	default:
		codec = NULL;
		break;
	}

	base->codec = codec;

	switch (base->alg) {
	case GSTDSP_JPEGDEC:
		du_port_alloc_buffers(base->ports[0], 1);
		du_port_alloc_buffers(base->ports[1], 1);
		break;
	default:
		du_port_alloc_buffers(base->ports[0], 4);
		du_port_alloc_buffers(base->ports[1], 4);
		break;
	}

skip_setup:
	out_caps = gst_caps_new_empty();
	configure_caps(self, caps, out_caps);
	base->tmp_caps = out_caps;

	ret = gst_pad_set_caps(pad, caps);
	if (!ret)
		return FALSE;

	save_codec_data(base, in_struc);
	return TRUE;
}

static gboolean
src_event(GstDspBase *self,
GstEvent *event)
{
	gboolean ret = TRUE;

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_QOS:
	{
		gdouble proportion;
		GstClockTimeDiff jitter;
		GstClockTime timestamp;

		gst_event_parse_qos(event, &proportion, &jitter, &timestamp);
		if (G_UNLIKELY(jitter > 0) || (jitter > (GstClockTimeDiff)self->default_duration))
			g_atomic_int_set(&self->qos, true);
		else
			g_atomic_int_set(&self->qos, false);

		ret = gst_pad_push_event(self->sinkpad, event);
		break;
	}
	default:
		ret = gst_pad_push_event(self->sinkpad, event);
		break;
	}

	return ret;
}

static void
set_property(GObject *obj,
	     guint prop_id,
	     const GValue *value,
	     GParamSpec *pspec)
{

	GstDspVDec *self = GST_DSP_VDEC(obj);

	switch (prop_id) {
	case ARG_MODE:
		self->mode = g_value_get_enum(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
get_property(GObject *obj,
	     guint prop_id,
	     GValue *value,
	     GParamSpec *pspec)
{

	GstDspVDec *self = GST_DSP_VDEC(obj);

	switch (prop_id) {
	case ARG_MODE:
		g_value_set_enum(value, self->mode);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}


static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspBase *base;
	GstDspVDec *self;

	base = GST_DSP_BASE(instance);
	self = GST_DSP_VDEC(instance);

	base->use_pad_alloc = TRUE;
	base->create_node = create_node;
	self->mode = DEFAULT_MODE;

	gst_pad_set_setcaps_function(base->sinkpad, sink_setcaps);
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;

	element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_set_details_simple(element_class,
					     "DSP video decoder",
					     "Codec/Decoder/Video",
					     "Decodes video with TI's DSP algorithms",
					     "Felipe Contreras");

	template = gst_pad_template_new("src", GST_PAD_SRC,
					GST_PAD_ALWAYS,
					generate_src_template());

	gst_element_class_add_pad_template(element_class, template);
	gst_object_unref(template);

	template = gst_pad_template_new("sink", GST_PAD_SINK,
					GST_PAD_ALWAYS,
					generate_sink_template());

	gst_element_class_add_pad_template(element_class, template);
	gst_object_unref(template);
}

static void
class_init(gpointer g_class,
	   gpointer class_data)
{
	GstDspBaseClass *class;
	GObjectClass *gobject_class;
	class = GST_DSP_BASE_CLASS(g_class);
	gobject_class = G_OBJECT_CLASS(g_class);

	parent_class = g_type_class_peek_parent(g_class);

	gobject_class->set_property = set_property;
	gobject_class->get_property = get_property;

	g_object_class_install_property(gobject_class, ARG_MODE,
					g_param_spec_enum("mode", "Decoding mode",
							  "Decoding mode",
							  GST_TYPE_DSPVDEC_MODE,
							  DEFAULT_MODE,
							  G_PARAM_READWRITE));

	class->src_event = src_event;
}

GType
gst_dsp_vdec_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspVDecClass),
			.class_init = class_init,
			.base_init = base_init,
			.instance_size = sizeof(GstDspVDec),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_BASE_TYPE, "GstDspVDec", &type_info, 0);
	}

	return type;
}
