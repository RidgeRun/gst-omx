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

#ifndef __GST_OMX_BUFFER_ALLOC_H__
#define __GST_OMX_BUFFER_ALLOC_H__


#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "gstomx.h"
G_BEGIN_DECLS


#define GST_TYPE_OMX_BUFFER_ALLOC \
  (gst_omx_buffer_alloc_get_type())
#define GST_OMX_BUFFER_ALLOC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OMX_BUFFER_ALLOC,GstOMXBufferAlloc))
#define GST_OMX_BUFFER_ALLOC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OMX_BUFFER_ALLOC,GstOMXBufferAllocClass))
#define GST_IS_OMX_BUFFER_ALLOC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OMX_BUFFER_ALLOC))
#define GST_IS_OMX_BUFFER_ALLOC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OMX_BUFFER_ALLOC))

typedef struct _GstOMXBufferAlloc GstOMXBufferAlloc;
typedef struct _GstOMXBufferAllocClass GstOMXBufferAllocClass;
typedef struct _GstOMXBufferAllocPrivate GstOMXBufferAllocPrivate;

/**
 * GstOMXBufferAlloc:
 *
 * Opaque #GstOMXBufferAlloc data structure
 */
struct _GstOMXBufferAlloc {
  GstBaseTransform 	 element;

  guint num_buffers;
  GstOMXPort port;
  /*< private >*/
  GstOMXBufferAllocPrivate *priv;

};

struct _GstOMXBufferAllocClass {
  GstBaseTransformClass parent_class;
};

G_GNUC_INTERNAL GType gst_omx_buffer_alloc_get_type (void);

G_END_DECLS

#endif /* __GST_OMX_BUFFER_ALLOC_H__ */
