#!/usr/bin/env python3
"""Contact header input/render contracts using host API doubles, not an emulator.

The native title painter is intentionally opaque to the application. Tests
allow production code to add only the seven pixels of one existing arrow.
Mouse input is driven through the actual subclass, including disabled native
sort flags; do not inject LBRE_TITLECLICK to bypass the hit/activation path.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

def production():
    s=(ROOT/'src/gui_contacts.c').read_text(encoding='utf-8')
    icons=(ROOT/'src/gui_icons.c').read_text(encoding='utf-8')
    return '\n'.join((
        icons[icons.index('static const UBYTE sort_up_rows[4]'):icons.index('static Class *reply_arrow_class;')],
        icons[icons.index('static void draw_mask_rows('):icons.index('static void draw_reply_arrow(')],
        icons[icons.index('void gui_draw_sort_icon('):icons.rindex('#endif')],
        s[s.index('enum ContactColumn {'):s.index('static int selected_contact_id(')]))

def wiring():
    s=(ROOT/'src/gui_contacts.c').read_text(encoding='utf-8')
    for invalid in ('repaint_contact_sort_headings', 'RectFill(', 'SetFont(',
                    'WINDOW_PostRefreshHook', 'LISTBROWSER_SortColumn,',
                    'LBCIA_Sortable, TRUE', 'LBCIA_AutoSort, TRUE',
                    'LBCIA_SortArrow, TRUE'):
        assert invalid not in s, invalid
    for flag in ('Sortable','AutoSort','SortArrow'):
        assert len(re.findall(r'\{ LBCIA_'+flag+r', FALSE \}',s))==3
    assert s.count('view.list_class, NULL,')==2
    assert s.count('LISTBROWSER_TitleClickable, TRUE,')==2
    assert s.count('LISTBROWSER_MultiSelect, TRUE,')==2
    assert s.count('init_contact_list_view(&view);')==2
    for name,end in [('gui_contacts_dialog','static int ascii_segment_equal_ci'),
                     ('gui_contacts_select_emails','#endif')]:
        part=s[s.index(name+'('):]
        part=part[:part.index(end)]
        assert 'RA_HandleInput(dialog, &input_code)' in part
        assert 'handle_contact_sort_event(&view, input_code)' in part
        assert part.count('close_contact_list_class(&view)')==3
    for call in re.finditer(r'\b(rebuild_contact_list|import_contacts)\((.*?)\);',s,re.S):
        if '{' not in call.group(2):
            assert call.group(2).rstrip().endswith(('view','&view')),call.group(0)
    print('Both native contact dialogs wired; no title repaint/native sort requests.')

API = r'''

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contacts.h"
#include "catalog_ids.h"
typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short UWORD;
typedef short WORD;
typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
typedef void *APTR;
typedef void Object;
typedef struct IClass Class;
struct MessageBase { ULONG MethodID; };
typedef struct MessageBase *Msg;
struct Hook {
    ULONG (*h_Entry)(struct Hook *, APTR, APTR);
    ULONG (*h_SubEntry)(struct Hook *, APTR, APTR);
    APTR h_Data;
};
struct Node {
    struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; BYTE ln_Pri; char *ln_Name;
    ULONG id, selected;
    char *text[3]; int owned[3]; struct Hook *hooks[3];
};
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; };
struct ColumnInfo {
    ULONG weight, flags; int sortable, auto_sort, sort_arrow, draggable;
    const char *title;
};
struct TextFont { int identity; };
struct RastPort { UBYTE FgPen, DrawMode; UWORD TxHeight, TxBaseline; struct TextFont *Font; LONG x,y; };
struct Window { struct RastPort *RPort; };
struct Gadget {
    WORD LeftEdge, TopEdge, Width, Height; UWORD Flags; Class *cl;
    struct List *labels; struct ColumnInfo *columns;
    ULONG top, rel_column, sort_column, cursor_index, rel_event;
    struct Node *cursor;
    LONG left[3], right[3];
    unsigned detaches, refreshes, title_events;
    int title_clickable;
};
struct Rectangle { WORD MinX, MinY, MaxX, MaxY; };
struct DrawInfo { UWORD dri_Pens[8]; };
struct LBDrawMsg {
    ULONG lbdm_MethodID; struct RastPort *lbdm_RastPort;
    struct DrawInfo *lbdm_DrawInfo; struct Rectangle lbdm_Bounds; ULONG lbdm_State;
};
#define TRUE 1UL
#define FALSE 0UL
#define TAG_DONE 0UL
#define TAG_IGNORE 1UL
#define LBMSORT_FORWARD 0UL
#define LBMSORT_REVERSE 1UL
#define LB_DRAW 1UL
#define LBCB_OK 0UL
#define LBCB_UNKNOWN 1UL
#define JAM1 0UL
#define TEXTPEN 1UL
#define FILLTEXTPEN 2UL
#define LBR_SELECTED 1UL
#define T(id,en) (en)
#define CIF_DRAGGABLE 2UL
#define CIF_SORTABLE 8UL
enum {
    LBCIA_Column=100, LBCIA_Title, LBCIA_Weight, LBCIA_AutoSort,
    LBCIA_SortArrow, LBCIA_DraggableSeparator, LBCIA_SortDirection,
    LBCIA_Sortable, LBCIA_Flags,
    LBNA_Column, LBNA_UserData, LBNA_Selected, LBNCA_Text,
    LBNCA_CopyText, LBNCA_RenderHook, LBNCA_HookHeight,
    LISTBROWSER_Top, LISTBROWSER_CursorNode, LISTBROWSER_Labels,
    LISTBROWSER_ColumnInfo, LISTBROWSER_SortColumn, LISTBROWSER_CursorSelect,
    LISTBROWSER_RelColumn, LISTBROWSER_Selected,
    LISTBROWSER_RelEvent, LBRE_TITLECLICK, LBRE_COLUMNADJUST, LBRE_NORMAL, LBRE_DOUBLECLICK
};
static unsigned long checks, comparisons;
static UBYTE pixels[128][1024];
#define CHECK(c) do { ++checks; if (!(c)) { \
    fprintf(stderr,"check failed at line %d: %s\n", __LINE__, #c); exit(1); \
} } while (0)
static ULONG HookEntry(struct Hook *h, APTR o, APTR m)
{ return h && h->h_SubEntry ? h->h_SubEntry(h,o,m) : 0UL; }
static void SetAPen(struct RastPort *rp, ULONG pen) { rp->FgPen=(UBYTE)pen; }
static void SetDrMd(struct RastPort *rp, ULONG mode) { rp->DrawMode=(UBYTE)mode; }
static void Move(struct RastPort *rp, LONG x, LONG y) { rp->x=x; rp->y=y; }
static void Text(struct RastPort *rp, CONST_STRPTR s, ULONG n)
{ CHECK(rp->y>=40L); CHECK(n == strlen((const char *)s)); }
static LONG WritePixel(struct RastPort *rp, LONG x, LONG y)
{
    CHECK(x>=0 && x<1024 && y>=0 && y<128);
    pixels[y][x]=rp->FgPen; return 0L;
}
static void NewList(struct List *l)
{
    l->lh_Head=(struct Node *)&l->lh_Tail;
    l->lh_Tail=NULL;
    l->lh_TailPred=(struct Node *)&l->lh_Head;
}
static void AddTail(struct List *l, struct Node *n)
{
    n->ln_Succ=(struct Node *)&l->lh_Tail; n->ln_Pred=l->lh_TailPred;
    l->lh_TailPred->ln_Succ=n; l->lh_TailPred=n;
}
static void Remove(struct Node *n)
{ n->ln_Pred->ln_Succ=n->ln_Succ; n->ln_Succ->ln_Pred=n->ln_Pred; }
static struct Node *RemHead(struct List *l)
{ struct Node *n=l->lh_Head; if(!n->ln_Succ)return NULL; Remove(n); return n; }
static unsigned fold(unsigned x)
{ if((x>='A' && x<='Z') || (x>=0xc0U && x<=0xdeU && x!=0xd7U)) x+=32U; return x; }
static LONG Stricmp(CONST_STRPTR a, CONST_STRPTR b)
{
    ++comparisons;
    while(*a && fold(*a)==fold(*b)){ ++a; ++b; }
    return (LONG)fold(*a)-(LONG)fold(*b);
}
static void utf8_to_local_copy(const char *s, char *out, size_t n)
{ snprintf(out,n,"%s",s ? s : ""); }
static char *duplicate(const char *s)
{ size_t n=strlen(s)+1U; char *p=malloc(n); CHECK(p); memcpy(p,s,n); return p; }
static struct ColumnInfo *test_alloc_columns(ULONG count,const ULONG *t)
{
    ULONG col=0; struct ColumnInfo *c=calloc(count,sizeof(*c)); CHECK(c && count==3UL);
    for(;*t!=TAG_DONE;t+=2){
        if(t[0]==LBCIA_Column){col=t[1];CHECK(col<count);continue;}
        switch(t[0]){
        case LBCIA_Title:c[col].title=(const char *)(uintptr_t)t[1];break;
        case LBCIA_Weight:c[col].weight=t[1];break;
        case LBCIA_Flags:c[col].flags=t[1];
            c[col].sortable=(t[1]&CIF_SORTABLE)!=0UL;
            c[col].draggable=(t[1]&CIF_DRAGGABLE)!=0UL;break;
        case LBCIA_Sortable:c[col].sortable=(int)t[1];
            if(t[1])c[col].flags|=CIF_SORTABLE;
            else c[col].flags&=~CIF_SORTABLE;
            break;
        case LBCIA_AutoSort:c[col].auto_sort=(int)t[1];break;
        case LBCIA_SortArrow:c[col].sort_arrow=(int)t[1];break;
        case LBCIA_DraggableSeparator:c[col].draggable=(int)t[1];break;
        default:CHECK(0);break;
        }
    }
    return c;
}
#define AllocLBColumnInfo(n,...) test_alloc_columns(n,(const ULONG[]){__VA_ARGS__})
static struct Node *test_alloc_node(ULONG count,const ULONG *t)
{
    ULONG col=0; int copy[3]={0,0,0}; struct Node *n=calloc(1,sizeof(*n)); CHECK(n && count==3UL);
    for(;*t!=TAG_DONE;t+=2){
        if(t[0]==LBNA_Column){col=t[1];CHECK(col<count);continue;}
        switch(t[0]){
        case LBNA_UserData:n->id=t[1];break;
        case LBNCA_CopyText:copy[col]=(int)t[1];break;
        case LBNCA_Text:n->text[col]=(char *)(uintptr_t)t[1];
            if(copy[col]){n->text[col]=duplicate(n->text[col]);n->owned[col]=1;} break;
        case LBNCA_RenderHook:n->hooks[col]=(struct Hook *)(uintptr_t)t[1];break;
        case LBNCA_HookHeight:break;
        default:CHECK(0);break;
        }
    }
    return n;
}
#define AllocListBrowserNode(n,...) test_alloc_node(n,(const ULONG[]){__VA_ARGS__})
static void FreeListBrowserList(struct List *l)
{
    struct Node *n;unsigned i;
    while((n=RemHead(l))){for(i=0;i<3;i++)if(n->owned[i])free(n->text[i]);free(n);}
}
static void test_get_node(struct Node *n,const ULONG *t)
{
    ULONG col=0;
    for(;*t!=TAG_DONE;t+=2){
        ULONG value;
        if(t[0]==LBNA_Column){col=t[1];CHECK(col<3);continue;}
        if(t[0]==LBNCA_Text)value=(ULONG)(uintptr_t)n->text[col];
        else if(t[0]==LBNA_UserData)value=n->id;
        else { CHECK(t[0]==LBNA_Selected);value=n->selected; }
        memcpy((void *)(uintptr_t)t[1],&value,sizeof(value));
    }
}
#define GetListBrowserNodeAttrs(n,...) test_get_node(n,(const ULONG[]){__VA_ARGS__})
static ULONG GetAttr(ULONG attr,Object *obj,ULONG *value)
{
    struct Gadget *g=(struct Gadget *)obj;
    switch(attr){
    case LISTBROWSER_Top:*value=g->top;break;
    case LISTBROWSER_CursorNode:*value=(ULONG)(uintptr_t)g->cursor;break;
    case LISTBROWSER_RelColumn:*value=g->rel_column;break;
    case LISTBROWSER_RelEvent:*value=g->rel_event;break;
    default:CHECK(0);break;
    }
    return 1;
}
static void test_set_gadget(struct Gadget *g,struct Window *w,APTR requester,const ULONG *t)
{
    (void)w;(void)requester;
    for(;*t!=TAG_DONE;t+=2){
        switch(t[0]){
        case TAG_IGNORE:break;
        case LISTBROWSER_Labels:
            if(t[1]==~0UL || t[1]==0UL){g->labels=NULL;++g->detaches;}
            else g->labels=(struct List *)(uintptr_t)t[1];
            break;
        case LISTBROWSER_ColumnInfo:g->columns=(struct ColumnInfo *)(uintptr_t)t[1];break;
        case LISTBROWSER_SortColumn:CHECK(t[1]==~0UL);g->sort_column=t[1];break;
        case LISTBROWSER_CursorSelect:g->cursor_index=t[1];break;
        case LISTBROWSER_Top:g->top=t[1];break;
        case LISTBROWSER_Selected:CHECK(t[1]==~0UL);break;
        default:CHECK(0);break;
        }
    }
}
#define SetGadgetAttrs(g,w,r,...) test_set_gadget(g,w,r,(const ULONG[]){__VA_ARGS__})
static void RefreshGList(struct Gadget *,struct Window *,APTR,LONG);
void gui_draw_sort_icon(struct RastPort *, LONG, LONG, int, LONG);

struct TagItem { ULONG ti_Tag, ti_Data; };
struct IClass { struct Hook cl_Dispatcher; ULONG cl_UserData; };
struct GadgetInfo { struct RastPort *gi_RastPort; struct DrawInfo *gi_DrInfo; };
struct InputEvent { UBYTE ie_Class; UWORD ie_Code; };
struct gpHitTest { ULONG MethodID; struct GadgetInfo *gpht_GInfo; struct { WORD X,Y; } gpht_Mouse; };
struct gpInput { ULONG MethodID; struct GadgetInfo *gpi_GInfo; struct InputEvent *gpi_IEvent; LONG *gpi_Termination; struct { WORD X,Y; } gpi_Mouse; };
struct gpGoInactive { ULONG MethodID; struct GadgetInfo *gpgi_GInfo; ULONG gpgi_Abort; };
struct gpRender { ULONG MethodID; struct GadgetInfo *gpr_GInfo; struct RastPort *gpr_RPort; LONG gpr_Redraw; };
#define GM_HITTEST 0UL
#define GM_RENDER 1UL
#define GM_GOACTIVE 2UL
#define GM_HANDLEINPUT 3UL
#define GM_GOINACTIVE 4UL
#define GM_LAYOUT 6UL
#define GMR_MEACTIVE 0UL
#define GMR_NOREUSE 2UL
#define GMR_REUSE 4UL
#define GMR_VERIFY 8UL
#define GMR_GADGETHIT 4UL
#define GREDRAW_REDRAW 1L
#define GFLG_DISABLED 0x100U
#define IECLASS_RAWKEY 1U
#define IECLASS_RAWMOUSE 2U
#define IECLASS_TIMER 6U
#define SELECTDOWN 0x68U
#define SELECTUP 0xe8U
#define MENUDOWN 0x69U
static Class native_class;
static int fail_class;
static unsigned allocated_classes, freed_classes, super_hits, super_inputs, super_inactive;
static unsigned obtained_rp, released_rp, native_header_passes;
static UWORD draw_font_height=8U;
static struct TextFont native_font={123}, wrong_font={999};
static struct DrawInfo pens={{0,5,6,7,8,9,10,11}};
static Class *LISTBROWSER_GetClass(void) { return &native_class; }
static Class *MakeClass(CONST_STRPTR name,CONST_STRPTR super,Class *base,ULONG size,ULONG flags)
{
    (void)name;(void)super;CHECK(base==&native_class && size==0 && flags==0);
    if(fail_class)return NULL;
    ++allocated_classes;return calloc(1,sizeof(Class));
}
static LONG FreeClass(Class *cl) { CHECK(cl && cl!=&native_class);++freed_classes;free(cl);return 1; }
static ULONG DoSuperMethodA(Class *,Object *,Msg);
static struct RastPort *ObtainGIRPort(struct GadgetInfo *gi)
{ ++obtained_rp;return gi?gi->gi_RastPort:NULL; }
static void ReleaseGIRPort(struct RastPort *rp) { CHECK(rp);++released_rp; }
static struct ColumnInfo *AllocLBColumnInfoA(UWORD count,struct TagItem *tags)
{
    ULONG pairs[128];unsigned i=0;
    while(tags->ti_Tag){CHECK(i+3<128);pairs[i++]=tags->ti_Tag;pairs[i++]=tags->ti_Data;++tags;}
    pairs[i]=TAG_DONE;return test_alloc_columns(count,pairs);
}

'''

TESTS = r'''
static void native_paint(struct Gadget *g,struct gpRender *render)
{
    struct Node *n;unsigned col,x,y;
    struct RastPort *rp=render->gpr_RPort;
    struct TextFont *old=rp->Font;
    UWORD old_h=rp->TxHeight,old_b=rp->TxBaseline;
    ++native_header_passes;
    memset(pixels,0,sizeof(pixels));
    rp->Font=&native_font;rp->TxHeight=draw_font_height;rp->TxBaseline=draw_font_height-2U;
    /* Opaque native header pattern: custom code must not touch any of it
     * apart from the seven foreground pixels of the current arrow. */
    for(y=(unsigned)g->TopEdge;y<(unsigned)g->TopEdge+draw_font_height+4U;++y)
        for(x=(unsigned)g->LeftEdge;x<(unsigned)(g->LeftEdge+g->Width);++x)
            pixels[y][x]=(UBYTE)(32U+(x%11U));
    CHECK(g->columns);
    for(col=0;col<3;col++){
        CHECK(!g->columns[col].sortable && !g->columns[col].auto_sort);
        CHECK(!g->columns[col].sort_arrow);
        CHECK((g->columns[col].flags&CIF_SORTABLE)==0UL);
    }
    if(g->labels && (n=g->labels->lh_Head)->ln_Succ){
        for(col=0;col<3;col++)if(n->hooks[col]){
            struct LBDrawMsg draw;
            memset(&draw,0,sizeof(draw));draw.lbdm_MethodID=LB_DRAW;
            draw.lbdm_RastPort=rp;draw.lbdm_DrawInfo=&pens;
            draw.lbdm_Bounds.MinX=(WORD)g->left[col];draw.lbdm_Bounds.MaxX=(WORD)g->right[col];
            draw.lbdm_Bounds.MinY=(WORD)(g->TopEdge+draw_font_height+5U);
            draw.lbdm_Bounds.MaxY=(WORD)(draw.lbdm_Bounds.MinY+draw_font_height+1U);
            CHECK(((ULONG (*)(struct Hook*,struct Node*,APTR))n->hooks[col]->h_SubEntry)
                  (n->hooks[col],n,&draw)==LBCB_OK);
        }
    }
    rp->Font=old;rp->TxHeight=old_h;rp->TxBaseline=old_b;
}
static ULONG DoSuperMethodA(Class *cl,Object *object,Msg message)
{
    struct Gadget *g=(struct Gadget *)object;
    (void)cl;
    if(message->MethodID==GM_RENDER){native_paint(g,(struct gpRender *)message);return 17UL;}
    if(message->MethodID==GM_HITTEST){++super_hits;return GMR_GADGETHIT;}
    if(message->MethodID==GM_GOACTIVE || message->MethodID==GM_HANDLEINPUT){
        struct gpInput *input=(struct gpInput *)message;
        ++super_inputs;
        if(input->gpi_IEvent && input->gpi_IEvent->ie_Class==IECLASS_RAWMOUSE &&
           input->gpi_IEvent->ie_Code==SELECTUP){
            *input->gpi_Termination=CONTACT_SORT_LAST_CODE; /* collide on purpose */
            return GMR_NOREUSE|GMR_VERIFY;
        }
        return GMR_MEACTIVE;
    }
    if(message->MethodID==GM_GOINACTIVE){++super_inactive;return 0;}
    return 0;
}
static void RefreshGList(struct Gadget *g,struct Window *w,APTR req,LONG count)
{
    struct gpRender render;struct GadgetInfo gi;
    (void)req;CHECK(count==1);++g->refreshes;
    memset(&render,0,sizeof(render));gi.gi_RastPort=w->RPort;gi.gi_DrInfo=&pens;
    render.MethodID=GM_RENDER;render.gpr_GInfo=&gi;render.gpr_RPort=w->RPort;
    render.gpr_Redraw=GREDRAW_REDRAW;
    CHECK(contact_list_dispatcher(&g->cl->cl_Dispatcher,g,&render)==17UL);
}
static void setup(ContactListView *v,struct Hook hooks[3],struct List *l,
                  struct Gadget *g,struct Window *w,struct RastPort *rp)
{
    memset(g,0,sizeof(*g));memset(rp,0,sizeof(*rp));w->RPort=rp;
    rp->Font=&wrong_font;rp->TxHeight=20;rp->TxBaseline=16;rp->FgPen=7;rp->DrawMode=3;
    g->LeftEdge=13;g->TopEdge=30;g->Width=550;g->Height=85;
    g->left[0]=15;g->right[0]=131;g->left[1]=135;g->right[1]=343;
    g->left[2]=347;g->right[2]=540;
    init_contact_list_view(v);v->columns=contacts_columns();CHECK(v->columns);
    CHECK(open_contact_list_class(v));g->cl=v->list_class;
    init_contact_render_hooks(hooks,v);NewList(l);v->list=l;g->labels=l;
    g->columns=v->columns;v->window=w;v->gadget=g;g->rel_event=LBRE_NORMAL;
    CHECK(v->sort_column==CONTACT_COLUMN_FIRST && v->sort_direction==LBMSORT_FORWARD);
}
static void cleanup(ContactListView *v)
{ FreeListBrowserList(v->list);free(v->columns);close_contact_list_class(v);CHECK(v->list_class==NULL); }
static void add_contact(struct List *l,struct Hook h[3],ULONG id,const char *first,const char *last)
{
    AmgContact c;struct Node *n;memset(&c,0,sizeof(c));c.id=id;
    snprintf(c.first_name,sizeof(c.first_name),"%s",first);
    snprintf(c.last_name,sizeof(c.last_name),"%s",last);
    snprintf(c.email,sizeof(c.email),"%lu@example.com",9999UL-id);
    n=contact_node(&c,h,10);CHECK(n);n->selected=id%2UL;AddTail(l,n);
}
static void paint_and_check(ContactListView *v)
{
    struct Gadget *g=v->gadget;struct RastPort *rp=v->window->RPort;
    unsigned x,y,count=0;
    LONG left=g->right[v->sort_column]-8L;
    LONG top=g->TopEdge+1L+((LONG)draw_font_height-4L)/2L;
    const UBYTE *mask=v->sort_direction==LBMSORT_FORWARD?sort_down_rows:sort_up_rows;
    RefreshGList(g,v->window,NULL,1);
    for(y=(unsigned)g->TopEdge;y<(unsigned)g->TopEdge+draw_font_height+4U;++y){
        for(x=(unsigned)g->LeftEdge;x<(unsigned)(g->LeftEdge+g->Width);++x){
            UBYTE expected=(UBYTE)(32U+(x%11U));
            if((LONG)x>=left && (LONG)x<left+5 && (LONG)y>=top && (LONG)y<top+4 &&
               (mask[(LONG)y-top]&(1U<<(4L-((LONG)x-left))))){expected=5U;++count;}
            CHECK(pixels[y][x]==expected);
        }
    }
    CHECK(count==7U);
    CHECK(rp->Font==&wrong_font && rp->TxHeight==20U && rp->TxBaseline==16U);
    CHECK(rp->FgPen==7U && rp->DrawMode==3U);
}
static ULONG dispatch_input(ContactListView *v,ULONG method,UBYTE event_class,
                             UWORD event_code,LONG x,LONG y,LONG *termination)
{
    struct GadgetInfo gi;struct gpInput input;struct InputEvent event;
    gi.gi_RastPort=v->window->RPort;gi.gi_DrInfo=&pens;
    memset(&input,0,sizeof(input));event.ie_Class=event_class;event.ie_Code=event_code;
    input.MethodID=method;input.gpi_GInfo=&gi;input.gpi_IEvent=&event;
    input.gpi_Termination=termination;input.gpi_Mouse.X=(WORD)x;input.gpi_Mouse.Y=(WORD)y;
    return contact_list_dispatcher(&v->list_class->cl_Dispatcher,v->gadget,&input);
}
static void inactive(ContactListView *v,ULONG abort)
{
    struct gpGoInactive msg;memset(&msg,0,sizeof(msg));msg.MethodID=GM_GOINACTIVE;
    msg.gpgi_Abort=abort;
    (void)contact_list_dispatcher(&v->list_class->cl_Dispatcher,v->gadget,&msg);
    CHECK(!v->header_active);
}
static UWORD release_click(ContactListView *v,ULONG column)
{
    struct Gadget *g=v->gadget;
    struct gpHitTest hit;LONG termination=-1;
    LONG x=g->left[column]-g->LeftEdge+10L;
    unsigned before=super_inputs;ULONG old_col=v->sort_column,old_dir=v->sort_direction;
    memset(&hit,0,sizeof(hit));hit.MethodID=GM_HITTEST;hit.gpht_Mouse.X=(WORD)x;hit.gpht_Mouse.Y=5;
    CHECK(contact_list_dispatcher(&v->list_class->cl_Dispatcher,g,&hit)==GMR_GADGETHIT);
    CHECK(dispatch_input(v,GM_GOACTIVE,IECLASS_RAWMOUSE,SELECTDOWN,x,5,&termination)==GMR_MEACTIVE);
    CHECK(v->header_active && super_inputs==before);
    CHECK(v->sort_column==old_col && v->sort_direction==old_dir);
    CHECK(dispatch_input(v,GM_HANDLEINPUT,IECLASS_TIMER,0,x,5,&termination)==GMR_MEACTIVE);
    CHECK(dispatch_input(v,GM_HANDLEINPUT,IECLASS_RAWMOUSE,SELECTUP,x,5,&termination)==(GMR_VERIFY|GMR_NOREUSE));
    inactive(v,0);
    CHECK(super_inputs==before);
    CHECK(termination==(column==0?CONTACT_SORT_FIRST_CODE:CONTACT_SORT_LAST_CODE));
    return (UWORD)termination;
}
static void click(ContactListView *v,ULONG col)
{ UWORD code=release_click(v,col);CHECK(handle_contact_sort_event(v,code)==1); }
static void check_order(ContactListView *v,size_t length)
{
    struct List *l=v->list;struct Node *n=l->lh_Head,*prev=NULL;size_t count=0;
    while(n->ln_Succ){
        CHECK(count<length);
        CHECK(n->ln_Pred==(prev?prev:(struct Node*)&l->lh_Head));
        if(prev)CHECK(compare_contact_nodes(prev,n,v->sort_column,v->sort_direction)<=0L);
        CHECK(n->selected==n->id%2UL);prev=n;n=n->ln_Succ;++count;
    }
    CHECK(count==length && l->lh_Tail==NULL);
    CHECK(l->lh_TailPred==(prev?prev:(struct Node*)&l->lh_Head));
}
static void test_sequences(void)
{
    unsigned sequence;
    for(sequence=0;sequence<256U;++sequence){
        ContactListView v;struct Hook hooks[3];struct List l;struct Gadget g;struct Window w;struct RastPort rp;
        unsigned i;ULONG expected_column=0,expected_direction=LBMSORT_FORWARD;
        setup(&v,hooks,&l,&g,&w,&rp);
        add_contact(&l,hooks,1,"Zoe","Adams");add_contact(&l,hooks,2,"Anna","Zulu");
        add_contact(&l,hooks,3,"Mia","Brown");
        sort_contact_nodes(&l,0,LBMSORT_FORWARD);paint_and_check(&v);check_order(&v,3);
        g.cursor=l.lh_Head;g.top=2;
        for(i=0;i<8U;++i){
            ULONG col=(sequence>>i)&1U;
            if(col==expected_column)expected_direction=expected_direction==LBMSORT_FORWARD?LBMSORT_REVERSE:LBMSORT_FORWARD;
            else {expected_column=col;expected_direction=LBMSORT_FORWARD;}
            click(&v,col);
            CHECK(v.sort_column==expected_column && v.sort_direction==expected_direction);
            CHECK(g.top==2UL);check_order(&v,3);paint_and_check(&v);
        }
        cleanup(&v);
    }
    puts("256 eight-click sequences: release-only sorting, direction reset, native-header preservation.");
}
static void test_input_edges(void)
{
    ContactListView v;struct Hook h[3];struct List l;struct Gadget g;struct Window w;struct RastPort rp;
    LONG term=-1;unsigned before;UWORD pending[4];unsigned i;
    setup(&v,h,&l,&g,&w,&rp);add_contact(&l,h,1,"A","Z");add_contact(&l,h,2,"Z","A");
    paint_and_check(&v);
    /* Press then leave the title: no sorting. */
    CHECK(dispatch_input(&v,GM_GOACTIVE,IECLASS_RAWMOUSE,SELECTDOWN,15,5,&term)==GMR_MEACTIVE);
    CHECK(dispatch_input(&v,GM_HANDLEINPUT,IECLASS_RAWMOUSE,SELECTUP,15,45,&term)==GMR_NOREUSE);
    inactive(&v,0);CHECK(term==-1 && v.sort_column==0 && v.sort_direction==LBMSORT_FORWARD);
    /* Releasing over the other header does not activate that other header. */
    CHECK(dispatch_input(&v,GM_GOACTIVE,IECLASS_RAWMOUSE,SELECTDOWN,15,5,&term)==GMR_MEACTIVE);
    CHECK(dispatch_input(&v,GM_HANDLEINPUT,IECLASS_RAWMOUSE,SELECTUP,150,5,&term)==GMR_NOREUSE);
    inactive(&v,0);
    CHECK(dispatch_input(&v,GM_GOACTIVE,IECLASS_RAWMOUSE,SELECTDOWN,15,5,&term)==GMR_MEACTIVE);
    CHECK(dispatch_input(&v,GM_HANDLEINPUT,IECLASS_RAWKEY,0x45,15,5,&term)==GMR_NOREUSE);
    inactive(&v,0);
    CHECK(dispatch_input(&v,GM_GOACTIVE,IECLASS_RAWMOUSE,SELECTDOWN,15,5,&term)==GMR_MEACTIVE);
    CHECK(dispatch_input(&v,GM_HANDLEINPUT,IECLASS_RAWMOUSE,MENUDOWN,15,5,&term)==GMR_REUSE);
    inactive(&v,0);
    CHECK(dispatch_input(&v,GM_GOACTIVE,IECLASS_RAWMOUSE,SELECTDOWN,15,5,&term)==GMR_MEACTIVE);
    inactive(&v,1);CHECK(term==-1);
    /* Email header ignored, no native sorter is entered. */
    before=super_inputs;
    CHECK(dispatch_input(&v,GM_GOACTIVE,IECLASS_RAWMOUSE,SELECTDOWN,400,5,&term)==GMR_NOREUSE);
    CHECK(super_inputs==before);
    /* Separator band is delegated unchanged. */
    CHECK(dispatch_input(&v,GM_GOACTIVE,IECLASS_RAWMOUSE,SELECTDOWN,120,5,&term)==GMR_MEACTIVE);
    CHECK(super_inputs==before+1U && !v.header_active);
    CHECK(dispatch_input(&v,GM_HANDLEINPUT,IECLASS_RAWMOUSE,SELECTUP,120,5,&term)==(GMR_NOREUSE|GMR_VERIFY));
    CHECK(term==0);inactive(&v,0);
    /* List rows and wheel/key input stay with the native class. */
    CHECK(dispatch_input(&v,GM_GOACTIVE,IECLASS_RAWMOUSE,SELECTDOWN,15,45,&term)==GMR_MEACTIVE);
    CHECK(dispatch_input(&v,GM_HANDLEINPUT,IECLASS_RAWKEY,0x7a,15,45,&term)==GMR_MEACTIVE);
    CHECK(v.native_active);
    before=super_inactive;
    CHECK(dispatch_input(&v,GM_HANDLEINPUT,IECLASS_RAWMOUSE,SELECTDOWN,15,5,&term)==GMR_MEACTIVE);
    CHECK(super_inactive==before+1U && v.header_active && !v.native_active);
    inactive(&v,1);
    /* Native releases cannot collide with our title event codes. */
    term=99;CHECK(dispatch_input(&v,GM_GOACTIVE,IECLASS_RAWMOUSE,SELECTDOWN,15,45,&term)==GMR_MEACTIVE);
    CHECK(dispatch_input(&v,GM_HANDLEINPUT,IECLASS_RAWMOUSE,SELECTUP,15,45,&term)==(GMR_VERIFY|GMR_NOREUSE));
    CHECK(term==0);inactive(&v,0);
    /* Queued clicks carry their own columns, not one overwritten shared field. */
    pending[0]=release_click(&v,0);pending[1]=release_click(&v,1);
    pending[2]=release_click(&v,1);pending[3]=release_click(&v,0);
    for(i=0;i<4;++i)CHECK(handle_contact_sort_event(&v,pending[i]));
    CHECK(v.sort_column==0 && v.sort_direction==LBMSORT_FORWARD);
    /* Changed geometry is ignored until a native paint supplies fresh bounds. */
    g.Width=600;CHECK(contact_header_column(&v,&g,15,5)==-1);
    g.left[0]=15;g.right[0]=240;g.left[1]=244;g.right[1]=390;
    g.left[2]=394;g.right[2]=580;
    draw_font_height=12;paint_and_check(&v);click(&v,1);paint_and_check(&v);
    g.Flags=GFLG_DISABLED;CHECK(contact_header_column(&v,&g,15,5)==-1);g.Flags=0;
    cleanup(&v);draw_font_height=8;
    CHECK(obtained_rp==released_rp);
    puts("Input aborts, queued clicks, divider/body delegation, fonts and resizing checked.");
}
static void test_rebuild(void)
{
    ContactListView v;struct Hook h[3];struct List l;struct Gadget g;struct Window w;struct RastPort rp;
    AmgContactBook book;AmgContact rows[3];unsigned i;
    setup(&v,h,&l,&g,&w,&rp);memset(&book,0,sizeof(book));
    rebuild_contact_list(&g,&w,&l,&book,0,h,10,&v);paint_and_check(&v);
    click(&v,1);paint_and_check(&v);CHECK(v.sort_column==1);
    memset(rows,0,sizeof(rows));
    for(i=0;i<3;++i){rows[i].id=i+1U;snprintf(rows[i].first_name,sizeof(rows[i].first_name),"Name%u",i);
        snprintf(rows[i].last_name,sizeof(rows[i].last_name),"Last%u",3U-i);}
    strcpy(rows[1].email,"test@example.com");book.items=rows;book.count=3;
    rebuild_contact_list(&g,&w,&l,&book,0,h,10,&v);CHECK(v.sort_column==1);
    CHECK(l.lh_Head->id==3UL);paint_and_check(&v);
    rebuild_contact_list(&g,&w,&l,&book,1,h,10,&v);CHECK(l.lh_Head->id==2UL && !l.lh_Head->ln_Succ->ln_Succ);
    paint_and_check(&v);cleanup(&v);
    init_contact_list_view(&v);fail_class=1;CHECK(!open_contact_list_class(&v));
    close_contact_list_class(&v);fail_class=0;
    puts("Empty list, rebuild, email-only selection and class failure/cleanup checked.");
}
static void test_large_sort(void)
{
    ContactListView v;struct Hook h[3];struct List l;struct Gadget g;struct Window w;struct RastPort rp;
    unsigned i;char a[32],b[32];ULONG before;
    setup(&v,h,&l,&g,&w,&rp);
    for(i=0;i<1024;++i){snprintf(a,sizeof(a),"First%04u",(i*73U)%1024U);
        snprintf(b,sizeof(b),"Last%04u",(i*29U)%1024U);add_contact(&l,h,i+1,a,b);}
    before=comparisons;sort_contact_nodes(&l,0,LBMSORT_FORWARD);CHECK(comparisons-before<30000UL);
    paint_and_check(&v);check_order(&v,1024);click(&v,1);check_order(&v,1024);click(&v,1);check_order(&v,1024);
    cleanup(&v);
}
int main(void)
{
    test_sequences();test_input_edges();test_rebuild();test_large_sort();
    CHECK(allocated_classes==freed_classes);
    printf("Contact rebuild: %lu assertions passed (host API doubles).\n",checks);
    return 0;
}

'''

def main():
    wiring()
    cc=shlex.split(os.environ.get('HOST_CC','gcc'))
    extra=shlex.split(os.environ.get('CONTACT_ICON_TEST_FLAGS',''))
    flags=['-std=c99','-Wall','-Wextra','-Wshadow','-Wpointer-arith',
           '-Wstrict-prototypes','-Wmissing-prototypes','-Wformat=2','-Werror',
           '-fno-strict-aliasing','-O1','-g']
    with tempfile.TemporaryDirectory(prefix='amimail-contacts-rebuilt-') as tmp:
        c=Path(tmp)/'test.c';exe=Path(tmp)/'test'
        c.write_text(API+'\n'+production()+'\n'+TESTS,encoding='utf-8')
        subprocess.run(cc+flags+extra+['-I'+str(ROOT/'include'),str(c),'-o',str(exe)],check=True)
        env=os.environ.copy();env['UBSAN_OPTIONS']='halt_on_error=1:print_stacktrace=1'
        subprocess.run([str(exe)],check=True,env=env)
    print('Host doubles only; a real AmigaOS visual/input test remains required.')

if __name__=='__main__':
    main()
