#ifndef AMIMAIL_NOTIFY_NATIVE_DOUBLE_H
#define AMIMAIL_NOTIFY_NATIVE_DOUBLE_H
/* Explicit host test doubles, NOT an Amiga SDK or ABI validation. */
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short UWORD;
typedef signed char BYTE;
typedef unsigned char UBYTE;
typedef void *APTR;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
typedef unsigned long BPTR;
struct Task { pthread_mutex_t lock; pthread_cond_t condition; ULONG signals; int id; };
struct Process { struct Task pr_Task; };
struct Library { int present; };
struct MsgPort { struct Task *mp_SigTask; UBYTE mp_SigBit; };
struct IORequest { UWORD io_Command; int complete; struct MsgPort *reply; };
struct timerequest { struct IORequest tr_node; struct { ULONG tv_secs, tv_micro; } tr_time; };
struct TagItem { ULONG ti_Tag, ti_Data; };
typedef struct Object { int live; } Object;
struct DTMethod { const char *dtm_Label; const char *dtm_Command; ULONG dtm_Method; };
struct dtTrigger { ULONG MethodID; void *dtt_GInfo; ULONG dtt_Function; void *dtt_Data; };
typedef struct { ULONG MethodID; } *Msg;
typedef struct { int notification_sound; char notification_sound_path[512]; } AmgAccount;
typedef struct { AmgAccount *account; } AmgGui;
#define AMIMAIL_GUI_INTERNAL_H
#define AMIGMAIL_AMIGA 1
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0UL
#define SIGBREAKF_CTRL_C (1UL << 12)
#define ACCESS_READ (-2L)
#define TIMERNAME "timer.device"
enum { UNIT_VBLANK = 1, TR_ADDREQUEST = 9 };
enum { DTA_SourceType=100, DTST_FILE, DTA_GroupID, GID_SOUND,
 SDTA_SignalTask, SDTA_SignalBit, SDTA_Cycles, SDTA_Volume, DTA_Repeat,
 DTM_TRIGGER, STM_PLAY, STM_STOP, NP_Entry, NP_Name, NP_StackSize, NP_WindowPtr };
void Forbid(void); void Permit(void);
ULONG Wait(ULONG mask); ULONG SetSignal(ULONG value, ULONG mask);
void Signal(struct Task *task, ULONG mask);
struct Task *FindTask(CONST_STRPTR name);
BYTE AllocSignal(LONG bit); void FreeSignal(LONG bit);
BPTR Lock(CONST_STRPTR path, LONG mode); void UnLock(BPTR lock);
LONG NameFromLock(BPTR lock, STRPTR path, LONG length);
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version);
void CloseLibrary(struct Library *base);
struct MsgPort *CreateMsgPort(void); void DeleteMsgPort(struct MsgPort *port);
struct IORequest *CreateIORequest(struct MsgPort *port, ULONG size);
void DeleteIORequest(struct IORequest *request);
BYTE OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *io, ULONG flags);
void CloseDevice(struct IORequest *io);
void SendIO(struct IORequest *io); LONG WaitIO(struct IORequest *io);
struct IORequest *CheckIO(struct IORequest *io); LONG AbortIO(struct IORequest *io);
struct Process *CreateNewProcTags(ULONG tag, ...);
Object *test_NewDTObjectA(APTR path, const struct TagItem *tags);
ULONG test_DoDTMethodA(Object *obj, void *window, void *requester, Msg msg);
/* Classic NDK prototypes use an opaque plural tag although the table
 * elements are struct DTMethod in datatypesclass.h. */
struct DTMethods;
struct DTMethods *test_GetDTTriggerMethods(Object *object);
void test_DisposeDTObject(Object *object);
void test_base(struct Library *base);
#define NewDTObjectA(path, tags) (test_base(DataTypesBase), test_NewDTObjectA(path, tags))
#define DoDTMethodA(o, w, r, m) (test_base(DataTypesBase), test_DoDTMethodA(o, w, r, m))
#define GetDTTriggerMethods(o) (test_base(DataTypesBase), test_GetDTTriggerMethods(o))
#define DisposeDTObject(o) (test_base(DataTypesBase), test_DisposeDTObject(o))
void gui_notify_cleanup(void);
int gui_notify_preview_sound(AmgGui *gui, const char *path);
void gui_notify_new_mail(AmgGui *gui);
void gui_notify_new_mail_for_account(AmgGui *gui, const AmgAccount *account);
#endif
