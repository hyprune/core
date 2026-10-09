// Hostile-title X11 fixture. Run only in the verified nested DISPLAY.
// x11-title <class-prefix> <units> [invalid]: ~units*10 code point UTF-8
// _NET_WM_NAME, a 300+ byte WM_CLASS; "invalid" appends malformed UTF-8 bytes.
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    if (argc < 3)
        return 2;
    int units = atoi(argv[2]), invalid = argc > 3 && !strcmp(argv[3], "invalid");
    const char *unit = "kitty \xc3\xa9\xe6\xbc\xa2\xf0\x9f\x98\x80 ";
    size_t size = 16 + (size_t)units * strlen(unit);
    char *title = malloc(size), *cls = malloc(strlen(argv[1]) + 400);
    strcpy(title, "LONG-");
    for (int i = 0; i < units; i++)
        strcat(title, unit);
    if (invalid)
        strcat(title, "\xff\xe6\xbc");
    strcpy(cls, argv[1]);
    strcat(cls, "-");
    for (int i = 0; i < 300; i++)
        strcat(cls, "c");
    if (invalid)
        strcat(cls, "\xe9");
    Display *d = XOpenDisplay(NULL);
    if (!d)
        return 3;
    Window w = XCreateSimpleWindow(d, DefaultRootWindow(d), 20, 20, 640, 480, 0, 0, 0x406080);
    XChangeProperty(d, w, XInternAtom(d, "_NET_WM_NAME", False), XInternAtom(d, "UTF8_STRING", False), 8,
                    PropModeReplace, (unsigned char *)title, strlen(title));
    XClassHint hint = {cls, cls};
    XSetClassHint(d, w, &hint);
    XMapWindow(d, w);
    XFlush(d);
    for (;;) {
        while (XPending(d)) {
            XEvent event;
            XNextEvent(d, &event);
        }
        usleep(10000);
    }
}
