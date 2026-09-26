/* Real HTML/codec functions; amg_tr is a host-side catalog test double.
 * German fixture strings are read from the delivered binary catalog by the
 * Python runner. No AmigaOS locale.library or GUI is emulated here. */
#include "mime.h"
#include "codec.h"
#include "i18n.h"
#include "catalog_marker_values.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
static unsigned language;
static unsigned utf8_lookups, local_lookups;
#define CHECK(test) do { ++checks; if (!(test)) { ++failures; \
    fprintf(stderr, "line %d: %s\n", __LINE__, #test); } } while (0)

static const char *utf8_marker(void)
{
    if (language == 1U) return CATALOG_GRAPHIC_UTF8;
    /* Different byte length from both English and German; exercises a
     * future non-ASCII translation without shipping a third catalog. */
    if (language == 2U) return "[Gr\303\241fico ampliado]";
    return "[Graphic]";
}

static const char *local_marker(void)
{
    if (language == 1U) return CATALOG_GRAPHIC_LOCAL;
    if (language == 2U) return "[Gr\341fico ampliado]";
    return "[Graphic]";
}

const char *amg_tr(long id, const char *fallback)
{
    if (id == MSG_GRAPHIC_PLACEHOLDER_UTF8) {
        ++utf8_lookups;
        CHECK(fallback && !strcmp(fallback, "[Graphic]"));
        return utf8_marker();
    }
    if (id == MSG_GRAPHIC_PLACEHOLDER_LOCAL) {
        ++local_lookups;
        CHECK(fallback && !strcmp(fallback, "[Graphic]"));
        return local_marker();
    }
    return fallback ? fallback : "";
}

static const char *buffer_text(AmgBuffer *buffer)
{
    int result = amg_buffer_terminate(buffer);
    CHECK(result == AMG_OK);
    return result == AMG_OK ? (const char *)buffer->data : "";
}

static size_t occurrences(const char *text, const char *word)
{
    size_t count = 0U, length = strlen(word);
    const char *found;
    if (!length) return 0U;
    while ((found = strstr(text, word)) != NULL) {
        ++count;
        text = found + length;
    }
    return count;
}

static void test_images(void)
{
    static const char html[] =
        "<p>Image <img src=\"image.png\"></p>"
        "<p>Logo <img src=\"logo.png\" alt=\"Company logo\"></p>"
        "<img src=\"hidden.png\" style=\"display:none\">"
        "<img src=\"tracking.gif\" width=\"1\" height=\"1\">";
    static const char escaped[] = "Before &lt;img src=&quot;image.png&quot;&gt; After";
    AmgBuffer output, local;
    const char *text;
    amg_buffer_init(&output);
    amg_buffer_init(&local);
    CHECK(amg_html_to_text(html, strlen(html), &output) == AMG_OK);
    text = buffer_text(&output);
    CHECK(occurrences(text, utf8_marker()) == 1U);
    CHECK(strstr(text, "Company logo") != NULL);
    CHECK(strstr(text, "tracking.gif") == NULL);
    CHECK(amg_utf8_to_local(text, &local) == AMG_OK);
    CHECK(occurrences(buffer_text(&local), local_marker()) == 1U);
    amg_buffer_free(&output);
    amg_buffer_free(&local);

    amg_buffer_init(&output);
    CHECK(amg_html_to_text(escaped, strlen(escaped), &output) == AMG_OK);
    CHECK(occurrences(buffer_text(&output), utf8_marker()) == 1U);
    CHECK(strstr(buffer_text(&output), "Before") != NULL);
    CHECK(strstr(buffer_text(&output), "After") != NULL);
    amg_buffer_free(&output);
}

static void test_links(void)
{
    static const char html[] =
        "<a href=\"https://example.invalid/image-only\"> <img src=\"x.png\"> </a>"
        "<p><a href=\"https://example.invalid/visible\">Read this</a></p>"
        "<a href=\"https://example.invalid/alt\"><img src=\"logo.png\" alt=\"Our site\"></a>";
    AmgBuffer output;
    const char *text;
    amg_buffer_init(&output);
    CHECK(amg_html_to_text(html, strlen(html), &output) == AMG_OK);
    text = buffer_text(&output);
    CHECK(occurrences(text, utf8_marker()) == 1U);
    CHECK(strstr(text, "https://example.invalid/image-only") == NULL);
    CHECK(strstr(text, "Read this <https://example.invalid/visible>") != NULL);
    CHECK(strstr(text, "Our site <https://example.invalid/alt>") != NULL);
    amg_buffer_free(&output);
}

static void test_emoji(void)
{
    AmgBuffer output;
    const char *text;
    unsigned utf8_before = utf8_lookups;
    amg_buffer_init(&output);
    CHECK(amg_utf8_to_local(
        "\xF0\x9F\xA7\x91\xE2\x80\x8D\xE2\x9A\x95\xEF\xB8\x8F"
        " Gr\xC3\xBC\xC3\x9F" "e \xE2\x9A\xA1", &output) == AMG_OK);
    text = buffer_text(&output);
    CHECK(occurrences(text, local_marker()) == 2U);
    CHECK(strstr(text, "Gr\374\337e") != NULL);
    CHECK(utf8_lookups == utf8_before); /* local output must use the local ID */
    amg_buffer_free(&output);
}

int main(void)
{
    for (language = 0U; language < 3U; ++language) {
        test_images();
        test_links();
        test_emoji();
    }
    CHECK(utf8_lookups > 0U);
    CHECK(local_lookups > 0U);
    printf("Locale markers: %u checks, %u failures\n", checks, failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
