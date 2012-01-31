/*
 * Copyright (C) 2011 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspbuffer.h"
#include "dmm_buffer.h"

typedef struct _GstDspBuffer GstDspBuffer;
typedef struct _GstDspBufferClass GstDspBufferClass;

static GstMiniObjectClass *parent_class;

struct _GstDspBuffer {
	GstBuffer parent;
	GstDspBase *base;
	struct td_buffer *tb;
	gint cookie;
};

struct _GstDspBufferClass {
	GstBufferClass parent_class;
};

static GType type;

GstBuffer *gst_dsp_buffer_new(GstDspBase *base, struct td_buffer *tb)
{
	GstBuffer *buf;
	GstDspBuffer *dsp_buf;
	dmm_buffer_t *b = tb->data;
	buf = (GstBuffer *) gst_mini_object_new(type);
	gst_buffer_set_caps(buf, GST_PAD_CAPS(base->srcpad));
	GST_BUFFER_MALLOCDATA(buf) = b->allocated_data;
	GST_BUFFER_DATA(buf) = b->data;
	GST_BUFFER_SIZE(buf) = b->len;
	/* not to be messed with elsewhere while it's ours */
	b->allocated_data = NULL;
	dsp_buf = (GstDspBuffer *) buf;
	dsp_buf->tb = tb;
	dsp_buf->base = gst_object_ref(base);
	g_mutex_lock(base->pool_mutex);
	dsp_buf->cookie = base->cycle;
	g_mutex_unlock(base->pool_mutex);
	return buf;
}

static void finalize(GstMiniObject *obj)
{
	GstDspBuffer *dsp_buf = (GstDspBuffer *) obj;
	GstDspBase *base = dsp_buf->base;
	struct td_buffer *tb = dsp_buf->tb;

	g_mutex_lock(base->pool_mutex);
	/* note: in this order, as ->tb may no longer be around */
	if (base->cycle == dsp_buf->cookie && tb->pinned) {
		if (G_UNLIKELY(g_atomic_int_get(&base->eos)))
			tb->clean = true;
		tb->data->allocated_data = GST_BUFFER_MALLOCDATA(dsp_buf);
		GST_BUFFER_MALLOCDATA(dsp_buf) = NULL;
		base->send_buffer(base, tb);
	}
	g_mutex_unlock(base->pool_mutex);
	gst_object_unref(base);
	parent_class->finalize(obj);
}

static void class_init(void *g_class, void *class_data)
{
	GstMiniObjectClass *obj_class;
	obj_class = GST_MINI_OBJECT_CLASS(g_class);
	obj_class->finalize = finalize;
	parent_class = g_type_class_peek_parent(g_class);
}

GType
gst_dsp_buffer_get_type(void)
{
	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspBufferClass),
			.class_init = class_init,
			.instance_size = sizeof(GstDspBuffer),
		};

		type = g_type_register_static(GST_TYPE_BUFFER, "GstDspBuffer", &type_info, 0);
	}

	return type;
}
