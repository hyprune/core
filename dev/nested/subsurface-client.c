// Original nested-only SHM client: offset wl_subsurface with its own input target.
#define _GNU_SOURCE
#include "xdg-shell.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_subcompositor *subcompositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_surface *root, *child, *entered;
static struct xdg_surface *xdg;
static struct xdg_toplevel *top;
static const char *marker;
static int width = 800, height = 500;
static struct wl_buffer *buffer(int w, int h, uint32_t color) {
    int fd = memfd_create("hyprune-nested-shm", MFD_CLOEXEC);
    size_t n = (size_t)w * h * 4;
    if (fd < 0 || ftruncate(fd, n))
        exit(2);
    uint32_t *pixels = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        exit(2);
    for (int i = 0; i < w * h; i++)
        pixels[i] = color;
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, n);
    struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    munmap(pixels, n);
    close(fd);
    return b;
}
static void configured(void *data, struct xdg_surface *surface, uint32_t serial) {
    (void)data;
    xdg_surface_ack_configure(surface, serial);
    wl_surface_attach(root, buffer(width, height, 0xff20445a), 0, 0);
    wl_surface_damage_buffer(root, 0, 0, width, height);
    wl_surface_attach(child, buffer(240, 140, 0xffe9af38), 0, 0);
    wl_surface_damage_buffer(child, 0, 0, 240, 140);
    wl_surface_commit(child);
    wl_surface_commit(root);
}
static const struct xdg_surface_listener xdg_listener = {.configure = configured};
static void top_config(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *a) {
    (void)d;
    (void)t;
    (void)a;
    if (w > 0)
        width = w;
    if (h > 0)
        height = h;
}
static void close_top(void *d, struct xdg_toplevel *t) {
    (void)d;
    (void)t;
    exit(0);
}
static const struct xdg_toplevel_listener top_listener = {.configure = top_config, .close = close_top};
static void ping(void *d, struct xdg_wm_base *w, uint32_t s) {
    (void)d;
    xdg_wm_base_pong(w, s);
}
static const struct xdg_wm_base_listener wm_listener = {.ping = ping};
static void enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *r, wl_fixed_t x, wl_fixed_t y) {
    (void)d;
    (void)p;
    (void)s;
    (void)x;
    (void)y;
    entered = r;
    fprintf(stderr, "enter %s %.1f %.1f\n", r == child ? "child" : "root", wl_fixed_to_double(x),
            wl_fixed_to_double(y));
    fflush(stderr);
}
static void leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *r) {
    (void)d;
    (void)p;
    (void)s;
    (void)r;
    entered = NULL;
}
static void motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    (void)d;
    (void)p;
    (void)t;
    (void)x;
    (void)y;
}
static void button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t state) {
    (void)d;
    (void)p;
    (void)s;
    (void)t;
    fprintf(stderr, "button %s %u %u\n", entered == child ? "child" : "root", b, state);
    fflush(stderr);
    if (entered == child && b == 272 && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        FILE *f = fopen(marker, "w");
        if (f) {
            fputs("subsurface click landed\n", f);
            fclose(f);
        }
    }
}
static void axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {
    (void)d;
    (void)p;
    (void)t;
    (void)a;
    (void)v;
}
static const struct wl_pointer_listener pointer_listener = {
    .enter = enter, .leave = leave, .motion = motion, .button = button, .axis = axis};
static void caps(void *d, struct wl_seat *s, uint32_t c) {
    (void)d;
    if (c & WL_SEAT_CAPABILITY_POINTER) {
        struct wl_pointer *p = wl_seat_get_pointer(s);
        wl_pointer_add_listener(p, &pointer_listener, NULL);
    }
}
static void name(void *d, struct wl_seat *s, const char *n) {
    (void)d;
    (void)s;
    (void)n;
}
static const struct wl_seat_listener seat_listener = {.capabilities = caps, .name = name};
static void global(void *d, struct wl_registry *r, uint32_t id, const char *n, uint32_t v) {
    (void)d;
    if (!strcmp(n, "wl_compositor"))
        compositor = wl_registry_bind(r, id, &wl_compositor_interface, v < 4 ? v : 4);
    if (!strcmp(n, "wl_subcompositor"))
        subcompositor = wl_registry_bind(r, id, &wl_subcompositor_interface, 1);
    if (!strcmp(n, "wl_shm"))
        shm = wl_registry_bind(r, id, &wl_shm_interface, 1);
    if (!strcmp(n, "xdg_wm_base")) {
        wm = wl_registry_bind(r, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm, &wm_listener, NULL);
    }
    if (!strcmp(n, "wl_seat")) {
        struct wl_seat *s = wl_registry_bind(r, id, &wl_seat_interface, 1);
        wl_seat_add_listener(s, &seat_listener, NULL);
    }
}
static void removed(void *d, struct wl_registry *r, uint32_t id) {
    (void)d;
    (void)r;
    (void)id;
}
int main(int argc, char **argv) {
    if (argc != 2 || !getenv("HYPRUNE_VERIFIED_NESTED"))
        return 1;
    marker = argv[1];
    display = wl_display_connect(NULL);
    if (!display)
        return 2;
    struct wl_registry *r = wl_display_get_registry(display);
    const struct wl_registry_listener listener = {global, removed};
    wl_registry_add_listener(r, &listener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !subcompositor || !shm || !wm)
        return 2;
    root = wl_compositor_create_surface(compositor);
    child = wl_compositor_create_surface(compositor);
    struct wl_subsurface *sub = wl_subcompositor_get_subsurface(subcompositor, child, root);
    wl_subsurface_set_position(sub, 100, 100);
    xdg = xdg_wm_base_get_xdg_surface(wm, root);
    xdg_surface_add_listener(xdg, &xdg_listener, NULL);
    top = xdg_surface_get_toplevel(xdg);
    xdg_toplevel_add_listener(top, &top_listener, NULL);
    xdg_toplevel_set_title(top, "M3A subsurface laboratory");
    xdg_toplevel_set_app_id(top, "hyprune-m3a-subsurface");
    wl_surface_commit(root);
    while (wl_display_dispatch(display) >= 0) {
    }
    return 0;
}
