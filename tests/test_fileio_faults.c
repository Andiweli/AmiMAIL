/* Compile alone: this includes the production fileio.c with only its host
 * rename/remove calls fault-injected. The backup/recovery algorithm is not
 * duplicated by the test. It uses the same no-overwrite policy as AmigaDOS. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int checks, failures, moves, fail_move, fail_restore, fail_delete;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static int injected_rename(const char *from, const char *to)
{
    ++moves;
    if (moves == fail_move || (fail_restore && strstr(from, ".bak"))) {
        errno = EACCES; return -1;
    }
    return rename(from, to);
}
static int injected_remove(const char *path)
{
    if (fail_delete) { errno = EACCES; return -1; }
    return remove(path);
}
#define rename injected_rename
#define remove injected_remove
#include "../src/fileio.c"
#undef rename
#undef remove

static void write_value(const char *path, const char *value)
{
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
    if (file) { CHECK(fputs(value, file) >= 0); CHECK(fclose(file) == 0); }
}
static int has_value(const char *path, const char *value)
{
    char buffer[80];
    FILE *file = fopen(path, "rb");
    size_t n;
    if (!file) return 0;
    n = fread(buffer, 1U, sizeof(buffer)-1U, file);
    fclose(file); buffer[n] = 0;
    return !strcmp(buffer, value);
}
static void reset(void)
{
    fail_move = fail_restore = fail_delete = moves = 0;
    (void)remove("build/review-settings.cfg");
    (void)remove("build/review-settings.cfg.bak");
    (void)remove("build/review-settings.cfg.new");
}
int main(void)
{
    const char *path = "build/review-settings.cfg";
    const char *tmp = "build/review-settings.cfg.new";
    const char *bak = "build/review-settings.cfg.bak";
    reset();
    write_value(tmp, "first");
    CHECK(amg_file_replace(tmp, path) == AMG_OK);
    CHECK(has_value(path, "first")); CHECK(file_state(tmp) == 0);
    write_value(tmp, "second");
    CHECK(amg_file_replace(tmp, path) == AMG_OK);
    CHECK(has_value(path, "second")); CHECK(file_state(bak) == 0);

    reset(); write_value(path, "old"); write_value(tmp, "new");
    fail_move = 1;
    CHECK(amg_file_replace(tmp, path) == AMG_ERR_IO);
    CHECK(has_value(path, "old")); CHECK(has_value(tmp, "new"));
    CHECK(file_state(bak) == 0);

    reset(); write_value(path, "old"); write_value(tmp, "new");
    fail_move = 2;
    CHECK(amg_file_replace(tmp, path) == AMG_ERR_IO);
    CHECK(has_value(path, "old")); CHECK(has_value(tmp, "new"));
    CHECK(file_state(bak) == 0);

    reset(); write_value(path, "old"); write_value(tmp, "new");
    fail_move = 2; fail_restore = 1;
    CHECK(amg_file_replace(tmp, path) == AMG_ERR_IO);
    CHECK(has_value(bak, "old")); CHECK(has_value(tmp, "new"));
    CHECK(file_state(path) == 0);
    fail_move = fail_restore = 0;
    CHECK(amg_file_recover(path) == AMG_OK);
    CHECK(has_value(path, "old")); CHECK(file_state(bak) == 0);

    reset(); write_value(path, "old"); write_value(tmp, "new");
    fail_delete = 1;
    CHECK(amg_file_replace(tmp, path) == AMG_OK);
    CHECK(has_value(path, "new")); CHECK(has_value(bak, "old"));
    fail_delete = 0; moves = 0;
    write_value(tmp, "third");
    CHECK(amg_file_replace(tmp, path) == AMG_OK);
    CHECK(has_value(path, "third")); CHECK(file_state(bak) == 0);

    reset(); write_value(path, "old");
    CHECK(amg_file_replace(tmp, path) == AMG_ERR_IO);
    CHECK(has_value(path, "old"));
    CHECK(amg_file_replace(path, path) == AMG_ERR_ARGUMENT);
    CHECK(has_value(path, "old"));
    CHECK(amg_file_replace(path, "build") == AMG_ERR_IO);
    CHECK(has_value(path, "old"));

    reset(); write_value(bak, "last-good"); write_value(tmp, "new");
    CHECK(amg_file_replace(bak, path) == AMG_ERR_IO);
    CHECK(has_value(bak, "last-good"));
    CHECK(amg_file_replace(tmp, path) == AMG_OK);
    CHECK(has_value(path, "new")); CHECK(file_state(bak) == 0);
    reset();
    printf("fileio: %d checks, %d failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
