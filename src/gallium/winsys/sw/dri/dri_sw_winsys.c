/**************************************************************************
 *
 * Copyright 2009, VMware, Inc.
 * All Rights Reserved.
 * Copyright 2010 George Sapountzis <gsapountzis@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if defined(HAVE_LIBDRM)
#include <xf86drm.h>
#include <vgem_drm.h>
#endif

#include "pipe/p_compiler.h"
#include "pipe/p_format.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "util/u_math.h"
#include "util/u_memory.h"

#include "state_tracker/sw_winsys.h"
#include "dri_sw_winsys.h"

#include "drm_driver.h"


enum displaytarget_type {
   DISPLAYTARGET_TYPE_USER,
   DISPLAYTARGET_TYPE_PRIME,
};

struct dri_sw_displaytarget
{
   enum displaytarget_type type;
   enum pipe_format format;
   unsigned width;
   unsigned height;
   unsigned stride;

#if defined(HAVE_LIBDRM)
   uint32_t vgem_handle;
#endif

   unsigned map_flags;
   void *data;
   void *mapped;
   const void *front_private;
};

struct dri_sw_winsys
{
   struct sw_winsys base;

   struct drisw_loader_funcs *lf;

#if defined(HAVE_LIBDRM)
   int vgem_fd;
#endif
};

#if defined(HAVE_LIBDRM)

const char g_sys_card_path_format[] =
   "/sys/bus/platform/devices/vgem/drm/renderD%d";
const char g_dev_card_path_format[] =
   "/dev/dri/renderD%d";

static int
drm_open_vgem()
{
   char *name;
   int i, fd;

   for (i = 128; i >= 0; i++) {
      struct stat _stat;
      int ret;
      ret = asprintf(&name, g_sys_card_path_format, i);
      assert(ret != -1);

      if (stat(name, &_stat) == -1) {
         free(name);
         continue;
      }

      free(name);
      ret = asprintf(&name, g_dev_card_path_format, i);
      assert(ret != -1);

      fd = open(name, O_RDWR);
      free(name);
      if (fd < 0)
         continue;
      return fd;
   }
   return -1;
}

static void *
mmap_dumb_bo(int fd, int handle, size_t size)
{
   struct drm_mode_map_dumb mmap_arg;
   void *ptr;
   int ret;

   memset(&mmap_arg, 0, sizeof(mmap_arg));

   mmap_arg.handle = handle;

   ret = drmIoctl(fd, DRM_IOCTL_VGEM_MODE_MAP_DUMB, &mmap_arg);
   if (ret)
      return NULL;

   ptr = mmap(NULL, size, (PROT_READ|PROT_WRITE), MAP_SHARED, fd,
         mmap_arg.offset);

   if (ptr == MAP_FAILED)
      return NULL;

   return ptr;
}

#endif

static inline struct dri_sw_displaytarget *
dri_sw_displaytarget( struct sw_displaytarget *dt )
{
   return (struct dri_sw_displaytarget *)dt;
}

static inline struct dri_sw_winsys *
dri_sw_winsys( struct sw_winsys *ws )
{
   return (struct dri_sw_winsys *)ws;
}


static boolean
dri_sw_is_displaytarget_format_supported( struct sw_winsys *ws,
                                          unsigned tex_usage,
                                          enum pipe_format format )
{
   /* TODO: check visuals or other sensible thing here */
   return TRUE;
}

static struct sw_displaytarget *
dri_sw_displaytarget_create(struct sw_winsys *winsys,
                            unsigned tex_usage,
                            enum pipe_format format,
                            unsigned width, unsigned height,
                            unsigned alignment,
                            const void *front_private,
                            unsigned *stride)
{
   struct dri_sw_displaytarget *dri_sw_dt;
   unsigned nblocksy, size, format_stride;

   dri_sw_dt = CALLOC_STRUCT(dri_sw_displaytarget);
   if(!dri_sw_dt)
      goto no_dt;

   dri_sw_dt->type = DISPLAYTARGET_TYPE_USER;
   dri_sw_dt->format = format;
   dri_sw_dt->width = width;
   dri_sw_dt->height = height;
   dri_sw_dt->front_private = front_private;

   format_stride = util_format_get_stride(format, width);
   dri_sw_dt->stride = align(format_stride, alignment);

   nblocksy = util_format_get_nblocksy(format, height);
   size = dri_sw_dt->stride * nblocksy;

   dri_sw_dt->data = align_malloc(size, alignment);
   if(!dri_sw_dt->data)
      goto no_data;

   *stride = dri_sw_dt->stride;
   return (struct sw_displaytarget *)dri_sw_dt;

no_data:
   FREE(dri_sw_dt);
no_dt:
   return NULL;
}

static void *
dri_sw_displaytarget_map(struct sw_winsys *ws,
                         struct sw_displaytarget *dt,
                         unsigned flags)
{
   struct dri_sw_displaytarget *dri_sw_dt = dri_sw_displaytarget(dt);
   struct dri_sw_winsys *dri_sw_ws = dri_sw_winsys(ws);

   switch (dri_sw_dt->type) {
   case DISPLAYTARGET_TYPE_USER:
      dri_sw_dt->mapped = dri_sw_dt->data;
      break;
   case DISPLAYTARGET_TYPE_PRIME:
      if (dri_sw_ws->vgem_fd >= 0 && !dri_sw_dt->mapped)
         dri_sw_dt->mapped = mmap_dumb_bo(dri_sw_ws->vgem_fd,
                                          dri_sw_dt->vgem_handle,
                                          dri_sw_dt->height * dri_sw_dt->stride);
      break;
   default:
         dri_sw_dt->mapped = NULL;
   }

   if (dri_sw_dt->mapped && dri_sw_dt->front_private && (flags & PIPE_TRANSFER_READ)) {
      struct dri_sw_winsys *dri_sw_ws = dri_sw_winsys(ws);
      dri_sw_ws->lf->get_image((void *)dri_sw_dt->front_private, 0, 0, dri_sw_dt->width, dri_sw_dt->height, dri_sw_dt->stride, dri_sw_dt->mapped);
   }
   dri_sw_dt->map_flags = flags;

   return dri_sw_dt->mapped;
}

static void
dri_sw_displaytarget_unmap(struct sw_winsys *ws,
                           struct sw_displaytarget *dt)
{
   struct dri_sw_displaytarget *dri_sw_dt = dri_sw_displaytarget(dt);
   if (dri_sw_dt->front_private && (dri_sw_dt->map_flags & PIPE_TRANSFER_WRITE)) {
      struct dri_sw_winsys *dri_sw_ws = dri_sw_winsys(ws);
      dri_sw_ws->lf->put_image2((void *)dri_sw_dt->front_private, dri_sw_dt->data, 0, 0, dri_sw_dt->width, dri_sw_dt->height, dri_sw_dt->stride);
   }
   dri_sw_dt->map_flags = 0;

#if defined(HAVE_LIBDRM)
   if (dri_sw_dt->mapped)
      munmap(dri_sw_dt->mapped, dri_sw_dt->height * dri_sw_dt->stride);
#endif
   dri_sw_dt->mapped = NULL;
}

static void
dri_sw_displaytarget_destroy(struct sw_winsys *ws,
                             struct sw_displaytarget *dt)
{
   struct dri_sw_winsys *dri_sw_ws = dri_sw_winsys(ws);
   struct dri_sw_displaytarget *dri_sw_dt = dri_sw_displaytarget(dt);

   if (dri_sw_dt->mapped) {
      dri_sw_displaytarget_unmap(ws, dt);
   }

#if defined(HAVE_LIBDRM)
   if (dri_sw_dt->type == DISPLAYTARGET_TYPE_PRIME && dri_sw_ws->vgem_fd >= 0) {
      struct drm_gem_close arg;
      memset(&arg, 0, sizeof(arg));
      arg.handle = dri_sw_dt->vgem_handle;

      drmIoctl(dri_sw_ws->vgem_fd, DRM_IOCTL_GEM_CLOSE, &arg);
   }
#endif

   FREE(dri_sw_dt->data);

   FREE(dri_sw_dt);
}

static struct sw_displaytarget *
dri_sw_displaytarget_from_handle(struct sw_winsys *ws,
                                 const struct pipe_resource *templ,
                                 struct winsys_handle *whandle,
                                 unsigned *stride)
{
#if defined(HAVE_LIBDRM)
   struct dri_sw_winsys *dri_sw_ws = dri_sw_winsys(ws);
   uint32_t imported_handle;
   struct dri_sw_displaytarget *dri_sw_dt;

   if (whandle->type != DRM_API_HANDLE_TYPE_FD || dri_sw_ws->vgem_fd < 0) {
      return NULL;
   }

   dri_sw_dt = CALLOC_STRUCT(dri_sw_displaytarget);
   if(!dri_sw_dt)
      return NULL;

   int ret = drmPrimeFDToHandle(dri_sw_ws->vgem_fd, whandle->handle,
                                &imported_handle);
   if (ret) {
      FREE(dri_sw_dt);
      return NULL;
   }

   dri_sw_dt->type = DISPLAYTARGET_TYPE_PRIME;
   dri_sw_dt->format = templ->format;
   dri_sw_dt->width = templ->width0;
   dri_sw_dt->height = templ->height0;
   dri_sw_dt->vgem_handle = imported_handle;
   dri_sw_dt->stride = whandle->stride;

   *stride = dri_sw_dt->stride;
   return (struct sw_displaytarget *)dri_sw_dt;
#else
   assert(0);
   return NULL;
#endif
}

static boolean
dri_sw_displaytarget_get_handle(struct sw_winsys *ws,
                                struct sw_displaytarget *dt,
                                struct winsys_handle *whandle)
{
#if defined(HAVE_LIBDRM)
   struct dri_sw_winsys *dri_sw_ws = dri_sw_winsys(ws);
   if (dri_sw_ws->vgem_fd < 0)
      return FALSE;

   struct dri_sw_displaytarget *dri_sw_dt = dri_sw_displaytarget(dt);
   if (whandle->type == DRM_API_HANDLE_TYPE_FD &&
       dri_sw_dt->type == DISPLAYTARGET_TYPE_PRIME) {
      int prime_fd;
      int ret = drmPrimeHandleToFD(dri_sw_ws->vgem_fd,
                                   dri_sw_dt->vgem_handle,
                                   DRM_CLOEXEC,
                                   &prime_fd);
      if (ret || prime_fd < 0)
         return FALSE;

      whandle->handle = (unsigned)prime_fd;
      whandle->stride = dri_sw_dt->stride;

      return TRUE;
   }
#else
   assert(0);
#endif

   return FALSE;
}

static void
dri_sw_displaytarget_display(struct sw_winsys *ws,
                             struct sw_displaytarget *dt,
                             void *context_private,
                             struct pipe_box *box)
{
   struct dri_sw_winsys *dri_sw_ws = dri_sw_winsys(ws);
   struct dri_sw_displaytarget *dri_sw_dt = dri_sw_displaytarget(dt);
   struct dri_drawable *dri_drawable = (struct dri_drawable *)context_private;
   unsigned width, height;
   unsigned blsize = util_format_get_blocksize(dri_sw_dt->format);

   /* Set the width to 'stride / cpp'.
    *
    * PutImage correctly clips to the width of the dst drawable.
    */
   width = dri_sw_dt->stride / blsize;

   height = dri_sw_dt->height;

   if (box) {
       void *data;
       data = dri_sw_dt->data + (dri_sw_dt->stride * box->y) + box->x * blsize;
       dri_sw_ws->lf->put_image2(dri_drawable, data,
                                 box->x, box->y, box->width, box->height, dri_sw_dt->stride);
   } else {
       dri_sw_ws->lf->put_image(dri_drawable, dri_sw_dt->data, width, height);
   }
}

static void
dri_destroy_sw_winsys(struct sw_winsys *winsys)
{
#if defined(HAVE_LIBDRM)
   int vgem_fd = dri_sw_winsys(winsys)->vgem_fd;
   if (vgem_fd >= 0)
      close(vgem_fd);
#endif
   FREE(winsys);
}

struct sw_winsys *
dri_create_sw_winsys(struct drisw_loader_funcs *lf)
{
   struct dri_sw_winsys *ws;

   ws = CALLOC_STRUCT(dri_sw_winsys);
   if (!ws)
      return NULL;

   ws->lf = lf;
#if defined(HAVE_LIBDRM)
   ws->vgem_fd = drm_open_vgem();
#endif
   ws->base.destroy = dri_destroy_sw_winsys;

   ws->base.is_displaytarget_format_supported = dri_sw_is_displaytarget_format_supported;

   /* screen texture functions */
   ws->base.displaytarget_create = dri_sw_displaytarget_create;
   ws->base.displaytarget_destroy = dri_sw_displaytarget_destroy;
   ws->base.displaytarget_from_handle = dri_sw_displaytarget_from_handle;
   ws->base.displaytarget_get_handle = dri_sw_displaytarget_get_handle;

   /* texture functions */
   ws->base.displaytarget_map = dri_sw_displaytarget_map;
   ws->base.displaytarget_unmap = dri_sw_displaytarget_unmap;

   ws->base.displaytarget_display = dri_sw_displaytarget_display;

   return &ws->base;
}

/* vim: set sw=3 ts=8 sts=3 expandtab: */
