/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Narrow GLib compatibility layer for QEMU-on-Zephyr.
 *
 * Copyright (c) 2026 Zephyr QEMU port contributors
 *
 * The implementation only uses standard C allocation, memory/string helpers,
 * vsnprintf() and qsort().  It contains no Linux syscalls, no GMainContext
 * implementation, no regex engine and no networking code.
 */

#include "glib.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <inttypes.h>
#include <errno.h>

#ifndef G_MAXSIZE
#define G_MAXSIZE ((gsize) -1)
#endif

/* ------------------------------------------------------------------ */
/* Small internal helpers                                              */
/* ------------------------------------------------------------------ */

static void z_abort_message(const gchar *format, ...) G_GNUC_NORETURN;

static void z_abort_message(const gchar *format, ...)
{
    va_list ap;

    fputs("GLib compatibility layer fatal error: ", stderr);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
    abort();
}

static gsize z_checked_mul(gsize a, gsize b)
{
    if (a != 0 && b > (G_MAXSIZE / a)) {
        z_abort_message("integer overflow in allocation size");
    }

    return a * b;
}

static gsize z_checked_add(gsize a, gsize b)
{
    if (b > (G_MAXSIZE - a)) {
        z_abort_message("integer overflow in allocation size");
    }

    return a + b;
}

static void z_ref_inc(guint *ref_count)
{
    (void) __atomic_add_fetch(ref_count, 1, __ATOMIC_RELAXED);
}

static gboolean z_ref_dec_and_test(guint *ref_count)
{
    return __atomic_sub_fetch(ref_count, 1, __ATOMIC_ACQ_REL) == 0;
}

/* ------------------------------------------------------------------ */
/* Memory allocation                                                   */
/* ------------------------------------------------------------------ */

gpointer g_malloc(gsize n_bytes)
{
    gpointer mem;

    if (n_bytes == 0) {
        return NULL;
    }

    mem = malloc(n_bytes);
    if (!mem) {
        z_abort_message("g_malloc(%lu) failed", (unsigned long) n_bytes);
    }

    return mem;
}

gpointer g_malloc0(gsize n_bytes)
{
    gpointer mem;

    if (n_bytes == 0) {
        return NULL;
    }

    mem = calloc(1, n_bytes);
    if (!mem) {
        z_abort_message("g_malloc0(%lu) failed", (unsigned long) n_bytes);
    }

    return mem;
}

gpointer g_realloc(gpointer mem, gsize n_bytes)
{
    gpointer new_mem;

    if (n_bytes == 0) {
        free(mem);
        return NULL;
    }

    if (!mem) {
        return g_malloc(n_bytes);
    }

    new_mem = realloc(mem, n_bytes);
    if (!new_mem) {
        z_abort_message("g_realloc(%lu) failed", (unsigned long) n_bytes);
    }

    return new_mem;
}

gpointer g_malloc_n(gsize n_blocks, gsize n_block_bytes)
{
    if (n_blocks == 0 || n_block_bytes == 0) {
        return g_malloc(0);
    }

    return g_malloc(z_checked_mul(n_blocks, n_block_bytes));
}

gpointer g_malloc0_n(gsize n_blocks, gsize n_block_bytes)
{
    if (n_blocks == 0 || n_block_bytes == 0) {
        return g_malloc0(0);
    }

    return g_malloc0(z_checked_mul(n_blocks, n_block_bytes));
}

gpointer g_realloc_n(gpointer mem, gsize n_blocks, gsize n_block_bytes)
{
    if (n_blocks == 0 || n_block_bytes == 0) {
        return g_realloc(mem, 0);
    }

    return g_realloc(mem, z_checked_mul(n_blocks, n_block_bytes));
}

static gboolean z_mul_overflows(gsize a, gsize b)
{
    return a != 0 && b > (G_MAXSIZE / a);
}

gpointer g_try_malloc_n(gsize n_blocks, gsize n_block_bytes)
{
    if (n_blocks == 0 || n_block_bytes == 0) {
        return NULL;
    }
    if (z_mul_overflows(n_blocks, n_block_bytes)) {
        return NULL;
    }

    return g_try_malloc(n_blocks * n_block_bytes);
}

gpointer g_try_malloc0_n(gsize n_blocks, gsize n_block_bytes)
{
    if (n_blocks == 0 || n_block_bytes == 0) {
        return NULL;
    }
    if (z_mul_overflows(n_blocks, n_block_bytes)) {
        return NULL;
    }

    return g_try_malloc0(n_blocks * n_block_bytes);
}

gpointer g_try_realloc_n(gpointer mem, gsize n_blocks, gsize n_block_bytes)
{
    if (n_blocks == 0 || n_block_bytes == 0) {
        free(mem);
        return NULL;
    }
    if (z_mul_overflows(n_blocks, n_block_bytes)) {
        return NULL;
    }

    return g_try_realloc(mem, n_blocks * n_block_bytes);
}

gpointer g_try_malloc(gsize n_bytes)
{
    if (n_bytes == 0) {
        return NULL;
    }

    return malloc(n_bytes);
}

gpointer g_try_malloc0(gsize n_bytes)
{
    if (n_bytes == 0) {
        return NULL;
    }

    return calloc(1, n_bytes);
}

gpointer g_try_realloc(gpointer mem, gsize n_bytes)
{
    if (n_bytes == 0) {
        free(mem);
        return NULL;
    }

    if (!mem) {
        return malloc(n_bytes);
    }

    return realloc(mem, n_bytes);
}

void g_free(gpointer mem)
{
    free(mem);
}

gpointer g_memdup(gconstpointer mem, guint byte_size)
{
    gpointer copy;

    if (!mem || byte_size == 0) {
        return NULL;
    }

    copy = g_malloc(byte_size);
    memcpy(copy, mem, byte_size);
    return copy;
}

gpointer g_memdup2(gconstpointer mem, gsize byte_size)
{
    gpointer copy;

    if (!mem || byte_size == 0) {
        return NULL;
    }

    copy = g_malloc(byte_size);
    memcpy(copy, mem, byte_size);
    return copy;
}

/* ------------------------------------------------------------------ */
/* Assertions and logging                                              */
/* ------------------------------------------------------------------ */

void g_assertion_message_expr(const gchar *domain, const gchar *file,
                              gint line, const gchar *func,
                              const gchar *expr)
{
    (void) domain;

    fprintf(stderr, "ERROR:%s:%d:%s: assertion failed",
            file ? file : "<unknown>", line,
            func ? func : "<unknown>");
    if (expr) {
        fprintf(stderr, ": (%s)", expr);
    }
    fputc('\n', stderr);
    abort();
}

void g_assertion_message(const gchar *domain, const gchar *file, gint line,
                         const gchar *func, const gchar *message)
{
    (void) domain;

    fprintf(stderr, "ERROR:%s:%d:%s: %s\n",
            file ? file : "<unknown>", line,
            func ? func : "<unknown>", message ? message : "assertion failed");
    abort();
}

void g_return_if_fail_warning(const gchar *log_domain,
                              const gchar *pretty_function,
                              const gchar *expression)
{
    fprintf(stderr, "WARNING:%s:%s: %s\n",
            log_domain ? log_domain : "GLib",
            pretty_function ? pretty_function : "<unknown>",
            expression ? expression : "<unknown>");
}

void g_warn_message(const gchar *domain, const gchar *file, gint line,
                    const gchar *func, const gchar *warnexpr)
{
    (void) domain;

    fprintf(stderr, "WARNING:%s:%d:%s: %s\n",
            file ? file : "<unknown>", line,
            func ? func : "<unknown>", warnexpr ? warnexpr : "<unknown>");
}

void g_error(const gchar *format, ...)
{
    va_list ap;

    fputs("ERROR: ", stderr);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
    abort();
}

void g_critical(const gchar *format, ...)
{
    va_list ap;

    fputs("CRITICAL: ", stderr);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void g_warning(const gchar *format, ...)
{
    va_list ap;

    fputs("WARNING: ", stderr);
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void g_message(const gchar *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
}

void g_debug(const gchar *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ------------------------------------------------------------------ */
/* Test-mode helpers (the default non-test semantics)                  */
/* ------------------------------------------------------------------ */

gboolean g_test_slow(void)
{
    return FALSE;
}

gboolean g_test_thorough(void)
{
    return FALSE;
}

gboolean g_test_quick(void)
{
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Program name                                                        */
/* ------------------------------------------------------------------ */

static gchar *z_prgname;

const gchar *g_get_prgname(void)
{
    return z_prgname;
}

void g_set_prgname(const gchar *prgname)
{
    gchar *copy = prgname ? g_strdup(prgname) : NULL;

    g_free(z_prgname);
    z_prgname = copy;
}

/* ------------------------------------------------------------------ */
/* Random numbers (non-cryptographic, reproducible default seed)       */
/* ------------------------------------------------------------------ */

static guint32 z_random_state = 0x9e3779b9u;

static guint32 z_random_next_u32(void)
{
    guint32 x = __atomic_add_fetch(&z_random_state, 0x9e3779b9u,
                                   __ATOMIC_RELAXED);

    /* splitmix32 finalizer */
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

gint32 g_random_int_range(gint32 begin, gint32 end)
{
    guint64 width;
    guint64 limit;
    guint32 r;

    g_return_val_if_fail(end > begin, begin);

    width = (guint64) ((gint64) end - (gint64) begin);
    if (width == 1) {
        return begin;
    }

    /*
     * Rejection sampling over the full uint32 space keeps the result
     * unbiased.  width is at most 2^32 - 1, so the accepted count is a
     * multiple of width.
     */
    limit = (0x100000000ULL / width) * width;
    do {
        r = z_random_next_u32();
    } while ((guint64) r >= limit);

    return (gint32) ((gint64) begin + (gint64) (r % width));
}

/* ------------------------------------------------------------------ */
/* ASCII helpers                                                       */
/* ------------------------------------------------------------------ */

static const guint16 z_ascii_table[256] = {
    0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004,
    0x004, 0x104, 0x104, 0x004, 0x104, 0x104, 0x004, 0x004,
    0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004,
    0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004, 0x004,
    0x140, 0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x0d0,
    0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x0d0,
    0x459, 0x459, 0x459, 0x459, 0x459, 0x459, 0x459, 0x459,
    0x459, 0x459, 0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x0d0,
    0x0d0, 0x653, 0x653, 0x653, 0x653, 0x653, 0x653, 0x253,
    0x253, 0x253, 0x253, 0x253, 0x253, 0x253, 0x253, 0x253,
    0x253, 0x253, 0x253, 0x253, 0x253, 0x253, 0x253, 0x253,
    0x253, 0x253, 0x253, 0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x0d0,
    0x0d0, 0x473, 0x473, 0x473, 0x473, 0x473, 0x473, 0x073,
    0x073, 0x073, 0x073, 0x073, 0x073, 0x073, 0x073, 0x073,
    0x073, 0x073, 0x073, 0x073, 0x073, 0x073, 0x073, 0x073,
    0x073, 0x073, 0x073, 0x0d0, 0x0d0, 0x0d0, 0x0d0, 0x004,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000
};

const guint16 * const g_ascii_table = z_ascii_table;

gchar g_ascii_tolower(gchar c)
{
    if (c >= 'A' && c <= 'Z') {
        return (gchar) (c + ('a' - 'A'));
    }

    return c;
}

gchar g_ascii_toupper(gchar c)
{
    if (c >= 'a' && c <= 'z') {
        return (gchar) (c - ('a' - 'A'));
    }

    return c;
}

gint g_ascii_digit_value(gchar c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }

    return -1;
}

gint g_ascii_xdigit_value(gchar c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }

    return -1;
}

gint g_ascii_strcasecmp(const gchar *s1, const gchar *s2)
{
    if (!s1 || !s2) {
        g_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC,
                                 "s1 != NULL && s2 != NULL");
        return 0;
    }

    for (;;) {
        guchar c1 = (guchar) *s1;
        guchar c2 = (guchar) *s2;

        if (c1 >= 'A' && c1 <= 'Z') {
            c1 = (guchar) (c1 + ('a' - 'A'));
        }
        if (c2 >= 'A' && c2 <= 'Z') {
            c2 = (guchar) (c2 + ('a' - 'A'));
        }
        if (c1 != c2) {
            return (gint) c1 - (gint) c2;
        }
        if (c1 == 0) {
            return 0;
        }
        s1++;
        s2++;
    }
}

gint g_ascii_strncasecmp(const gchar *s1, const gchar *s2, gsize n)
{
    gsize i;

    if (!s1 || !s2) {
        g_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC,
                                 "s1 != NULL && s2 != NULL");
        return 0;
    }

    for (i = 0; i < n; i++) {
        guchar c1 = (guchar) s1[i];
        guchar c2 = (guchar) s2[i];

        if (c1 >= 'A' && c1 <= 'Z') {
            c1 = (guchar) (c1 + ('a' - 'A'));
        }
        if (c2 >= 'A' && c2 <= 'Z') {
            c2 = (guchar) (c2 + ('a' - 'A'));
        }
        if (c1 != c2) {
            return (gint) c1 - (gint) c2;
        }
        if (c1 == 0) {
            return 0;
        }
    }

    return 0;
}

gint64 g_ascii_strtoll(const gchar *nptr, gchar **endptr, guint base)
{
    return strtoll(nptr, endptr, (int) base);
}

/* ------------------------------------------------------------------ */
/* Hash and comparison helpers                                         */
/* ------------------------------------------------------------------ */

guint g_direct_hash(gconstpointer v)
{
    return (guint) (guintptr) v;
}

gboolean g_direct_equal(gconstpointer v1, gconstpointer v2)
{
    return v1 == v2;
}

guint g_str_hash(gconstpointer v)
{
    const signed char *p = (const signed char *) v;
    guint h = 5381;

    while (*p != '\0') {
        h = (h << 5) + h + (guint) (gint) *p;
        p++;
    }

    return h;
}

gboolean g_str_equal(gconstpointer v1, gconstpointer v2)
{
    return strcmp((const gchar *) v1, (const gchar *) v2) == 0;
}

/* ------------------------------------------------------------------ */
/* String helpers                                                      */
/* ------------------------------------------------------------------ */

gchar *g_strdup(const gchar *str)
{
    gchar *copy;
    gsize len;

    if (!str) {
        return NULL;
    }

    len = strlen(str);
    copy = g_malloc(len + 1);
    memcpy(copy, str, len + 1);
    return copy;
}

gchar *g_strndup(const gchar *str, gsize n)
{
    gchar *copy;
    gsize len = 0;

    if (!str) {
        return NULL;
    }

    while (len < n && str[len] != '\0') {
        len++;
    }

    copy = g_malloc(len + 1);
    if (len > 0) {
        memcpy(copy, str, len);
    }
    copy[len] = '\0';
    return copy;
}

gchar *g_strdup_vprintf(const gchar *format, va_list args)
{
    va_list ap;
    gint n;
    gchar *buf;

    if (!format) {
        g_return_val_if_fail(format != NULL, NULL);
    }

    va_copy(ap, args);
    n = vsnprintf(NULL, 0, format, ap);
    va_end(ap);
    if (n < 0) {
        z_abort_message("vsnprintf() failed while formatting a string");
    }

    buf = g_malloc((gsize) n + 1);
    va_copy(ap, args);
    vsnprintf(buf, (gsize) n + 1, format, ap);
    va_end(ap);
    return buf;
}

gchar *g_strdup_printf(const gchar *format, ...)
{
    va_list ap;
    gchar *result;

    va_start(ap, format);
    result = g_strdup_vprintf(format, ap);
    va_end(ap);
    return result;
}

void g_strfreev(gchar **str_array)
{
    guint i;

    if (!str_array) {
        return;
    }

    for (i = 0; str_array[i] != NULL; i++) {
        g_free(str_array[i]);
    }
    g_free(str_array);
}

guint g_strv_length(gchar **str_array)
{
    guint len = 0;

    if (!str_array) {
        return 0;
    }

    while (str_array[len] != NULL) {
        len++;
    }

    return len;
}

gchar **g_strsplit(const gchar *string, const gchar *delimiter,
                   gint max_tokens)
{
    const gchar *s;
    const gchar *next;
    gchar **result;
    guint n_tokens = 0;
    guint alloc_tokens;
    gsize delimiter_len;

    g_return_val_if_fail(string != NULL, NULL);
    g_return_val_if_fail(delimiter != NULL, NULL);
    g_return_val_if_fail(delimiter[0] != '\0', NULL);

    if (string[0] == '\0') {
        result = g_new0(gchar *, 1);
        return result;
    }

    delimiter_len = strlen(delimiter);
    if (max_tokens == 1) {
        result = g_new0(gchar *, 2);
        result[0] = g_strdup(string);
        return result;
    }

    alloc_tokens = 4;
    result = g_new(gchar *, alloc_tokens + 1);

    s = string;
    for (;;) {
        if (max_tokens > 0 && n_tokens == (guint) max_tokens - 1) {
            next = s + strlen(s);
        } else {
            next = strstr(s, delimiter);
            if (!next) {
                next = s + strlen(s);
            }
        }

        if (n_tokens == alloc_tokens) {
            if (alloc_tokens > (G_MAXUINT / 2)) {
                z_abort_message("g_strsplit() token count overflow");
            }
            alloc_tokens *= 2;
            result = g_renew(gchar *, result, alloc_tokens + 1);
        }

        result[n_tokens] = g_strndup(s, (gsize) (next - s));
        n_tokens++;

        if (*next == '\0') {
            break;
        }
        s = next + delimiter_len;
    }

    result[n_tokens] = NULL;
    return result;
}

gchar *g_strjoinv(const gchar *separator, gchar **str_array)
{
    gsize sep_len = 0;
    gsize total = 0;
    guint i;
    gchar *result;
    gchar *p;

    if (!separator) {
        separator = "";
    }
    sep_len = strlen(separator);

    if (!str_array) {
        return g_strdup("");
    }

    for (i = 0; str_array[i] != NULL; i++) {
        total = z_checked_add(total, strlen(str_array[i]));
        if (str_array[i + 1] != NULL) {
            total = z_checked_add(total, sep_len);
        }
    }

    result = g_malloc(z_checked_add(total, 1));
    p = result;
    for (i = 0; str_array[i] != NULL; i++) {
        gsize len = strlen(str_array[i]);

        if (len > 0) {
            memcpy(p, str_array[i], len);
            p += len;
        }
        if (str_array[i + 1] != NULL) {
            if (sep_len > 0) {
                memcpy(p, separator, sep_len);
                p += sep_len;
            }
        }
    }
    *p = '\0';
    return result;
}

gchar *g_strconcat(const gchar *string1, ...)
{
    va_list ap;
    const gchar *string;
    gsize total;
    gchar *result;
    gchar *p;

    if (!string1) {
        return NULL;
    }

    total = strlen(string1);
    va_start(ap, string1);
    while ((string = va_arg(ap, const gchar *)) != NULL) {
        total = z_checked_add(total, strlen(string));
    }
    va_end(ap);

    total = z_checked_add(total, 1);
    result = g_malloc(total);
    p = result;

    memcpy(p, string1, strlen(string1));
    p += strlen(string1);

    va_start(ap, string1);
    while ((string = va_arg(ap, const gchar *)) != NULL) {
        gsize len = strlen(string);

        if (len > 0) {
            memcpy(p, string, len);
            p += len;
        }
    }
    va_end(ap);

    *p = '\0';
    return result;
}

gsize g_strlcpy(gchar *dest, const gchar *src, gsize dest_size)
{
    gsize src_len = strlen(src);

    if (dest_size > 0) {
        gsize copy_len = src_len < (dest_size - 1) ? src_len : (dest_size - 1);

        if (copy_len > 0) {
            memcpy(dest, src, copy_len);
        }
        dest[copy_len] = '\0';
    }

    return src_len;
}

gboolean g_str_has_prefix(const gchar *str, const gchar *prefix)
{
    gsize prefix_len;

    if (!str || !prefix) {
        return FALSE;
    }

    prefix_len = strlen(prefix);
    return strncmp(str, prefix, prefix_len) == 0;
}

gboolean g_str_has_suffix(const gchar *str, const gchar *suffix)
{
    gsize str_len;
    gsize suffix_len;

    if (!str || !suffix) {
        return FALSE;
    }

    str_len = strlen(str);
    suffix_len = strlen(suffix);
    if (suffix_len > str_len) {
        return FALSE;
    }

    return strcmp(str + (str_len - suffix_len), suffix) == 0;
}

gint g_strcmp0(const gchar *str1, const gchar *str2)
{
    if (str1 == str2) {
        return 0;
    }
    if (!str1) {
        return -1;
    }
    if (!str2) {
        return 1;
    }

    return strcmp(str1, str2);
}

gchar *g_path_get_dirname(const gchar *file_name)
{
    const gchar *last_sep;
    gsize len;

    if (!file_name) {
        return NULL;
    }

    last_sep = strrchr(file_name, G_DIR_SEPARATOR);
    if (!last_sep) {
        return g_strdup(".");
    }

    len = (gsize) (last_sep - file_name);
    while (len > 1 && file_name[len - 1] == G_DIR_SEPARATOR) {
        len--;
    }
    if (len == 0) {
        return g_strdup(G_DIR_SEPARATOR_S);
    }

    return g_strndup(file_name, len);
}

const gchar *g_strerror(gint errnum)
{
    return strerror(errnum);
}

/* ------------------------------------------------------------------ */
/* GArray                                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    GArray pub;
    guint alloc;
    guint element_size;
    guint ref_count;
    guint zero_terminated;
    guint clear;
    GDestroyNotify clear_func;
} ZGArray;

static ZGArray *z_array(GArray *array)
{
    return (ZGArray *) array;
}

static gsize z_array_total_size(const ZGArray *array, guint alloc)
{
    gsize total_elems = (gsize) alloc +
                        (array->zero_terminated ? (gsize) 1 : (gsize) 0);

    return z_checked_mul(total_elems, array->element_size);
}

static void z_array_set_terminator(ZGArray *array)
{
    if (array->zero_terminated) {
        gchar *term = array->pub.data +
                      ((gsize) array->pub.len * array->element_size);

        memset(term, 0, array->element_size);
    }
}

static void z_array_grow(ZGArray *array, guint need)
{
    guint new_alloc;
    gsize old_size;
    gsize new_size;

    if (need <= array->alloc) {
        return;
    }

    new_alloc = array->alloc ? array->alloc : 8;
    while (new_alloc < need) {
        if (new_alloc > (G_MAXUINT / 2)) {
            new_alloc = need;
            break;
        }
        new_alloc *= 2;
    }

    old_size = z_array_total_size(array, array->alloc);
    new_size = z_array_total_size(array, new_alloc);
    array->pub.data = g_realloc(array->pub.data, new_size);
    if (array->clear && new_size > old_size) {
        memset(array->pub.data + old_size, 0, new_size - old_size);
    }
    array->alloc = new_alloc;
    z_array_set_terminator(array);
}

GArray *g_array_new(gboolean zero_terminated, gboolean clear_,
                    guint element_size)
{
    ZGArray *array;

    if (element_size == 0) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "element_size > 0");
    }

    array = g_malloc0(sizeof(*array));
    array->pub.data = NULL;
    array->pub.len = 0;
    array->alloc = 0;
    array->element_size = element_size;
    array->ref_count = 1;
    array->zero_terminated = zero_terminated ? 1u : 0u;
    array->clear = clear_ ? 1u : 0u;
    array->clear_func = NULL;

    if (zero_terminated) {
        array->pub.data = g_malloc0(element_size);
    }

    return &array->pub;
}

GArray *g_array_sized_new(gboolean zero_terminated, gboolean clear_,
                          guint element_size, guint reserved_size)
{
    ZGArray *array;
    gsize total;

    if (element_size == 0) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "element_size > 0");
    }

    array = g_malloc0(sizeof(*array));
    array->pub.len = 0;
    array->alloc = reserved_size;
    array->element_size = element_size;
    array->ref_count = 1;
    array->zero_terminated = zero_terminated ? 1u : 0u;
    array->clear = clear_ ? 1u : 0u;
    array->clear_func = NULL;

    total = z_array_total_size(array, reserved_size);
    if (total > 0) {
        array->pub.data = clear_ ? g_malloc0(total) : g_malloc(total);
    } else {
        array->pub.data = NULL;
    }
    z_array_set_terminator(array);

    return &array->pub;
}

GArray *g_array_append_vals(GArray *array, gconstpointer data, guint len)
{
    ZGArray *a = z_array(array);
    gsize offset;
    gsize bytes;

    if (len == 0) {
        return array;
    }

    if (len > (G_MAXUINT - array->len)) {
        z_abort_message("GArray length overflow");
    }

    z_array_grow(a, array->len + len);
    offset = (gsize) array->len * a->element_size;
    bytes = (gsize) len * a->element_size;
    memmove(array->data + offset, data, bytes);
    array->len += len;
    z_array_set_terminator(a);
    return array;
}

GArray *g_array_prepend_vals(GArray *array, gconstpointer data, guint len)
{
    return g_array_insert_vals(array, 0, data, len);
}

GArray *g_array_insert_vals(GArray *array, guint index_, gconstpointer data,
                            guint len)
{
    ZGArray *a = z_array(array);
    gsize offset;
    gsize bytes;
    gsize tail_bytes;

    if (index_ > array->len) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "index_ <= array->len");
    }
    if (len == 0) {
        return array;
    }
    if (len > (G_MAXUINT - array->len)) {
        z_abort_message("GArray length overflow");
    }

    z_array_grow(a, array->len + len);
    offset = (gsize) index_ * a->element_size;
    bytes = (gsize) len * a->element_size;
    tail_bytes = ((gsize) array->len - index_) * a->element_size;

    memmove(array->data + offset + bytes, array->data + offset, tail_bytes);
    memmove(array->data + offset, data, bytes);
    array->len += len;
    z_array_set_terminator(a);
    return array;
}

GArray *g_array_set_size(GArray *array, guint length)
{
    ZGArray *a = z_array(array);

    if (length > array->len) {
        z_array_grow(a, length);
    } else if (length < array->len && a->clear_func) {
        guint i;

        for (i = length; i < array->len; i++) {
            a->clear_func(array->data + ((gsize) i * a->element_size));
        }
    }
    array->len = length;
    z_array_set_terminator(a);
    return array;
}

GArray *g_array_remove_index(GArray *array, guint index_)
{
    ZGArray *a = z_array(array);
    gsize offset;
    gsize tail_bytes;

    if (index_ >= array->len) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "index_ < array->len");
    }

    if (a->clear_func) {
        a->clear_func(array->data + ((gsize) index_ * a->element_size));
    }

    offset = (gsize) index_ * a->element_size;
    tail_bytes = ((gsize) array->len - index_ - 1) * a->element_size;
    memmove(array->data + offset, array->data + offset + a->element_size,
            tail_bytes);
    array->len--;
    z_array_set_terminator(a);
    return array;
}

GArray *g_array_remove_index_fast(GArray *array, guint index_)
{
    ZGArray *a = z_array(array);

    if (index_ >= array->len) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "index_ < array->len");
    }

    if (a->clear_func) {
        a->clear_func(array->data + ((gsize) index_ * a->element_size));
    }

    if (index_ != (array->len - 1)) {
        memmove(array->data + ((gsize) index_ * a->element_size),
                array->data + ((gsize) (array->len - 1) * a->element_size),
                a->element_size);
    }
    array->len--;
    z_array_set_terminator(a);
    return array;
}

GArray *g_array_remove_range(GArray *array, guint index_, guint length)
{
    ZGArray *a = z_array(array);
    gsize offset;
    gsize move_bytes;

    if (index_ > array->len || length > (array->len - index_)) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "range is within the array");
    }
    if (length == 0) {
        return array;
    }

    if (a->clear_func) {
        guint i;

        for (i = 0; i < length; i++) {
            a->clear_func(array->data +
                          ((gsize) (index_ + i) * a->element_size));
        }
    }

    offset = (gsize) index_ * a->element_size;
    move_bytes = ((gsize) array->len - index_ - length) * a->element_size;
    memmove(array->data + offset, array->data + offset +
            ((gsize) length * a->element_size), move_bytes);
    array->len -= length;
    z_array_set_terminator(a);
    return array;
}

gchar *g_array_free(GArray *array, gboolean free_segment)
{
    ZGArray *a = z_array(array);
    gchar *data = NULL;
    gboolean last_ref = z_ref_dec_and_test(&a->ref_count);

    if (free_segment) {
        if (a->clear_func) {
            guint i;

            for (i = 0; i < array->len; i++) {
                a->clear_func(array->data + ((gsize) i * a->element_size));
            }
        }
        g_free(array->data);
    } else {
        data = array->data;
    }

    array->data = NULL;
    array->len = 0;
    a->alloc = 0;

    if (last_ref) {
        g_free(a);
    }

    return data;
}

GArray *g_array_ref(GArray *array)
{
    z_ref_inc(&z_array(array)->ref_count);
    return array;
}

void g_array_unref(GArray *array)
{
    ZGArray *a = z_array(array);

    if (z_ref_dec_and_test(&a->ref_count)) {
        if (a->clear_func) {
            guint i;

            for (i = 0; i < array->len; i++) {
                a->clear_func(array->data + ((gsize) i * a->element_size));
            }
        }
        g_free(a->pub.data);
        g_free(a);
    }
}

guint g_array_get_element_size(GArray *array)
{
    return z_array(array)->element_size;
}

void g_array_sort(GArray *array, GCompareFunc compare_func)
{
    ZGArray *a = z_array(array);

    qsort(array->data, array->len, a->element_size, compare_func);
}

void g_array_sort_with_data(GArray *array, GCompareDataFunc compare_func,
                            gpointer user_data)
{
    (void) array;
    (void) compare_func;
    (void) user_data;
    g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                             "g_array_sort_with_data() is not implemented");
}

void g_array_set_clear_func(GArray *array, GDestroyNotify clear_func)
{
    z_array(array)->clear_func = clear_func;
}

/* ------------------------------------------------------------------ */
/* GPtrArray                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    GPtrArray pub;
    guint alloc;
    guint ref_count;
    GDestroyNotify free_func;
} ZGPtrArray;

static ZGPtrArray *z_ptr_array(GPtrArray *array)
{
    return (ZGPtrArray *) array;
}

static void z_ptr_array_grow(ZGPtrArray *array, guint need)
{
    guint new_alloc;

    if (need <= array->alloc) {
        return;
    }

    new_alloc = array->alloc ? array->alloc : 8;
    while (new_alloc < need) {
        if (new_alloc > (G_MAXUINT / 2)) {
            new_alloc = need;
            break;
        }
        new_alloc *= 2;
    }

    array->pub.pdata = g_realloc(array->pub.pdata,
                                 z_checked_mul(new_alloc, sizeof(gpointer)));
    array->alloc = new_alloc;
}

static void z_ptr_array_free_element(ZGPtrArray *array, gpointer element)
{
    if (array->free_func) {
        array->free_func(element);
    }
}

GPtrArray *g_ptr_array_new(void)
{
    ZGPtrArray *array = g_malloc0(sizeof(*array));

    array->ref_count = 1;
    return &array->pub;
}

GPtrArray *g_ptr_array_new_with_free_func(GDestroyNotify element_free_func)
{
    GPtrArray *array = g_ptr_array_new();

    g_ptr_array_set_free_func(array, element_free_func);
    return array;
}

GPtrArray *g_ptr_array_sized_new(guint reserved_size)
{
    ZGPtrArray *array = g_malloc0(sizeof(*array));

    array->ref_count = 1;
    if (reserved_size > 0) {
        array->pub.pdata = g_malloc0(z_checked_mul(reserved_size,
                                                   sizeof(gpointer)));
    }
    array->alloc = reserved_size;
    return &array->pub;
}

GPtrArray *g_ptr_array_new_full(guint reserved_size,
                                GDestroyNotify element_free_func)
{
    GPtrArray *array = g_ptr_array_sized_new(reserved_size);

    g_ptr_array_set_free_func(array, element_free_func);
    return array;
}

gpointer *g_ptr_array_free(GPtrArray *array, gboolean free_segment)
{
    ZGPtrArray *a = z_ptr_array(array);
    gpointer *pdata = NULL;
    gboolean last_ref = z_ref_dec_and_test(&a->ref_count);
    guint i;

    if (free_segment) {
        for (i = 0; i < array->len; i++) {
            z_ptr_array_free_element(a, array->pdata[i]);
        }
        g_free(array->pdata);
    } else {
        pdata = array->pdata;
    }

    array->pdata = NULL;
    array->len = 0;
    a->alloc = 0;

    if (last_ref) {
        g_free(a);
    }

    return pdata;
}

GPtrArray *g_ptr_array_ref(GPtrArray *array)
{
    z_ref_inc(&z_ptr_array(array)->ref_count);
    return array;
}

void g_ptr_array_unref(GPtrArray *array)
{
    ZGPtrArray *a = z_ptr_array(array);

    if (z_ref_dec_and_test(&a->ref_count)) {
        guint i;

        for (i = 0; i < array->len; i++) {
            z_ptr_array_free_element(a, array->pdata[i]);
        }
        g_free(array->pdata);
        g_free(a);
    }
}

void g_ptr_array_set_free_func(GPtrArray *array,
                               GDestroyNotify element_free_func)
{
    z_ptr_array(array)->free_func = element_free_func;
}

void g_ptr_array_set_size(GPtrArray *array, gint length)
{
    ZGPtrArray *a = z_ptr_array(array);
    guint new_len;
    guint i;

    if (length < 0) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "length >= 0");
    }

    new_len = (guint) length;
    if (new_len < array->len) {
        for (i = new_len; i < array->len; i++) {
            z_ptr_array_free_element(a, array->pdata[i]);
        }
    } else if (new_len > array->len) {
        z_ptr_array_grow(a, new_len);
        for (i = array->len; i < new_len; i++) {
            array->pdata[i] = NULL;
        }
    }
    array->len = new_len;
}

gpointer g_ptr_array_remove_index(GPtrArray *array, guint index_)
{
    ZGPtrArray *a = z_ptr_array(array);
    gpointer element;
    gsize tail_bytes;

    if (index_ >= array->len) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "index_ < array->len");
    }

    element = array->pdata[index_];
    z_ptr_array_free_element(a, element);
    tail_bytes = ((gsize) array->len - index_ - 1) * sizeof(gpointer);
    memmove(array->pdata + index_, array->pdata + index_ + 1, tail_bytes);
    array->len--;
    return element;
}

gpointer g_ptr_array_remove_index_fast(GPtrArray *array, guint index_)
{
    ZGPtrArray *a = z_ptr_array(array);
    gpointer element;

    if (index_ >= array->len) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "index_ < array->len");
    }

    element = array->pdata[index_];
    z_ptr_array_free_element(a, element);
    if (index_ != (array->len - 1)) {
        array->pdata[index_] = array->pdata[array->len - 1];
    }
    array->len--;
    return element;
}

gboolean g_ptr_array_remove(GPtrArray *array, gpointer data)
{
    ZGPtrArray *a = z_ptr_array(array);
    guint i;

    for (i = 0; i < array->len; i++) {
        if (array->pdata[i] == data) {
            z_ptr_array_free_element(a, array->pdata[i]);
            memmove(array->pdata + i, array->pdata + i + 1,
                    ((gsize) array->len - i - 1) * sizeof(gpointer));
            array->len--;
            return TRUE;
        }
    }

    return FALSE;
}

gboolean g_ptr_array_remove_fast(GPtrArray *array, gpointer data)
{
    ZGPtrArray *a = z_ptr_array(array);
    guint i;

    for (i = 0; i < array->len; i++) {
        if (array->pdata[i] == data) {
            z_ptr_array_free_element(a, array->pdata[i]);
            array->pdata[i] = array->pdata[array->len - 1];
            array->len--;
            return TRUE;
        }
    }

    return FALSE;
}

GPtrArray *g_ptr_array_remove_range(GPtrArray *array, guint index_,
                                    guint length)
{
    ZGPtrArray *a = z_ptr_array(array);
    guint i;
    gsize tail_bytes;

    if (index_ > array->len || length > (array->len - index_)) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "range is within the array");
    }

    for (i = 0; i < length; i++) {
        z_ptr_array_free_element(a, array->pdata[index_ + i]);
    }
    tail_bytes = ((gsize) array->len - index_ - length) * sizeof(gpointer);
    memmove(array->pdata + index_, array->pdata + index_ + length, tail_bytes);
    array->len -= length;
    return array;
}

void g_ptr_array_add(GPtrArray *array, gpointer data)
{
    ZGPtrArray *a = z_ptr_array(array);

    z_ptr_array_grow(a, array->len + 1);
    array->pdata[array->len++] = data;
}

void g_ptr_array_insert(GPtrArray *array, gint index_, gpointer data)
{
    ZGPtrArray *a = z_ptr_array(array);
    guint index;

    if (index_ < 0) {
        index = array->len;
    } else {
        index = (guint) index_;
    }
    if (index > array->len) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "index <= array->len");
    }

    z_ptr_array_grow(a, array->len + 1);
    memmove(array->pdata + index + 1, array->pdata + index,
            ((gsize) array->len - index) * sizeof(gpointer));
    array->pdata[index] = data;
    array->len++;
}

void g_ptr_array_sort(GPtrArray *array, GCompareFunc compare_func)
{
    qsort(array->pdata, array->len, sizeof(gpointer), compare_func);
}

void g_ptr_array_sort_with_data(GPtrArray *array,
                                GCompareDataFunc compare_func,
                                gpointer user_data)
{
    (void) array;
    (void) compare_func;
    (void) user_data;
    g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                             "g_ptr_array_sort_with_data() is not implemented");
}

void g_ptr_array_foreach(GPtrArray *array, GFunc func, gpointer user_data)
{
    guint i;

    for (i = 0; i < array->len; i++) {
        func(array->pdata[i], user_data);
    }
}

/* ------------------------------------------------------------------ */
/* GByteArray                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    GByteArray pub;
    guint alloc;
    guint ref_count;
} ZByteArray;

static ZByteArray *z_byte_array(GByteArray *array)
{
    return (ZByteArray *) array;
}

static void z_byte_array_grow(ZByteArray *array, guint need)
{
    guint new_alloc;

    if (need <= array->alloc) {
        return;
    }

    new_alloc = array->alloc ? array->alloc : 8;
    while (new_alloc < need) {
        if (new_alloc > (G_MAXUINT / 2)) {
            new_alloc = need;
            break;
        }
        new_alloc *= 2;
    }

    array->pub.data = g_realloc(array->pub.data, new_alloc);
    array->alloc = new_alloc;
}

GByteArray *g_byte_array_new(void)
{
    ZByteArray *array = g_malloc0(sizeof(*array));

    array->ref_count = 1;
    return &array->pub;
}

GByteArray *g_byte_array_sized_new(guint reserved_size)
{
    ZByteArray *array = g_malloc0(sizeof(*array));

    array->ref_count = 1;
    array->alloc = reserved_size;
    if (reserved_size > 0) {
        array->pub.data = g_malloc(reserved_size);
    }
    return &array->pub;
}

GByteArray *g_byte_array_append(GByteArray *array, const guint8 *data,
                                guint len)
{
    ZByteArray *a = z_byte_array(array);

    if (len == 0) {
        return array;
    }
    if (len > (G_MAXUINT - array->len)) {
        z_abort_message("GByteArray length overflow");
    }

    z_byte_array_grow(a, array->len + len);
    memmove(array->data + array->len, data, len);
    array->len += len;
    return array;
}

GByteArray *g_byte_array_set_size(GByteArray *array, guint length)
{
    ZByteArray *a = z_byte_array(array);

    if (length > array->len) {
        z_byte_array_grow(a, length);
    }
    array->len = length;
    return array;
}

guint8 *g_byte_array_free(GByteArray *array, gboolean free_segment)
{
    ZByteArray *a = z_byte_array(array);
    guint8 *data = NULL;
    gboolean last_ref = z_ref_dec_and_test(&a->ref_count);

    if (free_segment) {
        g_free(array->data);
    } else {
        data = array->data;
    }

    array->data = NULL;
    array->len = 0;
    a->alloc = 0;

    if (last_ref) {
        g_free(a);
    }

    return data;
}

GByteArray *g_byte_array_ref(GByteArray *array)
{
    z_ref_inc(&z_byte_array(array)->ref_count);
    return array;
}

void g_byte_array_unref(GByteArray *array)
{
    ZByteArray *a = z_byte_array(array);

    if (z_ref_dec_and_test(&a->ref_count)) {
        g_free(a->pub.data);
        g_free(a);
    }
}

/* ------------------------------------------------------------------ */
/* GHashTable                                                          */
/* ------------------------------------------------------------------ */

typedef struct ZHashNode {
    gpointer key;
    gpointer value;
    guint hash;
    gsize bucket;
    struct ZHashNode *next_bucket;
    struct ZHashNode *prev_all;
    struct ZHashNode *next_all;
} ZHashNode;

struct _GHashTable {
    GHashFunc hash_func;
    GEqualFunc equal_func;
    GDestroyNotify key_destroy_func;
    GDestroyNotify value_destroy_func;
    ZHashNode **buckets;
    gsize n_buckets;
    gsize n_nodes;
    guint version;
    guint ref_count;
    ZHashNode *head;
    ZHashNode *tail;
};

static ZHashNode *z_hash_lookup_node(GHashTable *table, gconstpointer key)
{
    guint hash = table->hash_func(key);
    gsize bucket = (gsize) hash % table->n_buckets;
    ZHashNode *node;

    for (node = table->buckets[bucket]; node; node = node->next_bucket) {
        if (node->hash == hash && table->equal_func(node->key, key)) {
            return node;
        }
    }

    return NULL;
}

static void z_hash_table_rehash(GHashTable *table, gsize new_n_buckets)
{
    ZHashNode **new_buckets;
    ZHashNode *node;

    new_buckets = g_malloc0(z_checked_mul(new_n_buckets,
                                          sizeof(ZHashNode *)));
    for (node = table->head; node; node = node->next_all) {
        gsize bucket = (gsize) node->hash % new_n_buckets;

        node->bucket = bucket;
        node->next_bucket = new_buckets[bucket];
        new_buckets[bucket] = node;
    }

    g_free(table->buckets);
    table->buckets = new_buckets;
    table->n_buckets = new_n_buckets;
}

static void z_hash_maybe_rehash(GHashTable *table)
{
    if (table->n_nodes > (table->n_buckets * 3 / 4)) {
        z_hash_table_rehash(table, table->n_buckets * 2);
    }
}

static void z_hash_link_node(GHashTable *table, ZHashNode *node)
{
    gsize bucket = (gsize) node->hash % table->n_buckets;

    node->bucket = bucket;
    node->next_bucket = table->buckets[bucket];
    table->buckets[bucket] = node;

    node->prev_all = table->tail;
    node->next_all = NULL;
    if (table->tail) {
        table->tail->next_all = node;
    } else {
        table->head = node;
    }
    table->tail = node;
    table->n_nodes++;
}

static void z_hash_unlink_node(GHashTable *table, ZHashNode *node)
{
    ZHashNode **link;

    for (link = &table->buckets[node->bucket]; *link; link = &(*link)->next_bucket) {
        if (*link == node) {
            *link = node->next_bucket;
            break;
        }
    }

    if (node->prev_all) {
        node->prev_all->next_all = node->next_all;
    } else {
        table->head = node->next_all;
    }
    if (node->next_all) {
        node->next_all->prev_all = node->prev_all;
    } else {
        table->tail = node->prev_all;
    }

    table->n_nodes--;
}

static void z_hash_destroy_node(GHashTable *table, ZHashNode *node)
{
    if (table->key_destroy_func) {
        table->key_destroy_func(node->key);
    }
    if (table->value_destroy_func) {
        table->value_destroy_func(node->value);
    }
    g_free(node);
}

static ZHashNode *z_hash_find_or_create(GHashTable *table, gpointer key)
{
    ZHashNode *node = z_hash_lookup_node(table, key);

    if (node) {
        return node;
    }

    node = g_malloc0(sizeof(*node));
    node->key = key;
    node->hash = table->hash_func(key);
    z_hash_link_node(table, node);
    z_hash_maybe_rehash(table);
    return node;
}

GHashTable *g_hash_table_new(GHashFunc hash_func, GEqualFunc key_equal_func)
{
    return g_hash_table_new_full(hash_func, key_equal_func, NULL, NULL);
}

GHashTable *g_hash_table_new_full(GHashFunc hash_func,
                                  GEqualFunc key_equal_func,
                                  GDestroyNotify key_destroy_func,
                                  GDestroyNotify value_destroy_func)
{
    GHashTable *table = g_malloc0(sizeof(*table));

    table->hash_func = hash_func ? hash_func : g_direct_hash;
    table->equal_func = key_equal_func ? key_equal_func : g_direct_equal;
    table->key_destroy_func = key_destroy_func;
    table->value_destroy_func = value_destroy_func;
    table->n_buckets = 8;
    table->buckets = g_malloc0(z_checked_mul(table->n_buckets,
                                             sizeof(ZHashNode *)));
    table->ref_count = 1;
    return table;
}

GHashTable *g_hash_table_ref(GHashTable *hash_table)
{
    z_ref_inc(&hash_table->ref_count);
    return hash_table;
}

void g_hash_table_unref(GHashTable *hash_table)
{
    ZHashNode *node;

    if (!z_ref_dec_and_test(&hash_table->ref_count)) {
        return;
    }

    node = hash_table->head;
    while (node) {
        ZHashNode *next = node->next_all;

        z_hash_destroy_node(hash_table, node);
        node = next;
    }
    g_free(hash_table->buckets);
    g_free(hash_table);
}

void g_hash_table_destroy(GHashTable *hash_table)
{
    g_hash_table_remove_all(hash_table);
    g_hash_table_unref(hash_table);
}

gboolean g_hash_table_insert(GHashTable *hash_table, gpointer key,
                             gpointer value)
{
    ZHashNode *node = z_hash_lookup_node(hash_table, key);

    if (node) {
        if (hash_table->key_destroy_func) {
            hash_table->key_destroy_func(key);
        }
        if (hash_table->value_destroy_func) {
            hash_table->value_destroy_func(node->value);
        }
        node->value = value;
        hash_table->version++;
        return FALSE;
    }

    node = z_hash_find_or_create(hash_table, key);
    node->value = value;
    hash_table->version++;
    return TRUE;
}

gboolean g_hash_table_replace(GHashTable *hash_table, gpointer key,
                              gpointer value)
{
    ZHashNode *node = z_hash_lookup_node(hash_table, key);

    if (node) {
        if (hash_table->key_destroy_func) {
            hash_table->key_destroy_func(node->key);
        }
        if (hash_table->value_destroy_func) {
            hash_table->value_destroy_func(node->value);
        }
        node->key = key;
        node->hash = hash_table->hash_func(key);
        node->value = value;
        hash_table->version++;
        return FALSE;
    }

    node = z_hash_find_or_create(hash_table, key);
    node->value = value;
    hash_table->version++;
    return TRUE;
}

gboolean g_hash_table_add(GHashTable *hash_table, gpointer key)
{
    return g_hash_table_replace(hash_table, key, key);
}

gboolean g_hash_table_remove(GHashTable *hash_table, gconstpointer key)
{
    ZHashNode *node = z_hash_lookup_node(hash_table, key);

    if (!node) {
        return FALSE;
    }

    z_hash_unlink_node(hash_table, node);
    z_hash_destroy_node(hash_table, node);
    hash_table->version++;
    return TRUE;
}

void g_hash_table_remove_all(GHashTable *hash_table)
{
    ZHashNode *node = hash_table->head;

    while (node) {
        ZHashNode *next = node->next_all;

        z_hash_destroy_node(hash_table, node);
        node = next;
    }
    memset(hash_table->buckets, 0,
           z_checked_mul(hash_table->n_buckets, sizeof(ZHashNode *)));
    hash_table->head = NULL;
    hash_table->tail = NULL;
    hash_table->n_nodes = 0;
    hash_table->version++;
}

gpointer g_hash_table_lookup(GHashTable *hash_table, gconstpointer key)
{
    ZHashNode *node = z_hash_lookup_node(hash_table, key);

    return node ? node->value : NULL;
}

gboolean g_hash_table_contains(GHashTable *hash_table, gconstpointer key)
{
    return z_hash_lookup_node(hash_table, key) != NULL;
}

gboolean g_hash_table_lookup_extended(GHashTable *hash_table,
                                      gconstpointer lookup_key,
                                      gpointer *orig_key,
                                      gpointer *value)
{
    ZHashNode *node = z_hash_lookup_node(hash_table, lookup_key);

    if (!node) {
        return FALSE;
    }
    if (orig_key) {
        *orig_key = node->key;
    }
    if (value) {
        *value = node->value;
    }
    return TRUE;
}

void g_hash_table_foreach(GHashTable *hash_table, GHFunc func,
                          gpointer user_data)
{
    ZHashNode *node = hash_table->head;
    guint version = hash_table->version;

    while (node) {
        ZHashNode *next = node->next_all;

        func(node->key, node->value, user_data);
        if (hash_table->version != version) {
            g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__,
                                     G_STRFUNC,
                                     "hash table modified during foreach");
        }
        node = next;
    }
}

gpointer g_hash_table_find(GHashTable *hash_table, GHRFunc predicate,
                           gpointer user_data)
{
    ZHashNode *node = hash_table->head;
    guint version = hash_table->version;

    while (node) {
        if (predicate(node->key, node->value, user_data)) {
            return node->value;
        }
        if (hash_table->version != version) {
            g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__,
                                     G_STRFUNC,
                                     "hash table modified during find");
        }
        node = node->next_all;
    }

    return NULL;
}

guint g_hash_table_foreach_remove(GHashTable *hash_table, GHRFunc func,
                                  gpointer user_data)
{
    ZHashNode *node = hash_table->head;
    guint removed = 0;

    while (node) {
        ZHashNode *next = node->next_all;

        if (func(node->key, node->value, user_data)) {
            z_hash_unlink_node(hash_table, node);
            z_hash_destroy_node(hash_table, node);
            removed++;
            hash_table->version++;
        }
        node = next;
    }

    return removed;
}

guint g_hash_table_size(GHashTable *hash_table)
{
    return (guint) hash_table->n_nodes;
}

void g_hash_table_iter_init(GHashTableIter *iter, GHashTable *hash_table)
{
    iter->table = hash_table;
    iter->current = NULL;
    iter->next = hash_table->head;
    iter->version = hash_table->version;
    iter->current_valid = FALSE;
}

gboolean g_hash_table_iter_next(GHashTableIter *iter, gpointer *key,
                                gpointer *value)
{
    ZHashNode *node;

    if (iter->version != iter->table->version) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "hash table modified during iteration");
    }

    node = (ZHashNode *) iter->next;
    if (!node) {
        iter->current = NULL;
        iter->current_valid = FALSE;
        return FALSE;
    }

    iter->current = node;
    iter->next = node->next_all;
    iter->current_valid = TRUE;

    if (key) {
        *key = node->key;
    }
    if (value) {
        *value = node->value;
    }
    return TRUE;
}

GHashTable *g_hash_table_iter_get_hash_table(GHashTableIter *iter)
{
    return iter->table;
}

void g_hash_table_iter_remove(GHashTableIter *iter)
{
    ZHashNode *node = (ZHashNode *) iter->current;

    if (!iter->current_valid || !node) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "iterator points at a live entry");
    }

    z_hash_unlink_node(iter->table, node);
    z_hash_destroy_node(iter->table, node);
    iter->current = NULL;
    iter->current_valid = FALSE;
}

void g_hash_table_iter_replace(GHashTableIter *iter, gpointer value)
{
    ZHashNode *node = (ZHashNode *) iter->current;

    if (!iter->current_valid || !node) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "iterator points at a live entry");
    }

    if (iter->table->value_destroy_func) {
        iter->table->value_destroy_func(node->value);
    }
    node->value = value;
}

void g_hash_table_iter_steal(GHashTableIter *iter)
{
    ZHashNode *node = (ZHashNode *) iter->current;

    if (!iter->current_valid || !node) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "iterator points at a live entry");
    }

    z_hash_unlink_node(iter->table, node);
    g_free(node);
    iter->current = NULL;
    iter->current_valid = FALSE;
}

/* ------------------------------------------------------------------ */
/* GSList                                                              */
/* ------------------------------------------------------------------ */

GSList *g_slist_alloc(void)
{
    return g_malloc0(sizeof(GSList));
}

void g_slist_free(GSList *list)
{
    while (list) {
        GSList *next = list->next;

        g_free(list);
        list = next;
    }
}

void g_slist_free_1(GSList *list)
{
    g_free(list);
}

void g_slist_free_full(GSList *list, GDestroyNotify free_func)
{
    while (list) {
        GSList *next = list->next;

        if (free_func) {
            free_func(list->data);
        }
        g_free(list);
        list = next;
    }
}

GSList *g_slist_append(GSList *list, gpointer data)
{
    GSList *node = g_slist_alloc();
    GSList *last;

    node->data = data;
    node->next = NULL;

    if (!list) {
        return node;
    }

    for (last = list; last->next; last = last->next) {
    }
    last->next = node;
    return list;
}

GSList *g_slist_prepend(GSList *list, gpointer data)
{
    GSList *node = g_slist_alloc();

    node->data = data;
    node->next = list;
    return node;
}

GSList *g_slist_insert(GSList *list, gpointer data, gint position)
{
    GSList *node;
    GSList *iter;
    gint i;

    if (position <= 0 || !list) {
        return g_slist_prepend(list, data);
    }

    node = g_slist_alloc();
    node->data = data;
    node->next = NULL;

    iter = list;
    for (i = 0; i < position - 1 && iter->next; i++) {
        iter = iter->next;
    }
    node->next = iter->next;
    iter->next = node;
    return list;
}

GSList *g_slist_insert_sorted(GSList *list, gpointer data, GCompareFunc func)
{
    GSList *node;
    GSList *iter;
    GSList *prev = NULL;

    if (!list || func(data, list->data) <= 0) {
        return g_slist_prepend(list, data);
    }

    node = g_slist_alloc();
    node->data = data;
    node->next = NULL;

    for (iter = list; iter; prev = iter, iter = iter->next) {
        if (func(data, iter->data) <= 0) {
            break;
        }
    }

    if (!iter) {
        prev->next = node;
    } else {
        prev->next = node;
        node->next = iter;
    }
    return list;
}

GSList *g_slist_insert_sorted_with_data(GSList *list, gpointer data,
                                        GCompareDataFunc func,
                                        gpointer user_data)
{
    GSList *node;
    GSList *iter;
    GSList *prev = NULL;

    if (!list || func(data, list->data, user_data) <= 0) {
        return g_slist_prepend(list, data);
    }

    node = g_slist_alloc();
    node->data = data;
    node->next = NULL;

    for (iter = list; iter; prev = iter, iter = iter->next) {
        if (func(data, iter->data, user_data) <= 0) {
            break;
        }
    }

    if (!iter) {
        prev->next = node;
    } else {
        prev->next = node;
        node->next = iter;
    }
    return list;
}

GSList *g_slist_remove(GSList *list, gconstpointer data)
{
    GSList *iter = list;
    GSList *prev = NULL;

    while (iter) {
        if (iter->data == data) {
            if (prev) {
                prev->next = iter->next;
            } else {
                list = iter->next;
            }
            g_free(iter);
            return list;
        }
        prev = iter;
        iter = iter->next;
    }

    return list;
}

GSList *g_slist_remove_all(GSList *list, gconstpointer data)
{
    GSList *iter = list;
    GSList *prev = NULL;

    while (iter) {
        GSList *next = iter->next;

        if (iter->data == data) {
            if (prev) {
                prev->next = next;
            } else {
                list = next;
            }
            g_free(iter);
        } else {
            prev = iter;
        }
        iter = next;
    }

    return list;
}

GSList *g_slist_reverse(GSList *list)
{
    GSList *prev = NULL;

    while (list) {
        GSList *next = list->next;

        list->next = prev;
        prev = list;
        list = next;
    }

    return prev;
}

GSList *g_slist_copy(GSList *list)
{
    GSList *copy = NULL;
    GSList *tail = NULL;

    for (; list; list = list->next) {
        GSList *node = g_slist_alloc();

        node->data = list->data;
        if (tail) {
            tail->next = node;
        } else {
            copy = node;
        }
        tail = node;
    }

    return copy;
}

GSList *g_slist_nth(GSList *list, guint n)
{
    while (list && n > 0) {
        list = list->next;
        n--;
    }

    return list;
}

GSList *g_slist_find(GSList *list, gconstpointer data)
{
    while (list) {
        if (list->data == data) {
            return list;
        }
        list = list->next;
    }

    return NULL;
}

GSList *g_slist_last(GSList *list)
{
    if (!list) {
        return NULL;
    }

    while (list->next) {
        list = list->next;
    }

    return list;
}

guint g_slist_length(GSList *list)
{
    guint len = 0;

    while (list) {
        len++;
        list = list->next;
    }

    return len;
}

void g_slist_foreach(GSList *list, GFunc func, gpointer user_data)
{
    while (list) {
        func(list->data, user_data);
        list = list->next;
    }
}

gpointer g_slist_nth_data(GSList *list, guint n)
{
    GSList *node = g_slist_nth(list, n);

    return node ? node->data : NULL;
}

static GSList *z_slist_merge(GSList *left, GSList *right,
                             GCompareDataFunc compare_func,
                             gpointer user_data)
{
    GSList head;
    GSList *tail = &head;

    head.next = NULL;
    while (left && right) {
        if (compare_func(left->data, right->data, user_data) <= 0) {
            tail->next = left;
            left = left->next;
        } else {
            tail->next = right;
            right = right->next;
        }
        tail = tail->next;
    }
    tail->next = left ? left : right;
    return head.next;
}

static GSList *z_slist_sort_impl(GSList *list, GCompareDataFunc compare_func,
                                 gpointer user_data)
{
    GSList *slow;
    GSList *fast;
    GSList *right;

    if (!list || !list->next) {
        return list;
    }

    slow = list;
    fast = list->next;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }

    right = slow->next;
    slow->next = NULL;

    return z_slist_merge(z_slist_sort_impl(list, compare_func, user_data),
                         z_slist_sort_impl(right, compare_func, user_data),
                         compare_func, user_data);
}

GSList *g_slist_sort_with_data(GSList *list, GCompareDataFunc compare_func,
                               gpointer user_data)
{
    return z_slist_sort_impl(list, compare_func, user_data);
}

/* ------------------------------------------------------------------ */
/* GQueue                                                              */
/* ------------------------------------------------------------------ */

GQueue *g_queue_new(void)
{
    return g_malloc0(sizeof(GQueue));
}

void g_queue_init(GQueue *queue)
{
    queue->head = NULL;
    queue->tail = NULL;
    queue->length = 0;
}

void g_queue_clear(GQueue *queue)
{
    GList *node = queue->head;

    while (node) {
        GList *next = node->next;

        g_free(node);
        node = next;
    }

    g_queue_init(queue);
}

void g_queue_clear_full(GQueue *queue, GDestroyNotify free_func)
{
    GList *node = queue->head;

    while (node) {
        GList *next = node->next;

        if (free_func) {
            free_func(node->data);
        }
        g_free(node);
        node = next;
    }

    g_queue_init(queue);
}

void g_queue_free(GQueue *queue)
{
    g_queue_clear(queue);
    g_free(queue);
}

void g_queue_free_full(GQueue *queue, GDestroyNotify free_func)
{
    g_queue_clear_full(queue, free_func);
    g_free(queue);
}

gboolean g_queue_is_empty(GQueue *queue)
{
    return queue->length == 0;
}

void g_queue_push_tail(GQueue *queue, gpointer data)
{
    GList *node = g_malloc0(sizeof(GList));

    node->data = data;
    node->prev = queue->tail;
    if (queue->tail) {
        queue->tail->next = node;
    } else {
        queue->head = node;
    }
    queue->tail = node;
    queue->length++;
}

void g_queue_push_head(GQueue *queue, gpointer data)
{
    GList *node = g_malloc0(sizeof(GList));

    node->data = data;
    node->next = queue->head;
    if (queue->head) {
        queue->head->prev = node;
    } else {
        queue->tail = node;
    }
    queue->head = node;
    queue->length++;
}

gpointer g_queue_pop_tail(GQueue *queue)
{
    GList *node = queue->tail;
    gpointer data;

    if (!node) {
        return NULL;
    }

    data = node->data;
    queue->tail = node->prev;
    if (queue->tail) {
        queue->tail->next = NULL;
    } else {
        queue->head = NULL;
    }
    queue->length--;
    g_free(node);
    return data;
}

gpointer g_queue_pop_head(GQueue *queue)
{
    GList *node = queue->head;
    gpointer data;

    if (!node) {
        return NULL;
    }

    data = node->data;
    queue->head = node->next;
    if (queue->head) {
        queue->head->prev = NULL;
    } else {
        queue->tail = NULL;
    }
    queue->length--;
    g_free(node);
    return data;
}

gpointer g_queue_peek_tail(GQueue *queue)
{
    return queue->tail ? queue->tail->data : NULL;
}

gpointer g_queue_peek_head(GQueue *queue)
{
    return queue->head ? queue->head->data : NULL;
}

guint g_queue_get_length(GQueue *queue)
{
    return queue->length;
}

/* ------------------------------------------------------------------ */
/* GString                                                             */
/* ------------------------------------------------------------------ */

static void z_string_ensure(GString *string, gsize need)
{
    gsize new_alloc;

    if (need <= string->allocated_len) {
        return;
    }

    new_alloc = string->allocated_len ? string->allocated_len : 2;
    while (new_alloc < need) {
        if (new_alloc > (G_MAXSIZE / 2)) {
            new_alloc = need;
            break;
        }
        new_alloc *= 2;
    }

    string->str = g_realloc(string->str, new_alloc);
    string->allocated_len = new_alloc;
}

GString *g_string_new(const gchar *init)
{
    return g_string_new_len(init, init ? (gssize) strlen(init) : 0);
}

GString *g_string_new_len(const gchar *init, gssize len)
{
    GString *string = g_malloc0(sizeof(*string));
    gsize real_len;

    if (len < 0) {
        real_len = init ? strlen(init) : 0;
    } else {
        real_len = (gsize) len;
    }

    if (!init && real_len != 0) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "init != NULL || len == 0");
    }

    if (real_len == G_MAXSIZE) {
        z_abort_message("GString length overflow");
    }

    string->allocated_len = real_len + 1;
    string->str = g_malloc(string->allocated_len);
    if (real_len > 0) {
        memcpy(string->str, init, real_len);
    }
    string->str[real_len] = '\0';
    string->len = real_len;
    return string;
}

GString *g_string_sized_new(gsize dfl_size)
{
    GString *string = g_malloc0(sizeof(*string));

    if (dfl_size == G_MAXSIZE) {
        z_abort_message("GString length overflow");
    }

    string->allocated_len = dfl_size + 1;
    if (string->allocated_len < 2) {
        string->allocated_len = 2;
    }
    string->str = g_malloc(string->allocated_len);
    string->str[0] = '\0';
    string->len = 0;
    return string;
}

GString *g_string_assign(GString *string, const gchar *rval)
{
    gsize len = rval ? strlen(rval) : 0;

    z_string_ensure(string, len + 1);
    if (len > 0) {
        memmove(string->str, rval, len);
    }
    string->str[len] = '\0';
    string->len = len;
    return string;
}

GString *g_string_truncate(GString *string, gsize len)
{
    if (len < string->len) {
        string->len = len;
    }
    string->str[string->len] = '\0';
    return string;
}

GString *g_string_set_size(GString *string, gsize len)
{
    if (len > string->len) {
        gsize old_len = string->len;

        z_string_ensure(string, len + 1);
        memset(string->str + old_len, 0, len - old_len);
    }
    string->len = len;
    string->str[len] = '\0';
    return string;
}

GString *g_string_insert_len(GString *string, gssize pos,
                             const gchar *val, gssize len)
{
    gsize real_pos;
    gsize real_len;
    gsize tail_len;

    if (len < 0) {
        real_len = val ? strlen(val) : 0;
    } else {
        real_len = (gsize) len;
    }
    if (!val && real_len != 0) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "val != NULL || len == 0");
    }

    real_pos = (pos < 0) ? string->len : (gsize) pos;
    if (real_pos > string->len) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "pos <= string->len");
    }

    if (real_len == 0) {
        return string;
    }

    z_string_ensure(string, z_checked_add(z_checked_add(string->len,
                                                        real_len), 1));
    tail_len = string->len - real_pos;
    memmove(string->str + real_pos + real_len, string->str + real_pos,
            tail_len);
    memmove(string->str + real_pos, val, real_len);
    string->len += real_len;
    string->str[string->len] = '\0';
    return string;
}

GString *g_string_append_len(GString *string, const gchar *val, gssize len)
{
    return g_string_insert_len(string, -1, val, len);
}

GString *g_string_append(GString *string, const gchar *val)
{
    return g_string_append_len(string, val, -1);
}

GString *g_string_append_c(GString *string, gchar c)
{
    return g_string_append_len(string, &c, 1);
}

GString *g_string_prepend(GString *string, const gchar *val)
{
    return g_string_insert_len(string, 0, val, -1);
}

GString *g_string_prepend_c(GString *string, gchar c)
{
    return g_string_insert_len(string, 0, &c, 1);
}

GString *g_string_insert(GString *string, gssize pos, const gchar *val)
{
    return g_string_insert_len(string, pos, val, -1);
}

GString *g_string_insert_c(GString *string, gssize pos, gchar c)
{
    return g_string_insert_len(string, pos, &c, 1);
}

GString *g_string_erase(GString *string, gssize pos, gssize len)
{
    gsize real_pos;
    gsize real_len;
    gsize tail_len;

    if (pos < 0 || (gsize) pos > string->len) {
        g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,
                                 "pos within the string");
    }
    real_pos = (gsize) pos;

    if (len < 0) {
        real_len = string->len - real_pos;
    } else {
        real_len = (gsize) len;
        if (real_len > (string->len - real_pos)) {
            real_len = string->len - real_pos;
        }
    }

    tail_len = string->len - real_pos - real_len;
    memmove(string->str + real_pos, string->str + real_pos + real_len,
            tail_len);
    string->len -= real_len;
    string->str[string->len] = '\0';
    return string;
}

static void z_string_set_vprintf(GString *string, const gchar *format,
                                 va_list args)
{
    gchar *formatted = g_strdup_vprintf(format, args);

    g_string_assign(string, formatted);
    g_free(formatted);
}

static void z_string_append_vprintf(GString *string, const gchar *format,
                                    va_list args)
{
    gchar *formatted = g_strdup_vprintf(format, args);

    g_string_append_len(string, formatted, -1);
    g_free(formatted);
}

void g_string_printf(GString *string, const gchar *format, ...)
{
    va_list ap;

    va_start(ap, format);
    z_string_set_vprintf(string, format, ap);
    va_end(ap);
}

void g_string_vprintf(GString *string, const gchar *format, va_list args)
{
    z_string_set_vprintf(string, format, args);
}

void g_string_append_printf(GString *string, const gchar *format, ...)
{
    va_list ap;

    va_start(ap, format);
    z_string_append_vprintf(string, format, ap);
    va_end(ap);
}

void g_string_append_vprintf(GString *string, const gchar *format,
                             va_list args)
{
    z_string_append_vprintf(string, format, args);
}

gchar *g_string_free(GString *string, gboolean free_segment)
{
    gchar *segment;

    if (!string) {
        return NULL;
    }

    if (free_segment) {
        g_free(string->str);
        g_free(string);
        return NULL;
    }

    segment = string->str;
    g_free(string);
    return segment;
}

/* ------------------------------------------------------------------ */
/* GPatternSpec                                                        */
/* ------------------------------------------------------------------ */

struct _GPatternSpec {
    gchar *pattern;
};

static gsize z_utf8_char_len(const gchar *str)
{
    const guchar c = (const guchar) *str;

    if (c == 0) {
        return 0;
    }
    if (c < 0x80) {
        return 1;
    }
    if (c < 0xC0) {
        return 1;
    }
    if (c < 0xE0) {
        return 2;
    }
    if (c < 0xF0) {
        return 3;
    }
    if (c < 0xF8) {
        return 4;
    }
    if (c < 0xFC) {
        return 5;
    }
    if (c < 0xFE) {
        return 6;
    }
    return 1;
}

static const gchar *z_utf8_next(const gchar *str)
{
    gsize len = z_utf8_char_len(str);
    gsize i;

    if (len == 0) {
        return str;
    }
    for (i = 1; i < len; i++) {
        if (str[i] == '\0') {
            return str + 1;
        }
    }

    return str + len;
}

static gboolean z_pattern_match(const gchar *pattern, const gchar *string)
{
    while (*pattern != '\0') {
        if (*pattern == '?') {
            if (*string == '\0') {
                return FALSE;
            }
            pattern = z_utf8_next(pattern);
            string = z_utf8_next(string);
        } else if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') {
                return TRUE;
            }
            for (;;) {
                if (z_pattern_match(pattern, string)) {
                    return TRUE;
                }
                if (*string == '\0') {
                    return FALSE;
                }
                string = z_utf8_next(string);
            }
        } else {
            gsize pattern_len = z_utf8_char_len(pattern);
            gsize string_len = z_utf8_char_len(string);

            if (pattern_len == 0 || pattern_len != string_len ||
                memcmp(pattern, string, pattern_len) != 0) {
                return FALSE;
            }
            pattern += pattern_len;
            string += string_len;
        }
    }

    return *string == '\0';
}

GPatternSpec *g_pattern_spec_new(const gchar *pattern)
{
    GPatternSpec *pspec;

    g_return_val_if_fail(pattern != NULL, NULL);

    pspec = g_malloc0(sizeof(*pspec));
    pspec->pattern = g_strdup(pattern);
    return pspec;
}

gboolean g_pattern_match_string(GPatternSpec *pspec, const gchar *string)
{
    g_return_val_if_fail(pspec != NULL, FALSE);
    g_return_val_if_fail(string != NULL, FALSE);

    return z_pattern_match(pspec->pattern, string);
}

void g_pattern_spec_free(GPatternSpec *pspec)
{
    g_return_if_fail(pspec != NULL);

    g_free(pspec->pattern);
    g_free(pspec);
}

/* ------------------------------------------------------------------ */
/* GFileError                                                          */
/* ------------------------------------------------------------------ */

GQuark g_file_error_quark(void)
{
    /*
     * This layer does not provide GLib's full quark interning subsystem.
     * Use a stable nonzero domain value so all code compiled against this
     * header sees the same G_FILE_ERROR domain.
     */
    return (GQuark) 0x47464651u;
}

GFileError g_file_error_from_errno(gint err_no)
{
    switch (err_no) {
#ifdef EEXIST
    case EEXIST:
        return G_FILE_ERROR_EXIST;
#endif
#ifdef EISDIR
    case EISDIR:
        return G_FILE_ERROR_ISDIR;
#endif
#ifdef EACCES
    case EACCES:
        return G_FILE_ERROR_ACCES;
#endif
#ifdef ENAMETOOLONG
    case ENAMETOOLONG:
        return G_FILE_ERROR_NAMETOOLONG;
#endif
#ifdef ENOENT
    case ENOENT:
        return G_FILE_ERROR_NOENT;
#endif
#ifdef ENOTDIR
    case ENOTDIR:
        return G_FILE_ERROR_NOTDIR;
#endif
#ifdef ENXIO
    case ENXIO:
        return G_FILE_ERROR_NXIO;
#endif
#ifdef ENODEV
    case ENODEV:
        return G_FILE_ERROR_NODEV;
#endif
#ifdef EROFS
    case EROFS:
        return G_FILE_ERROR_ROFS;
#endif
#ifdef ETXTBSY
    case ETXTBSY:
        return G_FILE_ERROR_TXTBSY;
#endif
#ifdef EFAULT
    case EFAULT:
        return G_FILE_ERROR_FAULT;
#endif
#ifdef ELOOP
    case ELOOP:
        return G_FILE_ERROR_LOOP;
#endif
#ifdef ENOSPC
    case ENOSPC:
        return G_FILE_ERROR_NOSPC;
#endif
#ifdef ENOMEM
    case ENOMEM:
        return G_FILE_ERROR_NOMEM;
#endif
#ifdef EMFILE
    case EMFILE:
        return G_FILE_ERROR_MFILE;
#endif
#ifdef ENFILE
    case ENFILE:
        return G_FILE_ERROR_NFILE;
#endif
#ifdef EBADF
    case EBADF:
        return G_FILE_ERROR_BADF;
#endif
#ifdef EINVAL
    case EINVAL:
        return G_FILE_ERROR_INVAL;
#endif
#ifdef EPIPE
    case EPIPE:
        return G_FILE_ERROR_PIPE;
#endif
#ifdef EAGAIN
    case EAGAIN:
        return G_FILE_ERROR_AGAIN;
#endif
#ifdef EINTR
    case EINTR:
        return G_FILE_ERROR_INTR;
#endif
#ifdef EIO
    case EIO:
        return G_FILE_ERROR_IO;
#endif
#ifdef EPERM
    case EPERM:
        return G_FILE_ERROR_PERM;
#endif
#ifdef ENOSYS
    case ENOSYS:
        return G_FILE_ERROR_NOSYS;
#endif
    default:
        return G_FILE_ERROR_FAILED;
    }
}

/* ------------------------------------------------------------------ */
/* GError                                                              */
/* ------------------------------------------------------------------ */

void g_error_free(GError *error)
{
    if (!error) {
        return;
    }

    g_free(error->message);
    g_free(error);
}
