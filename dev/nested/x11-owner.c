// Isolated X11 transient owner fixture. Run only in the verified nested DISPLAY.
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void name(Display *d, Window w, const char *n) {
    XStoreName(d, w, n);
    XClassHint c = {"hyprune-owner-audit", "hyprune-owner-audit"};
    XSetClassHint(d, w, &c);
    unsigned long pid = getpid();
    XChangeProperty(d, w, XInternAtom(d, "_NET_WM_PID", False), XA_CARDINAL, 32, PropModeReplace, (unsigned char *)&pid,
                    1);
}
int main(int argc, char **argv) {
    if (argc != 3)
        return 2;
    Display *d = XOpenDisplay(NULL);
    if (!d)
        return 3;
    Window root = DefaultRootWindow(d), w = XCreateSimpleWindow(d, root, 20, 20, 640, 480, 0, 0, 0x408080);
    name(d, w, argv[1]);
    XMapWindow(d, w);
    XFlush(d);
    int shown = 0;
    for (;;) {
        if (!shown && access(argv[2], F_OK) == 0) {
            shown = 1;
            XSetWindowAttributes attr = {.override_redirect = True, .background_pixel = 0xd0c040};
            Window menu = XCreateWindow(d, root, 80, 80, 320, 160, 0, CopyFromParent, InputOutput, CopyFromParent,
                                        CWOverrideRedirect | CWBackPixel, &attr);
            name(d, menu, "Audit X11 menu");
            XSetTransientForHint(d, menu, w);
            XMapWindow(d, menu);
            Window dialog = XCreateSimpleWindow(d, root, 30, 30, 1600, 900, 0, 0, 0x408080);
            name(d, dialog, "Audit X11 dialog");
            XSetTransientForHint(d, dialog, w);
            XMapWindow(d, dialog);
            XFlush(d);
        }
        while (XPending(d)) {
            XEvent event;
            XNextEvent(d, &event);
        }
        usleep(10000);
    }
}
