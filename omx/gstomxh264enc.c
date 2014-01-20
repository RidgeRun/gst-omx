/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
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

#include <OMX_Component.h>
#include <OMX_TI_Index.h>
#include <OMX_TI_Video.h>

#include "gstomxh264enc.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_h264_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_h264_enc_debug_category


#define GST_TYPE_OMX_H264_ENC_ENCODING_PRESET (gst_omx_h264_enc_encoding_preset_get_type ())
static GType
gst_omx_h264_enc_encoding_preset_get_type ()
{
  static GType qtype = 0;

  if (!qtype) {
    static const GEnumValue values[] = {
      {OMX_Video_Enc_High_Quality, "High Quality", "hq"},
      {OMX_Video_Enc_High_Speed, "High Speed", "hs"},
      {OMX_Video_Enc_User_Defined, "User Defined", "user"},
      {OMX_Video_Enc_High_Speed_Med_Quality, "High Speed Medium Quality",
          "hsmq"},
      {OMX_Video_Enc_Med_Speed_Med_Quality, "Medium Speed Medium Quality",
          "msmq"},
      {OMX_Video_Enc_Med_Speed_High_Quality, "Medium Speed High Quality",
          "mshq"},

      {0, NULL, NULL},
    };

    qtype = g_enum_register_static ("GstOmxH264EncEncodingPreset", values);
  }
  return qtype;
}

#define GST_TYPE_OMX_H264_ENC_RATE_CONTROL_PRESET (gst_omx_h264_enc_rate_control_preset_get_type ())
static GType
gst_omx_h264_enc_rate_control_preset_get_type ()
{
  static GType qtype = 0;

  if (!qtype) {
    static const GEnumValue values[] = {
      {OMX_Video_RC_Low_Delay, "Low Delay", "low-delay"},
      {OMX_Video_RC_Storage, "Storage", "storage"},
      {OMX_Video_RC_Twopass, "Two Pass", "two-pass"},
      {OMX_Video_RC_None, "none", "none"},
      {0, NULL, NULL},
    };

    qtype = g_enum_register_static ("GstOmxH264EncRateControlPreset", values);
  }
  return qtype;
}

/* prototypes */
static void gst_omx_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_h264_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_omx_h264_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static GstCaps *gst_omx_h264_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static GstFlowReturn gst_omx_h264_enc_handle_output_frame (GstOMXVideoEnc *
    enc, GstOMXPort * port, GstOMXBuffer * buf, GstVideoCodecFrame * frame);

enum
{
  PROP_0,
  PROP_I_PERIOD,
  PROP_IDR_PERIOD,
  PROP_ENCODING_PRESET,
  PROP_RATE_CONTROL_PRESET,
  PROP_FORCE_IDR
};

#define GST_OMX_H264_ENC_I_PERIOD_DEFAULT 90
#define GST_OMX_H264_ENC_IDR_PERIOD_DEFAULT 0
#define GST_OMX_H264_ENC_ENCODING_PRESET_DEFAULT OMX_Video_Enc_High_Speed_Med_Quality
#define GST_OMX_H264_ENC_RATE_CONTROL_PRESET_DEFAULT OMX_Video_RC_Low_Delay
#define GST_OMX_H264_ENC_FORCE_IDR_DEFAULT  FALSE

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_h264_enc_debug_category, "omxh264enc", 0, \
      "debug category for gst-omx video encoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXH264Enc, gst_omx_h264_enc,
    GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

static void
gst_omx_h264_enc_class_init (GstOMXH264EncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);

  gobject_class->set_property = gst_omx_h264_enc_set_property;
  gobject_class->get_property = gst_omx_h264_enc_get_property;

  g_object_class_install_property (gobject_class, PROP_I_PERIOD,
      g_param_spec_uint ("i-period", "I period",
          "Specifies periodicity of I frames (0:Disable)",
          0, G_MAXINT32, GST_OMX_H264_ENC_I_PERIOD_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_IDR_PERIOD,
      g_param_spec_uint ("idr-period", "IDR period",
          "Specifies periodicity of IDR frames (0:Only the first frame to be IDR)",
          0, G_MAXINT32, GST_OMX_H264_ENC_IDR_PERIOD_DEFAULT,
          G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_ENCODING_PRESET,
      g_param_spec_enum ("encoding-preset", "Encoding Preset",
          "Specifies which encoding preset to use",
          GST_TYPE_OMX_H264_ENC_ENCODING_PRESET,
          GST_OMX_H264_ENC_ENCODING_PRESET_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_RATE_CONTROL_PRESET,
      g_param_spec_enum ("rate-control-preset", "Rate Control Preset",
          "Specifies what rate control preset to use",
          GST_TYPE_OMX_H264_ENC_RATE_CONTROL_PRESET,
          GST_OMX_H264_ENC_RATE_CONTROL_PRESET_DEFAULT, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_FORCE_IDR,
      g_param_spec_boolean ("force-idr", "Force IDR",
          "Force next frame to be IDR", GST_OMX_H264_ENC_FORCE_IDR_DEFAULT,
          G_PARAM_WRITABLE));

  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_h264_enc_set_format);
  videoenc_class->get_caps = GST_DEBUG_FUNCPTR (gst_omx_h264_enc_get_caps);

  videoenc_class->cdata.default_src_template_caps = "video/x-h264, "
      "width=(int) [ 16, 4096 ], " "height=(int) [ 16, 4096 ]";
  videoenc_class->handle_output_frame =
      GST_DEBUG_FUNCPTR (gst_omx_h264_enc_handle_output_frame);

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX H.264 Video Encoder",
      "Codec/Encoder/Video",
      "Encode H.264 video streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_omx_set_default_role (&videoenc_class->cdata, "video_encoder.avc");
}

static void
gst_omx_h264_enc_init (GstOMXH264Enc * self)
{
  self->i_period = GST_OMX_H264_ENC_I_PERIOD_DEFAULT;
  self->idr_period = GST_OMX_H264_ENC_FORCE_IDR_DEFAULT;
  self->encoding_preset = GST_OMX_H264_ENC_ENCODING_PRESET_DEFAULT;
  self->rate_control_preset = GST_OMX_H264_ENC_RATE_CONTROL_PRESET_DEFAULT;

  self->idr_count = 0;
}

static gboolean
gst_omx_h264_enc_force_idr (GstOMXH264Enc * self)
{
  OMX_CONFIG_INTRAREFRESHVOPTYPE idr_config;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&idr_config);

  idr_config.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_config (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexConfigVideoIntraVOPRefresh, &idr_config);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Setting encoding/rate control preset not supported by component");
    return TRUE;
  }

  idr_config.IntraRefreshVOP = TRUE;

  err =
      gst_omx_component_set_config (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexConfigVideoIntraVOPRefresh, &idr_config);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting idr configuration not supported by component");
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error forcing idr : %s (0x%08x)", gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Succesfully force idr");
  return TRUE;
}


static gboolean
gst_omx_h264_enc_set_encoder_preset (GstOMXH264Enc * self)
{
  OMX_VIDEO_PARAM_ENCODER_PRESETTYPE param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_TI_IndexParamVideoEncoderPreset, &param);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Setting encoding/rate control preset not supported by component");
    return TRUE;
  }

  param.eEncodingModePreset = self->encoding_preset;
  param.eRateControlPreset = self->rate_control_preset;

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_TI_IndexParamVideoEncoderPreset, &param);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting encoding/rate control preset not supported by component");
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error setting encoding preset %u and rate control preset %u: %s (0x%08x)",
        (guint) param.eEncodingModePreset, (guint) param.eRateControlPreset,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_h264_enc_set_avc (GstOMXH264Enc * self)
{
  OMX_VIDEO_PARAM_AVCTYPE param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);

  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoAvc, &param);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Setting encoding/rate control preset not supported by component");
    return TRUE;
  }

  param.eLevel = self->level;
  param.eProfile = self->profile;
  param.nPFrames = self->i_period - 1;
  param.nBFrames = 0;

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoAvc, &param);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self, "Setting avc params not supported by component");
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error setting level %u, profile %u and i-frame period %u: %s (0x%08x)",
        (guint) param.eLevel, (guint) param.eProfile, (guint) param.nPFrames,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self,
      "Succesfully setting level %u, profile %u and i-frame period %u",
      (guint) param.eLevel, (guint) param.eProfile, (guint) param.nPFrames);
  return TRUE;
}

static gboolean
gst_omx_h264_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);
  GstCaps *peercaps;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  OMX_ERRORTYPE err;
  const gchar *profile_string, *level_string;

  gst_omx_port_get_port_definition (GST_OMX_VIDEO_ENC (self)->enc_out_port,
      &port_def);
  port_def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
  err =
      gst_omx_port_update_port_definition (GST_OMX_VIDEO_ENC
      (self)->enc_out_port, &port_def);
  if (err != OMX_ErrorNone)
    return FALSE;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self,
        "Setting profile/level not supported by component");
    return TRUE;
  }

  peercaps = gst_pad_peer_query_caps (GST_VIDEO_ENCODER_SRC_PAD (enc),
      gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (enc)));
  if (peercaps) {
    GstStructure *s;

    if (gst_caps_is_empty (peercaps)) {
      gst_caps_unref (peercaps);
      GST_ERROR_OBJECT (self, "Empty caps");
      return FALSE;
    }

    s = gst_caps_get_structure (peercaps, 0);
    profile_string = gst_structure_get_string (s, "profile");
    if (profile_string) {
      if (g_str_equal (profile_string, "baseline")) {
        param.eProfile = OMX_VIDEO_AVCProfileBaseline;
      } else if (g_str_equal (profile_string, "main")) {
        param.eProfile = OMX_VIDEO_AVCProfileMain;
      } else if (g_str_equal (profile_string, "extended")) {
        param.eProfile = OMX_VIDEO_AVCProfileExtended;
      } else if (g_str_equal (profile_string, "high")) {
        param.eProfile = OMX_VIDEO_AVCProfileHigh;
      } else if (g_str_equal (profile_string, "high-10")) {
        param.eProfile = OMX_VIDEO_AVCProfileHigh10;
      } else if (g_str_equal (profile_string, "high-4:2:2")) {
        param.eProfile = OMX_VIDEO_AVCProfileHigh422;
      } else if (g_str_equal (profile_string, "high-4:4:4")) {
        param.eProfile = OMX_VIDEO_AVCProfileHigh444;
      } else {
        goto unsupported_profile;
      }
    }
    level_string = gst_structure_get_string (s, "level");
    if (level_string) {
      if (g_str_equal (level_string, "1")) {
        param.eLevel = OMX_VIDEO_AVCLevel1;
      } else if (g_str_equal (level_string, "1b")) {
        param.eLevel = OMX_VIDEO_AVCLevel1b;
      } else if (g_str_equal (level_string, "1.1")) {
        param.eLevel = OMX_VIDEO_AVCLevel11;
      } else if (g_str_equal (level_string, "1.2")) {
        param.eLevel = OMX_VIDEO_AVCLevel12;
      } else if (g_str_equal (level_string, "1.3")) {
        param.eLevel = OMX_VIDEO_AVCLevel13;
      } else if (g_str_equal (level_string, "2")) {
        param.eLevel = OMX_VIDEO_AVCLevel2;
      } else if (g_str_equal (level_string, "2.1")) {
        param.eLevel = OMX_VIDEO_AVCLevel21;
      } else if (g_str_equal (level_string, "2.2")) {
        param.eLevel = OMX_VIDEO_AVCLevel22;
      } else if (g_str_equal (level_string, "3")) {
        param.eLevel = OMX_VIDEO_AVCLevel3;
      } else if (g_str_equal (level_string, "3.1")) {
        param.eLevel = OMX_VIDEO_AVCLevel31;
      } else if (g_str_equal (level_string, "3.2")) {
        param.eLevel = OMX_VIDEO_AVCLevel32;
      } else if (g_str_equal (level_string, "4")) {
        param.eLevel = OMX_VIDEO_AVCLevel4;
      } else if (g_str_equal (level_string, "4.1")) {
        param.eLevel = OMX_VIDEO_AVCLevel41;
      } else if (g_str_equal (level_string, "4.2")) {
        param.eLevel = OMX_VIDEO_AVCLevel42;
      } else if (g_str_equal (level_string, "5")) {
        param.eLevel = OMX_VIDEO_AVCLevel5;
      } else if (g_str_equal (level_string, "5.1")) {
        param.eLevel = OMX_VIDEO_AVCLevel51;
      } else {
        goto unsupported_level;
      }
    }
    gst_caps_unref (peercaps);
  }
  self->level = param.eLevel;
  self->profile = param.eProfile;

  err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err == OMX_ErrorUnsupportedIndex) {
    GST_WARNING_OBJECT (self,
        "Setting profile/level not supported by component");
  } else if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Error setting profile %u and level %u: %s (0x%08x)",
        (guint) param.eProfile, (guint) param.eLevel,
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  if (!gst_omx_h264_enc_set_avc (self))
    return FALSE;

  if (!gst_omx_h264_enc_set_encoder_preset (self))
    return FALSE;

  return TRUE;

unsupported_profile:
  GST_ERROR_OBJECT (self, "Unsupported profile %s", profile_string);
  gst_caps_unref (peercaps);
  return FALSE;

unsupported_level:
  GST_ERROR_OBJECT (self, "Unsupported level %s", level_string);
  gst_caps_unref (peercaps);
  return FALSE;
}

static GstCaps *
gst_omx_h264_enc_get_caps (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_VIDEO_PARAM_PROFILELEVELTYPE param;
  const gchar *profile, *level;

  caps = gst_caps_new_simple ("video/x-h264",
      "stream-format", G_TYPE_STRING, "byte-stream",
      "alignment", G_TYPE_STRING, "au", NULL);

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamVideoProfileLevelCurrent, &param);
  if (err != OMX_ErrorNone && err != OMX_ErrorUnsupportedIndex)
    return NULL;

  if (err == OMX_ErrorNone) {
    switch (param.eProfile) {
      case OMX_VIDEO_AVCProfileBaseline:
        profile = "baseline";
        break;
      case OMX_VIDEO_AVCProfileMain:
        profile = "main";
        break;
      case OMX_VIDEO_AVCProfileExtended:
        profile = "extended";
        break;
      case OMX_VIDEO_AVCProfileHigh:
        profile = "high";
        break;
      case OMX_VIDEO_AVCProfileHigh10:
        profile = "high-10";
        break;
      case OMX_VIDEO_AVCProfileHigh422:
        profile = "high-4:2:2";
        break;
      case OMX_VIDEO_AVCProfileHigh444:
        profile = "high-4:4:4";
        break;
      default:
        g_assert_not_reached ();
        return NULL;
    }

    switch (param.eLevel) {
      case OMX_VIDEO_AVCLevel1:
        level = "1";
        break;
      case OMX_VIDEO_AVCLevel1b:
        level = "1b";
        break;
      case OMX_VIDEO_AVCLevel11:
        level = "1.1";
        break;
      case OMX_VIDEO_AVCLevel12:
        level = "1.2";
        break;
      case OMX_VIDEO_AVCLevel13:
        level = "1.3";
        break;
      case OMX_VIDEO_AVCLevel2:
        level = "2";
        break;
      case OMX_VIDEO_AVCLevel21:
        level = "2.1";
        break;
      case OMX_VIDEO_AVCLevel22:
        level = "2.2";
        break;
      case OMX_VIDEO_AVCLevel3:
        level = "3";
        break;
      case OMX_VIDEO_AVCLevel31:
        level = "3.1";
        break;
      case OMX_VIDEO_AVCLevel32:
        level = "3.2";
        break;
      case OMX_VIDEO_AVCLevel4:
        level = "4";
        break;
      case OMX_VIDEO_AVCLevel41:
        level = "4.1";
        break;
      case OMX_VIDEO_AVCLevel42:
        level = "4.2";
        break;
      case OMX_VIDEO_AVCLevel5:
        level = "5";
        break;
      case OMX_VIDEO_AVCLevel51:
        level = "5.1";
        break;
      default:
        g_assert_not_reached ();
        return NULL;
    }
    gst_caps_set_simple (caps,
        "profile", G_TYPE_STRING, profile, "level", G_TYPE_STRING, level, NULL);
  }

  return caps;
}

static GstFlowReturn
gst_omx_h264_enc_handle_output_frame (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstOMXBuffer * buf, GstVideoCodecFrame * frame)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (enc);

  if (buf->omx_buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG) {
    /* The codec data is SPS/PPS with a startcode => bytestream stream format
     * For bytestream stream format the SPS/PPS is only in-stream and not
     * in the caps!
     */
    if (buf->omx_buf->nFilledLen >= 4 &&
        GST_READ_UINT32_BE (buf->omx_buf->pBuffer +
            buf->omx_buf->nOffset) == 0x00000001) {
      GList *l = NULL;
      GstBuffer *hdrs;
      GstMapInfo map = GST_MAP_INFO_INIT;

      GST_DEBUG_OBJECT (self, "got codecconfig in byte-stream format");
      buf->omx_buf->nFlags &= ~OMX_BUFFERFLAG_CODECCONFIG;

      hdrs = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);

      gst_buffer_map (hdrs, &map, GST_MAP_WRITE);
      memcpy (map.data,
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);
      gst_buffer_unmap (hdrs, &map);
      l = g_list_append (l, hdrs);
      gst_video_encoder_set_headers (GST_VIDEO_ENCODER (self), l);
    }
  }

  if (self->idr_period) {
    if (self->idr_period == self->idr_count) {
      self->force_idr = TRUE;
      self->idr_count = 0;
    } else {
      self->idr_count++;
    }
  }

  if (self->force_idr) {
    gst_omx_h264_enc_force_idr (self);
    self->force_idr = FALSE;
  }

  return
      GST_OMX_VIDEO_ENC_CLASS
      (gst_omx_h264_enc_parent_class)->handle_output_frame (enc, port, buf,
      frame);
}

static void
gst_omx_h264_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (object);

  switch (prop_id) {
    case PROP_I_PERIOD:
      self->i_period = g_value_get_uint (value);
      break;
    case PROP_IDR_PERIOD:
      self->idr_period = g_value_get_uint (value);
      break;
    case PROP_ENCODING_PRESET:
      self->encoding_preset = g_value_get_enum (value);
      break;
    case PROP_RATE_CONTROL_PRESET:
      self->rate_control_preset = g_value_get_enum (value);
      break;
    case PROP_FORCE_IDR:
      self->force_idr = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_h264_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOMXH264Enc *self = GST_OMX_H264_ENC (object);

  switch (prop_id) {
    case PROP_I_PERIOD:
      g_value_set_uint (value, self->i_period);
      break;
    case PROP_IDR_PERIOD:
      g_value_set_uint (value, self->idr_period);
      break;
    case PROP_ENCODING_PRESET:
      g_value_set_enum (value, self->encoding_preset);
      break;
    case PROP_RATE_CONTROL_PRESET:
      g_value_set_enum (value, self->rate_control_preset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
