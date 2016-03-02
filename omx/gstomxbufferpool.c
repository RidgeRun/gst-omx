/*
 * Copyright (C) 2013 RidgeRun, LLC (http://www.ridgerun.com)
 * Copyright (C) 2013, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, either version 2.1 of the License, or
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
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/gstvideometa.h>
#include <gst/gstatomicqueue.h>

#include <string.h>

#include "gstomxbufferpool.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_buffer_pool_debug);
#define GST_CAT_DEFAULT gst_omx_buffer_pool_debug


typedef struct _GstOMXMemoryAllocator GstOMXMemoryAllocator;
typedef struct _GstOMXMemoryAllocatorClass GstOMXMemoryAllocatorClass;

struct _GstOMXMemoryAllocator
{
  GstAllocator parent;
};

struct _GstOMXMemoryAllocatorClass
{
  GstAllocatorClass parent_class;
};

/* Buffer pool for the buffers of an OpenMAX port.
 *
 * This pool is only used if we either passed buffers from another
 * pool to the OMX port or provide the OMX buffers directly to other
 * elements.
 *
 *
 * A buffer is in the pool if it is currently owned by the port,
 * i.e. after OMX_{Fill,Empty}ThisBuffer(). A buffer is outside
 * the pool after it was taken from the port after it was handled
 * by the port, i.e. {Empty,Fill}BufferDone.
 *
 * Buffers can be allocated by us (OMX_AllocateBuffer()) or allocated
 * by someone else and (temporarily) passed to this pool
 * (OMX_UseBuffer(), OMX_UseEGLImage()). In the latter case the pool of
 * the buffer will be overriden, and restored in free_buffer(). Other
 * buffers are just freed there.
 *
 * The pool always has a fixed number of minimum and maximum buffers
 * and these are allocated while starting the pool and released afterwards.
 * They correspond 1:1 to the OMX buffers of the port, which are allocated
 * before the pool is started.
 *
 * Acquiring a buffer from this pool happens after the OMX buffer has
 * been acquired from the port. gst_buffer_pool_acquire_buffer() is
 * supposed to return the buffer that corresponds to the OMX buffer.
 *
 * For buffers provided to upstream, the buffer will be passed to
 * the component manually when it arrives and then unreffed. If the
 * buffer is released before reaching the component it will be just put
 * back into the pool as if EmptyBufferDone has happened. If it was
 * passed to the component, it will be back into the pool when it was
 * released and EmptyBufferDone has happened.
 *
 * For buffers provided to downstream, the buffer will be returned
 * back to the component (OMX_FillThisBuffer()) when it is released.
 */

static GQuark gst_omx_buffer_data_quark = 0;

#define GST_OMX_BUFFER_POOL_GET_PRIVATE(obj)  \
    (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_OMX_BUFFER_POOL, \
        GstOMXBufferPoolPrivate))

struct _GstOMXBufferPoolPrivate
{

  GstCaps *caps;
  gboolean add_videometa;
  GstVideoInfo video_info;

  /* Owned by element, element has to stop this pool before
   * it destroys component or port */
  GstOMXComponent *component;
  GstOMXPort *port;

  /* For handling OpenMAX allocated memory */
  GstAllocator *allocator;

  /* For populating the pool from another one */
  GstBufferPool *other_pool;
  GPtrArray *buffers;
  GstAtomicQueue *queue;
};

static GstMemory *
gst_omx_memory_allocator_alloc_dummy (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_assert_not_reached ();
  return NULL;
}

static void
gst_omx_memory_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstOMXMemory *omem = (GstOMXMemory *) mem;

  /* TODO: We need to remember which memories are still used
   * so we can wait until everything is released before allocating
   * new memory
   */

  g_slice_free (GstOMXMemory, omem);
}

static gpointer
gst_omx_memory_map (GstMemory * mem, gsize maxsize, GstMapFlags flags)
{
  GstOMXMemory *omem = (GstOMXMemory *) mem;

  return omem->buf->omx_buf->pBuffer + omem->mem.offset;
}

static void
gst_omx_memory_unmap (GstMemory * mem)
{
}

static GstMemory *
gst_omx_memory_share (GstMemory * mem, gssize offset, gssize size)
{
  g_assert_not_reached ();
  return NULL;
}

GType gst_omx_memory_allocator_get_type (void);
G_DEFINE_TYPE (GstOMXMemoryAllocator, gst_omx_memory_allocator,
    GST_TYPE_ALLOCATOR);

#define GST_OMX_MEMORY_TYPE "openmax"
#define GST_TYPE_OMX_MEMORY_ALLOCATOR   (gst_omx_memory_allocator_get_type())
#define GST_IS_OMX_MEMORY_ALLOCATOR(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_OMX_MEMORY_ALLOCATOR))

static void
gst_omx_memory_allocator_class_init (GstOMXMemoryAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class;

  allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = gst_omx_memory_allocator_alloc_dummy;
  allocator_class->free = gst_omx_memory_allocator_free;
}

static void
gst_omx_memory_allocator_init (GstOMXMemoryAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = GST_OMX_MEMORY_TYPE;
  alloc->mem_map = gst_omx_memory_map;
  alloc->mem_unmap = gst_omx_memory_unmap;
  alloc->mem_share = gst_omx_memory_share;

  /* default copy & is_span */

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static GstMemory *
gst_omx_memory_allocator_alloc (GstAllocator * allocator, GstMemoryFlags flags,
    GstOMXBuffer * buf)
{
  GstOMXMemory *mem;

  /* FIXME: We don't allow sharing because we need to know
   * when the memory becomes unused and can only then put
   * it back to the pool. Which is done in the pool's release
   * function
   */
  flags |= GST_MEMORY_FLAG_NO_SHARE;

  mem = g_slice_new (GstOMXMemory);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (mem), flags, allocator, NULL,
      buf->omx_buf->nAllocLen, buf->port->port_def.nBufferAlignment - 1,
      0, buf->omx_buf->nAllocLen);

  mem->buf = buf;

  return GST_MEMORY_CAST (mem);
}


#define GST_OMX_BUFFER_POOL_GET_PRIVATE(obj)  \
   (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_OMX_BUFFER_POOL, GstOMXBufferPoolPrivate))

#define gst_omx_buffer_pool_parent_class parent_class
G_DEFINE_TYPE (GstOMXBufferPool, gst_omx_buffer_pool, GST_TYPE_BUFFER_POOL);

static gboolean
gst_omx_buffer_pool_start (GstBufferPool * bpool)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstOMXBufferPoolPrivate *priv = pool->priv;

  /* Only allow to start the pool if we still are attached
   * to a component and port */
  GST_OBJECT_LOCK (pool);
  if (!priv->port) {
    GST_OBJECT_UNLOCK (pool);
    return FALSE;
  }

  if (!priv->component)
    priv->queue =
        gst_atomic_queue_new (priv->port->port_def.nBufferCountActual);

  GST_OBJECT_UNLOCK (pool);

  return GST_BUFFER_POOL_CLASS (parent_class)->start (bpool);
}

static gboolean
gst_omx_buffer_pool_stop (GstBufferPool * bpool)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstOMXBufferPoolPrivate *priv = pool->priv;

  GST_DEBUG_OBJECT (pool, "realeasing buffer from pool");
  if (!priv->component)
    while (gst_atomic_queue_pop (priv->queue));
  /* Remove any buffers that are there */
  g_ptr_array_set_size (priv->buffers, 0);

  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = NULL;

  pool->current_buffer_index = 0;
  priv->add_videometa = FALSE;

  return GST_BUFFER_POOL_CLASS (parent_class)->stop (bpool);
}

static const gchar **
gst_omx_buffer_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *raw_video_options[] =
      { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL };
  static const gchar *options[] = { NULL };
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstOMXBufferPoolPrivate *priv = pool->priv;

  GST_OBJECT_LOCK (pool);
  if (priv->port && priv->port->port_def.eDomain == OMX_PortDomainVideo
      && priv->port->port_def.format.video.eCompressionFormat ==
      OMX_VIDEO_CodingUnused) {
    GST_OBJECT_UNLOCK (pool);
    return raw_video_options;
  }
  GST_OBJECT_UNLOCK (pool);

  return options;
}

static gboolean
gst_omx_buffer_pool_set_config (GstBufferPool * bpool, GstStructure * config)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstOMXBufferPoolPrivate *priv = pool->priv;
  GstCaps *caps;
  guint min_buffers, max_buffers;
  GST_OBJECT_LOCK (pool);

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, &min_buffers,
          &max_buffers))
    goto wrong_config;

  GST_DEBUG_OBJECT (bpool,
      "Configuring pool %p min buffers:%d max buffers: %d",
      pool, min_buffers, max_buffers);

  if (caps == NULL)
    goto no_caps;

  if (priv->port && priv->port->port_def.eDomain == OMX_PortDomainVideo
      && priv->port->port_def.format.video.eCompressionFormat ==
      OMX_VIDEO_CodingUnused) {
    GstVideoInfo info;

    /* now parse the caps from the config */
    if (!gst_video_info_from_caps (&info, caps))
      goto wrong_video_caps;

    /* enable metadata based on config of the pool */
    priv->add_videometa =
        gst_buffer_pool_config_has_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    priv->video_info = info;
  }

  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = gst_caps_ref (caps);

  GST_OBJECT_UNLOCK (pool);

  return GST_BUFFER_POOL_CLASS (parent_class)->set_config (bpool, config);

  /* ERRORS */
wrong_config:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool, "invalid config");
    return FALSE;
  }
no_caps:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool, "no caps in config");
    return FALSE;
  }
wrong_video_caps:
  {
    GST_OBJECT_UNLOCK (pool);
    GST_WARNING_OBJECT (pool,
        "failed getting geometry from caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }
}

static GstFlowReturn
gst_omx_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstOMXBufferPoolPrivate *priv = pool->priv;
  GstBuffer *buf;
  GstOMXBuffer *omx_buf;

  g_return_val_if_fail (pool->allocating, GST_FLOW_ERROR);

  omx_buf = g_ptr_array_index (priv->port->buffers, pool->current_buffer_index);
  g_return_val_if_fail (omx_buf != NULL, GST_FLOW_ERROR);
  GST_LOG_OBJECT (pool, "allocating buffer for OMX buffer %d:%p",
      pool->current_buffer_index, omx_buf->omx_buf->pBuffer);
  if (priv->other_pool) {
    guint i, n;

    buf = g_ptr_array_index (priv->buffers, pool->current_buffer_index);
    g_assert (priv->other_pool == buf->pool);
    gst_object_replace ((GstObject **) & buf->pool, NULL);

    n = gst_buffer_n_memory (buf);
    for (i = 0; i < n; i++) {
      GstMemory *mem = gst_buffer_peek_memory (buf, i);

      /* FIXME: We don't allow sharing because we need to know
       * when the memory becomes unused and can only then put
       * it back to the pool. Which is done in the pool's release
       * function
       */
      GST_MINI_OBJECT_FLAG_SET (mem, GST_MEMORY_FLAG_NO_SHARE);
    }

    if (priv->add_videometa) {
      GstVideoMeta *meta;

      meta = gst_buffer_get_video_meta (buf);
      if (!meta) {
        gst_buffer_add_video_meta (buf, GST_VIDEO_FRAME_FLAG_NONE,
            GST_VIDEO_INFO_FORMAT (&priv->video_info),
            GST_VIDEO_INFO_WIDTH (&priv->video_info),
            GST_VIDEO_INFO_HEIGHT (&priv->video_info));
      }
    }
  } else {
    GstMemory *mem;

    mem = gst_omx_memory_allocator_alloc (priv->allocator, 0, omx_buf);
    buf = gst_buffer_new ();
    gst_buffer_append_memory (buf, mem);
    g_ptr_array_add (priv->buffers, buf);

    if (priv->add_videometa) {
      gsize offset[4] = { 0, };
      gint stride[4] = { 0, };

      switch (priv->video_info.finfo->format) {
        case GST_VIDEO_FORMAT_I420:
          offset[0] = 0;
          stride[0] = priv->port->port_def.format.video.nStride;
          offset[1] =
              stride[0] * priv->port->port_def.format.video.nSliceHeight;
          stride[1] = priv->port->port_def.format.video.nStride / 2;
          offset[2] =
              offset[1] +
              stride[1] * (priv->port->port_def.format.video.nSliceHeight / 2);
          stride[2] = priv->port->port_def.format.video.nStride / 2;
          break;
        case GST_VIDEO_FORMAT_NV12:
          offset[0] = 0;
          stride[0] = priv->port->port_def.format.video.nStride;
          offset[1] =
              stride[0] * priv->port->port_def.format.video.nSliceHeight;
          stride[1] = priv->port->port_def.format.video.nStride;
          break;
        default:
          g_assert_not_reached ();
          break;
      }

      gst_buffer_add_video_meta_full (buf, GST_VIDEO_FRAME_FLAG_NONE,
          GST_VIDEO_INFO_FORMAT (&priv->video_info),
          GST_VIDEO_INFO_WIDTH (&priv->video_info),
          GST_VIDEO_INFO_HEIGHT (&priv->video_info),
          GST_VIDEO_INFO_N_PLANES (&priv->video_info), offset, stride);
    }
  }

  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (buf),
      gst_omx_buffer_data_quark, omx_buf, NULL);

  *buffer = buf;

  pool->current_buffer_index++;

  return GST_FLOW_OK;
}

static void
gst_omx_buffer_pool_free_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstOMXBufferPoolPrivate *priv = pool->priv;

  /* If the buffers belong to another pool, restore them now */
  GST_OBJECT_LOCK (pool);
  if (priv->other_pool) {
    gst_object_replace ((GstObject **) & buffer->pool,
        (GstObject *) priv->other_pool);
  }
  GST_OBJECT_UNLOCK (pool);

  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (buffer),
      gst_omx_buffer_data_quark, NULL, NULL);

  GST_BUFFER_POOL_CLASS (parent_class)->free_buffer (bpool, buffer);
}

static GstFlowReturn
gst_omx_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstOMXBufferPoolPrivate *priv = pool->priv;
  gboolean acquired = FALSE;
  GTimeVal wait_end;

  if (!priv->component) {
    while (!acquired) {
      *buffer = gst_atomic_queue_pop (priv->queue);
      if (G_LIKELY (*buffer)) {
        ret = GST_FLOW_OK;
        GST_LOG_OBJECT (pool, "acquired buffer %p", *buffer);
        acquired = TRUE;
      } else {
        g_get_current_time (&wait_end);
        g_time_val_add (&wait_end, 10);
        ret = GST_FLOW_ERROR;
        GST_WARNING_OBJECT (pool, "no more buffers");
        /* Wait until a new buffer is released or timeout expired */
        g_mutex_lock (&(pool->acquired_mutex));
        g_cond_timed_wait (&(pool->acquired_cond), &(pool->acquired_mutex),
            &wait_end);
        g_mutex_unlock (&(pool->acquired_mutex));
      }
    }
    goto done;
  }

  if (priv->port->port_def.eDir == OMX_DirOutput) {
    GstBuffer *buf;

    g_return_val_if_fail (pool->current_buffer_index != -1, GST_FLOW_ERROR);

    buf = g_ptr_array_index (priv->buffers, pool->current_buffer_index);
    g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);
    *buffer = buf;
    ret = GST_FLOW_OK;

    /* If it's our own memory we have to set the sizes */
    if (!priv->other_pool) {
      GstMemory *mem = gst_buffer_peek_memory (*buffer, 0);

      g_assert (mem
          && g_strcmp0 (mem->allocator->mem_type, GST_OMX_MEMORY_TYPE) == 0);
      mem->size = ((GstOMXMemory *) mem)->buf->omx_buf->nFilledLen;
      mem->offset = ((GstOMXMemory *) mem)->buf->omx_buf->nOffset;
    }
  } else {
    /* Acquire any buffer that is available to be filled by upstream */
    ret =
        GST_BUFFER_POOL_CLASS (parent_class)->acquire_buffer
        (bpool, buffer, params);
  }

done:
  return ret;
}

static void
gst_omx_buffer_pool_release_buffer (GstBufferPool * bpool, GstBuffer * buffer)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (bpool);
  GstOMXBufferPoolPrivate *priv = pool->priv;
  GstMapInfo info;

  OMX_ERRORTYPE err;
  GstOMXBuffer *omx_buf;

  gst_buffer_map (buffer, &info, GST_MAP_READ);
  GST_LOG_OBJECT (pool, "releasing buffer %p with data %p", buffer, info.data);
  gst_buffer_unmap (buffer, &info);

  if (!priv->component) {
    g_mutex_lock (&(pool->acquired_mutex));
    gst_atomic_queue_push (priv->queue, buffer);
    g_cond_broadcast (&(pool->acquired_cond));
    g_mutex_unlock (&(pool->acquired_mutex));
    return;
  }

  g_assert (pool->component && priv->port);

  if (!pool->allocating && !pool->deactivated) {
    omx_buf =
        gst_mini_object_get_qdata (GST_MINI_OBJECT_CAST (buffer),
        gst_omx_buffer_data_quark);
    if (priv->port->port_def.eDir == OMX_DirOutput && !omx_buf->used) {
      /* Release back to the port, can be filled again */
      err = gst_omx_port_release_buffer (priv->port, omx_buf);
      if (err != OMX_ErrorNone) {
        GST_ELEMENT_ERROR (pool->element, LIBRARY, SETTINGS, (NULL),
            ("Failed to relase output buffer to component: %s (0x%08x)",
                gst_omx_error_to_string (err), err));
      }
    } else if (!omx_buf->used) {
      /* TODO: Implement.
       *
       * If not used (i.e. was not passed to the component) this should do
       * the same as EmptyBufferDone.
       * If it is used (i.e. was passed to the component) this should do
       * nothing until EmptyBufferDone.
       *
       * EmptyBufferDone should release the buffer to the pool so it can
       * be allocated again
       *
       * Needs something to call back here in EmptyBufferDone, like keeping
       * a ref on the buffer in GstOMXBuffer until EmptyBufferDone... which
       * would ensure that the buffer is always unused when this is called.
       */
      g_assert_not_reached ();
      GST_BUFFER_POOL_CLASS (parent_class)->release_buffer (bpool, buffer);
    }
  }
}

static void
gst_omx_buffer_pool_finalize (GObject * object)
{
  GstOMXBufferPool *pool = GST_OMX_BUFFER_POOL (object);
  GstOMXBufferPoolPrivate *priv = pool->priv;

  if (pool->element)
    gst_object_unref (pool->element);
  pool->element = NULL;

  if (priv->buffers)
    g_ptr_array_unref (priv->buffers);
  priv->buffers = NULL;

  if (priv->queue)
    gst_atomic_queue_unref (priv->queue);
  priv->queue = NULL;

  if (priv->other_pool)
    gst_object_unref (priv->other_pool);
  priv->other_pool = NULL;

  if (priv->allocator)
    gst_object_unref (priv->allocator);
  priv->allocator = NULL;

  if (priv->caps)
    gst_caps_unref (priv->caps);
  priv->caps = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_omx_buffer_pool_class_init (GstOMXBufferPoolClass * klass)
{
  GObjectClass *gobject_class;
  GstBufferPoolClass *gstbufferpool_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbufferpool_class = GST_BUFFER_POOL_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_omx_buffer_pool_debug, "omxbufferpool", 0,
      "OMX buffer pool");

  g_type_class_add_private (klass, sizeof (GstOMXBufferPoolPrivate));
  gst_omx_buffer_data_quark = g_quark_from_static_string ("GstOMXBufferData");

  gobject_class->finalize = gst_omx_buffer_pool_finalize;

  gstbufferpool_class->start = gst_omx_buffer_pool_start;
  gstbufferpool_class->stop = gst_omx_buffer_pool_stop;
  gstbufferpool_class->get_options = gst_omx_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_omx_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_omx_buffer_pool_alloc_buffer;
  gstbufferpool_class->free_buffer = gst_omx_buffer_pool_free_buffer;
  gstbufferpool_class->acquire_buffer = gst_omx_buffer_pool_acquire_buffer;
  gstbufferpool_class->release_buffer = gst_omx_buffer_pool_release_buffer;
}

static void
gst_omx_buffer_pool_init (GstOMXBufferPool * pool)
{
  GstOMXBufferPoolPrivate *priv = pool->priv;

  GST_DEBUG_OBJECT (pool, "Initializing OMX buffer pool %p", pool);

  priv = pool->priv = GST_OMX_BUFFER_POOL_GET_PRIVATE (pool);
  priv->queue = NULL;
  priv->buffers = g_ptr_array_new ();
  priv->allocator = g_object_new (gst_omx_memory_allocator_get_type (), NULL);
}

GstBufferPool *
gst_omx_buffer_pool_new (GstElement * element, GstOMXComponent * component,
    GstOMXPort * port)
{
  GstOMXBufferPool *pool;
  GstOMXBufferPoolPrivate *priv;

  pool = g_object_new (gst_omx_buffer_pool_get_type (), NULL);
  GST_DEBUG_OBJECT (pool, "Created a new OMX buffer pool %p in buffer pool %p",
      pool, GST_BUFFER_POOL (pool));
  pool->element = gst_object_ref (element);
  pool->current_buffer_index = 0;

  priv = pool->priv;
  priv->component = component;
  priv->port = port;

  return GST_BUFFER_POOL (pool);
}
