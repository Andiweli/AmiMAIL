/* GUI and NP_Entry workers share this executable's C runtime.  Some Amiga
 * libnix/newlib builds have an unprotected allocator and FILE-slot pool.
 * Linker wrapping covers project calls AND unresolved calls inside libc;
 * replacing only __malloc_lock would miss libnix's MemMap allocator.
 *
 * This lock never spans a network operation.  It is recursive because, for
 * example, fopen may allocate a FILE block or buffer through _malloc_r.
 * Even failed allocation/open/close operations must release it.
 */
#include "amigmail.h"
#include <stdio.h>
#include <stdlib.h>

#if AMIGMAIL_AMIGA
#include <exec/semaphores.h>
#include <proto/exec.h>

static struct SignalSemaphore runtime_lock;
static int runtime_lock_ready;

static void runtime_acquire(void)
{
    /* May also be called during CRT startup, before main. SysBase is already
     * available then. Initialization itself neither allocates nor waits. */
    Forbid();
    if (!runtime_lock_ready) {
        InitSemaphore(&runtime_lock);
        runtime_lock_ready = 1;
    }
    Permit();
    ObtainSemaphore(&runtime_lock);
}

static void runtime_release(void)
{
    ReleaseSemaphore(&runtime_lock);
}

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void __real_free(void *pointer);
FILE *__real_fopen(const char *path, const char *mode);
int __real_fclose(FILE *file);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);
void __wrap_free(void *pointer);
FILE *__wrap_fopen(const char *path, const char *mode);
int __wrap_fclose(FILE *file);

void *__wrap_malloc(size_t size)
{
    void *result;
    runtime_acquire();
    result = __real_malloc(size);
    runtime_release();
    return result;
}

void *__wrap_calloc(size_t count, size_t size)
{
    void *result;
    runtime_acquire();
    result = __real_calloc(count, size);
    runtime_release();
    return result;
}

void *__wrap_realloc(void *pointer, size_t size)
{
    void *result;
    runtime_acquire();
    result = __real_realloc(pointer, size);
    runtime_release();
    return result;
}

void __wrap_free(void *pointer)
{
    runtime_acquire();
    __real_free(pointer);
    runtime_release();
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
    FILE *result;
    runtime_acquire();
    result = __real_fopen(path, mode);
    runtime_release();
    return result;
}

int __wrap_fclose(FILE *file)
{
    int result;
    runtime_acquire();
    result = __real_fclose(file);
    runtime_release();
    return result;
}

#if defined(__NEWLIB__)
/* These references come from libc's formatting, stream and cleanup code.
 * Public fopen/fclose have same-object calls to their _r implementations;
 * wrapping only the _r names would therefore leave those calls unprotected.
 * __sfp/__sinit also contain same-object internal calls: their entire entry
 * is guarded, rather than wrapping the no-op lock hooks in findfp.o. */
#include <sys/reent.h>
void *__real__malloc_r(struct _reent *context, size_t size);
void *__real__calloc_r(struct _reent *context, size_t count, size_t size);
void *__real__realloc_r(struct _reent *context, void *pointer, size_t size);
void __real__free_r(struct _reent *context, void *pointer);
FILE *__real__fopen_r(struct _reent *context, const char *path, const char *mode);
int __real__fclose_r(struct _reent *context, FILE *file);
FILE *__real___sfp(struct _reent *context);
void __real___sinit(struct _reent *context);
void *__wrap__malloc_r(struct _reent *context, size_t size);
void *__wrap__calloc_r(struct _reent *context, size_t count, size_t size);
void *__wrap__realloc_r(struct _reent *context, void *pointer, size_t size);
void __wrap__free_r(struct _reent *context, void *pointer);
FILE *__wrap__fopen_r(struct _reent *context, const char *path, const char *mode);
int __wrap__fclose_r(struct _reent *context, FILE *file);
FILE *__wrap___sfp(struct _reent *context);
void __wrap___sinit(struct _reent *context);

void *__wrap__malloc_r(struct _reent *context, size_t size)
{
    void *result;
    runtime_acquire();
    result = __real__malloc_r(context, size);
    runtime_release();
    return result;
}

void *__wrap__calloc_r(struct _reent *context, size_t count, size_t size)
{
    void *result;
    runtime_acquire();
    result = __real__calloc_r(context, count, size);
    runtime_release();
    return result;
}

void *__wrap__realloc_r(struct _reent *context, void *pointer, size_t size)
{
    void *result;
    runtime_acquire();
    result = __real__realloc_r(context, pointer, size);
    runtime_release();
    return result;
}

void __wrap__free_r(struct _reent *context, void *pointer)
{
    runtime_acquire();
    __real__free_r(context, pointer);
    runtime_release();
}

FILE *__wrap__fopen_r(struct _reent *context, const char *path, const char *mode)
{
    FILE *result;
    runtime_acquire();
    result = __real__fopen_r(context, path, mode);
    runtime_release();
    return result;
}

int __wrap__fclose_r(struct _reent *context, FILE *file)
{
    int result;
    runtime_acquire();
    result = __real__fclose_r(context, file);
    runtime_release();
    return result;
}

FILE *__wrap___sfp(struct _reent *context)
{
    FILE *result;
    runtime_acquire();
    result = __real___sfp(context);
    runtime_release();
    return result;
}

void __wrap___sinit(struct _reent *context)
{
    runtime_acquire();
    __real___sinit(context);
    runtime_release();
}
#endif
#endif
