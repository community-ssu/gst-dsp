/*
 * Copyright (C) 2009 Felipe Contreras
 * Copyright (C) 2008-2009 Nokia Corporation.
 *
 * Authors:
 * Felipe Contreras <felipe.contreras@nokia.com>
 * Marco Ballesio <marco.ballesio@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef DMM_BUFFER_H
#define DMM_BUFFER_H

#include <stdlib.h> /* for calloc, free */
#include <string.h> /* for memset */

#include "dsp_bridge.h"
#include "log.h"

#define ROUND_UP(num, scale) (((num) + ((scale) - 1)) & ~((scale) - 1))
#define PAGE_SIZE 0x1000

enum dma_data_direction {
	DMA_BIDIRECTIONAL,
	DMA_TO_DEVICE,
	DMA_FROM_DEVICE,
};

typedef struct {
	int handle;
	void *proc;
	void *data;
	void *allocated_data;
	size_t size;
	size_t len;
	void *reserve;
	void *map;
	bool need_copy;
	size_t alignment;
	void *user_data;
	bool used;
	bool keyframe;
	int dir;
} dmm_buffer_t;

static inline dmm_buffer_t *
dmm_buffer_new(int handle,
	       void *proc,
	       int dir)
{
	dmm_buffer_t *b;
	b = calloc(1, sizeof(*b));

	pr_debug(NULL, "%p", b);
	b->handle = handle;
	b->proc = proc;
	b->alignment = 128;
	b->dir = dir;

	return b;
}

static inline void
dmm_buffer_free(dmm_buffer_t *b)
{
	pr_debug(NULL, "%p", b);
	if (!b)
		return;
	if (b->map)
		dsp_unmap(b->handle, b->proc, b->map);
	if (b->reserve)
		dsp_unreserve(b->handle, b->proc, b->reserve);
	free(b->allocated_data);
	free(b);
}

static inline void
dmm_buffer_begin(dmm_buffer_t *b,
		 size_t len)
{
	pr_debug(NULL, "%p", b);
	if (b->dir == DMA_FROM_DEVICE)
		dsp_invalidate(b->handle, b->proc, b->data, len);
	else
		dsp_flush(b->handle, b->proc, b->data, len, 1);
}

static inline void
dmm_buffer_end(dmm_buffer_t *b,
	       size_t len)
{
	pr_debug(NULL, "%p", b);
	if (b->dir != DMA_TO_DEVICE)
		dsp_invalidate(b->handle, b->proc, b->data, len);
}

static inline void
dmm_buffer_clean(dmm_buffer_t *b,
		 size_t len)
{
	pr_debug(NULL, "%p", b);
	if (G_UNLIKELY(len > b->size))
		g_error("wrong cache flush");
	dsp_flush(b->handle, b->proc, b->data, len, 1);
}

static inline void
dmm_buffer_invalidate(dmm_buffer_t *b,
		      size_t len)
{
	pr_debug(NULL, "%p", b);
	if (G_UNLIKELY(len > b->size))
		g_error("wrong cache flush");
	dsp_invalidate(b->handle, b->proc, b->data, len);
}

static inline void
dmm_buffer_flush(dmm_buffer_t *b,
		 size_t len)
{
	pr_debug(NULL, "%p", b);
	if (G_UNLIKELY(len > b->size))
		g_error("wrong cache flush");
	dsp_flush(b->handle, b->proc, b->data, len, 0);
}

static inline void
dmm_buffer_map(dmm_buffer_t *b)
{
	size_t to_reserve;
	pr_debug(NULL, "%p", b);
	if (b->map)
		dsp_unmap(b->handle, b->proc, b->map);
	if (b->reserve)
		dsp_unreserve(b->handle, b->proc, b->reserve);
	/**
	 * @todo What exactly do we want to do here? Shouldn't the driver
	 * calculate this?
	 */
	to_reserve = ROUND_UP(b->size, PAGE_SIZE) + PAGE_SIZE;
	dsp_reserve(b->handle, b->proc, to_reserve, &b->reserve);
	dsp_map(b->handle, b->proc, b->data, b->size, b->reserve, &b->map, 0);
}

static inline void
dmm_buffer_allocate(dmm_buffer_t *b,
		    size_t size)
{
	pr_debug(NULL, "%p", b);
	free(b->allocated_data);
	if (b->alignment != 0) {
		if (posix_memalign(&b->allocated_data, b->alignment, ROUND_UP(size, b->alignment)) != 0)
			b->allocated_data = NULL;
		b->data = b->allocated_data;
	}
	else
		b->data = b->allocated_data = malloc(size);
	b->size = size;
	dmm_buffer_map(b);
}

static inline void
dmm_buffer_use(dmm_buffer_t *b,
	       void *data,
	       size_t size)
{
	pr_debug(NULL, "%p", b);
	b->data = data;
	b->size = size;
	dmm_buffer_map(b);
}

static inline dmm_buffer_t *
dmm_buffer_calloc(int handle,
		  void *proc,
		  size_t size,
		  int dir)
{
	dmm_buffer_t *tmp;
	tmp = dmm_buffer_new(handle, proc, dir);
	dmm_buffer_allocate(tmp, size);
	memset(tmp->data, 0, size);
	return tmp;
}

#endif /* DMM_BUFFER_H */
