/* Copyright Neverware 2017 */
#ifndef _GBM_WRAP_IMPL_H
#define _GBM_WRAP_IMPL_H

#include "gbmint.h"
/**
 * Must keep track of two types of gbm_device, gbm_surface, and gbm_bo
 * pointers.
 *  - internal to this library from mesa, complete.
 *  - external opaque pointers from the wrapped library.
 *
 * Attr to indicate which pointers are externally wrapped versions
 * and should not be derefed in this context.
 * Use to indicate gbm buffer structs that are external to the
 * wrapped library.
 * Motivation behind this is the sparse methods similarly.
 * typedefs could help but are a bit uglier as they the remove
 * the struct from the name, causing confusion.
 * Nothing checks this, unless it can be found or sparse could be run?
 *
 * related: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=59850
 */
#if 0
#define __wrapped  __attribute__((noderef))
#else
#define __wrapped
#endif

struct wrapped_gbm_functions {
   struct gbm_bo __wrapped *(*gbm_bo_create)(struct gbm_device __wrapped *gbm, uint32_t width, uint32_t height, uint32_t format, uint32_t flags);
   void (*gbm_bo_destroy)(struct gbm_bo __wrapped *bo);
   struct gbm_device __wrapped * (*gbm_bo_get_device)(struct gbm_bo __wrapped *bo);
   int (*gbm_bo_get_fd)(struct gbm_bo __wrapped *bo);
   uint32_t (*gbm_bo_get_format)(struct gbm_bo __wrapped *bo);
   union gbm_bo_handle (*gbm_bo_get_handle)(struct gbm_bo __wrapped *bo);
   uint32_t (*gbm_bo_get_height)(struct gbm_bo __wrapped *bo);
   uint32_t (*gbm_bo_get_stride)(struct gbm_bo __wrapped *bo);
   //void * (*gbm_bo_get_user_data)(struct gbm_bo __wrapped *bo);
   int (*gbm_bo_write)(struct gbm_bo __wrapped *bo, const void *buf, size_t count);
   uint32_t (*gbm_bo_get_width)(struct gbm_bo __wrapped *bo);
   struct gbm_bo __wrapped *(*gbm_bo_import)(struct gbm_device __wrapped *gbm, uint32_t type, void *buffer, uint32_t usage);
   void * (*gbm_bo_map)(struct gbm_bo __wrapped *bo, uint32_t x, uint32_t y, uint32_t width, uint32_t height, uint32_t flags, uint32_t *stride, void **map_data);
   //void (*gbm_bo_set_user_data)(struct gbm_bo *bo, void *data, void (*destroy_user_data)(struct gbm_bo *, void *));
   void (*gbm_bo_unmap)(struct gbm_bo __wrapped *bo, void *map_data);
   struct gbm_device __wrapped *(*gbm_create_device)(int fd);
   void (*gbm_device_destroy)(struct gbm_device __wrapped *gbm);
   const char *(*gbm_device_get_backend_name)(struct gbm_device __wrapped *gbm);
   int (*gbm_device_get_fd)(struct gbm_device __wrapped *gbm);
   int (*gbm_device_is_format_supported)(struct gbm_device __wrapped *gbm, uint32_t format, uint32_t usage);
   struct gbm_surface *(*gbm_surface_create)(struct gbm_device __wrapped *gbm, uint32_t width, uint32_t height, uint32_t format, uint32_t flags);
   void (*gbm_surface_destroy)(struct gbm_surface __wrapped *surface);
   int (*gbm_surface_has_free_buffers)(struct gbm_surface __wrapped *surface);
   struct gbm_bo __wrapped *(*gbm_surface_lock_front_buffer)(struct gbm_surface __wrapped *surface);
   void (*gbm_surface_release_buffer)(struct gbm_surface __wrapped *surface, struct gbm_bo __wrapped *bo);
};


struct gbm_wrapper_device {
   struct gbm_device base; // Base device internal to mesagbm.
   struct gbm_device __wrapped *gbm_device; // Pointer to the wrapped gbm device.
   void *library;
   /* Wrapped functions loaded from the target lib. */
   struct wrapped_gbm_functions wfuncs;
};

/* Wrap an underlying bo. */
struct gbm_wrapper_bo {
   struct gbm_bo base;
   struct gbm_bo __wrapped *wrapped_bo; // wrapped bo pointer. NOTE: this has different impl details!
};

/* Wrap an underlying bo. */
struct gbm_wrapper_surface {
   struct gbm_surface base;
   struct gbm_surface __wrapped *wrapped_surface; // wrapped bo pointer. NOTE: this has different impl details!
};

/* Helper to get the wrapper container pointers from
 * the holder members.
 */
#define container_of(type, from, member) \
   ((type *)(((char *)from) - offsetof(type, member)));
#endif
