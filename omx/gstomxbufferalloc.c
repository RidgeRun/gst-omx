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

/**
 * SECTION:element-omxbufferalloc
 *
 * Dummy element that passes incoming data through unmodified. It
 * proposes an #GstOMXBufferPool to the upstream elements.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdlib.h>
#include <string.h>
#include <gst/video/video.h>
#include <gst/video/gstvideometa.h>

#include <xdc/std.h>
#include <ti/ipc/SharedRegion.h>
#include <ti/syslink/utils/Memory.h>

#include "gstomxbufferpool.h"
#include "gstomxbufferalloc.h"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define GST_OMX_BUFFER_ALLOC_NUM_BUFFERS_DEFAULT    3

GST_DEBUG_CATEGORY_STATIC (gst_omx_buffer_alloc_debug);
#define GST_CAT_DEFAULT gst_omx_buffer_alloc_debug

enum
{
  PROP_0,
  PROP_NUMBUFFERS
};

#define GST_OMX_BUFFER_ALLOC_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_OMX_BUFFER_ALLOC, \
        GstOMXBufferAllocPrivate))

struct _GstOMXBufferAllocPrivate
{
  Ptr heap;
};


#define _do_init \
    GST_DEBUG_CATEGORY_INIT (gst_omx_buffer_alloc_debug, "omxbufferalloc", 0, "OMX buffer allocation element");
#define gst_omx_buffer_alloc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstOMXBufferAlloc, gst_omx_buffer_alloc,
    GST_TYPE_BASE_TRANSFORM, _do_init);

static void gst_omx_buffer_alloc_finalize (GObject * object);
static void gst_omx_buffer_alloc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_buffer_alloc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstFlowReturn gst_omx_buffer_alloc_transform_ip (GstBaseTransform *
    trans, GstBuffer * buf);
static gboolean gst_omx_buffer_alloc_propose_allocation (GstBaseTransform *
    trans, GstQuery * decide_query, GstQuery * query);
static gboolean gst_omx_buffer_alloc_start (GstBaseTransform * trans);
static gboolean gst_omx_buffer_alloc_stop (GstBaseTransform * trans);

static void
gst_omx_buffer_alloc_finalize (GObject * object)
{
  GstOMXBufferAlloc *omxalloc;

  omxalloc = GST_OMX_BUFFER_ALLOC (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_omx_buffer_alloc_class_init (GstOMXBufferAllocClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseTransformClass *gstbasetrans_class;

  g_type_class_add_private (klass, sizeof (GstOMXBufferAllocPrivate));

  gobject_class = G_OBJECT_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstbasetrans_class = GST_BASE_TRANSFORM_CLASS (klass);

  gobject_class->set_property = gst_omx_buffer_alloc_set_property;
  gobject_class->get_property = gst_omx_buffer_alloc_get_property;
  gobject_class->finalize = gst_omx_buffer_alloc_finalize;

  g_object_class_install_property (gobject_class, PROP_NUMBUFFERS,
      g_param_spec_uint ("num-buffers", "number of buffers",
          "Number of buffers to be allocated by component",
          1, 16, 10, G_PARAM_WRITABLE));

  gst_element_class_set_static_metadata (gstelement_class,
      "omxbufferalloc",
      "Generic",
      "Pass data without modification",
      "Melissa Montero <melissa.montero@ridgerun.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gstbasetrans_class->transform_ip =
      GST_DEBUG_FUNCPTR (gst_omx_buffer_alloc_transform_ip);
  gstbasetrans_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_omx_buffer_alloc_propose_allocation);
  gstbasetrans_class->start = GST_DEBUG_FUNCPTR (gst_omx_buffer_alloc_start);
  gstbasetrans_class->stop = GST_DEBUG_FUNCPTR (gst_omx_buffer_alloc_stop);
}

static void
gst_omx_buffer_alloc_init (GstOMXBufferAlloc * omxalloc)
{
  GstOMXPort *port;

  GST_LOG_OBJECT (omxalloc, "initialiazing OMX buffer alloc");

  port = &omxalloc->port;

  port->port_def.eDir = OMX_DirOutput;
  port->port_def.nBufferAlignment = 128;
  port->port_def.nBufferCountMin = 3;

  omxalloc->num_buffers = GST_OMX_BUFFER_ALLOC_NUM_BUFFERS_DEFAULT;
  omxalloc->priv = GST_OMX_BUFFER_ALLOC_GET_PRIVATE (omxalloc);

  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (omxalloc), TRUE);
  gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM_CAST (omxalloc), TRUE);
}

static void
gst_omx_buffer_alloc_allocate_buffers (GstOMXBufferAlloc * omxalloc, guint size)
{
  GstOMXBufferAllocPrivate *priv;
  GstOMXPort *port;
  guint num_buffers, i;

  priv = omxalloc->priv;
  port = &omxalloc->port;
  num_buffers = omxalloc->num_buffers;

  GST_DEBUG_OBJECT (omxalloc, "allocating %d buffers of size:%d!!", num_buffers,
      size);
  priv->heap = SharedRegion_getHeap (2);

  omxalloc->port.buffers = g_ptr_array_sized_new (num_buffers);

  for (i = 0; i < num_buffers; i++) {
    GstOMXBuffer *buf;
    buf = g_slice_new0 (GstOMXBuffer);
    buf->port = port;
    buf->used = FALSE;
    buf->settings_cookie = port->settings_cookie;
    buf->omx_buf = g_new0 (OMX_BUFFERHEADERTYPE, 1);
    buf->omx_buf->pBuffer = Memory_alloc (priv->heap, size, 128, NULL);
    buf->omx_buf->nAllocLen = size;

    GST_DEBUG_OBJECT (omxalloc, "allocated outbuf:%p", buf->omx_buf->pBuffer);

    g_ptr_array_add (port->buffers, buf);
  }

  port->port_def.nBufferCountActual = num_buffers;
  port->port_def.nBufferSize = size;

  return;
}

static gboolean
gst_omx_buffer_alloc_propose_allocation (GstBaseTransform * trans,
    GstQuery * decide_query, GstQuery * query)
{
  GstOMXBufferAlloc *omxalloc;
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  guint size, min_buffers, max_buffers;
  gboolean need_pool;

  omxalloc = GST_OMX_BUFFER_ALLOC (trans);

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if (need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    /* the normal size of a frame */
    size = info.size;

    GST_DEBUG_OBJECT (omxalloc, "create new pool");

    gst_omx_buffer_alloc_allocate_buffers (omxalloc, size);

    pool =
        gst_omx_buffer_pool_new (GST_ELEMENT (omxalloc), NULL, &omxalloc->port);
    GST_OMX_BUFFER_POOL (pool)->allocating = TRUE;

    if (pool) {
      max_buffers = min_buffers = omxalloc->port.port_def.nBufferCountActual;
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_set_params (config, caps, size, min_buffers,
          max_buffers);
      if (!gst_buffer_pool_set_config (pool, config))
        goto config_failed;

      gst_query_add_allocation_pool (query, pool, size, min_buffers,
          max_buffers);
      gst_object_unref (pool);
    }
  }

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (trans, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (trans, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (trans, "failed setting config");
    gst_object_unref (pool);
    return FALSE;
  }
}


static GstFlowReturn
gst_omx_buffer_alloc_transform_ip (GstBaseTransform * trans, GstBuffer * buf)
{
  GstFlowReturn ret = GST_FLOW_OK;

  return ret;

  /* ERRORS */
}

static void
gst_omx_buffer_alloc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXBufferAlloc *omxalloc;

  omxalloc = GST_OMX_BUFFER_ALLOC (object);

  switch (prop_id) {
    case PROP_NUMBUFFERS:
      omxalloc->num_buffers = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_buffer_alloc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOMXBufferAlloc *omxalloc;

  omxalloc = GST_OMX_BUFFER_ALLOC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_omx_buffer_alloc_start (GstBaseTransform * trans)
{
  GstOMXBufferAlloc *omxalloc;

  omxalloc = GST_OMX_BUFFER_ALLOC (trans);

  OMX_Init ();

  return TRUE;
}

static gboolean
gst_omx_buffer_alloc_stop (GstBaseTransform * trans)
{
  GstOMXBufferAlloc *omxalloc;
  GstOMXPort *port;
  int i;

  omxalloc = GST_OMX_BUFFER_ALLOC (trans);
  port = &omxalloc->port;

  if (port->buffers) {
    for (i = 0; i < omxalloc->num_buffers; i++) {
      GstOMXBuffer *buf = g_ptr_array_index (port->buffers, i);

      if (buf->used)
        GST_ERROR_OBJECT (omxalloc, "Trying to free used buffer %p", buf);

      if (buf->omx_buf) {
        GST_DEBUG_OBJECT (omxalloc, "deallocating buffer %p (%p)",
            buf, buf->omx_buf->pBuffer);

        Memory_free (omxalloc->priv->heap, buf->omx_buf->pBuffer,
            buf->omx_buf->nAllocLen);
      }
      g_slice_free (GstOMXBuffer, buf);
    }

    g_ptr_array_unref (port->buffers);
    port->buffers = NULL;
  }

  return TRUE;
}
