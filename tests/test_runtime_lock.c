/* Actual wrappers; pthread-backed Exec doubles; deliberately yielding libc
 * backends. Mutual exclusion, recursive calls and failures, no Amiga ABI. */
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/runtime_lock.c"
static pthread_mutex_t scheduling = PTHREAD_MUTEX_INITIALIZER;
static _Thread_local unsigned forbidden, nesting;
static _Thread_local int fail_close;
static atomic_uint owners, calls, initializations;
void Forbid(void) {
    assert(!forbidden); assert(!pthread_mutex_lock(&scheduling)); ++forbidden;
}
void Permit(void) {
    assert(forbidden == 1U); --forbidden; assert(!pthread_mutex_unlock(&scheduling));
}
void InitSemaphore(struct SignalSemaphore *lock) {
    pthread_mutexattr_t attributes;
    assert(forbidden == 1U);
    assert(!pthread_mutexattr_init(&attributes));
    assert(!pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_RECURSIVE));
    assert(!pthread_mutex_init(&lock->mutex, &attributes));
    assert(!pthread_mutexattr_destroy(&attributes));
    atomic_fetch_add(&initializations, 1U);
}
void ObtainSemaphore(struct SignalSemaphore *lock) {
    assert(!forbidden); assert(!pthread_mutex_lock(&lock->mutex));
}
void ReleaseSemaphore(struct SignalSemaphore *lock) {
    assert(!forbidden); assert(!pthread_mutex_unlock(&lock->mutex));
}
static void enter_backend(void) {
    if (!nesting++) assert(atomic_fetch_add(&owners, 1U) == 0U);
    atomic_fetch_add(&calls, 1U); sched_yield(); sched_yield();
}
static void leave_backend(void) {
    assert(nesting > 0U);
    if (!--nesting) assert(atomic_fetch_sub(&owners, 1U) == 1U);
}
void *__real_malloc(size_t size) {
    void *result;
    enter_backend(); result = size == SIZE_MAX ? NULL : malloc(size);
    if (!result) errno = ENOMEM;
    leave_backend(); return result;
}
void *__real_calloc(size_t count, size_t size) {
    void *result = NULL;
    enter_backend();
    if (!size || count <= SIZE_MAX / size) {
        result = __wrap_malloc(count * size);
        if (result) memset(result, 0, count * size);
    } else errno = ENOMEM;
    leave_backend(); return result;
}
void *__real_realloc(void *pointer, size_t size) {
    void *result;
    enter_backend(); result = size == SIZE_MAX ? NULL : realloc(pointer, size);
    if (!result && size) errno = ENOMEM;
    leave_backend(); return result;
}
void __real_free(void *pointer) {
    enter_backend(); free(pointer); leave_backend();
}
FILE *__real_fopen(const char *path, const char *mode) {
    FILE *result;
    void *buffer;
    (void)mode;
    enter_backend(); buffer = __wrap_malloc(32U); __wrap_free(buffer);
    result = !strcmp(path, "deny") ? NULL : tmpfile();
    if (!result) errno = EACCES;
    leave_backend(); return result;
}
int __real_fclose(FILE *file) {
    int result;
    enter_backend(); result = fclose(file);
    if (fail_close) { result = EOF; errno = EIO; }
    leave_backend(); return result;
}
#if defined(__NEWLIB__)
void *__real__malloc_r(struct _reent *context, size_t size) {
    void *result; (void)context;
    enter_backend(); result = __wrap_malloc(size); leave_backend(); return result;
}
void *__real__calloc_r(struct _reent *context, size_t count, size_t size) {
    void *result; (void)context;
    enter_backend(); result = __wrap_calloc(count, size); leave_backend(); return result;
}
void *__real__realloc_r(struct _reent *context, void *pointer, size_t size) {
    void *result; (void)context;
    enter_backend(); result = __wrap_realloc(pointer, size); leave_backend(); return result;
}
void __real__free_r(struct _reent *context, void *pointer) {
    (void)context; enter_backend(); __wrap_free(pointer); leave_backend();
}
FILE *__real__fopen_r(struct _reent *context, const char *path, const char *mode) {
    FILE *result; (void)context;
    enter_backend(); result = __wrap_fopen(path, mode); leave_backend(); return result;
}
int __real__fclose_r(struct _reent *context, FILE *file) {
    int result; (void)context;
    enter_backend(); result = __wrap_fclose(file); leave_backend(); return result;
}
FILE *__real___sfp(struct _reent *context) {
    FILE *result;
    enter_backend(); result = __wrap__fopen_r(context, "pool", "wb");
    leave_backend(); return result;
}
void __real___sinit(struct _reent *context) {
    FILE *file;
    enter_backend(); file = __wrap___sfp(context); assert(file);
    assert(!__wrap__fclose_r(context, file)); leave_backend();
}
#endif
static void *exercise(void *argument) {
    unsigned iteration;
    (void)argument;
    for (iteration = 0; iteration < 200U; ++iteration) {
        unsigned char *pointer = __wrap_calloc(32U, 1U);
        FILE *file;
        unsigned index;
        assert(pointer);
        for (index = 0; index < 32U; ++index) assert(!pointer[index]);
        memset(pointer, 0x5a, 32U);
        pointer = __wrap_realloc(pointer, 64U); assert(pointer);
        for (index = 0; index < 32U; ++index) assert(pointer[index] == 0x5a);
        assert(!__wrap_realloc(pointer, SIZE_MAX));
        assert(pointer[0] == 0x5a); __wrap_free(pointer);
        assert(!__wrap_malloc(SIZE_MAX)); assert(!__wrap_calloc(SIZE_MAX, 2U));
        __wrap_free(NULL); assert(!__wrap_fopen("deny", "rb"));
        file = __wrap_fopen("ok", "wb"); assert(file);
        assert(fputs("payload", file) >= 0); assert(!__wrap_fclose(file));
        file = __wrap_fopen("ok", "wb"); assert(file);
        fail_close = 1; assert(__wrap_fclose(file) == EOF); fail_close = 0;
#if defined(__NEWLIB__)
        {
            struct _reent context = {0};
            pointer = __wrap__malloc_r(&context, 32U); assert(pointer);
            pointer = __wrap__realloc_r(&context, pointer, 64U); assert(pointer);
            assert(!__wrap__realloc_r(&context, pointer, SIZE_MAX));
            __wrap__free_r(&context, pointer);
            assert(!__wrap__malloc_r(&context, SIZE_MAX));
            assert(!__wrap__calloc_r(&context, SIZE_MAX, 2U));
            pointer = __wrap__calloc_r(&context, 32U, 1U); assert(pointer);
            assert(!pointer[0]); __wrap__free_r(&context, pointer);
            assert(!__wrap__fopen_r(&context, "deny", "wb"));
            file = __wrap__fopen_r(&context, "ok", "wb"); assert(file);
            fail_close = 1; assert(__wrap__fclose_r(&context, file) == EOF);
            fail_close = 0; __wrap___sinit(&context);
        }
#endif
        assert(!nesting && !forbidden);
    }
    return NULL;
}
int main(void) {
    pthread_t tasks[6];
    unsigned index;
    for (index = 0; index < 6U; ++index)
        assert(!pthread_create(&tasks[index], NULL, exercise, NULL));
    for (index = 0; index < 6U; ++index) assert(!pthread_join(tasks[index], NULL));
    assert(!atomic_load(&owners) && atomic_load(&initializations) == 1U);
    printf("Runtime lock: %u backend calls across 6 tasks; recursion/failures OK.\n",
           atomic_load(&calls));
    return 0;
}
