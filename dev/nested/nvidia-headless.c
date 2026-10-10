// LD_PRELOAD for nested Hyprland on NVIDIA-only hosts. Aquamarine's headless
// output offers only implicit/LINEAR modifiers. NVIDIA can't render to LINEAR,
// and aquamarine's fallback assumes LINEAR without asking, so HEADLESS-1 never
// gets a renderable buffer. Fail over to an implicit-modifier render buffer;
// aquamarine then reads the real (block-linear) modifier back from the bo.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <stdint.h>
#include <stdlib.h>

// Only Hyprland itself needs this; keep it out of hypruned and test clients.
__attribute__((constructor)) static void only_this_process(void) { unsetenv("LD_PRELOAD"); }

struct gbm_bo *gbm_bo_create_with_modifiers2(struct gbm_device *device, uint32_t width, uint32_t height, uint32_t format,
                                             const uint64_t *modifiers, const unsigned int count, uint32_t flags) {
    static struct gbm_bo *(*real)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, unsigned int, uint32_t);
    if (!real)
        real = (struct gbm_bo *(*)(struct gbm_device *, uint32_t, uint32_t, uint32_t, const uint64_t *, unsigned int, uint32_t))dlsym(
            RTLD_NEXT, "gbm_bo_create_with_modifiers2");
    struct gbm_bo *bo = real(device, width, height, format, modifiers, count, flags);
    if (!bo && count == 1 && modifiers[0] == DRM_FORMAT_MOD_LINEAR && (flags & GBM_BO_USE_RENDERING))
        bo = gbm_bo_create(device, width, height, format, flags & ~GBM_BO_USE_LINEAR);
    return bo;
}
