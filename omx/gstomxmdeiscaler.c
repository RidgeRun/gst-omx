/*
 * Copyright (C) 2016 Radeus Labs Inc.
 * Copyright (C) 2016 RidgeRun LLC
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "OMX_TI_Common.h"
#include <omx_vfpc.h>
#include <OMX_TI_Index.h>

#include "gstomxmdeiscaler.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_mdeiscaler_debug_category);
#define GST_CAT_DEFAULT gst_omx_mdeiscaler_debug_category

static GstStaticPadTemplate src_template_yuv2 =
GST_STATIC_PAD_TEMPLATE ("src_00",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format=(string)YUY2, "
        "width=(int)[ 16, 1920 ], "
        "height=(int)[ 16, 1080 ], " "framerate=" GST_VIDEO_FPS_RANGE));

static GstStaticPadTemplate src_template_nv12 =
GST_STATIC_PAD_TEMPLATE ("src_01",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw, "
        "format=(string)NV12, "
        "width=(int)[ 16, 1920 ], "
        "height=(int)[ 16, 1080 ], " "framerate=" GST_VIDEO_FPS_RANGE));

/* prototypes */
static void gst_omx_mdeiscaler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_mdeiscaler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_omx_mdeiscaler_set_format (GstOMXVideoFilter * videofilter,
    GstCaps * incaps, GstVideoInfo * ininfo, GList * outcaps_list,
    GList * outinfo_list);
static GstCaps *gst_omx_mdeiscaler_fixed_src_caps (GstOMXVideoFilter * self,
    GstCaps * incaps, GstPad * srcpad);

enum
{
  PROP_0,
};

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_mdeiscaler_debug_category, "omxmdeiscaler", 0, \
      "debug category for gst-omx mdei video scaler");

G_DEFINE_TYPE_WITH_CODE (GstOMXMDEIScaler, gst_omx_mdeiscaler,
    GST_TYPE_OMX_VIDEO_FILTER, DEBUG_INIT);

static void
gst_omx_mdeiscaler_class_init (GstOMXMDEIScalerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXVideoFilterClass *videofilter_class =
      GST_OMX_VIDEO_FILTER_CLASS (klass);

  gobject_class->set_property = gst_omx_mdeiscaler_set_property;
  gobject_class->get_property = gst_omx_mdeiscaler_get_property;

  videofilter_class->num_outputs = 2;
  videofilter_class->sink_templ_only = TRUE;
  videofilter_class->fixed_src_caps =
      GST_DEBUG_FUNCPTR (gst_omx_mdeiscaler_fixed_src_caps);
  videofilter_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_mdeiscaler_set_format);
  videofilter_class->cdata.default_sink_template_caps = "video/x-raw, "
      "width = (int) [ 16, 1920 ], "
      "height = (int) [ 16, 1080 ], "
      "framerate=" GST_VIDEO_FPS_RANGE ", " "format = (string) { NV12, YUY2 }";
  gst_element_class_set_static_metadata (element_class,
      "OpenMAX MDEI Video Scaler",
      "Filter/Encoder/Video",
      "Deinterlace and Scale raw video streams",
      "Troy Wood <troy@radeuslabs.com>, Carlos Rodriguez <carlos.rodriguez@ridgerun.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_yuv2));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_nv12));
}

static void
gst_omx_mdeiscaler_init (GstOMXMDEIScaler * self)
{

}

static void
gst_omx_mdeiscaler_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_mdeiscaler_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstCaps *
gst_omx_mdeiscaler_fixed_src_caps (GstOMXVideoFilter * self,
    GstCaps * incaps, GstPad * srcpad)
{
  GstCaps *intersection;
  GstCaps *srctempl;
  GstCaps *srccaps;
  GstPad *srcpeer;
  GstCaps *peercaps;
  gboolean is_fixed;
  gchar *caps_str = NULL;

  srctempl = gst_pad_get_pad_template_caps (srcpad);

  /* first lets see what the peer has to offer */
  srcpeer = gst_pad_get_peer (srcpad);
  peercaps = gst_pad_query_caps (srcpeer, srctempl);

  intersection =
      gst_caps_intersect_full (srctempl, peercaps, GST_CAPS_INTERSECT_FIRST);
  gst_caps_unref (peercaps);

  srccaps = gst_caps_is_empty (intersection) ? gst_caps_ref (srctempl) :
      gst_caps_ref (intersection);
  gst_caps_unref (intersection);
  gst_caps_unref (srctempl);

  GST_DEBUG_OBJECT (self, "intersect with peercaps (%" GST_PTR_FORMAT "): %s",
      srccaps, caps_str = gst_caps_to_string (srccaps));
  if (caps_str)
    g_free (caps_str);

  /* now lets intersect with incoming caps */
  intersection =
      gst_caps_intersect_full (srccaps, incaps, GST_CAPS_INTERSECT_FIRST);
  srccaps =
      gst_caps_is_empty (intersection) ? gst_caps_ref (srccaps) :
      gst_caps_ref (intersection);
  gst_caps_unref (intersection);

  GST_DEBUG_OBJECT (self, "intersect with incaps (%" GST_PTR_FORMAT "): %s",
      srccaps, caps_str = gst_caps_to_string (srccaps));
  if (caps_str)
    g_free (caps_str);

  srccaps = gst_caps_fixate (srccaps);

  if (srcpeer)
    gst_object_unref (srcpeer);

  GST_DEBUG_OBJECT (self, "fixated to (%" GST_PTR_FORMAT "): %s", srccaps,
      caps_str = gst_caps_to_string (srccaps));
  if (caps_str)
    g_free (caps_str);

  return srccaps;
}

static gboolean
gst_omx_mdeiscaler_set_format (GstOMXVideoFilter * videofilter,
    GstCaps * incaps, GstVideoInfo * ininfo, GList * outcaps_list,
    GList * outinfo_list)
{
  OMX_PARAM_VFPC_NUMCHANNELPERHANDLE num_channel;
  OMX_CONFIG_VIDCHANNEL_RESOLUTION channel_resolution;
  OMX_CONFIG_ALG_ENABLE alg_enable;
  OMX_ERRORTYPE err;
  GList *outinfo;
  OMX_PARAM_BUFFER_MEMORYTYPE memory;

  OMX_CONFIG_SUBSAMPLING_FACTOR subsampling_factor = { 0 };
  GST_OMX_INIT_STRUCT (&subsampling_factor);
  subsampling_factor.nSubSamplingFactor = 1;
  err =
      gst_omx_component_set_config (videofilter->comp,
      OMX_TI_IndexConfigSubSamplingFactor, &subsampling_factor);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (videofilter,
        "failed to set input channel resolution: %s (0x%08x) ",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (videofilter, "setting number of channels");
  GST_OMX_INIT_STRUCT (&num_channel);
  num_channel.nNumChannelsPerHandle = 1;
  err = gst_omx_component_set_parameter (videofilter->comp,
      (OMX_INDEXTYPE) OMX_TI_IndexParamVFPCNumChPerHandle, &num_channel);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (videofilter,
        "Failed to set num of channels: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (videofilter, "setting input resolution");
  GST_OMX_INIT_STRUCT (&channel_resolution);
  channel_resolution.Frm0Width = GST_VIDEO_INFO_WIDTH (ininfo);
  channel_resolution.Frm0Height = GST_VIDEO_INFO_HEIGHT (ininfo);
  channel_resolution.Frm0Pitch = GST_VIDEO_INFO_PLANE_STRIDE (ininfo, 0);
  channel_resolution.Frm1Width = 0;
  channel_resolution.Frm1Height = 0;
  channel_resolution.Frm1Pitch = 0;
  channel_resolution.FrmStartX = 0;
  channel_resolution.FrmStartY = 0;
  channel_resolution.FrmCropWidth = GST_VIDEO_INFO_WIDTH (ininfo);
  channel_resolution.FrmCropHeight = GST_VIDEO_INFO_HEIGHT (ininfo);
  channel_resolution.eDir = OMX_DirInput;
  channel_resolution.nChId = 0;
  err = gst_omx_component_set_config (videofilter->comp,
      OMX_TI_IndexConfigVidChResolution, &channel_resolution);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (videofilter,
        "failed to set input channel resolution: %s (0x%08x) ",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  outinfo = outinfo_list;
  GST_DEBUG_OBJECT (videofilter, "setting output resolution");
  GST_OMX_INIT_STRUCT (&channel_resolution);
  channel_resolution.Frm0Width =
      GST_VIDEO_INFO_WIDTH ((GstVideoInfo *) outinfo->data);
  channel_resolution.Frm0Height =
      GST_VIDEO_INFO_HEIGHT ((GstVideoInfo *) outinfo->data);
  channel_resolution.Frm0Pitch =
      GST_VIDEO_INFO_PLANE_STRIDE ((GstVideoInfo *) outinfo->data, 0);
  GST_DEBUG_OBJECT (videofilter, "(Frame0 = %dx%d) Pitch: %d",
      channel_resolution.Frm0Width, channel_resolution.Frm0Height,
      channel_resolution.Frm0Pitch);

  outinfo = outinfo->next;

  channel_resolution.Frm1Width =
      GST_VIDEO_INFO_WIDTH ((GstVideoInfo *) outinfo->data);
  channel_resolution.Frm1Height =
      GST_VIDEO_INFO_HEIGHT ((GstVideoInfo *) outinfo->data);
  channel_resolution.Frm1Pitch =
      GST_VIDEO_INFO_PLANE_STRIDE ((GstVideoInfo *) outinfo->data, 0);
  GST_DEBUG_OBJECT (videofilter, "(Frame1 = %dx%d) Pitch: %d",
      channel_resolution.Frm1Width, channel_resolution.Frm1Height,
      channel_resolution.Frm1Pitch);

  channel_resolution.FrmStartX = 0;
  channel_resolution.FrmStartY = 0;
  channel_resolution.FrmCropWidth = 0;
  channel_resolution.FrmCropHeight = 0;
  channel_resolution.eDir = OMX_DirOutput;
  channel_resolution.nChId = 0;
  err = gst_omx_component_set_config (videofilter->comp,
      OMX_TI_IndexConfigVidChResolution, &channel_resolution);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (videofilter,
        "failed to set output channel resolution: %s (0x%08x) ",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (videofilter, "disable algorithm bypass mode");
  GST_OMX_INIT_STRUCT (&alg_enable);
  alg_enable.nPortIndex = 0;
  alg_enable.nChId = 0;
  alg_enable.bAlgBypass = 1;
  err = gst_omx_component_set_config (videofilter->comp,
      OMX_TI_IndexConfigAlgEnable, &alg_enable);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (videofilter,
        "failed to set disable algorithm bypass mode: %s (0x%08x) ",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}
