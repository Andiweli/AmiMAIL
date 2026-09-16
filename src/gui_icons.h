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
void gui_draw_sort_icon(struct RastPort *rp, LONG left, LONG top,
                        int ascending, LONG pen);
#endif

#endif
