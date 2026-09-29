#ifndef AMIMAIL_HERALD_NATIVE_DOUBLE_H
#define AMIMAIL_HERALD_NATIVE_DOUBLE_H
/* Explicit native API doubles, not a replacement NDK and not an ABI test.
 * Signatures follow the classic rexxsyslib/exec/timer APIs; pointer signedness
 * intentionally differs from plain C char*, just like the m68k NDK. */
#include <stddef.h>
#include <stdint.h>
#include "account.h"
#include "herald.h"
#include "i18n.h"
#undef AMIGMAIL_AMIGA
#define AMIGMAIL_AMIGA 1
#define AMIMAIL_GUI_INTERNAL_H

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short UWORD;
typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef void *APTR;
typedef UBYTE *STRPTR;
typedef const UBYTE *CONST_STRPTR;
struct Node { struct Node *ln_Succ, *ln_Pred; UBYTE ln_Type; BYTE ln_Pri; char *ln_Name; };
struct List { struct Node *lh_Head, *lh_Tail, *lh_TailPred; UBYTE lh_Type, l_pad; };
struct Task { ULONG signals; };
struct MsgPort { struct Node mp_Node; UBYTE mp_Flags, mp_SigBit; struct Task *mp_SigTask; struct List mp_MsgList; };
struct Message { struct Node mn_Node; struct MsgPort *mn_ReplyPort; UWORD mn_Length; };
struct Library { int used; };
struct RxsLib { struct Library base; };
struct RexxMsg { struct Message rm_Node; ULONG rm_Action; LONG rm_Result1, rm_Result2; STRPTR rm_Args[16]; };
struct IORequest { struct Message io_Message; UWORD io_Command; int complete, pending; };
struct timerequest { struct IORequest tr_node; struct { ULONG tv_secs, tv_micro; } tr_time; };
typedef struct AmgGui { AmgAccountSet *account_set; AmgHerald *herald; } AmgGui;
#define TRUE 1
#define FALSE 0
#define NT_MESSAGE 5U
#define NT_REPLYMSG 7U
#define PA_SIGNAL 0U
#define PA_IGNORE 2U
#define RC_OK 0L
#define RXCOMM 0x01000000UL
#define RXFF_RESULT 0x20000UL
#define TIMERNAME "timer.device"
#define UNIT_VBLANK 1UL
#define TR_ADDREQUEST 9U
void Forbid(void); void Permit(void);
ULONG SetSignal(ULONG value, ULONG mask); ULONG Wait(ULONG mask);
void FreeSignal(LONG signal_bit);
struct MsgPort *FindPort(CONST_STRPTR name);
struct MsgPort *CreateMsgPort(void); void DeleteMsgPort(struct MsgPort *port);
struct Message *GetMsg(struct MsgPort *port);
void PutMsg(struct MsgPort *port, struct Message *message);
void ReplyMsg(struct Message *message);
void Remove(struct Node *node);
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
void CloseLibrary(struct Library *base);
struct IORequest *CreateIORequest(struct MsgPort *port, ULONG size);
void DeleteIORequest(struct IORequest *request);
BYTE OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *request, ULONG flags);
void CloseDevice(struct IORequest *request);
void SendIO(struct IORequest *request);
struct IORequest *CheckIO(struct IORequest *request);
LONG AbortIO(struct IORequest *request); LONG WaitIO(struct IORequest *request);
struct RexxMsg *test_CreateRexxMsg(struct MsgPort *reply, STRPTR extension, STRPTR host);
void test_DeleteRexxMsg(struct RexxMsg *message);
STRPTR test_CreateArgstring(STRPTR text, ULONG length);
void test_DeleteArgstring(UBYTE *text);
void test_rexx_base(const struct RxsLib *base);
#define CreateRexxMsg(p,e,h) (test_rexx_base(RexxSysBase),test_CreateRexxMsg(p,e,h))
#define DeleteRexxMsg(m) (test_rexx_base(RexxSysBase),test_DeleteRexxMsg(m))
#define CreateArgstring(s,n) (test_rexx_base(RexxSysBase),test_CreateArgstring(s,n))
#define DeleteArgstring(s) (test_rexx_base(RexxSysBase),test_DeleteArgstring(s))
void gui_herald_new_mail(AmgGui *gui, size_t account_slot, unsigned long count,
                         const unsigned char *payload, size_t payload_length,
                         unsigned long previous_uid);
AmgHeraldResult gui_herald_test(AmgGui *gui, size_t account_slot, const char *name);
const char *gui_herald_result_text(AmgHeraldResult result);
#endif
