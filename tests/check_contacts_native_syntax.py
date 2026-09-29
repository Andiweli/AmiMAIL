#!/usr/bin/env python3
"""Compile the complete contacts GUI branch against host API declarations.

This extends the existing explicit syntax doubles with the public ListBrowser
and gadget input structures used by the contact subclass. It checks the whole
source file, both dialogs, and project prototypes. It is NOT a real NDK build,
a 32-bit ABI test, or an AmigaOS renderer/input implementation.

Run: python3 tests/check_contacts_native_syntax.py
     HOST_CC=clang python3 tests/check_contacts_native_syntax.py
"""
from pathlib import Path
import importlib.util, tempfile, subprocess, sys, os, shlex
root=Path(__file__).resolve().parent.parent
spec=importlib.util.spec_from_file_location('syntax',root/'tests/check_native_syntax.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
api=m.API
api=api.replace('STRPTR ln_Name;', 'char *ln_Name;')
api=api.replace('struct Gadget { WORD LeftEdge, TopEdge, Width, Height; };','struct Gadget { WORD LeftEdge, TopEdge, Width, Height; UWORD Flags; };')
api=api.replace('struct RastPort { struct TextFont *Font; UWORD TxHeight, TxBaseline; };','struct RastPort { struct TextFont *Font; UWORD TxHeight, TxBaseline; UBYTE FgPen, DrawMode; };')
api=api.replace('struct Screen *WScreen; };','struct Screen *WScreen; struct RastPort *RPort; };')
api=api.replace('#endif',r'''
struct IClass { struct Hook cl_Dispatcher; ULONG cl_UserData; };
struct _Msg { ULONG MethodID; }; typedef struct _Msg *Msg;
struct DrawInfo { UWORD *dri_Pens; };
struct Rectangle { WORD MinX, MinY, MaxX, MaxY; };
struct LBDrawMsg { ULONG lbdm_MethodID; struct RastPort *lbdm_RastPort;
 struct DrawInfo *lbdm_DrawInfo; struct Rectangle lbdm_Bounds; ULONG lbdm_State; };
struct GadgetInfo { struct DrawInfo *gi_DrInfo; };
struct InputEvent { struct InputEvent *ie_NextEvent; UBYTE ie_Class, ie_SubClass;
 UWORD ie_Code, ie_Qualifier; };
struct gpHitTest { ULONG MethodID; struct GadgetInfo *gpht_GInfo;
 struct { WORD X, Y; } gpht_Mouse; };
struct gpInput { ULONG MethodID; struct GadgetInfo *gpi_GInfo;
 struct InputEvent *gpi_IEvent; LONG *gpi_Termination;
 struct { WORD X, Y; } gpi_Mouse; APTR gpi_TabletData; };
struct gpGoInactive { ULONG MethodID; struct GadgetInfo *gpgi_GInfo; ULONG gpgi_Abort; };
struct gpRender { ULONG MethodID; struct GadgetInfo *gpr_GInfo;
 struct RastPort *gpr_RPort; LONG gpr_Redraw; };
#define GM_HITTEST 0
#define GM_RENDER 1
#define GM_GOACTIVE 2
#define GM_HANDLEINPUT 3
#define GM_GOINACTIVE 4
#define GMR_GADGETHIT 4
#define GMR_MEACTIVE 0
#define GMR_NOREUSE 2
#define GMR_REUSE 4
#define GMR_VERIFY 8
#define GREDRAW_REDRAW 1
#define GFLG_DISABLED 0x0100
#define IECLASS_RAWKEY 1
#define IECLASS_RAWMOUSE 2
#define SELECTDOWN 0x68
#define SELECTUP 0xe8
#define MENUDOWN 0x69
#define TAG_IGNORE 1UL
#define LBMSORT_FORWARD 0UL
#define LBMSORT_REVERSE 1UL
#define LB_DRAW 0x202UL
#define LBCB_OK 0UL
#define LBCB_UNKNOWN 1UL
#define JAM1 0
#define TEXTPEN 1
#define FILLTEXTPEN 4
#define LBR_SELECTED 1UL
enum { LBCIA_Sortable=2000, LBCIA_AutoSort, LBCIA_SortArrow,
 LBCIA_DraggableSeparator, LBNCA_RenderHook, LBNCA_HookHeight,
 LISTBROWSER_CursorNode, LISTBROWSER_CursorSelect, LISTBROWSER_RelEvent,
 LISTBROWSER_SelectedNode, LISTBROWSER_Selected, LISTBROWSER_TitleClickable, LISTBROWSER_Spacing,
 LBRE_NORMAL, LBRE_COLUMNADJUST, LBRE_TITLECLICK, LBRE_DOUBLECLICK,
 WA_Activate, WA_CloseGadget, WA_DepthGadget, WA_DragBar, WA_SizeGadget,
 WINDOW_Position, WPOS_CENTERSCREEN, WINDOW_RefWindow, WPOS_CENTERWINDOW,
 WINDOW_ParentGroup, GA_TabCycle, STRINGA_BufferPos, LAYOUT_EvenSize,
 ASLFR_DoSaveMode, ASLFR_InitialPattern, ASLFR_DoPatterns, ASLFR_AcceptPattern,
 WFLG_SIZEGADGET, WA_MinWidth, WA_MinHeight };
#define fr_Drawer rf_Dir
#define fr_File rf_File
#define WindowObject NewObject(WINDOW_GetClass(), NULL
#define VGroupObject NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_VERT
#define HGroupObject NewObject(LAYOUT_GetClass(), NULL, LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ
#define StringObject NewObject(STRING_GetClass(), NULL
#define EndObject TAG_DONE)
#define EndWindow TAG_DONE)
struct ColumnInfo *AllocLBColumnInfoA(ULONG columns, struct TagItem *tags);
ULONG SetLBColumnInfoAttrs(struct ColumnInfo *columns, ULONG tag, ...);
LONG Stricmp(CONST_STRPTR left, CONST_STRPTR right);
void Remove(struct Node *node); struct Node *RemHead(struct List *list);
void WindowToFront(struct Window *window); void ActivateWindow(struct Window *window);
BOOL ActivateLayoutGadget(struct Gadget *, struct Window *, struct Requester *, ULONG);
LONG ParsePatternNoCase(CONST_STRPTR source, STRPTR destination, LONG length);
BOOL AddPart(STRPTR drawer, CONST_STRPTR filename, ULONG size);
Class *MakeClass(CONST_STRPTR id, CONST_STRPTR superclassid, Class *superclass, ULONG instance_size, ULONG flags);
BOOL FreeClass(Class *cl);
ULONG DoSuperMethodA(Class *cl, Object *obj, Msg msg);
struct RastPort *ObtainGIRPort(struct GadgetInfo *info);
void ReleaseGIRPort(struct RastPort *rp);
ULONG HookEntry(struct Hook *hook, APTR object, APTR message);
void SetDrMd(struct RastPort *rp, ULONG mode); void SetAPen(struct RastPort *rp, ULONG pen);
void Move(struct RastPort *rp, LONG x, LONG y); void Text(struct RastPort *rp, CONST_STRPTR text, ULONG length);
#endif
''')
headers=set(m.HEADERS)|set('devices/inputevent.h intuition/gadgetclass.h proto/graphics.h proto/utility.h proto/string.h graphics/rastport.h'.split())
cc=shlex.split(os.environ.get('HOST_CC','gcc'))
with tempfile.TemporaryDirectory() as d:
    d=Path(d)
    (d/'api.h').write_text(api)
    for name in headers:
        p=d/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_text('/* Host C syntax double only. */\n')
    r=subprocess.run(cc+['-std=c99','-Wall','-Wextra','-Wshadow','-Wpointer-arith','-Wstrict-prototypes','-Wmissing-prototypes','-Wformat=2','-Werror','-fsyntax-only','-D__amigaos__','-include',str(d/'api.h'),'-I'+str(d),'-Iinclude','src/gui_contacts.c'],cwd=root)
    if r.returncode == 0:
        print('Complete gui_contacts.c native branch: host API syntax check passed.')
        print('No m68k linking, ABI or real AmigaOS UI behavior was tested.')
    sys.exit(r.returncode)
