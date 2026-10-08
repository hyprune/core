// Original nested-only fixture: a real xdg popup, dialog and client fullscreen request.
#include <gtk/gtk.h>
#include <stdio.h>
static GtkWidget *window;
static const char *marker;
static gboolean fs = FALSE;
static void selected(GtkMenuItem *item, gpointer data) {
    (void)item;
    (void)data;
    FILE *f = fopen(marker, "w");
    if (f) {
        fputs("popup click landed\n", f);
        fclose(f);
    }
    GtkWidget *menu = gtk_widget_get_parent(GTK_WIDGET(item));
    gtk_menu_popdown(GTK_MENU(menu));
    gtk_widget_destroy(menu);
}
static gboolean scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    (void)widget;
    (void)event;
    (void)data;
    char *path = g_strconcat(marker, ".scroll", NULL);
    FILE *f = fopen(path, "w");
    if (f) {
        fputs("scroll landed\n", f);
        fclose(f);
    }
    g_free(path);
    return TRUE;
}
static gboolean button(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    (void)data;
    fprintf(stderr, "fixture button %u at %.1f %.1f\n", event->button, event->x, event->y);
    fflush(stderr);
    if (event->button == 1) {
        fs = !fs;
        if (fs)
            gtk_window_fullscreen(GTK_WINDOW(window));
        else
            gtk_window_unfullscreen(GTK_WINDOW(window));
        return TRUE;
    }
    if (event->button != 3)
        return FALSE;
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item = gtk_menu_item_new_with_label("Confirm popup click");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(selected), NULL);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}
static gboolean dialog_key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)data;
    fprintf(stderr, "dialog key %u\n", event->keyval);
    fflush(stderr);
    if (event->keyval == GDK_KEY_Return) {
        gtk_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_OK);
        return TRUE;
    }
    return FALSE;
}
static gboolean key(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget;
    (void)data;
    fprintf(stderr, "fixture key %u\n", event->keyval);
    fflush(stderr);
    if (event->keyval == GDK_KEY_F11) {
        fs = !fs;
        if (fs)
            gtk_window_fullscreen(GTK_WINDOW(window));
        else
            gtk_window_unfullscreen(GTK_WINDOW(window));
        return TRUE;
    }
    if (event->keyval == GDK_KEY_F10) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO,
                                                   GTK_BUTTONS_OK, "Attached dialog");
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
        g_signal_connect_swapped(dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
        g_signal_connect(dialog, "key-press-event", G_CALLBACK(dialog_key), NULL);
        gtk_widget_show_all(dialog);
        gtk_window_present(GTK_WINDOW(dialog));
        gtk_widget_grab_focus(gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK));
        return TRUE;
    }
    return FALSE;
}
int main(int argc, char **argv) {
    if (argc < 2)
        return 1;
    marker = argv[1];
    gtk_init(&argc, &argv);
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css, "label { font-size: 24px; } menuitem { font-size: 20px; padding: 12px; }", -1,
                                    NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(css),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), argc > 2 ? argv[2] : "M3A popup laboratory");
    gtk_window_set_default_size(GTK_WINDOW(window), 800, 500);
    GtkWidget *box = gtk_event_box_new();
    gtk_widget_set_can_focus(box, TRUE);
    gtk_widget_add_events(box, GDK_BUTTON_PRESS_MASK | GDK_KEY_PRESS_MASK | GDK_SCROLL_MASK);
    GtkWidget *label = gtk_label_new("HYPRUNE  /  HANDS-ON DESKTOP\n\nRight click to open a real Wayland popup\nLeft "
                                     "click fullscreen  /  F10 attached dialog");
    gtk_container_add(GTK_CONTAINER(box), label);
    gtk_container_add(GTK_CONTAINER(window), box);
    g_signal_connect(box, "button-press-event", G_CALLBACK(button), NULL);
    g_signal_connect(box, "scroll-event", G_CALLBACK(scroll), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(key), NULL);
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(box);
    gtk_main();
    return 0;
}
