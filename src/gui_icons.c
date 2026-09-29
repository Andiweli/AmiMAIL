#include "gui_icons.h"

#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <gadgets/button.h>
#include <intuition/gadgetclass.h>
#include <intuition/screens.h>
#include <proto/button.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <string.h>

/* Exact opaque-pixel masks from the four tiny PNGs supplied for AmiMAIL.
 * The PNG background is transparent, therefore only the set pixels are ever
 * drawn.  In particular the reply arrow is rendered *after* button.gadget has
 * painted its normal/selected face, so no grey image rectangle can appear
 * when the button is pressed. */
static const UBYTE reply_up_rows[5] = {
    0x18U, 0x3cU, 0x66U, 0xc3U, 0x81U
};
static const UBYTE reply_down_rows[5] = {
    0x81U, 0xc3U, 0x66U, 0x3cU, 0x18U
};
static const UBYTE sort_up_rows[4] = {
    0x04U, 0x0aU, 0x11U, 0x11U
};
static const UBYTE sort_down_rows[4] = {
    0x11U, 0x11U, 0x0aU, 0x04U
};

static Class *reply_arrow_class;
static int reply_arrow_expanded;
/* The dispatcher runs in Intuition's input context. It only records native
 * activation/completion and signals the application; it never disposes a
 * window, pumps input, or waits. All object ownership stays in the GUI task. */
static struct Task *reply_release_task;
static ULONG reply_release_signal;
static volatile int reply_release_pressed;
static volatile int reply_release_finished;

void gui_reply_arrow_watch_release(struct Task *task, ULONG signal_mask)
{
    Forbid();
    reply_release_pressed = 0;
    reply_release_finished = 0;
    reply_release_signal = signal_mask;
    reply_release_task = task;
    Permit();
}

void gui_reply_arrow_unwatch_release(void)
{
    Forbid();
    reply_release_task = NULL;
    reply_release_signal = 0UL;
    reply_release_pressed = 0;
    reply_release_finished = 0;
    Permit();
}

int gui_reply_arrow_was_pressed(void)
{
    return reply_release_pressed != 0;
}

int gui_reply_arrow_was_released(void)
{
    return reply_release_finished != 0;
}

static void draw_mask_rows(struct RastPort *rp, LONG left, LONG top,
                           const UBYTE *rows, WORD width, WORD height,
                           ULONG pen)
{
    UBYTE old_pen;
    WORD y, x;
    if (!rp || !rows || width <= 0 || height <= 0) return;

    old_pen = rp->FgPen;
    SetAPen(rp, pen);
    for (y = 0; y < height; ++y) {
        UBYTE bits = rows[y];
        for (x = 0; x < width; ++x) {
            if (bits & (UBYTE)(1U << (width - 1 - x)))
                WritePixel(rp, left + (LONG)x, top + (LONG)y);
        }
    }
    SetAPen(rp, old_pen);
}

static void draw_reply_arrow(struct RastPort *rp, struct Gadget *gadget,
                             struct DrawInfo *draw_info)
{
    const UBYTE *rows;
    ULONG pen;
    LONG left, top;
    int selected;

    if (!rp || !gadget) return;

    /* Keep the currently active popup-state arrow visible while the button
     * is held down.  The persistent state changes only after GADGETUP has
     * actually opened/closed the popup:
     *
     *   closed: DOWN remains DOWN while pressed, then becomes UP on open
     *   open:   UP remains UP while pressed, then becomes DOWN on close
     *
     * button.gadget may repaint its selected face while held; the dispatcher
     * redraws this overlay afterwards, but it must not anticipate the state
     * transition before the mouse button is released. */
    selected = (gadget->Flags & GFLG_SELECTED) ? 1 : 0;
    (void)selected;
    rows = reply_arrow_expanded ? reply_up_rows : reply_down_rows;

    pen = (ULONG)rp->FgPen;
    if (draw_info) {
        if (gadget->Flags & GFLG_DISABLED)
            pen = draw_info->dri_Pens[SHADOWPEN];
        else
            pen = draw_info->dri_Pens[TEXTPEN];
    }

    left = (LONG)gadget->LeftEdge +
           ((LONG)gadget->Width - 8L) / 2L;
    top = (LONG)gadget->TopEdge +
          ((LONG)gadget->Height - 5L) / 2L;
    draw_mask_rows(rp, left, top, rows, 8, 5, pen);
}

static void redraw_reply_arrow_after_input(struct Gadget *gadget,
                                           struct GadgetInfo *ginfo)
{
    struct RastPort *rp;
    struct DrawInfo *draw_info = NULL;

    if (!gadget || !ginfo) return;
    rp = ObtainGIRPort(ginfo);
    if (!rp) return;
    draw_info = ginfo->gi_DrInfo;
    draw_reply_arrow(rp, gadget, draw_info);
    ReleaseGIRPort(rp);
}

static ULONG reply_arrow_dispatcher(struct Hook *hook, APTR object_ptr,
                                    APTR message_ptr)
{
    Class *cl = hook ? (Class *)hook->h_Data : NULL;
    Object *object = (Object *)object_ptr;
    Msg message = (Msg)message_ptr;
    ULONG result;

    if (!cl || !object || !message) return 0UL;
    if (message->MethodID == GM_GOACTIVE && reply_release_task) {
        reply_release_pressed = 1;
        reply_release_finished = 0;
    }
    result = DoSuperMethodA(cl, object, message);

    if (message->MethodID == GM_RENDER) {
        struct gpRender *render = (struct gpRender *)message_ptr;
        struct Gadget *gadget = (struct Gadget *)object_ptr;
        struct DrawInfo *draw_info = NULL;

        if (!render->gpr_RPort) return result;
        if (render->gpr_GInfo)
            draw_info = render->gpr_GInfo->gi_DrInfo;
        draw_reply_arrow(render->gpr_RPort, gadget, draw_info);
    } else if (message->MethodID == GM_GOACTIVE ||
               message->MethodID == GM_HANDLEINPUT) {
        struct gpInput *input = (struct gpInput *)message_ptr;

        /* button.gadget repaints its selected face from GM_GOACTIVE /
         * GM_HANDLEINPUT without necessarily sending our subclass a separate
         * GM_RENDER.  That native repaint used to erase the down arrow while
         * the user held the mouse button.  Repaint the transparent overlay
         * after the superclass has finished drawing the pressed state. */
        redraw_reply_arrow_after_input((struct Gadget *)object_ptr,
                                       input->gpi_GInfo);
    } else if (message->MethodID == GM_GOINACTIVE) {
        struct gpGoInactive *inactive = (struct gpGoInactive *)message_ptr;

        /* Restore the persistent arrow after button.gadget paints its
         * released face.  The following GADGETUP may then open/close the
         * popup and update reply_arrow_expanded normally. */
        redraw_reply_arrow_after_input((struct Gadget *)object_ptr,
                                       inactive->gpgi_GInfo);
        if (reply_release_task && reply_release_pressed) {
            reply_release_finished = 1;
            if (reply_release_signal)
                Signal(reply_release_task, reply_release_signal);
        }
    }

    return result;
}

int gui_icons_init(void)
{
    if (reply_arrow_class) return 1;

    reply_arrow_class = MakeClass(NULL, NULL, BUTTON_GetClass(), 0UL, 0UL);
    if (!reply_arrow_class) return 0;

    reply_arrow_class->cl_Dispatcher.h_Entry =
        (__typeof__(reply_arrow_class->cl_Dispatcher.h_Entry))HookEntry;
    reply_arrow_class->cl_Dispatcher.h_SubEntry =
        (__typeof__(reply_arrow_class->cl_Dispatcher.h_SubEntry))
            reply_arrow_dispatcher;
    reply_arrow_class->cl_Dispatcher.h_Data = reply_arrow_class;
    reply_arrow_expanded = 0;
    return 1;
}

void gui_icons_cleanup(void)
{
    gui_reply_arrow_unwatch_release();
    if (reply_arrow_class) {
        FreeClass(reply_arrow_class);
        reply_arrow_class = NULL;
    }
    reply_arrow_expanded = 0;
}

Class *gui_reply_arrow_button_class(void)
{
    return reply_arrow_class;
}

void gui_set_reply_arrow_expanded(int expanded)
{
    reply_arrow_expanded = expanded ? 1 : 0;
}

void gui_draw_sort_icon(struct RastPort *rp, LONG left, LONG top,
                        int ascending, LONG pen)
{
    const UBYTE *rows = ascending ? sort_up_rows : sort_down_rows;
    if (!rp || pen < 0) return;
    draw_mask_rows(rp, left, top, rows, 5, 4, (ULONG)pen);
}

#endif
