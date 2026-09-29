#ifndef AMIMAIL_GUI_ICONS_H
#define AMIMAIL_GUI_ICONS_H

#include "amigmail.h"

#if AMIGMAIL_AMIGA
#include <intuition/intuition.h>
#include <intuition/classes.h>
#include <graphics/rastport.h>

int gui_icons_init(void);
void gui_icons_cleanup(void);
Class *gui_reply_arrow_button_class(void);
void gui_set_reply_arrow_expanded(int expanded);
struct Task;
void gui_reply_arrow_watch_release(struct Task *task, ULONG signal_mask);
void gui_reply_arrow_unwatch_release(void);
int gui_reply_arrow_was_pressed(void);
int gui_reply_arrow_was_released(void);
void gui_draw_sort_icon(struct RastPort *rp, LONG left, LONG top,
                        int ascending, LONG pen);
#endif

#endif
