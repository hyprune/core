// Original nested-only session lock client (ext-session-lock-v1): locks the nested
// compositor, shows a plain lock surface on every output and prints "locked"; a line
// "unlock" on stdin unlocks it, prints "unlocked" and exits. EOF unlocks too.
#define _GNU_SOURCE
#include "ext-session-lock.h"
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct ext_session_lock_manager_v1 *manager;
static struct ext_session_lock_v1 *lock;
static struct wl_output *outputs[8];
static int outputCount, locked, finished;
static struct wl_buffer *buffer(int w, int h) {
    int fd = memfd_create("hyprune-nested-lock", MFD_CLOEXEC);
    size_t n = (size_t)w * h * 4;
    if (fd < 0 || ftruncate(fd, n))
        exit(2);
    uint32_t *pixels = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        exit(2);
    for (size_t i = 0; i < (size_t)w * h; i++)
        pixels[i] = 0xff202830;
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, n);
    struct wl_buffer *b = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    munmap(pixels, n);
    close(fd);
    return b;
}
static void configure(void *data, struct ext_session_lock_surface_v1 *s, uint32_t serial, uint32_t w, uint32_t h) {
    struct wl_surface *surface = data;
    ext_session_lock_surface_v1_ack_configure(s, serial);
    wl_surface_attach(surface, buffer(w ? w : 64, h ? h : 64), 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_commit(surface);
}
static const struct ext_session_lock_surface_v1_listener surfaceListener = {.configure = configure};
static void lockedEvent(void *data, struct ext_session_lock_v1 *l) {
    (void)data, (void)l;
    locked = 1;
    printf("locked\n");
    fflush(stdout);
}
static void finishedEvent(void *data, struct ext_session_lock_v1 *l) {
    (void)data, (void)l;
    finished = 1;
    printf("finished\n");
    fflush(stdout);
}
static const struct ext_session_lock_v1_listener lockListener = {.locked = lockedEvent, .finished = finishedEvent};
static void global(void *data, struct wl_registry *r, uint32_t name, const char *iface, uint32_t version) {
    (void)data, (void)version;
    if (!strcmp(iface, wl_compositor_interface.name))
        compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
    else if (!strcmp(iface, wl_shm_interface.name))
        shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!strcmp(iface, ext_session_lock_manager_v1_interface.name))
        manager = wl_registry_bind(r, name, &ext_session_lock_manager_v1_interface, 1);
    else if (!strcmp(iface, wl_output_interface.name) && outputCount < 8)
        outputs[outputCount++] = wl_registry_bind(r, name, &wl_output_interface, 1);
}
static void removed(void *data, struct wl_registry *r, uint32_t name) { (void)data, (void)r, (void)name; }
static const struct wl_registry_listener registryListener = {.global = global, .global_remove = removed};
int main(void) {
    display = wl_display_connect(NULL);
    if (!display)
        return 2;
    wl_registry_add_listener(wl_display_get_registry(display), &registryListener, NULL);
    wl_display_roundtrip(display);
    if (!compositor || !shm || !manager) {
        fprintf(stderr, "lock-client: no ext-session-lock-v1\n");
        return 2;
    }
    lock = ext_session_lock_manager_v1_lock(manager);
    ext_session_lock_v1_add_listener(lock, &lockListener, NULL);
    for (int i = 0; i < outputCount; i++) {
        struct wl_surface *surface = wl_compositor_create_surface(compositor);
        struct ext_session_lock_surface_v1 *s = ext_session_lock_v1_get_lock_surface(lock, surface, outputs[i]);
        ext_session_lock_surface_v1_add_listener(s, &surfaceListener, surface);
    }
    struct pollfd fds[2] = {{wl_display_get_fd(display), POLLIN, 0}, {0, POLLIN, 0}};
    char line[64];
    while (!finished) {
        wl_display_flush(display);
        if (poll(fds, 2, -1) < 0)
            return 2;
        if (fds[0].revents & POLLIN && wl_display_dispatch(display) < 0)
            return 2;
        if (fds[1].revents & (POLLIN | POLLHUP)) {
            if (!fgets(line, sizeof line, stdin) || !strncmp(line, "unlock", 6))
                break;
        }
    }
    if (!finished && locked)
        ext_session_lock_v1_unlock_and_destroy(lock);
    else
        ext_session_lock_v1_destroy(lock);
    wl_display_roundtrip(display);
    printf("unlocked\n");
    fflush(stdout);
    return 0;
}
