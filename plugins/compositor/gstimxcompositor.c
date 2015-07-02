/* GStreamer IMX video compositor plugin
 * Copyright (c) 2015, Freescale Semiconductor, Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-imxcompositor
 *
 * imxompositor can composite a group of video streams with different format
 * into one video frame. videos can be overlapped or alpha blent if the
 * corresponding hardware support. videos will be performed colorspace
 * conversion and scaling as well as rotation. For each of the requested
 * sink pads it will compare the incoming geometry and framerate to define the
 * output parameters. Indeed output video frames will have the geometry of the
 * biggest incoming video stream and the framerate of the fastest incoming one.
 *
 * Individual parameters for each input stream can be configured on the
 * #GstImxCompositorPad.
 * <itemizedlist>
 * <listitem>
 * "xpos": The x-coordinate position of the top-left corner of the picture
 * (#gint)
 * </listitem>
 * <listitem>
 * "ypos": The y-coordinate position of the top-left corner of the picture
 * (#gint)
 * </listitem>
 * <listitem>
 * "width": The width of the picture; the input will be scaled if necessary
 * (#gint)
 * </listitem>
 * <listitem>
 * "height": The height of the picture; the input will be scaled if necessary
 * (#gint)
 * </listitem>
 * <listitem>
 * "alpha": The transparency of the picture; between 0.0 and 1.0. This feature
 * depends on the underlying hardware, if hardware don't support, then alpha
 * will be ignored
 * (#gdouble)
 * </listitem>
 * <listitem>
 * "rotate": The rotation of the picture in the composition
 * (#guint)
 * </listitem>
 * <listitem>
 * "keep-ratio": Keep the aspect ratio of the picture after resize
 * (#gboolean)
 * </listitem>
 * </itemizedlist>
 *
 * <refsect2>
 * <title>Sample pipelines</title>
 * |[
 * gst-launch-1.0 imxcompositor_pxp background=0x0000FFFF name=comp
 *   sink_0::alpha=0.6
 *   sink_1::alpha=0.8 sink_1::xpos=100 sink_1::ypos=120
 *   sink_2::xpos=320 sink_2::ypos=240 ! imxv4l2sink
 *   videotestsrc ! video/x-raw,format=RGB16,width=320,height=240 ! comp.sink_0
 *   videotestsrc ! video/x-raw,format=RGB16,width=800, height=600 ! comp.sink_1
 *   videotestsrc ! video/x-raw, width=1280, height=720 ! comp.sink_2
 * ]| A pipeline to demonstrate imxcompositor_pxp
 * This should show a color bar 320x240 with alpha 0.6
 * showing the yellow background. showing a color bar 800x600 at (100,120) with
 * alpha 0.8 overlapped with previous one. showing a color bar 1280x720 at
 * (320,240) with alpha 1.0 overlapped with previous one.
 * |[
 * gst-launch-1.0 imxcompositor_ipu background=0x0000FFFF name=comp
 *   sink_0::xpos=0 sink_0::ypos=0
 *   sink_1::xpos=96 sink_1::ypos=96 sink_1::width=800 sink_1::height=400
 *   sink_2::xpos=400 sink_2::ypos=300 sink_2::width=400 sink_2::height=300
 *   ! video/x-raw,format=RGBx,width=1024,height=768 ! overlaysink
 *   videotestsrc ! video/x-raw,format=RGB16,width=320,height=240 ! comp.sink_0
 *   videotestsrc ! video/x-raw,format=RGB16,width=1080,height=720 ! comp.sink_1
 *   videotestsrc ! video/x-raw, width=800, height=400 ! comp.sink_2
 * ]|A pipeline using imxcompositor_ipu to composite videos with scaling and
 * blending
 * |[
 * gst-launch-1.0 imxcompositor_g2d background=0x00FFFFFF name=comp
 *   sink_0::alpha=0.6 sink_0::xpos=50 sink_0::ypos=50 sink_0::width=1280
 *   sink_0::height=720
 *   sink_1::alpha=0.8 sink_1::xpos=300 sink_1::ypos=400 sink_1::width=800
 *   sink_1::height=540 sink_1::rotate=1
 *   sink_2::alpha=0.7 sink_2::xpos=800 sink_2::ypos=500 sink_2::width=640
 *   sink_2::height=480
 *   sink_3::xpos=900 sink_3::ypos=200 sink_3::alpha=0.5 sink_3::width=720
 *   sink_3::height=480 ! overlaysink sync=false
 *   uridecodebin uri=file://$FILE1 ! comp.sink_0
 *   uridecodebin uri=file://$FILE2 ! comp.sink_1
 *   uridecodebin uri=file://$FILE3 ! comp.sink_2
 *   videotestsrc ! video/x-raw,format=RGB16,width=1280,height=720 ! comp.sink_3
 * ]| A pipeline using imxcompositor_g2d to composite video from decoders and
 * videotestsrc by scaling, rotation and alpha blending
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "gstallocatorphymem.h"
#include "allocator/gstphymemmeta.h"
#include "gstimxcompositor.h"
#include "gstimxcompositorpad.h"

//#define USE_GST_VIDEO_SAMPLE_CONVERT  //bad performance

#define IMX_COMPOSITOR_INPUT_POOL_MIN_BUFFERS   1
#define IMX_COMPOSITOR_INPUT_POOL_MAX_BUFFERS   30
#define IMX_COMPOSITOR_OUTPUT_POOL_MIN_BUFFERS   3
#define IMX_COMPOSITOR_OUTPUT_POOL_MAX_BUFFERS   30

#define DEFAULT_IMXCOMPOSITOR_BACKGROUND        0x00000000
#define SINK_TEMP_BUFFER_SIZE                   (2048*2048*4)

#define GST_IMX_COMPOSITOR_PARAMS_QDATA   \
          g_quark_from_static_string("imxcompositor-params")

#define GST_IMX_COMPOSITOR_UNREF_BUFFER(buffer) {\
    if (buffer) {                             \
      GST_LOG ("unref buffer (%p)", buffer);  \
      gst_buffer_unref(buffer);               \
      buffer = NULL;                          \
    }                                         \
  }

#define GST_IMX_COMPOSITOR_UNREF_POOL(pool)  {   \
    if (pool) {                               \
      GST_LOG ("unref pool (%p)", pool);      \
      gst_buffer_pool_set_active (pool, FALSE);\
      gst_object_unref(pool);                 \
      pool = NULL;                            \
    }                                         \
  }

GST_DEBUG_CATEGORY_STATIC (gst_imxcompositor_debug);
#define GST_CAT_DEFAULT gst_imxcompositor_debug

/* properties utility*/
enum {
  PROP_0,
#if 0
  PROP_IMXCOMPOSITOR_OUTPUT_WIDTH,
  PROP_IMXCOMPOSITOR_OUTPUT_HEIGHT,
#endif
  PROP_IMXCOMPOSITOR_BACKGROUND_ENABLE,
  PROP_IMXCOMPOSITOR_BACKGROUND_COLOR
};

static GstElementClass *parent_class = NULL;

static void gst_imxcompositor_finalize (GObject * object)
{
  GstImxCompositor *imxcomp = (GstImxCompositor *)(object);
  GstStructure *config;
  GstImxCompositorClass *klass =
        (GstImxCompositorClass *) G_OBJECT_GET_CLASS (imxcomp);

  GST_IMX_COMPOSITOR_UNREF_BUFFER (imxcomp->sink_tmp_buf);
  GST_IMX_COMPOSITOR_UNREF_POOL (imxcomp->out_pool);
  if (imxcomp->allocator) {
    gst_object_unref (imxcomp->allocator);
    imxcomp->allocator = NULL;
  }

  if (imxcomp->device) {
    imxcomp->device->close(imxcomp->device);
    if (klass->in_plugin)
      klass->in_plugin->destroy(imxcomp->device);
    imxcomp->device = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize ((GObject *) (imxcomp));
}

static void
gst_imxcompositor_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstImxCompositor *imxcomp = (GstImxCompositor *) (object);

  switch (prop_id) {
    case PROP_IMXCOMPOSITOR_BACKGROUND_ENABLE:
      g_value_set_boolean(value, imxcomp->background_enable);
      break;
    case PROP_IMXCOMPOSITOR_BACKGROUND_COLOR:
      g_value_set_uint (value, imxcomp->background);
      break;
#if 0
    case PROP_IMXCOMPOSITOR_OUTPUT_WIDTH:
      g_value_set_uint (value, imxcomp->width);
      break;
    case PROP_IMXCOMPOSITOR_OUTPUT_HEIGHT:
      g_value_set_uint (value, imxcomp->height);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_imxcompositor_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstImxCompositor *imxcomp = (GstImxCompositor *) (object);

  switch (prop_id) {
    case PROP_IMXCOMPOSITOR_BACKGROUND_ENABLE:
      imxcomp->background_enable = g_value_get_boolean(value);
      break;
    case PROP_IMXCOMPOSITOR_BACKGROUND_COLOR:
      imxcomp->background = g_value_get_uint (value);
      break;
#if 0
    case PROP_IMXCOMPOSITOR_OUTPUT_WIDTH:
      imxcomp->width = g_value_get_uint (value);
      break;
    case PROP_IMXCOMPOSITOR_OUTPUT_HEIGHT:
      imxcomp->height = g_value_get_uint (value);
      break;
#endif
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_imxcompositor_set_pool_alignment(GstCaps *caps, GstBufferPool *pool)
{
  GstVideoInfo info;
  GstVideoAlignment alignment;
  GstStructure *config = gst_buffer_pool_get_config(pool);
  gst_video_info_from_caps (&info, caps);

  memset (&alignment, 0, sizeof (GstVideoAlignment));

  gint w = GST_VIDEO_INFO_WIDTH (&info);
  gint h = GST_VIDEO_INFO_HEIGHT (&info);
  if (!ISALIGNED (w, ALIGNMENT) || !ISALIGNED (h, ALIGNMENT)) {
    alignment.padding_right = ALIGNTO (w, ALIGNMENT) - w;
    alignment.padding_bottom = ALIGNTO (h, ALIGNMENT) - h;
  }

  GST_DEBUG ("pool(%p), [%d, %d]:padding_right (%d), padding_bottom (%d)",
      pool, w, h, alignment.padding_right, alignment.padding_bottom);

  if (!gst_buffer_pool_config_has_option (config, \
        GST_BUFFER_POOL_OPTION_VIDEO_META)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  if (!gst_buffer_pool_config_has_option (config,
            GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  }

  gst_buffer_pool_config_set_video_alignment (config, &alignment);
  gst_buffer_pool_set_config(pool, config);
}

static GstBufferPool*
gst_imxcompositor_create_bufferpool(GstImxCompositor *imxcomp,
                    GstCaps *caps, guint size, guint min, guint max)
{
  GstBufferPool *pool;
  GstStructure *config;
  pool = gst_video_buffer_pool_new ();
  if (pool) {
    if (!imxcomp->allocator)
      imxcomp->allocator =
          gst_imx_2d_device_allocator_new((gpointer)(imxcomp->device));

    if (!imxcomp->allocator) {
      GST_ERROR ("new imx compositor allocator failed.");
      gst_buffer_pool_set_active (pool, FALSE);
      gst_object_unref (pool);
      return NULL;
    }

    gst_imxcompositor_set_pool_alignment(caps, pool);

    config = gst_buffer_pool_get_config(pool);
    gst_buffer_pool_config_set_params(config, caps, size, min, max);
    gst_buffer_pool_config_set_allocator(config, imxcomp->allocator, NULL);
    gst_buffer_pool_config_add_option(config,GST_BUFFER_POOL_OPTION_VIDEO_META);
    if (!gst_buffer_pool_set_config(pool, config)) {
      GST_ERROR ("set buffer pool config failed.");
      gst_buffer_pool_set_active (pool, FALSE);
      gst_object_unref (pool);
      return NULL;
    }
  }

  GST_LOG ("created a buffer pool (%p).", pool);
  return pool;
}

static gboolean
gst_imxcompositor_sink_query (GstAggregator * agg, GstAggregatorPad * bpad,
    GstQuery * query)
{
  gboolean ret = FALSE;
  GstImxCompositor *imxcomp = (GstImxCompositor *) (agg);

  GST_TRACE ("QUERY %" GST_PTR_FORMAT, query);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ALLOCATION:
    {
      GST_OBJECT_LOCK (imxcomp);
      if (G_UNLIKELY (!(imxcomp->negotiated))) {
        GST_DEBUG_OBJECT (bpad,
            "not negotiated yet, can't answer ALLOCATION query");
        GST_OBJECT_UNLOCK (imxcomp);
        return FALSE;
      }
      GST_OBJECT_UNLOCK (imxcomp);

      GstImxCompositorPad *imxcompo_pad = GST_IMXCOMPOSITOR_PAD (bpad);
      GstCaps *pool_caps = NULL;
      GstStructure *config = NULL;
      GstCaps *caps;
      gboolean need_pool = FALSE;
      GstBufferPool *pool = NULL;
      GstVideoInfo info;
      guint size = 0;

      gst_query_parse_allocation (query, &caps, &need_pool);
      if (caps == NULL) {
        GST_DEBUG_OBJECT (bpad, "no caps specified");
        return FALSE;
      }

      GST_DEBUG_OBJECT(bpad, "query allocation, caps: %" GST_PTR_FORMAT, caps);

      // proposal allocation pool
      if (imxcompo_pad->sink_pool) {
        config = gst_buffer_pool_get_config (imxcompo_pad->sink_pool);
        gst_buffer_pool_config_get_params(config, &pool_caps, &size, NULL, NULL);
        if (gst_caps_is_equal(pool_caps, caps)) {
          pool = imxcompo_pad->sink_pool;
          need_pool = FALSE;
        }
      }

      if (need_pool) {
        if (!gst_video_info_from_caps (&info, caps))
          return FALSE;

        size = PAGE_ALIGN(GST_VIDEO_INFO_SIZE (&info));

        pool = gst_imxcompositor_create_bufferpool(imxcomp, caps, size,
            IMX_COMPOSITOR_INPUT_POOL_MIN_BUFFERS,
            IMX_COMPOSITOR_INPUT_POOL_MAX_BUFFERS);
        if (pool) {
          GST_IMX_COMPOSITOR_UNREF_POOL(imxcompo_pad->sink_pool);
          imxcompo_pad->sink_pool = pool;
          imxcompo_pad->sink_pool_update = TRUE;
        }
      }

      if (pool) {
        GST_DEBUG_OBJECT (bpad, "propose_allocation, pool(%p).", pool);
        GstStructure *config = gst_buffer_pool_get_config (pool);
        gst_buffer_pool_config_get_params (config, &caps, &size, NULL, NULL);
        gst_structure_free (config);

        gst_query_add_allocation_pool (query, pool, size,
            IMX_COMPOSITOR_INPUT_POOL_MIN_BUFFERS,
            IMX_COMPOSITOR_INPUT_POOL_MAX_BUFFERS);
        gst_query_add_allocation_param (query, imxcomp->allocator, NULL);
        gst_query_add_allocation_meta(query, GST_VIDEO_META_API_TYPE, 0);
        gst_query_add_allocation_meta(query,GST_VIDEO_CROP_META_API_TYPE, NULL);
      }

      GST_DEBUG_OBJECT (bpad, "ALLOCATION ret %p, %"
                        GST_PTR_FORMAT, imxcompo_pad->sink_pool, query);
      break;
    }
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;
      GstCaps *sinkcaps;
      GstCaps *template_caps;

      gst_query_parse_caps (query, &filter);

      GstCaps *filtered_caps;
      GstStructure *s;
      gboolean had_current_caps = TRUE;
      gint i, n;

      template_caps = gst_pad_get_pad_template_caps (GST_PAD (bpad));
      sinkcaps = gst_pad_get_current_caps (GST_PAD (bpad));
      if (sinkcaps == NULL) {
        had_current_caps = FALSE;
        sinkcaps = template_caps;
      }

      sinkcaps = gst_caps_make_writable (sinkcaps);

      n = gst_caps_get_size (sinkcaps);
      for (i = 0; i < n; i++) {
        s = gst_caps_get_structure (sinkcaps, i);
        gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT32,
            "height", GST_TYPE_INT_RANGE, 1, G_MAXINT32,
            "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT32, 1, NULL);
        if (!gst_structure_has_field (s, "pixel-aspect-ratio"))
          gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
              NULL);

        gst_structure_remove_fields (s, "colorimetry", "chroma-site", "format",
            NULL);
      }

      filtered_caps = sinkcaps;
      if (filter)
        filtered_caps = gst_caps_intersect (sinkcaps, filter);
      caps = gst_caps_intersect (filtered_caps, template_caps);

      gst_caps_unref (sinkcaps);
      if (filter)
        gst_caps_unref (filtered_caps);
      if (had_current_caps)
        gst_caps_unref (template_caps);

      GST_LOG_OBJECT(bpad, "query sink caps: %" GST_PTR_FORMAT, caps);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      ret = TRUE;
      break;
    }
    default:
      ret = GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, bpad, query);
      break;
  }

  return ret;
}

static gboolean
gst_imxcompositor_src_query (GstAggregator * agg, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;
      GstStructure *s;
      gint n;

      GstVideoAggregator *vagg = GST_VIDEO_AGGREGATOR (agg);

      gst_query_parse_caps (query, &filter);

      if (GST_VIDEO_INFO_FORMAT (&vagg->info) != GST_VIDEO_FORMAT_UNKNOWN)
        caps = gst_video_info_to_caps (&vagg->info);
      else
        caps = gst_pad_get_pad_template_caps (agg->srcpad);

      caps = gst_caps_make_writable (caps);
      n = gst_caps_get_size (caps) - 1;
      for (; n >= 0; n--) {
        s = gst_caps_get_structure (caps, n);
        gst_structure_set (s, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT32,
            "height", GST_TYPE_INT_RANGE, 1, G_MAXINT32, NULL);
        if (GST_VIDEO_INFO_FPS_D (&vagg->info) != 0) {
          gst_structure_set (s,
              "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT32, 1, NULL);
        }
      }

      if (filter)
        caps = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);

      gst_query_set_caps_result (query, caps);
      GST_LOG ("query src caps: %" GST_PTR_FORMAT, caps);
      gst_caps_unref (caps);

      res = TRUE;
      break;
    }
    default:
      res = GST_AGGREGATOR_CLASS (parent_class)->src_query (agg, query);
      break;
  }

  return res;
}

static gboolean
gst_imxcompositor_negotiated_caps (GstVideoAggregator * vagg, GstCaps * caps)
{
  GstImxCompositor *imxcomp = (GstImxCompositor *) (vagg);
  GstQuery *query;
  gboolean result = TRUE;
  GstStructure *config = NULL;
  guint size, num, min = 0, max = 0;
  GstAggregator *agg = GST_AGGREGATOR (imxcomp);

  GST_DEBUG("negotiated caps: %" GST_PTR_FORMAT, caps);

  if (imxcomp->self_out_pool) {
    GstCaps *pool_caps;
    config = gst_buffer_pool_get_config (imxcomp->self_out_pool);
    gst_buffer_pool_config_get_params(config, &pool_caps, &size, &min, &max);
    gst_structure_free (config);
    if (gst_caps_is_equal(pool_caps, caps)) {
      imxcomp->out_pool = imxcomp->self_out_pool;
      return TRUE;
    }
  }

  /* find a pool for the negotiated caps now */
  GST_DEBUG_OBJECT (imxcomp, "doing allocation query");
  query = gst_query_new_allocation (caps, TRUE);
  if (!gst_pad_peer_query (agg->srcpad, query)) {
    // nothing, just print
    GST_DEBUG_OBJECT (imxcomp, "peer ALLOCATION query failed");
  }

  GstBufferPool *pool = NULL;
  GstVideoInfo vinfo;
  GstAllocator *allocator = NULL;

  gst_video_info_init(&vinfo);
  gst_video_info_from_caps(&vinfo, caps);
  size = vinfo.size;

  num = gst_query_get_n_allocation_pools(query);
  GST_DEBUG_OBJECT(imxcomp, "number of allocation pools: %d", num);

  /* if downstream element provided buffer pool with phy buffers */
  if (num > 0) {
    guint i = 0;
    while (i < num ) {
      gst_query_parse_nth_allocation_pool(query, i, &pool, &size, &min, &max);
      if (pool) {
        gst_query_parse_nth_allocation_param (query, i, &allocator, NULL);

        if (allocator && GST_IS_ALLOCATOR_PHYMEM(allocator)) {
          gst_imxcompositor_set_pool_alignment(caps, pool);
          if (min < IMX_COMPOSITOR_OUTPUT_POOL_MIN_BUFFERS)
            min = IMX_COMPOSITOR_OUTPUT_POOL_MIN_BUFFERS;
          max = IMX_COMPOSITOR_OUTPUT_POOL_MAX_BUFFERS;
          config = gst_buffer_pool_get_config (pool);
          gst_buffer_pool_config_set_params (config, caps, size, min, max);
          gst_buffer_pool_set_config (pool, config);
          GST_IMX_COMPOSITOR_UNREF_POOL (imxcomp->out_pool);
          imxcomp->out_pool = pool;
          gst_object_unref (allocator);
          gst_query_unref (query);
          imxcomp->negotiated = TRUE;
          imxcomp->out_pool_update = TRUE;
          return TRUE;
        } else {
          GST_LOG_OBJECT (imxcomp, "no phy allocator in output pool (%p)",pool);
        }

        if (allocator) {
          gst_object_unref (allocator);
          allocator = NULL;
        }

        gst_object_unref (pool);
      }
      i++;
    }
  }
  gst_query_unref (query);

  size = PAGE_ALIGN(MAX(size, vinfo.size));

  /* downstream doesn't provide a pool or the pool has no ability to allocate
   * physical memory buffers, we need create new pool */
  GST_DEBUG_OBJECT(imxcomp, "creating new output pool");
  pool = gst_imxcompositor_create_bufferpool(imxcomp, caps, size,
      IMX_COMPOSITOR_OUTPUT_POOL_MIN_BUFFERS,
      IMX_COMPOSITOR_OUTPUT_POOL_MAX_BUFFERS);
  if (pool) {
    if (imxcomp->self_out_pool != imxcomp->out_pool) {
      GST_IMX_COMPOSITOR_UNREF_POOL(imxcomp->self_out_pool);
      GST_IMX_COMPOSITOR_UNREF_POOL (imxcomp->out_pool);
    } else {
      GST_IMX_COMPOSITOR_UNREF_POOL(imxcomp->self_out_pool);
    }
    imxcomp->self_out_pool = pool;
    imxcomp->out_pool = pool;
    gst_buffer_pool_set_active(pool, TRUE);
    GST_DEBUG_OBJECT(imxcomp, "pool config:  outcaps: %" GST_PTR_FORMAT "  "
        "size: %u  min buffers: %u  max buffers: %u", caps, size,
        IMX_COMPOSITOR_OUTPUT_POOL_MIN_BUFFERS,
        IMX_COMPOSITOR_OUTPUT_POOL_MAX_BUFFERS);
    imxcomp->negotiated = TRUE;
    imxcomp->out_pool_update = TRUE;
  } else {
    GST_WARNING_OBJECT (imxcomp, "Failed to decide allocation");
    imxcomp->negotiated = FALSE;
  }

  return imxcomp->negotiated;
}

static GstFlowReturn
gst_imxcompositor_get_output_buffer (GstVideoAggregator * vagg,
    GstBuffer ** outbuf)
{
  GstImxCompositor *imxcomp = (GstImxCompositor *) (vagg);

  if (!gst_buffer_pool_set_active (imxcomp->out_pool, TRUE)) {
    GST_ELEMENT_ERROR (imxcomp, RESOURCE, SETTINGS,
        ("failed to activate bufferpool"), ("failed to activate bufferpool"));
    return GST_FLOW_ERROR;
  }

  GstFlowReturn ret =
      gst_buffer_pool_acquire_buffer (imxcomp->out_pool, outbuf, NULL);

  GST_LOG_OBJECT(imxcomp, "Get out buffer %p from pool %p",
      *outbuf, imxcomp->out_pool);
  return ret;
}

static gboolean
gst_imxcompositor_update_info (GstVideoAggregator * vagg, GstVideoInfo * info)
{
  GList *l;
  gint best_width = -1, best_height = -1;
  gboolean ret = TRUE;

  GST_OBJECT_LOCK (vagg);
  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *vaggpad = l->data;
    GstImxCompositorPad *pad = GST_IMXCOMPOSITOR_PAD (vaggpad);
    gint this_width, this_height;
    gint width, height;

    width = GST_VIDEO_INFO_WIDTH (&vaggpad->info);
    height = GST_VIDEO_INFO_HEIGHT (&vaggpad->info);

    if (width == 0 || height == 0)
      continue;

    if (pad->width)
      width = pad->width;
    if (pad->height)
      height = pad->height;

    this_width = width + MAX (pad->xpos, 0);
    this_height = height + MAX (pad->ypos, 0);

    if (best_width < this_width)
      best_width = this_width;
    if (best_height < this_height)
      best_height = this_height;
  }
  GST_OBJECT_UNLOCK (vagg);

  if (best_width > 0 && best_height > 0) {
    gst_video_info_set_format (info, GST_VIDEO_INFO_FORMAT (info),
        best_width, best_height);
    GST_DEBUG("update output info %dx%d", best_width, best_height);
  }

  return ret;
}

static gint gst_imxcompositor_config_dst(GstImxCompositor *imxcomp,
    GstBuffer * outbuf, Imx2DFrame *dst)
{
  GstVideoFrame out_frame;
  GstPhyMemMeta *phymemmeta = NULL;

  if (!gst_buffer_is_phymem(outbuf)) {
    GST_ERROR("out buffer is not phy memory");
    return -1;
  }

  if (!gst_video_frame_map (&out_frame, &((GstVideoAggregator*)imxcomp)->info,
      outbuf, GST_MAP_WRITE)) {
    return -1;
  }

  if (imxcomp->out_pool_update) {
    if (imxcomp->out_pool) {
      GstStructure *config = gst_buffer_pool_get_config (imxcomp->out_pool);
      memset (&imxcomp->out_align, 0, sizeof(GstVideoAlignment));

      if (gst_buffer_pool_config_has_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
        gst_buffer_pool_config_get_video_alignment (config,&imxcomp->out_align);
        GST_DEBUG ("output pool has alignment (%d, %d) , (%d, %d)",
            imxcomp->out_align.padding_left, imxcomp->out_align.padding_top,
            imxcomp->out_align.padding_right,imxcomp->out_align.padding_bottom);
      }
      gst_structure_free (config);
    }

    /* set physical memory padding info */
    if (imxcomp->self_out_pool
        && gst_buffer_is_writable (out_frame.buffer)) {
      phymemmeta = GST_PHY_MEM_META_ADD (out_frame.buffer);
      phymemmeta->x_padding = imxcomp->out_align.padding_right;
      phymemmeta->y_padding = imxcomp->out_align.padding_bottom;
      GST_DEBUG_OBJECT (imxcomp, "out physical memory meta x_padding: %d "
          "y_padding: %d", phymemmeta->x_padding, phymemmeta->y_padding);
    }

    imxcomp->out_pool_update = FALSE;
  }

  dst->info.fmt = GST_VIDEO_INFO_FORMAT(&(out_frame.info));
  dst->info.w = out_frame.info.width +
      imxcomp->out_align.padding_left + imxcomp->out_align.padding_right;
  dst->info.h = out_frame.info.height +
      imxcomp->out_align.padding_top + imxcomp->out_align.padding_bottom;
  dst->info.stride = out_frame.info.stride[0];

  GST_LOG ("Output: %s, %dx%d",
      GST_VIDEO_FORMAT_INFO_NAME(out_frame.info.finfo),
      out_frame.info.width, out_frame.info.height);

  if (imxcomp->device->config_output(imxcomp->device, &dst->info) < 0) {
    GST_ERROR ("config output failed");
    gst_video_frame_unmap (&out_frame);
    return -1;
  }

  dst->mem = gst_buffer_query_phymem_block (out_frame.buffer);
  dst->alpha = 0xFF; //TODO how to use destination alpha?
  dst->rotate = IMX_2D_ROTATION_0;
  dst->interlace = IMX_2D_DEINTERLACE_NONE;
  dst->crop.x = 0;
  dst->crop.y = 0;
  dst->crop.w = out_frame.info.width;
  dst->crop.h = out_frame.info.height;

  GstVideoCropMeta *out_crop = NULL;
  out_crop = gst_buffer_get_video_crop_meta(out_frame.buffer);
  if (out_crop != NULL) {
    GST_LOG ("output crop meta: (%d, %d, %d, %d)", out_crop->x, out_crop->y,
        out_crop->width, out_crop->height);
    if ((out_crop->x >= out_frame.info.width)
        || (out_crop->y >= out_frame.info.height)) {
      gst_video_frame_unmap (&out_frame);
      return -1;
    }

    dst->crop.x += out_crop->x;
    dst->crop.y += out_crop->y;
    dst->crop.w = MIN(out_crop->width, (out_frame.info.width - out_crop->x));
    dst->crop.h = MIN(out_crop->height,(out_frame.info.height-out_crop->y));
  }

  gst_video_frame_unmap (&out_frame);
  return 0;
}

static gint gst_imxcompositor_config_src(GstImxCompositor *imxcomp,
    GstImxCompositorPad *pad, Imx2DFrame *src)
{
  GstPhyMemMeta *phymemmeta = NULL;
  GstVideoFrame temp_in_frame, frame;
  GstVideoCropMeta *in_crop = NULL;
  GstVideoAggregatorPad *ppad = (GstVideoAggregatorPad *)pad;

  if (!gst_video_frame_map (&frame, &ppad->buffer_vinfo, ppad->buffer,
          GST_MAP_READ)) {
    GST_WARNING_OBJECT (pad, "Could not map input buffer");
    return -1;
  }

  GstVideoFrame *inframe = &frame;
  /* Check if need copy input frame */
  if (!gst_buffer_is_phymem(ppad->buffer)) {
    GST_DEBUG_OBJECT (pad, "copy input frame to phy memory");
    if (!imxcomp->allocator)
      imxcomp->allocator =
               gst_imx_2d_device_allocator_new((gpointer)(imxcomp->device));

    if (!imxcomp->sink_tmp_buf)
      imxcomp->sink_tmp_buf = gst_buffer_new_allocate(imxcomp->allocator,
          SINK_TEMP_BUFFER_SIZE, NULL);

    if (imxcomp->sink_tmp_buf) {
      gst_video_frame_map(&temp_in_frame, &(inframe->info),
          imxcomp->sink_tmp_buf, GST_MAP_WRITE);
      gst_video_frame_copy(&temp_in_frame, inframe);
      inframe = &temp_in_frame;
      gst_video_frame_unmap(&temp_in_frame);
    } else {
      GST_ERROR_OBJECT (pad,
          "Can't get input buffer,ignore this frame,continue next");
      gst_video_frame_unmap (&frame);
      return -1;
    }
  }

  gst_video_frame_unmap (&frame);

  if (pad->sink_pool_update) {
    memset (&pad->align, 0, sizeof(GstVideoAlignment));
    if (pad->sink_pool && gst_buffer_pool_is_active (pad->sink_pool)) {
      GstStructure *config = gst_buffer_pool_get_config (pad->sink_pool);

      if (gst_buffer_pool_config_has_option (config,
          GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
        gst_buffer_pool_config_get_video_alignment (config, &pad->align);
        GST_DEBUG_OBJECT (pad, "input pool has alignment (%d, %d) , (%d, %d)",
            pad->align.padding_left, pad->align.padding_top,
            pad->align.padding_right, pad->align.padding_bottom);
      }
      gst_structure_free (config);
    } else {
      phymemmeta = GST_PHY_MEM_META_GET (ppad->buffer);
      if (phymemmeta) {
        pad->align.padding_right = phymemmeta->x_padding;
        pad->align.padding_bottom = phymemmeta->y_padding;
        GST_DEBUG_OBJECT (pad, "physical memory meta x_padding: %d "
            "y_padding: %d",phymemmeta->x_padding, phymemmeta->y_padding);
      }
    }
    pad->sink_pool_update = FALSE;
  }

  src->info.fmt = GST_VIDEO_INFO_FORMAT(&(inframe->info));
  src->info.w = inframe->info.width +
                pad->align.padding_left + pad->align.padding_right;
  src->info.h = inframe->info.height +
                pad->align.padding_top + pad->align.padding_bottom;
  src->info.stride = inframe->info.stride[0];

  GST_LOG_OBJECT (pad, "Input: %s, %dx%d", GST_VIDEO_FORMAT_INFO_NAME(inframe->info.finfo),
      inframe->info.width, inframe->info.height);

  if (imxcomp->device->config_input(imxcomp->device, &src->info) < 0) {
    GST_ERROR_OBJECT (pad, "config input failed");
    return -1;
  }

  src->mem = gst_buffer_query_phymem_block (inframe->buffer);
  src->alpha = (gint)(pad->alpha * 255);
  src->rotate = pad->rotate;
  src->interlace = IMX_2D_DEINTERLACE_NONE;
  src->crop.x = 0;
  src->crop.y = 0;
  src->crop.w = inframe->info.width;
  src->crop.h = inframe->info.height;

  in_crop = gst_buffer_get_video_crop_meta(ppad->buffer);
  if (in_crop != NULL) {
    GST_LOG_OBJECT (pad, "input crop meta: (%d, %d, %d, %d)",
        in_crop->x, in_crop->y, in_crop->width, in_crop->height);
    if ((in_crop->x >= inframe->info.width)
        || (in_crop->y >= inframe->info.height))
      return -1;

    src->crop.x += in_crop->x;
    src->crop.y += in_crop->y;
    src->crop.w = MIN(in_crop->width, (inframe->info.width - in_crop->x));
    src->crop.h = MIN(in_crop->height, (inframe->info.height - in_crop->y));
  }

  return 0;
}

#ifdef USE_GST_VIDEO_SAMPLE_CONVERT
static void
gst_imxcompositor_fill_background(Imx2DFrame *dst, guint RGBA8888)
{
  GstVideoInfo vinfo;
  GstCaps *from_caps, *to_caps;
  GstBuffer *from_buffer, *to_buffer;
  GstSample *from_sample, *to_sample;
  gint i;
  GstMapInfo map;

  from_buffer = gst_buffer_new_and_alloc (dst->info.stride * dst->info.h * 4);

  gst_buffer_map (from_buffer, &map, GST_MAP_WRITE);
  for (i = 0; i < dst->info.stride * dst->info.h; i++) {
    map.data[4 * i + 0] = RGBA8888 & 0x000000FF;
    map.data[4 * i + 1] = (RGBA8888 & 0x0000FF00) >> 8;
    map.data[4 * i + 2] = (RGBA8888 & 0x00FF0000) >> 16;
    map.data[4 * i + 3] = (RGBA8888 & 0xFF000000) >> 24;
  }
  gst_buffer_unmap (from_buffer, &map);

  gst_video_info_init (&vinfo);
  gst_video_info_set_format (&vinfo, GST_VIDEO_FORMAT_RGBA,
      dst->info.w, dst->info.h);
  from_caps = gst_video_info_to_caps (&vinfo);
  from_sample = gst_sample_new (from_buffer, from_caps, NULL, NULL);

  gst_video_info_set_format (&vinfo, dst->info.fmt, dst->info.w, dst->info.h);
  to_caps = gst_video_info_to_caps (&vinfo);
  to_sample = gst_video_convert_sample (from_sample, to_caps, GST_SECOND, NULL);
  if (to_sample) {
    to_buffer = gst_sample_get_buffer(to_sample);
    gst_buffer_map (to_buffer, &map, GST_MAP_READ);
    memcpy(dst->mem->vaddr, map.data, map.size);
    gst_buffer_unmap(to_buffer, &map);
    gst_sample_unref (to_sample);
  }
  gst_buffer_unref (from_buffer);
  gst_caps_unref (from_caps);
  gst_sample_unref (from_sample);
  gst_caps_unref (to_caps);
}
#else
static void
gst_imxcompositor_fill_background(Imx2DFrame *dst, guint RGBA8888)
{
  gchar *p = dst->mem->vaddr;
  gint i;
  gint R,G,B,A;
  gdouble Y,U,V;

  R = RGBA8888 & 0x000000FF;
  G = (RGBA8888 & 0x0000FF00) >> 8;
  B = (RGBA8888 & 0x00FF0000) >> 16;
  A = (RGBA8888 & 0xFF000000) >> 24;

  //BT.709
  Y = (0.213*R + 0.715*G + 0.072*B);
  U = -0.117*R - 0.394*G + 0.511*B + 128;
  V = 0.511*R - 0.464*G - 0.047*B + 128;

  if (Y > 255.0)  Y = 255;
  if (U < 0.0) U = 0;
  if (U > 255.0) U = 255;
  if (V < 0.0) V = 0;
  if (V > 255.0) V = 255;

  switch (dst->info.fmt) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
      for (i = 0; i < dst->mem->size/4; i++) {
        p[4 * i + 0] = R;
        p[4 * i + 1] = G;
        p[4 * i + 2] = B;
        p[4 * i + 3] = A;
      }
      break;
    case GST_VIDEO_FORMAT_BGR:
      for (i = 0; i < dst->mem->size/3; i++) {
        p[3 * i + 0] = B;
        p[3 * i + 1] = G;
        p[3 * i + 2] = R;
      }
      break;
    case GST_VIDEO_FORMAT_RGB:
      for (i = 0; i < dst->mem->size/3; i++) {
        p[3 * i + 0] = R;
        p[3 * i + 1] = G;
        p[3 * i + 2] = B;
      }
      break;
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      for (i = 0; i < dst->mem->size/4; i++) {
        p[4 * i + 0] = B;
        p[4 * i + 1] = G;
        p[4 * i + 2] = R;
        p[4 * i + 3] = A;
      }
      break;
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_xBGR:
      for (i = 0; i < dst->mem->size/4; i++) {
        p[4 * i + 0] = A;
        p[4 * i + 1] = B;
        p[4 * i + 2] = G;
        p[4 * i + 3] = R;
      }
      break;
    case GST_VIDEO_FORMAT_RGB16:
      for (i = 0; i < dst->mem->size/2; i++) {
        p[2 * i + 0] = ((G<<3) & 0xE0) | (B>>3);
        p[2 * i + 1] = (R & 0xF8) | (G>>5);
      }
      break;
    case GST_VIDEO_FORMAT_BGR16:
      for (i = 0; i < dst->mem->size/2; i++) {
        p[2 * i + 0] = ((G<<3) & 0xE0) | (R>>3);
        p[2 * i + 1] = (B & 0xF8) | (G>>5);
      }
      break;
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xRGB:
      for (i = 0; i < dst->mem->size/4; i++) {
        p[4 * i + 0] = A;
        p[4 * i + 1] = R;
        p[4 * i + 2] = G;
        p[4 * i + 3] = B;
      }
      break;
    case GST_VIDEO_FORMAT_Y444:
      memset(p, Y, dst->info.w*dst->info.h);
      memset(p+dst->info.w*dst->info.h, U, dst->info.w*dst->info.h);
      memset(p+dst->info.w*dst->info.h*2, V, dst->info.w*dst->info.h);
      break;
    case GST_VIDEO_FORMAT_I420:
      memset(p, Y, dst->info.w*dst->info.h);
      memset(p+dst->info.w*dst->info.h, U, dst->info.w*dst->info.h/4);
      memset(p+dst->info.w*dst->info.h*5/4, V, dst->info.w*dst->info.h/4);
      break;
    case GST_VIDEO_FORMAT_YV12:
      memset(p, Y, dst->info.w*dst->info.h);
      memset(p+dst->info.w*dst->info.h, V, dst->info.w*dst->info.h/4);
      memset(p+dst->info.w*dst->info.h*5/4, U, dst->info.w*dst->info.h/4);
      break;
    case GST_VIDEO_FORMAT_NV12:
      memset(p, Y, dst->info.w*dst->info.h);
      p += dst->info.w*dst->info.h;
      for (i = 0; i < dst->info.w*dst->info.h/4; i++) {
        *p++ = U;
        *p++ = V;
      }
      break;
    case GST_VIDEO_FORMAT_NV21:
      memset(p, Y, dst->info.w*dst->info.h);
      p += dst->info.w*dst->info.h;
      for (i = 0; i < dst->info.w*dst->info.h/4; i++) {
        *p++ = V;
        *p++ = U;
      }
      break;
    case GST_VIDEO_FORMAT_UYVY:
      for (i = 0; i < dst->info.w*dst->info.h/2; i++) {
        *p++ = U;
        *p++ = Y;
        *p++ = V;
        *p++ = Y;
      }
      break;
    case GST_VIDEO_FORMAT_Y42B:
      memset(p, Y, dst->info.w*dst->info.h);
      memset(p+dst->info.w*dst->info.h, U, dst->info.w*dst->info.h/2);
      memset(p+dst->info.w*dst->info.h*3/2, V, dst->info.w*dst->info.h/2);
      break;
    case GST_VIDEO_FORMAT_v308:
      for (i = 0; i < dst->info.w*dst->info.h; i++) {
        *p++ = Y;
        *p++ = U;
        *p++ = V;
      }
      break;
    case GST_VIDEO_FORMAT_GRAY8:
      memset(p, Y, dst->info.w*dst->info.h);
      break;
    case GST_VIDEO_FORMAT_NV16:
      memset(p, Y, dst->info.w*dst->info.h);
      p += dst->info.w*dst->info.h;
      for (i = 0; i < dst->info.w*dst->info.h/2; i++) {
        *p++ = U;
        *p++ = V;
      }
      break;
    default:
      GST_FIXME("Add support for %d", dst->info.fmt);
      memset(dst->mem->vaddr, 0, dst->mem->size);
      break;
  }
}
#endif

static GstFlowReturn
gst_imxcompositor_aggregate_frames (GstVideoAggregator * vagg,
                                    GstBuffer * outbuf)
{
  GList *l;
  GstImxCompositor *imxcomp = (GstImxCompositor *) (vagg);
  Imx2DDevice *device = imxcomp->device;
  GstFlowReturn ret;

  Imx2DFrame src, dst;
  guint aggregated = 0;

  if (!device)
    return GST_FLOW_ERROR;

  if (gst_imxcompositor_config_dst(imxcomp, outbuf, &dst) < 0)
    return GST_FLOW_ERROR;

  GST_OBJECT_LOCK (vagg);

  if (imxcomp->background_enable) {
    if (device->fill) {
      if(device->fill (device, &dst, imxcomp->background) < 0) {
        GST_LOG("fill color background by device failed");
        gst_imxcompositor_fill_background(&dst, imxcomp->background);
      }
    } else {
      GST_LOG("device has no fill interface");
      gst_imxcompositor_fill_background(&dst, imxcomp->background);
    }
  } else {
    gst_imxcompositor_fill_background(&dst, DEFAULT_IMXCOMPOSITOR_BACKGROUND);
  }

  for (l = GST_ELEMENT (vagg)->sinkpads; l; l = l->next) {
    GstVideoAggregatorPad *ppad = l->data;
    GstImxCompositorPad *pad = GST_IMXCOMPOSITOR_PAD (ppad);

    if (ppad->buffer != NULL) {
#if 0 //what's for?
      GstSegment *seg = &((GstAggregatorPad*)pad)->segment;
      GstClockTime timestamp = GST_BUFFER_TIMESTAMP (pad->buffer);
      gint64 stream_time =
          gst_segment_to_stream_time (seg, GST_FORMAT_TIME, timestamp);
      /* sync object properties on stream time */
      if (GST_CLOCK_TIME_IS_VALID (stream_time))
        gst_object_sync_values (GST_OBJECT (pad), stream_time);
#endif

      if (gst_imxcompositor_config_src(imxcomp, pad, &src) < 0) {
        continue;
      }
      if (device->set_rotate(device, src.rotate) < 0) {
        GST_WARNING_OBJECT (pad, "set rotate failed");
        continue;
      }
      if (device->set_deinterlace(device, src.interlace) < 0) {
        GST_WARNING_OBJECT (pad, "set deinterlace mode failed");
        continue;
      }

      //update destination location and size
      dst.crop.x = pad->xpos;
      dst.crop.y = pad->ypos;
      dst.crop.w = pad->width ? pad->width : ppad->buffer_vinfo.width;
      dst.crop.h = pad->height ? pad->height : ppad->buffer_vinfo.height;

      if (device->blend(device, &dst, &src) < 0) {
        GST_WARNING_OBJECT (pad, "frame blend fail");
        continue;
      }

      aggregated++;
    }
  }

  GST_LOG("Aggregated %d frames", aggregated);
  if (aggregated > 0 && device->blend_finish(device) < 0) {
    GST_ERROR ("frame blend finish fail");
  }

  GST_OBJECT_UNLOCK (vagg);

  return GST_FLOW_OK;
}

static GstCaps* imx_compositor_caps_from_fmt_list(GList* list)
{
  gint i;
  GstCaps *caps = NULL;

  for (i=0; i<g_list_length (list); i++) {
    GstVideoFormat fmt = (GstVideoFormat)g_list_nth_data(list, i);
    if (caps) {
      GstCaps *newcaps = gst_caps_new_simple("video/x-raw",
          "format", G_TYPE_STRING, gst_video_format_to_string(fmt), NULL);
      gst_caps_append (caps, newcaps);
    } else {
      caps = gst_caps_new_simple("video/x-raw",
          "format", G_TYPE_STRING, gst_video_format_to_string(fmt), NULL);
    }
  }
  return caps;
}

static void
gst_imxcompositor_class_init (GstImxCompositorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstVideoAggregatorClass *videoaggregator_class =
      (GstVideoAggregatorClass *) klass;
  GstAggregatorClass *agg_class = (GstAggregatorClass *) klass;
  GstCaps *caps;

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->finalize = gst_imxcompositor_finalize;
  gobject_class->get_property = gst_imxcompositor_get_property;
  gobject_class->set_property = gst_imxcompositor_set_property;

  Imx2DDeviceInfo *in_plugin = (Imx2DDeviceInfo *)g_type_get_qdata (
                G_OBJECT_CLASS_TYPE (klass), GST_IMX_COMPOSITOR_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  Imx2DDevice* dev = in_plugin->create(in_plugin->device_type);
  if (!dev)
    return;

  gchar longname[64] = {0};
  snprintf(longname, 32, "IMX %s Video Compositor", in_plugin->name);
  gst_element_class_set_static_metadata (gstelement_class, longname,
      "Filter/Editor/Video/ImxCompositor", "Composite multiple video streams",
      IMX_GST_PLUGIN_AUTHOR);

  GList *list = dev->get_supported_in_fmts(dev);
  caps = imx_compositor_caps_from_fmt_list(list);
  g_list_free(list);

  if (!caps) {
    GST_ERROR ("Couldn't create caps for device '%s'", in_plugin->name);
    caps = gst_caps_new_empty_simple ("video/x-raw");
  }
  gst_element_class_add_pad_template (gstelement_class,
      gst_pad_template_new ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST, caps));

  list = dev->get_supported_out_fmts(dev);
  caps = imx_compositor_caps_from_fmt_list(list);
  g_list_free(list);

  if (!caps) {
    GST_ERROR ("Couldn't create caps for device '%s'", in_plugin->name);
    caps = gst_caps_new_empty_simple ("video/x-raw");
  }
  gst_element_class_add_pad_template (gstelement_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps));

  klass->in_plugin = in_plugin;
  in_plugin->destroy(dev);

  agg_class->sinkpads_type = GST_TYPE_IMXCOMPOSITOR_PAD;
  agg_class->sink_query = gst_imxcompositor_sink_query;
  agg_class->src_query = gst_imxcompositor_src_query;

  videoaggregator_class->disable_frame_conversion = TRUE;
  videoaggregator_class->update_info = gst_imxcompositor_update_info;
  videoaggregator_class->aggregate_frames = gst_imxcompositor_aggregate_frames;
  videoaggregator_class->negotiated_caps = gst_imxcompositor_negotiated_caps;
  videoaggregator_class->get_output_buffer=gst_imxcompositor_get_output_buffer;

  g_object_class_install_property (gobject_class,
      PROP_IMXCOMPOSITOR_BACKGROUND_ENABLE,
      g_param_spec_boolean ("back-enable", "Background Enable",
          "Enable Fill Background color", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_IMXCOMPOSITOR_BACKGROUND_COLOR,
      g_param_spec_uint ("background", "Background",
          "Background color (ARGB format)",
          0, 0xFFFFFFFF, DEFAULT_IMXCOMPOSITOR_BACKGROUND,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

#if 0
  g_object_class_install_property (gobject_class,
      PROP_IMXCOMPOSITOR_OUTPUT_WIDTH,
      g_param_spec_uint ("output-width", "Output width",
          "Output frame width", 64, 4096, DEFAULT_IMXCOMPOSITOR_BACKGROUND,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class,
      PROP_IMXCOMPOSITOR_OUTPUT_HEIGHT,
      g_param_spec_uint ("output-height", "Output Height",
          "Output frame height", 64, 4096, DEFAULT_IMXCOMPOSITOR_BACKGROUND,
          G_PARAM_READWRITE));
#endif
}

static void
gst_imxcompositor_init (GstImxCompositor * imxcomp)
{
  imxcomp->background = DEFAULT_IMXCOMPOSITOR_BACKGROUND;
  imxcomp->background_enable = TRUE;
  GstImxCompositorClass *klass =
      (GstImxCompositorClass *) G_OBJECT_GET_CLASS (imxcomp);

  if (klass->in_plugin)
    imxcomp->device = klass->in_plugin->create(klass->in_plugin->device_type);

  if (imxcomp->device) {
    if (imxcomp->device->open(imxcomp->device) < 0) {
      GST_ERROR ("Open 2D device failed.");
    } else {
      imxcomp->sink_tmp_buf = NULL;
      imxcomp->out_pool = NULL;
      imxcomp->self_out_pool = NULL;
      imxcomp->out_pool_update = TRUE;
      imxcomp->allocator = NULL;
      imxcomp->capabilities =imxcomp->device->get_capabilities(imxcomp->device);
      memset (&imxcomp->out_align, 0, sizeof(GstVideoAlignment));
    }
  } else {
    GST_ERROR ("Create 2D device failed.");
  }
}

static gboolean gst_imx_compositor_register (GstPlugin * plugin)
{
  GTypeInfo tinfo = {
    sizeof (GstImxCompositorClass),
    NULL,
    NULL,
    (GClassInitFunc) gst_imxcompositor_class_init,
    NULL,
    NULL,
    sizeof (GstImxCompositor),
    0,
    (GInstanceInitFunc) gst_imxcompositor_init,
  };

  GType type;
  gchar *t_name;

  const Imx2DDeviceInfo *in_plugin = imx_get_2d_devices();

  while (in_plugin->name) {
    GST_LOG ("Registering %s compositor", in_plugin->name);

    if (!in_plugin->is_exist()) {
      GST_WARNING("device %s not exist", in_plugin->name);
      in_plugin++;
      continue;
    }

    t_name = g_strdup_printf ("imxcompositor_%s", in_plugin->name);
    type = g_type_from_name (t_name);

    if (!type) {
      type = g_type_register_static (GST_TYPE_VIDEO_AGGREGATOR,
                                      t_name, &tinfo, 0);
      g_type_set_qdata (type, GST_IMX_COMPOSITOR_PARAMS_QDATA,
                        (gpointer) in_plugin);
    }

    if (!gst_element_register (plugin, t_name, IMX_GST_PLUGIN_RANK, type)) {
      GST_ERROR ("Failed to register %s", t_name);
      g_free (t_name);
      return FALSE;
    }
    g_free (t_name);

    in_plugin++;
  }

  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_imxcompositor_debug, "imxcompositor", 0,
      "Freescale IMX Video Compositor");

  return gst_imx_compositor_register (plugin);
}

IMX_GST_PLUGIN_DEFINE(imxcompositor, "IMX Video Composition Plugins",
                      plugin_init);