// Nested-only pointer. Persistent mode keeps popup/implicit grabs alive between events.
#include "virtual-pointer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
static struct zwlr_virtual_pointer_manager_v1 *manager;
static void global(void *data, struct wl_registry *r, uint32_t id, const char *name, uint32_t version) {
    (void)data;
    if (!strcmp(name, "zwlr_virtual_pointer_manager_v1"))
        manager = wl_registry_bind(r, id, &zwlr_virtual_pointer_manager_v1_interface, version < 2 ? version : 2);
}
static void removed(void *data, struct wl_registry *r, uint32_t id) {
    (void)data;
    (void)r;
    (void)id;
}
static unsigned now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned)(t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static void command(struct wl_display *d, struct zwlr_virtual_pointer_v1 *p, int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[0], "button")) {
        int button = atoi(argv[1]), down = argc > 2 ? atoi(argv[2]) : 1;
        zwlr_virtual_pointer_v1_button(p, now(), button,
                                       down ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
        zwlr_virtual_pointer_v1_frame(p);
        if (argc == 2) {
            wl_display_roundtrip(d);
            usleep(60000);
            zwlr_virtual_pointer_v1_button(p, now(), button, WL_POINTER_BUTTON_STATE_RELEASED);
            zwlr_virtual_pointer_v1_frame(p);
        }
    } else if (argc == 2 && !strcmp(argv[0], "scroll")) {
        zwlr_virtual_pointer_v1_axis_source(p, WL_POINTER_AXIS_SOURCE_WHEEL);
        zwlr_virtual_pointer_v1_axis_discrete(p, now(), WL_POINTER_AXIS_VERTICAL_SCROLL,
                                              wl_fixed_from_double(atof(argv[1]) * 15), atoi(argv[1]));
        zwlr_virtual_pointer_v1_frame(p);
    } else if (argc == 3 && !strcmp(argv[0], "drag")) {
        zwlr_virtual_pointer_v1_button(p, now(), 272, 1);
        zwlr_virtual_pointer_v1_frame(p);
        wl_display_roundtrip(d);
        usleep(60000);
        zwlr_virtual_pointer_v1_motion(p, now(), wl_fixed_from_double(atof(argv[1])),
                                       wl_fixed_from_double(atof(argv[2])));
        zwlr_virtual_pointer_v1_frame(p);
        wl_display_roundtrip(d);
        usleep(60000);
        zwlr_virtual_pointer_v1_button(p, now(), 272, 0);
        zwlr_virtual_pointer_v1_frame(p);
    } else if (argc == 2) {
        zwlr_virtual_pointer_v1_motion(p, now(), wl_fixed_from_double(atof(argv[0])),
                                       wl_fixed_from_double(atof(argv[1])));
        zwlr_virtual_pointer_v1_frame(p);
    } else {
        char *click[] = {"button", "272"};
        command(d, p, 2, click);
    }
    wl_display_roundtrip(d);
}
int main(int argc, char **argv) {
    if (!getenv("HYPRUNE_VERIFIED_NESTED")) {
        fputs("use dev/nested/ctl.py env\n", stderr);
        return 1;
    }
    struct wl_display *d = wl_display_connect(NULL);
    if (!d)
        return 2;
    struct wl_registry *r = wl_display_get_registry(d);
    const struct wl_registry_listener listener = {global, removed};
    wl_registry_add_listener(r, &listener, NULL);
    wl_display_roundtrip(d);
    if (!manager)
        return 3;
    struct zwlr_virtual_pointer_v1 *p = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, NULL);
    if (argc == 2 && !strcmp(argv[1], "serve")) {
        char line[256];
        while (fgets(line, sizeof line, stdin)) {
            char *args[8];
            int n = 0;
            char *token = strtok(line, " \n");
            while (token && n < 8) {
                args[n++] = token;
                token = strtok(NULL, " \n");
            }
            command(d, p, n, args);
            puts("ok");
            fflush(stdout);
        }
    } else
        command(d, p, argc - 1, argv + 1);
    zwlr_virtual_pointer_v1_destroy(p);
    wl_display_roundtrip(d);
    wl_display_disconnect(d);
    return 0;
}
