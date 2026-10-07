// Nested-only virtual pointer for integration tests. Never connects implicitly.
#include <wayland-client.h>
#include "virtual-pointer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
static struct zwlr_virtual_pointer_manager_v1* manager;
static void global(void* data,struct wl_registry* r,uint32_t id,const char* name,uint32_t version){
    (void)data;if(!strcmp(name,"zwlr_virtual_pointer_manager_v1"))manager=wl_registry_bind(r,id,&zwlr_virtual_pointer_manager_v1_interface,version<2?version:2);
}
static void removed(void* data,struct wl_registry* r,uint32_t id){(void)data;(void)r;(void)id;}
int main(int argc,char** argv){
    if(!getenv("HYPRUNE_VERIFIED_NESTED")){fputs("use dev/nested/ctl.py env\n",stderr);return 1;}
    struct wl_display* display=wl_display_connect(NULL);if(!display)return 2;
    struct wl_registry* registry=wl_display_get_registry(display);const struct wl_registry_listener listener={global,removed};wl_registry_add_listener(registry,&listener,NULL);wl_display_roundtrip(display);if(!manager)return 3;
    struct zwlr_virtual_pointer_v1* pointer=zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager,NULL);
    if(argc==3){zwlr_virtual_pointer_v1_motion(pointer,1,wl_fixed_from_double(atof(argv[1])),wl_fixed_from_double(atof(argv[2])));zwlr_virtual_pointer_v1_frame(pointer);}
    else {zwlr_virtual_pointer_v1_button(pointer,1,272,WL_POINTER_BUTTON_STATE_PRESSED);zwlr_virtual_pointer_v1_frame(pointer);wl_display_roundtrip(display);usleep(60000);zwlr_virtual_pointer_v1_button(pointer,61,272,WL_POINTER_BUTTON_STATE_RELEASED);zwlr_virtual_pointer_v1_frame(pointer);}
    wl_display_roundtrip(display);zwlr_virtual_pointer_v1_destroy(pointer);wl_display_roundtrip(display);wl_display_disconnect(display);return 0;
}
