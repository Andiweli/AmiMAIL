#!/usr/bin/env python3
"""Syntax-check selected native branches using explicit host API test doubles.

NOT an Amiga SDK, m68k/ABI check, link test, or GUI runtime test. It verifies
project declarations and C syntax that host-check normally excludes. Unknown
OS names fail compilation; these stubs are deliberately not generated from
the C identifiers being checked. A real NDK build remains mandatory.
"""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HEADERS = """clib/alib_protos.h classes/window.h gadgets/layout.h
 gadgets/button.h gadgets/fuelgauge.h proto/fuelgauge.h gadgets/string.h gadgets/listbrowser.h proto/exec.h
 proto/button.h proto/string.h
 proto/intuition.h proto/layout.h proto/window.h proto/listbrowser.h
 proto/dos.h proto/asl.h reaction/reaction.h reaction/reaction_macros.h
 utility/tagitem.h utility/hooks.h libraries/asl.h libraries/gadtools.h
 devices/timer.h exec/lists.h exec/ports.h exec/tasks.h exec/memory.h
 intuition/classes.h intuition/intuition.h dos/dosextens.h dos/dostags.h
 dos/dos.h dos/var.h""".split()
API = r'''
#ifndef AMIMAIL_TEST_NATIVE_API_H
#define AMIMAIL_TEST_NATIVE_API_H
#include <stddef.h>
#include <stdint.h>
typedef unsigned long ULONG; typedef long LONG; typedef unsigned short UWORD;
typedef short WORD; typedef unsigned char UBYTE; typedef signed char BYTE;
typedef long BOOL; typedef void *APTR; typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR; typedef unsigned long BPTR;
typedef struct Object Object; typedef struct IClass Class;
struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; BYTE ln_Pri; STRPTR ln_Name; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; UBYTE lh_Type, l_pad; };
struct Task { int unused; };
struct MsgPort { UBYTE mp_SigBit; struct Task *mp_SigTask; struct List mp_MsgList; };
struct Message { struct Node mn_Node; struct MsgPort *mn_ReplyPort; UWORD mn_Length; };
struct Process { struct Task pr_Task; };
struct Hook { void *h_Entry, *h_SubEntry, *h_Data; };
struct Image { int unused; };
struct Gadget { WORD LeftEdge, TopEdge, Width, Height; };
struct TextFont { int unused; };
struct RastPort { struct TextFont *Font; UWORD TxHeight, TxBaseline; };
struct Screen { WORD Width, Height; struct RastPort RastPort; };
struct Window { WORD LeftEdge, TopEdge, Width, Height;
 BYTE BorderLeft, BorderRight, BorderTop, BorderBottom; struct Screen *WScreen; };
struct Requester { int unused; };
struct ColumnInfo { int unused; }; struct NewMenu; struct timerequest;
struct LayoutLimits { UWORD MinWidth, MinHeight, MaxWidth, MaxHeight; };
struct TagItem { ULONG ti_Tag, ti_Data; };
struct FileRequester { STRPTR rf_Dir, rf_File; };
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0UL
#define ACCESS_READ (-2L)
#define SIGBREAKF_CTRL_C (1UL << 12)
#define WMHI_CLASSMASK 0xffff0000UL
#define WMHI_GADGETMASK 0xffffUL
#define WMHI_LASTMSG 0UL
#define WMHI_CLOSEWINDOW 0x10000UL
#define WMHI_GADGETUP 0x20000UL
#define WMHI_RAWKEY 0x30000UL
#define WMHI_NEWSIZE 0x40000UL
#define WMHI_IGNORE (~0UL)
/* Match the classic NDK names/values and STRPTR signedness.  Do not add
 * compatibility aliases absent from those headers: that would hide failures
 * in real Amiga builds.
 * Reference: gadgets/layout.h and exec/types.h in the NDK.
 */
#define LAYOUT_HORIZONTAL 0
#define LAYOUT_VERTICAL 1
#define LAYOUT_ORIENT_HORIZ LAYOUT_HORIZONTAL
#define LAYOUT_ORIENT_VERT LAYOUT_VERTICAL
/* Classic gadgets/fuelgauge.h, 44.1: horizontal=0, vertical=1. */
#define FGORIENT_HORIZ 0
#define FGORIENT_VERT 1
/* Remaining tag values only need distinct identities for syntax checking. */
enum {
 FUELGAUGE_Min=900, FUELGAUGE_Max, FUELGAUGE_Level, FUELGAUGE_Percent,
 FUELGAUGE_Orientation,
 FUELGAUGE_Ticks, FUELGAUGE_ShortTicks, BUTTON_DomainString,
 BUTTON_Justification, BCJ_CENTER, BUTTON_TextPadding,
 CHILD_MinWidth=1000, CHILD_MinHeight, CHILD_MaxHeight, LAYOUT_ModifyChild, CHILD_WeightedHeight, CHILD_WeightedWidth, CHILD_NoDispose,
 GA_Disabled, GA_ID, GA_ReadOnly, GA_RelVerify, GA_Text,
 IDCMP_CLOSEWINDOW, IDCMP_GADGETUP, IDCMP_RAWKEY,
 LAYOUT_AddChild, LAYOUT_Orientation, LAYOUT_SpaceInner, LAYOUT_SpaceOuter,
 STRINGA_MaxChars, STRINGA_TextVal, WA_Flags, WA_Height, WA_IDCMP,
 WA_Left, WA_PubScreen, WA_Title, WA_Top, WA_Width,
 WFLG_ACTIVATE, WFLG_CLOSEGADGET, WFLG_DEPTHGADGET, WFLG_DRAGBAR,
 WINDOW_Layout, WINDOW_SigMask, ASLFR_DrawersOnly, ASLFR_RejectIcons,
 ASLFR_SleepWindow, ASLFR_TitleText, ASLFR_Window, ASL_FileRequest,
 LBCIA_Column, LBCIA_Title, LBCIA_Weight, LBNA_Column, LBNA_Selected,
 LBNA_UserData, LBNCA_CopyText, LBNCA_Text, LISTBROWSER_AutoWheel,
 LISTBROWSER_ColumnInfo, LISTBROWSER_ColumnTitles, LISTBROWSER_Labels,
 LISTBROWSER_MinVisible, LISTBROWSER_MultiSelect, LISTBROWSER_ShowSelected,
 LISTBROWSER_Top, LISTBROWSER_VerticalProp, NP_Entry, NP_Name, NP_StackSize,
 ERROR_OBJECT_NOT_FOUND, GVF_GLOBAL_ONLY, ERROR_OBJECT_EXISTS
};
void Forbid(void); void Permit(void); ULONG Wait(ULONG mask);
void Signal(struct Task *task, ULONG mask); ULONG SetSignal(ULONG signals, ULONG mask);
struct Task *FindTask(CONST_STRPTR name);
struct MsgPort *CreateMsgPort(void); void DeleteMsgPort(struct MsgPort *port);
struct Message *GetMsg(struct MsgPort *port);
void PutMsg(struct MsgPort *port, struct Message *message);
void ReplyMsg(struct Message *message);
struct Process *CreateNewProcTags(ULONG tag, ...);
void NewList(struct List *list); void AddTail(struct List *list, struct Node *node);
Class *WINDOW_GetClass(void); Class *LAYOUT_GetClass(void);
Class *FUELGAUGE_GetClass(void);
Class *BUTTON_GetClass(void); Class *STRING_GetClass(void);
Class *LISTBROWSER_GetClass(void);
Object *NewObject(Class *cl, CONST_STRPTR class_name, ULONG tag, ...);
Object *NewObjectA(Class *cl, CONST_STRPTR class_name, struct TagItem *tags);
void DisposeObject(Object *object);
ULONG GetAttr(ULONG attribute, Object *object, ULONG *value);
ULONG SetAttrs(Object *object, ULONG tag, ...);
ULONG SetGadgetAttrs(struct Gadget *gadget, struct Window *window, void *requester, ULONG tag, ...);
void RefreshGList(struct Gadget *gadget, struct Window *window, void *requester, LONG count);
void InitRequester(struct Requester *requester);
BOOL Request(struct Requester *requester, struct Window *window);
void EndRequest(struct Requester *requester, struct Window *window);
void LayoutLimits(struct Gadget *gadget, struct LayoutLimits *limits, struct TextFont *font, struct Screen *screen);
struct Window *RA_OpenWindow(Object *object);
ULONG RA_HandleInput(Object *object, UWORD *code);
struct ColumnInfo *AllocLBColumnInfo(ULONG columns, ...);
void FreeLBColumnInfo(struct ColumnInfo *columns);
struct Node *AllocListBrowserNode(ULONG columns, ...);
void FreeListBrowserList(struct List *list);
ULONG GetListBrowserNodeAttrs(struct Node *node, ...);
ULONG SetListBrowserNodeAttrs(struct Node *node, ...);
void *AllocAslRequestTags(ULONG kind, ...); BOOL AslRequest(void *requester, struct TagItem *tags);
void FreeAslRequest(void *requester);
BPTR CreateDir(CONST_STRPTR name); BPTR Lock(CONST_STRPTR name, LONG access);
void UnLock(BPTR lock); BOOL NameFromLock(BPTR lock, STRPTR buffer, LONG size);
BOOL DeleteFile(CONST_STRPTR path); BOOL Rename(CONST_STRPTR from, CONST_STRPTR to);
LONG IoErr(void); LONG GetVar(CONST_STRPTR name, STRPTR buffer, LONG size, ULONG flags);
#endif
'''
FILES = ('src/gui_transfer.c', 'src/gui_attachments.c', 'src/network_task.c',
         'src/transfer.c', 'src/attachment_export.c')

def main():
    cc = shlex.split(os.environ.get('HOST_CC', 'gcc'))
    flags = ['-std=c99', '-Wall', '-Wextra', '-Wshadow', '-Wpointer-arith',
             '-Wstrict-prototypes', '-Wmissing-prototypes', '-Wformat=2',
             '-Werror', '-fsyntax-only', '-D__amigaos__']
    with tempfile.TemporaryDirectory(prefix='amimail-syntax-') as temporary:
        directory = Path(temporary)
        api = directory / 'native_api.h'
        api.write_text(API, encoding='ascii')
        for name in HEADERS:
            header = directory / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('/* Host syntax double; not an SDK header. */\n', encoding='ascii')
        for file in FILES:
            subprocess.run(cc + flags + ['-include', str(api), '-I' + str(directory),
                           '-Iinclude', file], cwd=ROOT, check=True)
            print('native-branch syntax (API doubles):', file)
    print('No m68k ABI, linking or real ReAction behavior was tested.')

if __name__ == '__main__':
    main()
