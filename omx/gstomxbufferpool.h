/*
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses>.
 *
 */

#ifndef __GST_OMX_BUFFER_POOL_H__
#define __GST_OMX_BUFFER_POOL_H__

#include <string.h>

#include <gst/gst.h>
#include <gst/video/gstvideopool.h>

#include "gstomx.h"

G_BEGIN_DECLS

typedef struct _GstOMXMemory GstOMXMemory;

typedef struct _GstOMXBufferPool GstOMXBufferPool;
typedef struct _GstOMXBufferPoolClass GstOMXBufferPoolClass;
typedef struct _GstOMXBufferPoolPrivate GstOMXBufferPoolPrivate;


/**
 * GstOmxMemory:
 * @element: a reference to the our #GstElement
 * @buf: a reference to the OMX buffer
 * @allocating: set from outside this pool, TRUE if we're currently
 *    allocating all our buffers
 *
 * Subclass of #GstMemory containing additional information about OMX.
 */
struct _GstOMXMemory
{
  GstMemory mem;

  GstOMXBuffer *buf;
};

/* buffer pool functions */
#define GST_TYPE_OMX_BUFFER_POOL      (gst_omx_buffer_pool_get_type())
#define GST_IS_OMX_BUFFER_POOL(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_OMX_BUFFER_POOL))
#define GST_OMX_BUFFER_POOL(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_OMX_BUFFER_POOL, GstOMXBufferPool))
#define GST_OMX_BUFFER_POOL_CAST(obj) ((GstOMXBufferPool*)(obj))

/**
 * GstOMXBufferPool:
 * @element: a reference to the our #GstElement
 * @allocating: set from outside this pool, TRUE if we're currently
 *    allocating all our buffers
 * @deactivated: TRUE if the pool is not used anymore
 * @current_buffer_index: used during acquire for output ports to
 *    specify which buffer has to be retrieved and during alloc, which
 *    buffer has to be wrapped
 *
 * Subclass of #GstVideoBufferPool containing additional information about OMX.
 */
struct _GstOMXBufferPool
{
  GstVideoBufferPool bufferpool;
  GstElement *element;

  gboolean allocating;
  gboolean deactivated;
  gint current_buffer_index;
  GCond acquired_cond;
  GMutex acquired_mutex;

  GstOMXBufferPoolPrivate *priv;
};

struct _GstOMXBufferPoolClass
{
  GstVideoBufferPoolClass parent_class;
};

GType gst_omx_buffer_pool_get_type (void);
GstBufferPool * gst_omx_buffer_pool_new (GstElement * element,
    GstOMXComponent * component, GstOMXPort * port);

G_END_DECLS

#endif /*__GST_OMX_BUFFER_POOL_H__*/
