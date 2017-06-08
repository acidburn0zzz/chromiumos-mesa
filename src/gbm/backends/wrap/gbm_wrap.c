/* Copyright Neverware 2017 */
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

#include "gbmint.h"
#include "gbm_wrap_impl.h"

/** Helper functions. */

static struct gbm_wrapper_device *
gbm_to_wrap_device(struct gbm_device *gbm)
{
   return container_of(struct gbm_wrapper_device, gbm, base);
}

/**
 * Give a wrapper_surface pointer for a gbm_bo.
 */
static struct gbm_wrapper_surface *
gbm_surface_to_wrap_surface(struct gbm_surface *surface)
{
   return container_of(struct gbm_wrapper_surface, surface, base);
}

/**
 * Give a wrapper_bo pointer for a gbm_bo.
 */
static struct gbm_wrapper_bo *
gbm_bo_to_wrap_bo(struct gbm_bo *bo)
{
   return container_of(struct gbm_wrapper_bo, bo, base);
}

/**
 * Helper to take mesa gbm_device and give the wrapped underlying
 * library gbm_device pointer.
 */
static struct gbm_device __wrapped *
wrapped_gbm(struct gbm_device *gbm)
{
   return gbm_to_wrap_device(gbm)->gbm_device;
}

/**
 * Give the wrapped underlying bo for the internal
 * gbm_bo contained in the wrapper.
 *
 * \param bo bo object internal to this library.
 * \return gbm object for the wrapped library.
 */
static struct gbm_bo __wrapped *
wrapped_bo(struct gbm_bo *bo)
{
   return gbm_bo_to_wrap_bo(bo)->wrapped_bo;
}

/**
 * Return pointer to the wrapped gbm implementation surface
 * object from the mesagbm gbm_surface pointer.
 *
 * \return gbm surface pointer for the wrapped library.
 */
static struct gbm_surface __wrapped *
wrapped_surface(struct gbm_surface *surface)
{
   return gbm_surface_to_wrap_surface(surface)->wrapped_surface;
}

/**
 * Give the wrapper gbm device from the wrapper bo.
 */
static struct gbm_wrapper_device *
wrapper_device_from_bo(struct gbm_bo *bo)
{
   struct gbm_wrapper_bo *wrapper_bo = gbm_bo_to_wrap_bo(bo);
   return gbm_to_wrap_device(wrapper_bo->base.gbm);
}

/**
 * Give the wrapper gbm device from the wrapper surface.
 */
static struct gbm_wrapper_device *
gbm_surface_to_wrap_device(struct gbm_surface *surface)
{
   struct gbm_wrapper_surface *wrapper_surface = gbm_surface_to_wrap_surface(surface);
   return gbm_to_wrap_device(wrapper_surface->base.gbm);
}

/**
 * Free simple wrapper object gbm.
 * NOTE: only frees the simple buffer.
 */
static void
free_gbm(struct gbm_wrapper_device *gbm)
{
   free(gbm);
}

/**
 * Free wrapper bo memory.
 * NOTE: only frees the memory for the wrapper object.
 */
static void
free_bo(struct gbm_wrapper_bo *bo)
{
   free(bo);
}

/**
 * Free the wrapper surface data.
 * defined as function to make clear where this occurs
 * and it is typed and can be watched.
 */
static void free_surface(struct gbm_wrapper_surface *surface)
{
   free(surface);
}

/**
 * Wrap the external library bo into a newly created
 * wrapper bo.
 */
static struct gbm_wrapper_bo *
wrap_bo(struct gbm_wrapper_device *wgbm, struct gbm_bo __wrapped *bo)
{
   struct gbm_wrapper_bo* wrapper_bo;

   wrapper_bo = calloc(1, sizeof(*wrapper_bo));
   if (!wrapper_bo)
      return NULL;

   wrapper_bo->wrapped_bo = bo;
   wrapper_bo->base.gbm = &wgbm->base;
   /* NOTE: Pretty gross that it could just be passed through on the top
    * but does this to appease mesa gbm implementation.
    */
   wrapper_bo->base.width = wgbm->wfuncs.gbm_bo_get_width(bo);
   wrapper_bo->base.height = wgbm->wfuncs.gbm_bo_get_height(bo);
   wrapper_bo->base.format = wgbm->wfuncs.gbm_bo_get_format(bo);
   wrapper_bo->base.stride = wgbm->wfuncs.gbm_bo_get_stride(bo);
   wrapper_bo->base.handle = wgbm->wfuncs.gbm_bo_get_handle(bo);
   return wrapper_bo;
}

/**
 * Destroy the wrapper device.
 */
static void
_destroy_device(struct gbm_wrapper_device *wgbm)
{
   struct gbm_device __wrapped *gbm;
   gbm = wgbm->gbm_device;
   if (gbm) {
      wgbm->wfuncs.gbm_device_destroy(gbm);
   }
   dlclose(wgbm->library);
   free_gbm(wgbm);
}

/** Functions bridging backend pointers to wrapped calls. */
static void
wrap_device_destroy(struct gbm_device *gbm)
{
   return _destroy_device(gbm_to_wrap_device(gbm));
}

static struct gbm_bo *
gbm_wrap_bo_create(struct gbm_device *gbm, uint32_t width,
      uint32_t height, uint32_t format, uint32_t usage)
{
   struct gbm_wrapper_device *wgbm = gbm_to_wrap_device(gbm);
   struct gbm_wrapper_bo* wrapper_bo;
   struct gbm_bo __wrapped *new_bo;

   new_bo = wgbm->wfuncs.gbm_bo_create(wrapped_gbm(gbm), width, height, format,
         usage);
   if (!new_bo)
      return NULL;

   /* Wrap the new bo in a new bo wrapper... */
   wrapper_bo = wrap_bo(wgbm, new_bo);
   if (!wrapper_bo) {
      /* failure wrapping, destroy the underlying bo. */
      wgbm->wfuncs.gbm_bo_destroy(new_bo);
      return NULL;
   }
   return &wrapper_bo->base;
}

static struct gbm_bo *
gbm_wrap_bo_import(struct gbm_device *gbm, uint32_t type, void *buffer,
      uint32_t usage)
{
   struct gbm_wrapper_device *wgbm = gbm_to_wrap_device(gbm);
   struct gbm_wrapper_bo *wrapper_bo;
   struct gbm_bo __wrapped *new_bo; // underlying newly imported bo.

   new_bo = wgbm->wfuncs.gbm_bo_import(wrapped_gbm(gbm), type, buffer, usage);
   // Let the error go through.
   if (!new_bo)
      return NULL;

   /* Wrap the new bo in a new bo wrapper... */
   wrapper_bo = wrap_bo(wgbm, new_bo);
   if (!wrapper_bo) {
      /* failure wrapping, destroy the underlying bo. */
      wgbm->wfuncs.gbm_bo_destroy(new_bo);
      return NULL;
   }
   return &wrapper_bo->base;
}

static void *
gbm_wrap_bo_map(struct gbm_bo *bo, uint32_t x, uint32_t y, uint32_t width,
      uint32_t height, uint32_t flags, uint32_t *stride, void **map_data)
{
   struct gbm_wrapper_device *wgbm = wrapper_device_from_bo(bo);
   return wgbm->wfuncs.gbm_bo_map(wrapped_bo(bo), x, y, width, height, flags,
         stride, map_data);
}

static void
gbm_wrap_bo_unmap(struct gbm_bo *bo, void *map_data)
{
   struct gbm_wrapper_device *wgbm = wrapper_device_from_bo(bo);
   return wgbm->wfuncs.gbm_bo_unmap(wrapped_bo(bo), map_data);
}

static int
gbm_wrap_bo_write(struct gbm_bo *bo, const void *buf, size_t data)
{
   struct gbm_wrapper_device *wgbm = wrapper_device_from_bo(bo);
   return wgbm->wfuncs.gbm_bo_write(wrapped_bo(bo), buf, data);
}

static int
gbm_wrap_bo_get_fd(struct gbm_bo *bo)
{
   struct gbm_wrapper_device *wgbm = wrapper_device_from_bo(bo);
   return wgbm->wfuncs.gbm_bo_get_fd(wrapped_bo(bo));
}

static void
gbm_wrap_bo_destroy(struct gbm_bo *bo)
{
   struct gbm_wrapper_device *wgbm = wrapper_device_from_bo(bo);
   struct gbm_wrapper_bo *wbo = gbm_bo_to_wrap_bo(bo);
   wgbm->wfuncs.gbm_bo_destroy(wrapped_bo(bo));
   /* Free the wrapper bo. */
   free_bo(wbo);
}

static int
gbm_wrap_is_format_supported(struct gbm_device *gbm,
      uint32_t format, uint32_t usage)
{
   struct gbm_wrapper_device *wgbm = gbm_to_wrap_device(gbm);
   return wgbm->wfuncs.gbm_device_is_format_supported(wrapped_gbm(gbm), format,
         usage);
}

static struct gbm_wrapper_surface *
wrap_surface(struct gbm_wrapper_device *wgbm, struct gbm_surface __wrapped *surface)
{
   struct gbm_wrapper_surface* wrapper_surface;

   wrapper_surface = calloc(1, sizeof(*wrapper_surface));
   if (!wrapper_surface)
      return NULL;

   wrapper_surface->wrapped_surface = surface;
   wrapper_surface->base.gbm = &wgbm->base;
   /* NOTE: Pretty gross that it could just be passed through on the top
    * but does this to appease mesa gbm implementation.
    */
   /* Since we cannot get the height, width, format flags from the underlying impl,
    * force caller to fill these in.
    */
   return wrapper_surface;
}

static struct gbm_surface *
gbm_wrap_surface_create(struct gbm_device *gbm,
      uint32_t width, uint32_t height, uint32_t format, uint32_t flags)
{
   struct gbm_wrapper_device *wgbm = gbm_to_wrap_device(gbm);
   struct gbm_wrapper_surface* wrapper_surface;
   struct gbm_surface __wrapped *new_surface;

   new_surface = wgbm->wfuncs.gbm_surface_create(wrapped_gbm(gbm), width,
         height, format, flags);
   if (!new_surface)
      return NULL;

   /* Wrap the new surface in a new surface wrapper... */
   wrapper_surface = wrap_surface(wgbm, new_surface);
   if (!wrapper_surface) {
      /* failure wrapping, destroy the created surface. */
      wgbm->wfuncs.gbm_surface_destroy(new_surface);
      return NULL;
   }
   /* Wrapper function cannot fill these in as there are no getters...
    * set them here.
    * TODO: confirm there really isn't a way to query this.
    */
   wrapper_surface->base.height = height;
   wrapper_surface->base.width = width;
   wrapper_surface->base.format = format;
   wrapper_surface->base.flags = flags;
   return &wrapper_surface->base;
}

/**
 * XXX JAM: I *believe* that with the lock buffer and release buffer
 * are symmetrical. Users are not supposed to destroy buffers returned from
 * lock buffer, only call release buffer.
 * It is unclear to me if the data is able to be re-used...
 * I *believe* it cannot be reused by the caller, as they won't know this either.
 * On this belief, the release should free the wrapper for the bo here and
 * create that wrapper on lock buffer..
 * TODO: clear the comment up prove correct, show more evidence?
 * I took this purely from the gbm.h header for the functions.
 */
static void
gbm_wrap_surface_release_buffer(struct gbm_surface *surface, struct gbm_bo *bo)
{
   struct gbm_wrapper_device *wgbm = gbm_surface_to_wrap_device(surface);
   struct gbm_wrapper_bo *wbo = gbm_bo_to_wrap_bo(bo);
   wgbm->wfuncs.gbm_surface_release_buffer(wrapped_surface(surface),
         wrapped_bo(bo));
   /* The wrapper is no longer referenced, so free it. */
   free_bo(wbo);
}

static int
gbm_wrap_surface_has_free_buffers(struct gbm_surface *surface)
{
   struct gbm_wrapper_device *wgbm = gbm_surface_to_wrap_device(surface);
   return wgbm->wfuncs.gbm_surface_has_free_buffers(wrapped_surface(surface));
}

static void
gbm_wrap_surface_destroy(struct gbm_surface *surface)
{
   struct gbm_wrapper_device *wgbm = gbm_surface_to_wrap_device(surface);
   struct gbm_wrapper_surface *wsurf = gbm_surface_to_wrap_surface(surface);
   wgbm->wfuncs.gbm_surface_destroy(wrapped_surface(surface));
   free_surface(wsurf);
}

/* Wrap functions from the library into the backend. */
#if 0
#define WRAP_GBM_FUNCTION(wgbm, function) \
   do { wgbm->wfuncs.function = dlsym(wgbm->library, #function);} while (0);
#else
#define WRAP_GBM_FUNCTION(wgbm, function) \
   ({ \
      wgbm->wfuncs.function = dlsym(wgbm->library, #function); \
      if (wgbm->wfuncs.function == NULL) \
         fprintf(stderr, "gbm: Unable to wrap " #function "\n"); \
      ( wgbm->wfuncs.function == NULL); \
   })
#endif

/* Open and load the functions from the target library.
 * return negative on error, 0 on success.
 * */
static int
gbm_wrapper_loadsyms(const char *path, struct gbm_wrapper_device *wgbm)
{
   wgbm->library = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
   if (wgbm->library == NULL) {
      fprintf(stderr, "gbm: failed to open wrapped library (%s)\n", path);
      fprintf(stderr, "gbm: Last dlopen error: %s\n", dlerror());
      return -1;
    }

   /* Wrap all the functions. */
   if (WRAP_GBM_FUNCTION(wgbm, gbm_bo_create) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_destroy) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_get_device) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_get_fd) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_get_format) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_get_handle) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_get_height) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_get_stride) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_get_width) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_import) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_map) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_bo_unmap) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_create_device) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_device_destroy) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_device_get_backend_name) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_device_get_fd) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_device_is_format_supported) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_surface_create) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_surface_destroy) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_surface_has_free_buffers) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_surface_lock_front_buffer) ||
         WRAP_GBM_FUNCTION(wgbm, gbm_surface_release_buffer)) {
            /* Close if any of them are null.
             * We will just require them all to be implemented.
             */
            dlclose(wgbm->library);
            wgbm->library = NULL;
            return -1;
         }
   /* Success. */
   return 0;
}

/* Load and wrap the target wrapper library functions.
 */
static struct gbm_wrapper_device *
load_wrapped_gbm()
{
   struct gbm_wrapper_device *wgbm;
   const char * wrapped_lib;
   wgbm = calloc(1, sizeof *wgbm);
   if (!wgbm)
      return NULL;

   /* Allow users to specify the wrapped library by
    * env variable.
    */
   wrapped_lib = getenv("GBM_WRAP_LIBRARY");
   if (!wrapped_lib) {
      wrapped_lib = DEFAULT_WRAPPED_GBM_LIBRARY;
   }

   if (gbm_wrapper_loadsyms(wrapped_lib, wgbm)) {
     free_gbm(wgbm);
     return NULL;
   }
   /* Give a message for users to know it has happened.
    * NOTE: It could be considered excessive to log on success
    * but for testing/early adoption we want this to be clear
    * in the logs.
    */
   fprintf(stderr, "gbm: Using wrapped gbm library %s\n", wrapped_lib);
   return wgbm;
}

/**
 * Create the wrapper device.
 */
static struct gbm_device *
wrap_device_create(int fd)
{
   struct gbm_wrapper_device *wgbm;

   wgbm = load_wrapped_gbm();
   if (!wgbm)
      return NULL;

   /* gbm_device values for stat, refcount are
    * handled by the user of the backend.
    */
   wgbm->base.fd = fd;
   /* Wrap the base functions. */
   wgbm->base.destroy = wrap_device_destroy;
   wgbm->base.is_format_supported = gbm_wrap_is_format_supported;
   wgbm->base.bo_create = gbm_wrap_bo_create;
   wgbm->base.bo_import = gbm_wrap_bo_import;
   wgbm->base.bo_map = gbm_wrap_bo_map;
   wgbm->base.bo_unmap = gbm_wrap_bo_unmap;
   wgbm->base.bo_write = gbm_wrap_bo_write;
   wgbm->base.bo_get_fd = gbm_wrap_bo_get_fd;
   wgbm->base.bo_destroy = gbm_wrap_bo_destroy;
   wgbm->base.surface_create = gbm_wrap_surface_create;
   wgbm->base.surface_release_buffer = gbm_wrap_surface_release_buffer;
   wgbm->base.surface_has_free_buffers = gbm_wrap_surface_has_free_buffers;
   wgbm->base.surface_destroy = gbm_wrap_surface_destroy;

   wgbm->base.name = "wrapped";

   /* Create the underlying gbm. */
   wgbm->gbm_device = wgbm->wfuncs.gbm_create_device(fd);
   /* On failure, clean up what was allocated so far. */
   if (!wgbm->gbm_device) {
      _destroy_device(wgbm);
      return NULL;
   }

   return &wgbm->base;
}

struct gbm_backend gbm_wrapper_backend = {
   .backend_name = "wrapper",
   .create_device = wrap_device_create,
};
