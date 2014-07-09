/* GStreamer IMX G2D Video Processing
 * Copyright (c) 2014, Freescale Semiconductor, Inc. All rights reserved.
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

#include <fcntl.h>
#include <sys/ioctl.h>
#include "g2d.h"
#include "videoprocessdevice.h"

GST_DEBUG_CATEGORY_EXTERN (imxvideoconvert_debug);
#define GST_CAT_DEFAULT imxvideoconvert_debug

typedef struct _ImxVpDeviceG2d {
  gint capabilities;
  struct g2d_surface src;
  struct g2d_surface dst;
} ImxVpDeviceG2d;

typedef struct {
  GstVideoFormat gst_video_format;
  guint g2d_format;
  gchar *name;
  guint bpp;
} G2dFmtMap;

static G2dFmtMap g2d_fmts_map[] = {
    {GST_VIDEO_FORMAT_RGB16,  G2D_RGB565,   "RGB16",  16},
    {GST_VIDEO_FORMAT_RGBx,   G2D_RGBX8888, "RGBx",   32},
    {GST_VIDEO_FORMAT_RGBA,   G2D_RGBA8888, "RGBA",   32},
    {GST_VIDEO_FORMAT_BGRA,   G2D_BGRA8888, "BGRA",   32},
    {GST_VIDEO_FORMAT_BGRx,   G2D_BGRX8888, "BGRx",   32},
    {GST_VIDEO_FORMAT_BGR16,  G2D_BGR565,   "BGR16",  16},
    {GST_VIDEO_FORMAT_ARGB,   G2D_ARGB8888, "ARGB",   32},
    {GST_VIDEO_FORMAT_ABGR,   G2D_ABGR8888, "ABGR",   32},
    {GST_VIDEO_FORMAT_xRGB,   G2D_XRGB8888, "xRGB",   32},
    {GST_VIDEO_FORMAT_xBGR,   G2D_XBGR8888, "xBGR",   32},

    //this only for separate YUV format and RGB format
    {GST_VIDEO_FORMAT_UNKNOWN, -1,          "yuv",     1},

    {GST_VIDEO_FORMAT_I420,   G2D_I420,     "I420",   12},
    {GST_VIDEO_FORMAT_NV12,   G2D_NV12,     "NV12",   12},
    {GST_VIDEO_FORMAT_UYVY,   G2D_UYVY,     "UYVY",   16},
    {GST_VIDEO_FORMAT_YV12,   G2D_YV12,     "YV12",   12},
    {GST_VIDEO_FORMAT_YUY2,   G2D_YUYV,     "YUY2",   16},
    {GST_VIDEO_FORMAT_NV16,   G2D_NV16,     "NV16",   16},
    {GST_VIDEO_FORMAT_NV21,   G2D_NV21,     "NV21",   12},
    {GST_VIDEO_FORMAT_YVYU,   G2D_YVYU,     "YVYU",   16},

/* There is no corresponding GST Video format for those G2D formats
    {GST_VIDEO_FORMAT_VYUY,   G2D_VYUY,     "VYUY",   16},
    {GST_VIDEO_FORMAT_NV61,   G2D_NV61,     "NV61",   16},
*/
    {GST_VIDEO_FORMAT_UNKNOWN, -1,          NULL,     0}
};

static const G2dFmtMap * imx_g2d_get_format(GstVideoFormat format)
{
  const G2dFmtMap *map = g2d_fmts_map;
  while(map->name) {
    if (map->gst_video_format == format)
      return map;
    map++;
  };

  GST_ERROR ("g2d : format (%x) is not supported.",
              gst_video_format_to_string(format));

  return NULL;
}

static gint imx_g2d_open(ImxVideoProcessDevice *device)
{
  if (!device)
    return -1;

  ImxVpDeviceG2d *g2d = g_slice_alloc(sizeof(ImxVpDeviceG2d));
  if (!g2d) {
    GST_ERROR("allocate g2d structure failed\n");
    return -1;
  }

  memset(g2d, 0, sizeof (ImxVpDeviceG2d));
  device->priv = (gpointer)g2d;

  return 0;
}

static gint imx_g2d_close(ImxVideoProcessDevice *device)
{
  if (!device)
    return -1;

  if (device) {
    ImxVpDeviceG2d *g2d = (ImxVpDeviceG2d *) (device->priv);
    if (g2d)
      g_slice_free1(sizeof(ImxVpDeviceG2d), g2d);
    device->priv = NULL;
  }
  return 0;
}


static gint
imx_g2d_alloc_mem(ImxVideoProcessDevice *device, PhyMemBlock *memblk)
{
  struct g2d_buf *pbuf = NULL;

  if (!device || !device->priv || !memblk)
    return -1;

  memblk->size = PAGE_ALIGN(memblk->size);

  pbuf = g2d_alloc (memblk->size, 0);
  if (!pbuf) {
    GST_ERROR("G2D allocate %u bytes memory failed: %s",
              memblk->size, strerror(errno));
    return -1;
  }

  memblk->vaddr = (guint8*) pbuf->buf_vaddr;
  memblk->paddr = (guint8*) pbuf->buf_paddr;
  memblk->user_data = (gpointer) pbuf;

  return 0;
}

static gint imx_g2d_free_mem(ImxVideoProcessDevice *device, PhyMemBlock *memblk)
{
  if (!device || !device->priv || !memblk)
    return -1;

  gint ret = g2d_free ((struct g2d_buf*)(memblk->user_data));
  memblk->user_data = NULL;
  memblk->vaddr = NULL;
  memblk->paddr = NULL;
  memblk->size = 0;

  return ret;
}

static gint imx_g2d_frame_copy(ImxVideoProcessDevice *device,
                               PhyMemBlock *from, PhyMemBlock *to)
{
  struct g2d_buf src, dst;
  gint ret = 0;

  if (!device || !device->priv || !from || !to)
    return -1;

  ImxVpDeviceG2d *g2d = (ImxVpDeviceG2d *) (device->priv);

  void *g2d_handle = NULL;

  if(g2d_open(&g2d_handle) == -1 || g2d_handle == NULL) {
    GST_ERROR ("%s Failed to open g2d device.",__FUNCTION__);
    return -1;
  }

  src.buf_handle = NULL;
  src.buf_vaddr = (void*)(from->vaddr);
  src.buf_paddr = (gint)(from->paddr);
  src.buf_size = from->size;
  dst.buf_handle = NULL;
  dst.buf_vaddr = (void *)(to->vaddr);
  dst.buf_paddr = (gint)(to->paddr);
  dst.buf_size = to->size;

  ret = g2d_copy (g2d_handle, &dst, &src, dst.buf_size);

  g2d_close(g2d_handle);

  return ret;
}

static gint imx_g2d_set_input_frame(ImxVideoProcessDevice *device,
                                    GstVideoFrame *input)
{
  if (!device || !device->priv || !input)
    return -1;

  ImxVpDeviceG2d *g2d = (ImxVpDeviceG2d *) (device->priv);
  PhyMemBlock *memblk = gst_buffer_query_phymem_block (input->buffer);
  if (!memblk)
    return -1;

  GstVideoFormat gst_fmt = GST_VIDEO_INFO_FORMAT(&(input->info));
  const G2dFmtMap *fmtmap = imx_g2d_get_format(gst_fmt);
  if (!fmtmap)
    return -1;

  GstVideoCropMeta *crop_meta = gst_buffer_get_video_crop_meta(input->buffer);

  GST_LOG ("Input: %s, %dx%d", GST_VIDEO_FORMAT_INFO_NAME(input->info.finfo),
                    input->info.width, input->info.height);

  g2d->src.width = input->info.width;
  g2d->src.height = input->info.height;
  g2d->src.stride = input->info.stride[0] / (fmtmap->bpp/8);
  g2d->src.format = fmtmap->g2d_format;

  switch(g2d->src.format) {
    case G2D_I420:
    case G2D_YV12:
      g2d->src.planes[0] = (gint)(memblk->paddr);
      g2d->src.planes[1] = (gint)(memblk->paddr +
                                  g2d->src.width * g2d->src.height);
      g2d->src.planes[2] = g2d->src.planes[1]
                           + g2d->src.width * g2d->src.height / 4;
      break;
    case G2D_NV12:
    case G2D_NV21:
      g2d->src.planes[0] = (gint)(memblk->paddr);
      g2d->src.planes[1] = (gint)(memblk->paddr
                                  + g2d->src.width * g2d->src.height);
      break;
    case G2D_NV16:
      g2d->src.planes[0] = (gint)(memblk->paddr);
      g2d->src.planes[1] = (gint)(memblk->paddr
                                  + g2d->src.width * g2d->src.height);
      break;

    case G2D_RGB565:
    case G2D_RGBX8888:
    case G2D_RGBA8888:
    case G2D_BGRA8888:
    case G2D_BGRX8888:
    case G2D_BGR565:
    case G2D_ARGB8888:
    case G2D_ABGR8888:
    case G2D_XRGB8888:
    case G2D_XBGR8888:
    case G2D_UYVY:
    case G2D_YUYV:
    case G2D_YVYU:
      g2d->src.planes[0] = (gint)(memblk->paddr);
      break;
    default:
      GST_ERROR ("G2D: not supported format.");
      return -1;
  }

  if (crop_meta != NULL) {
    GST_LOG ("input crop meta: (%d, %d, %d, %d).", crop_meta->x, crop_meta->y,
            crop_meta->width, crop_meta->height);

    if ((crop_meta->x >= (guint)(input->info.width))
        || (crop_meta->y >= (guint)(input->info.height)))
      return -1;

    g2d->src.left = crop_meta->x;
    g2d->src.top = crop_meta->y;
    g2d->src.right = MIN((crop_meta->x + crop_meta->width), input->info.width);
    g2d->src.bottom = MIN((crop_meta->y+crop_meta->height), input->info.height);
  } else {
    g2d->src.left = 0;
    g2d->src.top = 0;
    g2d->src.right = g2d->src.width;
    g2d->src.bottom = g2d->src.height;
  }

  GST_TRACE ("set g2d src : %dx%d,%d(%d,%d-%d,%d), format=%s",
      g2d->src.width, g2d->src.height,g2d->src.stride, g2d->src.left,
      g2d->src.top, g2d->src.right, g2d->src.bottom,
      gst_video_format_to_string(gst_fmt));

  return 0;
}

static gint imx_g2d_set_output_frame(ImxVideoProcessDevice *device,
                                     GstVideoFrame *output)
{
  if (!device || !device->priv || !output)
    return -1;

  ImxVpDeviceG2d *g2d = (ImxVpDeviceG2d *) (device->priv);

  PhyMemBlock *memblk = gst_buffer_query_phymem_block (output->buffer);
  if (!memblk)
    return -1;

  GstVideoFormat gst_fmt = GST_VIDEO_INFO_FORMAT(&(output->info));
  const G2dFmtMap *fmtmap = imx_g2d_get_format(gst_fmt);
  if (!fmtmap)
    return -1;

  GstVideoCropMeta *crop_meta = gst_buffer_get_video_crop_meta(output->buffer);

  GST_LOG ("Output: %s, %dx%d", GST_VIDEO_FORMAT_INFO_NAME(output->info.finfo),
                    output->info.width, output->info.height);

  g2d->dst.width = output->info.width;
  g2d->dst.height = output->info.height;
  g2d->dst.stride = output->info.stride[0] / (fmtmap->bpp / 8);
  g2d->dst.format = fmtmap->g2d_format;
  g2d->dst.planes[0] = (gint)(memblk->paddr);

  if (crop_meta != NULL) {
    GST_LOG ("output crop meta: (%d, %d, %d, %d).", crop_meta->x, crop_meta->y,
        crop_meta->width, crop_meta->height);

    if ((crop_meta->x >= (guint)(output->info.width))
        || (crop_meta->y >= (guint)(output->info.height)))
      return -1;
    g2d->dst.left = crop_meta->x;
    g2d->dst.top = crop_meta->y;
    g2d->dst.right = MIN((crop_meta->x + crop_meta->width), output->info.width);
    g2d->dst.bottom = MIN((crop_meta->y+crop_meta->height),output->info.height);
  } else {
    g2d->dst.left = 0;
    g2d->dst.top = 0;
    g2d->dst.right = output->info.width;
    g2d->dst.bottom = output->info.height;
  }

  GST_TRACE ("set g2d dest : %dx%d,%d(%d,%d-%d,%d), format=%s",
      g2d->dst.width, g2d->dst.height,g2d->dst.stride, g2d->dst.left,
      g2d->dst.top, g2d->dst.right, g2d->dst.bottom,
      gst_video_format_to_string(gst_fmt));

  return 0;
}

static gint imx_g2d_do_convert(ImxVideoProcessDevice *device,
                               GstVideoFrame *from, GstVideoFrame *to)
{
  gint ret = 0;

  if (!device || !device->priv || !from || !to)
    return -1;

  void *g2d_handle = NULL;

  if(g2d_open(&g2d_handle) == -1 || g2d_handle == NULL) {
    GST_ERROR ("%s Failed to open g2d device.",__FUNCTION__);
    return -1;
  }

  ImxVpDeviceG2d *g2d = (ImxVpDeviceG2d *) (device->priv);

  ret = g2d_blit(g2d_handle, &g2d->src, &g2d->dst);
  ret |= g2d_finish(g2d_handle);
  g2d_close(g2d_handle);

  return ret;
}

static gint imx_g2d_set_rotate(ImxVideoProcessDevice *device,
                               ImxVideoRotationMode rot)
{
  if (!device || !device->priv)
    return -1;

  ImxVpDeviceG2d *g2d = (ImxVpDeviceG2d *) (device->priv);
  gint g2d_rotate = G2D_ROTATION_0;
  switch (rot) {
  case IMX_VIDEO_ROTATION_0:      g2d_rotate = G2D_ROTATION_0;    break;
  case IMX_VIDEO_ROTATION_90:     g2d_rotate = G2D_ROTATION_90;   break;
  case IMX_VIDEO_ROTATION_180:    g2d_rotate = G2D_ROTATION_180;  break;
  case IMX_VIDEO_ROTATION_270:    g2d_rotate = G2D_ROTATION_270;  break;
  case IMX_VIDEO_ROTATION_HFLIP:  g2d_rotate = G2D_FLIP_H;        break;
  case IMX_VIDEO_ROTATION_VFLIP:  g2d_rotate = G2D_FLIP_V;        break;
  default:                        g2d_rotate = G2D_ROTATION_0;    break;
  }

  g2d->dst.rot = g2d_rotate;
  return 0;
}

static gint imx_g2d_set_deinterlace(ImxVideoProcessDevice *device,
                                    ImxVideoDeinterlaceMode mode)
{
  return 0;
}

static ImxVideoRotationMode imx_g2d_get_rotate (ImxVideoProcessDevice* device)
{
  if (!device || !device->priv)
    return 0;

  ImxVpDeviceG2d *g2d = (ImxVpDeviceG2d *) (device->priv);
  ImxVideoRotationMode rot = IMX_VIDEO_ROTATION_0;
  switch (g2d->dst.rot) {
  case G2D_ROTATION_0:    rot = IMX_VIDEO_ROTATION_0;     break;
  case G2D_ROTATION_90:   rot = IMX_VIDEO_ROTATION_90;    break;
  case G2D_ROTATION_180:  rot = IMX_VIDEO_ROTATION_180;   break;
  case G2D_ROTATION_270:  rot = IMX_VIDEO_ROTATION_270;   break;
  case G2D_FLIP_H:        rot = IMX_VIDEO_ROTATION_HFLIP; break;
  case G2D_FLIP_V:        rot = IMX_VIDEO_ROTATION_VFLIP; break;
  default:                rot = IMX_VIDEO_ROTATION_0;     break;
  }

  return rot;
}

static ImxVideoDeinterlaceMode imx_g2d_get_deinterlace (
                                                ImxVideoProcessDevice* device)
{
  return IMX_VIDEO_DEINTERLACE_NONE;
}

static gint imx_g2d_get_capabilities (void)
{
  void *g2d_handle = NULL;
  gint capabilities = 0;

  if(g2d_open(&g2d_handle) == -1 || g2d_handle == NULL) {
    GST_ERROR ("Failed to open g2d device.");
  } else {
    capabilities = IMX_VP_DEVICE_CAP_SCALE|IMX_VP_DEVICE_CAP_CSC \
                      |IMX_VP_DEVICE_CAP_ROTATE;

    gboolean enable = FALSE;
    g2d_query_cap(g2d_handle, G2D_GLOBAL_ALPHA, &enable);
    if (enable)
      capabilities |= IMX_VP_DEVICE_CAP_ALPHA;

    g2d_close(g2d_handle);
  }

  return capabilities;
}

static GList* imx_g2d_get_supported_in_fmts(void)
{
  GList* list = NULL;
  const G2dFmtMap *map = g2d_fmts_map;
  while (map->bpp > 0) {
    if (map->gst_video_format != GST_VIDEO_FORMAT_UNKNOWN)
      list = g_list_append(list, (gpointer)(map->gst_video_format));
    map++;
  }

  return list;
}

static GList* imx_g2d_get_supported_out_fmts(void)
{
  GList* list = NULL;
  const G2dFmtMap *map = g2d_fmts_map;

  while (map->gst_video_format != GST_VIDEO_FORMAT_UNKNOWN) {
    list = g_list_append(list, (gpointer)(map->gst_video_format));
    map++;
  }

  return list;
}

ImxVideoProcessDevice * imx_g2d_create(void)
{
  ImxVideoProcessDevice * device = g_slice_alloc(sizeof(ImxVideoProcessDevice));
  if (!device) {
    GST_ERROR("allocate device structure failed\n");
    return NULL;
  }

  device->device_type = IMX_VP_DEVICE_G2D;
  device->priv = NULL;

  device->open                = imx_g2d_open;
  device->close               = imx_g2d_close;
  device->alloc_mem           = imx_g2d_alloc_mem;
  device->free_mem            = imx_g2d_free_mem;
  device->frame_copy          = imx_g2d_frame_copy;
  device->set_input_frame     = imx_g2d_set_input_frame;
  device->set_output_frame    = imx_g2d_set_output_frame;
  device->do_convert          = imx_g2d_do_convert;
  device->set_rotate          = imx_g2d_set_rotate;
  device->set_deinterlace     = imx_g2d_set_deinterlace;
  device->get_rotate          = imx_g2d_get_rotate;
  device->get_deinterlace     = imx_g2d_get_deinterlace;
  device->get_capabilities    = imx_g2d_get_capabilities;
  device->get_supported_in_fmts  = imx_g2d_get_supported_in_fmts;
  device->get_supported_out_fmts = imx_g2d_get_supported_out_fmts;

  return device;
}

gint imx_g2d_destroy(ImxVideoProcessDevice *device)
{
  if (!device)
    return -1;

  g_slice_free1(sizeof(ImxVideoProcessDevice), device);

  return 0;
}
