#ifndef MANGO_XDG_SESSION_H
#define MANGO_XDG_SESSION_H

struct wl_display;

void mango_xdg_session_manager_init(struct wl_display *display);
void mango_xdg_session_manager_finish(void);

#endif
