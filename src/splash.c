#include "splash.h"
#include "amigmail.h"
#include "banner_data.h"
#include "i18n.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if AMIGMAIL_AMIGA

#include <clib/alib_protos.h>
#include <classes/window.h>
#include <exec/libraries.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <graphics/gfxbase.h>
#include <graphics/rastport.h>
#include <graphics/view.h>
#include <images/bevel.h>
#include <intuition/classes.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>
#include <proto/button.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/window.h>
#include <utility/tagitem.h>

#define SPLASH_BANNER_WIDTH 170U
#define SPLASH_BANNER_HEIGHT 28U
#define SPLASH_BANNER_COLORS 8U
#define SPLASH_MARGIN_X 12UL
#define SPLASH_MARGIN_Y 8UL
#define SPLASH_BANNER_TEXT_GAP 7UL

/* gui.c owns the process-wide library bases used by the NDK inline calls.
 * During the very early splash lifetime gui.c has not opened them yet. The
 * splash therefore keeps its own OpenLibrary() references and binds the three
 * ReAction class bases only for the short period in which the BOOPSI object
 * tree is constructed. The original globals are restored before this helper
 * returns, so the later main GUI still owns its normal lifecycle. */
extern struct GfxBase *GfxBase;
extern struct Library *WindowBase;
extern struct Library *LayoutBase;
extern struct Library *ButtonBase;

static Object *splash_window_object = NULL;
static Object *splash_banner_slot = NULL;
static struct Window *splash_window = NULL;
static struct Screen *splash_screen = NULL;
static struct GfxBase *splash_graphics_base = NULL;
static struct Library *splash_window_class_base = NULL;
static struct Library *splash_layout_class_base = NULL;
static struct Library *splash_button_class_base = NULL;
static LONG splash_pens[SPLASH_BANNER_COLORS];
static unsigned char splash_pen_owned[SPLASH_BANNER_COLORS];

static unsigned short splash_be16(const unsigned char *data)
{
    return (unsigned short)(((unsigned)data[0] << 8) | data[1]);
}

static unsigned long splash_be32(const unsigned char *data)
{
    return ((unsigned long)data[0] << 24) |
           ((unsigned long)data[1] << 16) |
           ((unsigned long)data[2] << 8) | data[3];
}

static const unsigned char *splash_chunk(const char id[4], size_t *length)
{
    size_t position = 12U;
    if (!length || amg_banner_iff_size < position ||
        memcmp(amg_banner_iff, "FORM", 4U) != 0 ||
        memcmp(amg_banner_iff + 8U, "ILBM", 4U) != 0)
        return NULL;

    while (position + 8U <= amg_banner_iff_size) {
        size_t chunk_length =
            (size_t)splash_be32(amg_banner_iff + position + 4U);
        size_t data_position = position + 8U;
        if (chunk_length > amg_banner_iff_size - data_position) return NULL;
        if (memcmp(amg_banner_iff + position, id, 4U) == 0) {
            *length = chunk_length;
            return amg_banner_iff + data_position;
        }
        position = data_position + chunk_length + (chunk_length & 1U);
    }
    return NULL;
}

static void splash_prepare_pens(void)
{
    const unsigned char *palette;
    size_t palette_length = 0U;
    size_t i;

    if (!splash_screen || !splash_screen->ViewPort.ColorMap) return;
    memset(splash_pen_owned, 0, sizeof(splash_pen_owned));
    palette = splash_chunk("CMAP", &palette_length);

    for (i = 0U; i < SPLASH_BANNER_COLORS; ++i) {
        unsigned char red = 0x88U, green = 0x88U, blue = 0x88U;
        LONG pen;
        if (palette && i * 3U + 2U < palette_length) {
            red = palette[i * 3U];
            green = palette[i * 3U + 1U];
            blue = palette[i * 3U + 2U];
        }
        pen = ObtainBestPenA(
            splash_screen->ViewPort.ColorMap,
            (ULONG)red * 0x01010101UL,
            (ULONG)green * 0x01010101UL,
            (ULONG)blue * 0x01010101UL, NULL);
        if (pen >= 0) {
            splash_pens[i] = pen;
            splash_pen_owned[i] = 1U;
        } else {
            pen = FindColor(
                splash_screen->ViewPort.ColorMap,
                (ULONG)red * 0x01010101UL,
                (ULONG)green * 0x01010101UL,
                (ULONG)blue * 0x01010101UL, -1L);
            splash_pens[i] =
                pen >= 0 ? pen : (LONG)splash_screen->BlockPen;
        }
    }
}

static void splash_release_pens(void)
{
    size_t i;
    if (!splash_screen || !splash_screen->ViewPort.ColorMap) return;
    for (i = 0U; i < SPLASH_BANNER_COLORS; ++i) {
        if (splash_pen_owned[i]) {
            ReleasePen(splash_screen->ViewPort.ColorMap, splash_pens[i]);
            splash_pen_owned[i] = 0U;
        }
    }
}

static void splash_draw_banner(void)
{
    const unsigned char *header, *body;
    size_t header_length = 0U, body_length = 0U;
    unsigned width, height, planes, row_bytes;
    unsigned x, y;
    struct RastPort *rastport;
    struct Gadget *slot;
    LONG left, top;

    if (!splash_window || !splash_banner_slot) return;
    rastport = splash_window->RPort;
    if (!rastport) return;
    slot = (struct Gadget *)splash_banner_slot;
    left = (LONG)slot->LeftEdge;
    top = (LONG)slot->TopEdge;

    if (slot->Width > 0 && slot->Height > 0) {
        /* AmiMAIL CMAP index 2 is the exact #888888 banner background. */
        SetAPen(rastport, (ULONG)splash_pens[2]);
        RectFill(rastport,
                 left, top,
                 left + (LONG)slot->Width - 1L,
                 top + (LONG)slot->Height - 1L);
    }

    header = splash_chunk("BMHD", &header_length);
    body = splash_chunk("BODY", &body_length);
    if (!header || header_length < 20U || !body) return;

    width = splash_be16(header);
    height = splash_be16(header + 2U);
    planes = header[8U];
    if (!width || !height || planes != 3U || header[10U] != 0U) return;
    row_bytes = ((width + 15U) / 16U) * 2U;
    if ((size_t)row_bytes * planes * height > body_length) return;

    if (width > SPLASH_BANNER_WIDTH) width = SPLASH_BANNER_WIDTH;
    if (height > SPLASH_BANNER_HEIGHT) height = SPLASH_BANNER_HEIGHT;

    for (y = 0U; y < height; ++y) {
        const unsigned char *row = body + (size_t)y * row_bytes * planes;
        x = 0U;
        while (x < width) {
            unsigned run_start = x;
            unsigned color = 0U;
            unsigned next_color;
            unsigned plane;
            for (plane = 0U; plane < planes; ++plane)
                color |= ((row[plane * row_bytes + (x >> 3)] >>
                           (7U - (x & 7U))) & 1U) << plane;
            do {
                ++x;
                if (x >= width) break;
                next_color = 0U;
                for (plane = 0U; plane < planes; ++plane)
                    next_color |= ((row[plane * row_bytes + (x >> 3)] >>
                                    (7U - (x & 7U))) & 1U) << plane;
            } while (next_color == color);
            SetAPen(rastport, (ULONG)splash_pens[color]);
            RectFill(rastport,
                     left + (LONG)run_start, top + (LONG)y,
                     left + (LONG)x - 1L, top + (LONG)y);
        }
    }
}

static Object *splash_text_object(Class *button_class, const char *text)
{
    if (!button_class) return NULL;
    return NewObject(
        button_class, NULL,
        GA_ReadOnly, TRUE,
        GA_Text, (ULONG)(uintptr_t)(text ? text : ""),
        BUTTON_BevelStyle, BVS_NONE,
        BUTTON_Transparent, TRUE,
        BUTTON_Justification, BCJ_LEFT,
        TAG_DONE);
}

static void splash_close_class_libraries(void)
{
    if (splash_button_class_base) {
        CloseLibrary(splash_button_class_base);
        splash_button_class_base = NULL;
    }
    if (splash_layout_class_base) {
        CloseLibrary(splash_layout_class_base);
        splash_layout_class_base = NULL;
    }
    if (splash_window_class_base) {
        CloseLibrary(splash_window_class_base);
        splash_window_class_base = NULL;
    }
}

static int splash_open_class_libraries(void)
{
    splash_window_class_base =
        OpenLibrary((CONST_STRPTR)"window.class", 44);
    splash_layout_class_base =
        OpenLibrary((CONST_STRPTR)"gadgets/layout.gadget", 44);
    splash_button_class_base =
        OpenLibrary((CONST_STRPTR)"gadgets/button.gadget", 44);
    if (splash_window_class_base && splash_layout_class_base &&
        splash_button_class_base)
        return 1;
    splash_close_class_libraries();
    return 0;
}

static Object *splash_create_window_object(void)
{
    Object *line1 = NULL;
    Object *line2 = NULL;
    Object *line3 = NULL;
    Object *text_group = NULL;
    Object *root_group = NULL;
    Object *window_object = NULL;
    Class *window_class = NULL;
    Class *layout_class = NULL;
    Class *button_class = NULL;
    struct Library *saved_window_base = WindowBase;
    struct Library *saved_layout_base = LayoutBase;
    struct Library *saved_button_base = ButtonBase;
    const char *client_line = "AmiMAIL " AMIMAIL_VERSION;
    const char *description_line =
        amg_tr(MSG_MAIL_CLIENT_FOR_AMIGAOS_3_2, "Mail client for AmigaOS 3.2");
    const char *copyright_line = "\251 Andreas St\374rmer";

    /* The class GetClass() entry points use the conventional global class
     * bases from the NDK proto headers. AmiMail's main GUI has not opened
     * them yet at this early point, so bind those globals only while the
     * splash object tree is constructed. The splash keeps its own library
     * references open until the objects are disposed, then the original
     * globals are restored immediately. */
    WindowBase = splash_window_class_base;
    LayoutBase = splash_layout_class_base;
    ButtonBase = splash_button_class_base;

    window_class = WINDOW_GetClass();
    layout_class = LAYOUT_GetClass();
    button_class = BUTTON_GetClass();
    if (!window_class || !layout_class || !button_class) goto fail;

    splash_banner_slot = NewObject(
        layout_class, NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_ShrinkWrap, TRUE,
        TAG_DONE);
    line1 = splash_text_object(button_class, client_line);
    line2 = splash_text_object(button_class, description_line);
    line3 = splash_text_object(button_class, copyright_line);
    if (!splash_banner_slot || !line1 || !line2 || !line3) goto fail;

    text_group = NewObject(
        layout_class, NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_ShrinkWrap, TRUE,
        LAYOUT_HorizAlignment, LALIGN_LEFT,
        LAYOUT_AddChild, (ULONG)(uintptr_t)line1,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)(uintptr_t)line2,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)(uintptr_t)line3,
        CHILD_WeightedHeight, 0,
        TAG_DONE);
    if (!text_group) goto fail;
    line1 = NULL;
    line2 = NULL;
    line3 = NULL;

    root_group = NewObject(
        layout_class, NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter, TRUE,
        LAYOUT_LeftSpacing, SPLASH_MARGIN_X,
        LAYOUT_RightSpacing, SPLASH_MARGIN_X,
        LAYOUT_TopSpacing, SPLASH_MARGIN_Y,
        LAYOUT_BottomSpacing, SPLASH_MARGIN_Y,
        LAYOUT_SpaceInner, TRUE,
        LAYOUT_InnerSpacing, SPLASH_BANNER_TEXT_GAP,
        LAYOUT_ShrinkWrap, TRUE,
        LAYOUT_HorizAlignment, LALIGN_LEFT,
        LAYOUT_BevelStyle, BVS_THIN,
        LAYOUT_AddChild, (ULONG)(uintptr_t)splash_banner_slot,
        CHILD_MinWidth, SPLASH_BANNER_WIDTH,
        CHILD_WeightedWidth, 100,
        CHILD_MinHeight, SPLASH_BANNER_HEIGHT,
        CHILD_MaxHeight, SPLASH_BANNER_HEIGHT,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)(uintptr_t)text_group,
        CHILD_WeightedHeight, 0,
        TAG_DONE);
    if (!root_group) goto fail;
    text_group = NULL;

    window_object = NewObject(
        window_class, NULL,
        WA_PubScreen, (ULONG)(uintptr_t)splash_screen,
        WA_Flags, WFLG_BORDERLESS | WFLG_SMART_REFRESH | WFLG_RMBTRAP,
        WA_Activate, FALSE,
        WA_IDCMP, 0UL,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, (ULONG)(uintptr_t)root_group,
        TAG_DONE);
    if (!window_object) goto fail;

    WindowBase = saved_window_base;
    LayoutBase = saved_layout_base;
    ButtonBase = saved_button_base;
    return window_object;

fail:
    if (root_group) DisposeObject(root_group);
    else {
        if (text_group) DisposeObject(text_group);
        else {
            if (line3) DisposeObject(line3);
            if (line2) DisposeObject(line2);
            if (line1) DisposeObject(line1);
        }
        if (splash_banner_slot) DisposeObject(splash_banner_slot);
    }
    splash_banner_slot = NULL;
    WindowBase = saved_window_base;
    LayoutBase = saved_layout_base;
    ButtonBase = saved_button_base;
    return NULL;
}

void amg_splash_open(void)
{
    if (splash_window_object || splash_window) return;

    splash_graphics_base = (struct GfxBase *)
        OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    if (!splash_graphics_base) return;
    if (!GfxBase) GfxBase = splash_graphics_base;

    splash_screen = LockPubScreen(NULL);
    if (!splash_screen) {
        if (GfxBase == splash_graphics_base) GfxBase = NULL;
        CloseLibrary((struct Library *)splash_graphics_base);
        splash_graphics_base = NULL;
        return;
    }

    if (!splash_open_class_libraries()) {
        UnlockPubScreen(NULL, splash_screen);
        splash_screen = NULL;
        if (GfxBase == splash_graphics_base) GfxBase = NULL;
        CloseLibrary((struct Library *)splash_graphics_base);
        splash_graphics_base = NULL;
        return;
    }

    splash_window_object = splash_create_window_object();
    if (!splash_window_object) {
        splash_close_class_libraries();
        UnlockPubScreen(NULL, splash_screen);
        splash_screen = NULL;
        if (GfxBase == splash_graphics_base) GfxBase = NULL;
        CloseLibrary((struct Library *)splash_graphics_base);
        splash_graphics_base = NULL;
        return;
    }

    splash_window = (struct Window *)(uintptr_t)
        DoMethod(splash_window_object, WM_OPEN);
    if (!splash_window) {
        DisposeObject(splash_window_object);
        splash_window_object = NULL;
        splash_banner_slot = NULL;
        splash_close_class_libraries();
        UnlockPubScreen(NULL, splash_screen);
        splash_screen = NULL;
        if (GfxBase == splash_graphics_base) GfxBase = NULL;
        CloseLibrary((struct Library *)splash_graphics_base);
        splash_graphics_base = NULL;
        return;
    }

    splash_prepare_pens();
    splash_draw_banner();
    WindowToFront(splash_window);
    WaitTOF();
}

void amg_splash_close(void)
{
    int supplied_graphics_base = 0;

    /* If GUI creation failed after temporarily replacing GfxBase, make our
     * still-open splash library available again while its pens are released. */
    if (!GfxBase && splash_graphics_base) {
        GfxBase = splash_graphics_base;
        supplied_graphics_base = 1;
    }

    if (splash_window_object) {
        if (splash_window)
            (void)DoMethod(splash_window_object, WM_CLOSE);
        splash_window = NULL;
        DisposeObject(splash_window_object);
        splash_window_object = NULL;
        splash_banner_slot = NULL;
    }

    splash_release_pens();
    if (splash_screen) {
        UnlockPubScreen(NULL, splash_screen);
        splash_screen = NULL;
    }

    /* The main GUI may have opened the same shared classes in the meantime.
     * These pointers represent only the splash's own OpenLibrary references;
     * closing them merely decrements the open counts and never changes
     * gui.c's global WindowBase/LayoutBase/ButtonBase variables. */
    splash_close_class_libraries();

    if (splash_graphics_base) {
        /* OpenLibrary() returns the same graphics.library base address for
         * repeated opens. After amg_gui_create() succeeds, gui.c owns a
         * second open reference and GfxBase normally equals this address. Do
         * not clear it: only close the splash's own reference. */
        if (supplied_graphics_base) GfxBase = NULL;
        CloseLibrary((struct Library *)splash_graphics_base);
        splash_graphics_base = NULL;
    }
}

#else

void amg_splash_open(void)
{
}

void amg_splash_close(void)
{
}

#endif /* AMIGMAIL_AMIGA */
