#ifndef SCREENHACK_STANDALONE_H
#define SCREENHACK_STANDALONE_H

#include <X11/Xlib.h>
#include <X11/Xresource.h>

extern char *progname;

int get_integer_resource(Display *dpy, const char *name,
                         const char *resource_class);

typedef void *(*screenhack_init_fn)(Display *, Window);
typedef unsigned long (*screenhack_draw_fn)(Display *, Window, void *);
typedef void (*screenhack_reshape_fn)(Display *, Window, void *,
                                      unsigned int, unsigned int);
typedef Bool (*screenhack_event_fn)(Display *, Window, void *, XEvent *);
typedef void (*screenhack_free_fn)(Display *, Window, void *);

int screenhack_standalone_main(int argc, char **argv,
                               const char *class_name,
                               const char *const *defaults,
                               const XrmOptionDescRec *module_options,
                               screenhack_init_fn init_cb,
                               screenhack_draw_fn draw_cb,
                               screenhack_reshape_fn reshape_cb,
                               screenhack_event_fn event_cb,
                               screenhack_free_fn free_cb);

#define XSCREENSAVER_MODULE(class_name, prefix)                         \
  int main(int argc, char **argv)                                      \
  {                                                                    \
    return screenhack_standalone_main(argc, argv, class_name,           \
                                      prefix##_defaults,                \
                                      prefix##_options,                 \
                                      prefix##_init,                    \
                                      prefix##_draw,                    \
                                      prefix##_reshape,                 \
                                      prefix##_event,                   \
                                      prefix##_free);                   \
  }

#endif
