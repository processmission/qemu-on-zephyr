/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Narrow GLib compatibility layer for QEMU-on-Zephyr.
 *
 * This header intentionally implements only the GLib 2.66 API surface that
 * is needed by the QEMU QOM/qdev/MemoryRegion/PL011 slice.  It is not a
 * complete GLib replacement and does not claim to be one.  Unimplemented
 * entry points are declared here so that QEMU sources can still be parsed,
 * but they have no definitions in glib.c and therefore fail at link time
 * instead of pretending to succeed.
 *
 * The implementation backend is plain C (malloc/free/realloc, memory and
 * string helpers, vsnprintf and qsort) and deliberately contains no Linux
 * syscalls, no GMainContext implementation, no regex engine and no
 * networking.
 *
 * Copyright (c) 2026 Zephyr QEMU port contributors
 */

#ifndef __GLIB_H__
#define __GLIB_H__

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Version information.                                                */
/*                                                                     */
/* The supported compatibility surface is deliberately pinned to GLib  */
/* 2.66 semantics (QEMU's minimum requirement).  Do not bump these      */
/* numbers without adding and testing the corresponding APIs.           */
/* ------------------------------------------------------------------ */

#define GLIB_MAJOR_VERSION 2
#define GLIB_MINOR_VERSION 66
#define GLIB_MICRO_VERSION 0

#define GLIB_ENCODE_VERSION(major, minor) (((major) << 16) | (minor))
#define GLIB_VERSION_2_2   GLIB_ENCODE_VERSION(2, 2)
#define GLIB_VERSION_2_4   GLIB_ENCODE_VERSION(2, 4)
#define GLIB_VERSION_2_6   GLIB_ENCODE_VERSION(2, 6)
#define GLIB_VERSION_2_8   GLIB_ENCODE_VERSION(2, 8)
#define GLIB_VERSION_2_10  GLIB_ENCODE_VERSION(2, 10)
#define GLIB_VERSION_2_12  GLIB_ENCODE_VERSION(2, 12)
#define GLIB_VERSION_2_14  GLIB_ENCODE_VERSION(2, 14)
#define GLIB_VERSION_2_16  GLIB_ENCODE_VERSION(2, 16)
#define GLIB_VERSION_2_18  GLIB_ENCODE_VERSION(2, 18)
#define GLIB_VERSION_2_20  GLIB_ENCODE_VERSION(2, 20)
#define GLIB_VERSION_2_22  GLIB_ENCODE_VERSION(2, 22)
#define GLIB_VERSION_2_24  GLIB_ENCODE_VERSION(2, 24)
#define GLIB_VERSION_2_26  GLIB_ENCODE_VERSION(2, 26)
#define GLIB_VERSION_2_28  GLIB_ENCODE_VERSION(2, 28)
#define GLIB_VERSION_2_30  GLIB_ENCODE_VERSION(2, 30)
#define GLIB_VERSION_2_32  GLIB_ENCODE_VERSION(2, 32)
#define GLIB_VERSION_2_34  GLIB_ENCODE_VERSION(2, 34)
#define GLIB_VERSION_2_36  GLIB_ENCODE_VERSION(2, 36)
#define GLIB_VERSION_2_38  GLIB_ENCODE_VERSION(2, 38)
#define GLIB_VERSION_2_40  GLIB_ENCODE_VERSION(2, 40)
#define GLIB_VERSION_2_42  GLIB_ENCODE_VERSION(2, 42)
#define GLIB_VERSION_2_44  GLIB_ENCODE_VERSION(2, 44)
#define GLIB_VERSION_2_46  GLIB_ENCODE_VERSION(2, 46)
#define GLIB_VERSION_2_48  GLIB_ENCODE_VERSION(2, 48)
#define GLIB_VERSION_2_50  GLIB_ENCODE_VERSION(2, 50)
#define GLIB_VERSION_2_52  GLIB_ENCODE_VERSION(2, 52)
#define GLIB_VERSION_2_54  GLIB_ENCODE_VERSION(2, 54)
#define GLIB_VERSION_2_56  GLIB_ENCODE_VERSION(2, 56)
#define GLIB_VERSION_2_58  GLIB_ENCODE_VERSION(2, 58)
#define GLIB_VERSION_2_60  GLIB_ENCODE_VERSION(2, 60)
#define GLIB_VERSION_2_62  GLIB_ENCODE_VERSION(2, 62)
#define GLIB_VERSION_2_64  GLIB_ENCODE_VERSION(2, 64)
#define GLIB_VERSION_2_66  GLIB_ENCODE_VERSION(2, 66)

#define GLIB_VERSION_CUR_STABLE   GLIB_VERSION_2_66
#define GLIB_VERSION_PREV_STABLE  GLIB_VERSION_2_64

#ifndef GLIB_VERSION_MIN_REQUIRED
#define GLIB_VERSION_MIN_REQUIRED GLIB_VERSION_2_66
#endif
#ifndef GLIB_VERSION_MAX_ALLOWED
#define GLIB_VERSION_MAX_ALLOWED  GLIB_VERSION_2_66
#endif

#define GLIB_CHECK_VERSION(major, minor, micro)                                \
    (GLIB_MAJOR_VERSION > (major) ||                                           \
     (GLIB_MAJOR_VERSION == (major) && GLIB_MINOR_VERSION > (minor)) ||        \
     (GLIB_MAJOR_VERSION == (major) && GLIB_MINOR_VERSION == (minor) &&        \
      GLIB_MICRO_VERSION >= (micro)))

/* ------------------------------------------------------------------ */
/* Compiler and macro helpers.                                         */
/* ------------------------------------------------------------------ */

#ifdef __GNUC__
#define G_GNUC_CHECK_VERSION(major, minor)                                     \
    ((__GNUC__ > (major)) || ((__GNUC__ == (major)) &&                         \
                              (__GNUC_MINOR__ >= (minor))))
#define G_GNUC_UNUSED              __attribute__((__unused__))
#define G_GNUC_NORETURN            __attribute__((__noreturn__))
#define G_GNUC_CONST               __attribute__((__const__))
#define G_GNUC_PURE                __attribute__((__pure__))
#define G_GNUC_MALLOC              __attribute__((__malloc__))
#define G_GNUC_WARN_UNUSED_RESULT  __attribute__((__warn_unused_result__))
#define G_GNUC_NULL_TERMINATED     __attribute__((__sentinel__))
#define G_GNUC_PRINTF(fmt, args)   __attribute__((__format__(__printf__, fmt, args)))
#define G_GNUC_SCANF(fmt, args)    __attribute__((__format__(__scanf__, fmt, args)))
#define G_GNUC_ALLOC_SIZE(x)       __attribute__((__alloc_size__(x)))
#define G_GNUC_ALLOC_SIZE2(x, y)   __attribute__((__alloc_size__(x, y)))
#define G_GNUC_FALLTHROUGH         __attribute__((__fallthrough__))
#define G_GNUC_DEPRECATED          __attribute__((__deprecated__))
#define G_GNUC_DEPRECATED_FOR(f)   G_GNUC_DEPRECATED
#define G_GNUC_NO_INLINE           __attribute__((__noinline__))
#define G_GNUC_FLAG_ENUM
#define G_GNUC_EXTENSION           __extension__
#else
#define G_GNUC_CHECK_VERSION(major, minor) 0
#define G_GNUC_UNUSED
#define G_GNUC_NORETURN
#define G_GNUC_CONST
#define G_GNUC_PURE
#define G_GNUC_MALLOC
#define G_GNUC_WARN_UNUSED_RESULT
#define G_GNUC_NULL_TERMINATED
#define G_GNUC_PRINTF(fmt, args)
#define G_GNUC_SCANF(fmt, args)
#define G_GNUC_ALLOC_SIZE(x)
#define G_GNUC_ALLOC_SIZE2(x, y)
#define G_GNUC_FALLTHROUGH
#define G_GNUC_DEPRECATED
#define G_GNUC_DEPRECATED_FOR(f)
#define G_GNUC_NO_INLINE
#define G_GNUC_EXTENSION
#endif

#define G_NORETURN G_GNUC_NORETURN

#define G_BEGIN_DECLS
#define G_END_DECLS

#define G_STMT_START do
#define G_STMT_END   while (0)

#define G_STRINGIFY(macro_or_string) #macro_or_string
#define G_STRINGIFY_ARG(x) G_STRINGIFY(x)
#define G_STRFUNC __func__
#define G_STRLOC __FILE__ ":" G_STRINGIFY(__LINE__)

#define G_N_ELEMENTS(arr) (sizeof(arr) / sizeof((arr)[0]))
#define G_LIKELY(expr)    (__builtin_expect(!!(expr), 1))
#define G_UNLIKELY(expr)  (__builtin_expect(!!(expr), 0))

#define G_DIR_SEPARATOR        '/'
#define G_DIR_SEPARATOR_S      "/"
#define G_SEARCHPATH_SEPARATOR ':'
#define G_SEARCHPATH_SEPARATOR_S ":"
#define G_IS_DIR_SEPARATOR(c)  ((c) == G_DIR_SEPARATOR)

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef G_LOG_DOMAIN
#define G_LOG_DOMAIN ((gchar *) 0)
#endif

#define GLIB_VAR extern
#define GLIB_AVAILABLE_IN_ALL
#define GLIB_AVAILABLE_IN_2_2
#define GLIB_AVAILABLE_IN_2_4
#define GLIB_AVAILABLE_IN_2_6
#define GLIB_AVAILABLE_IN_2_8
#define GLIB_AVAILABLE_IN_2_10
#define GLIB_AVAILABLE_IN_2_12
#define GLIB_AVAILABLE_IN_2_14
#define GLIB_AVAILABLE_IN_2_16
#define GLIB_AVAILABLE_IN_2_18
#define GLIB_AVAILABLE_IN_2_20
#define GLIB_AVAILABLE_IN_2_22
#define GLIB_AVAILABLE_IN_2_24
#define GLIB_AVAILABLE_IN_2_26
#define GLIB_AVAILABLE_IN_2_28
#define GLIB_AVAILABLE_IN_2_30
#define GLIB_AVAILABLE_IN_2_32
#define GLIB_AVAILABLE_IN_2_34
#define GLIB_AVAILABLE_IN_2_36
#define GLIB_AVAILABLE_IN_2_38
#define GLIB_AVAILABLE_IN_2_40
#define GLIB_AVAILABLE_IN_2_42
#define GLIB_AVAILABLE_IN_2_44
#define GLIB_AVAILABLE_IN_2_46
#define GLIB_AVAILABLE_IN_2_48
#define GLIB_AVAILABLE_IN_2_50
#define GLIB_AVAILABLE_IN_2_52
#define GLIB_AVAILABLE_IN_2_54
#define GLIB_AVAILABLE_IN_2_56
#define GLIB_AVAILABLE_IN_2_58
#define GLIB_AVAILABLE_IN_2_60
#define GLIB_AVAILABLE_IN_2_62
#define GLIB_AVAILABLE_IN_2_64
#define GLIB_AVAILABLE_IN_2_66
#define GLIB_DEPRECATED
#define GLIB_DEPRECATED_FOR(f)
#define GLIB_DEPRECATED_IN_2_2
#define GLIB_DEPRECATED_IN_2_4
#define GLIB_DEPRECATED_IN_2_6
#define GLIB_DEPRECATED_IN_2_8
#define GLIB_DEPRECATED_IN_2_10
#define GLIB_DEPRECATED_IN_2_12
#define GLIB_DEPRECATED_IN_2_14
#define GLIB_DEPRECATED_IN_2_16
#define GLIB_DEPRECATED_IN_2_18
#define GLIB_DEPRECATED_IN_2_20
#define GLIB_DEPRECATED_IN_2_22
#define GLIB_DEPRECATED_IN_2_24
#define GLIB_DEPRECATED_IN_2_26
#define GLIB_DEPRECATED_IN_2_28
#define GLIB_DEPRECATED_IN_2_30
#define GLIB_DEPRECATED_IN_2_32
#define GLIB_DEPRECATED_IN_2_34
#define GLIB_DEPRECATED_IN_2_36
#define GLIB_DEPRECATED_IN_2_38
#define GLIB_DEPRECATED_IN_2_40
#define GLIB_DEPRECATED_IN_2_42
#define GLIB_DEPRECATED_IN_2_44
#define GLIB_DEPRECATED_IN_2_46
#define GLIB_DEPRECATED_IN_2_48
#define GLIB_DEPRECATED_IN_2_50
#define GLIB_DEPRECATED_IN_2_52
#define GLIB_DEPRECATED_IN_2_54
#define GLIB_DEPRECATED_IN_2_56
#define GLIB_DEPRECATED_IN_2_58
#define GLIB_DEPRECATED_IN_2_60
#define GLIB_DEPRECATED_IN_2_62
#define GLIB_DEPRECATED_IN_2_64
#define GLIB_DEPRECATED_IN_2_66
#define G_GNUC_BEGIN_IGNORE_DEPRECATIONS
#define G_GNUC_END_IGNORE_DEPRECATIONS
#define G_GNUC_INTERNAL
#define G_GNUC_NO_INSTRUMENT

/* ------------------------------------------------------------------ */
/* Basic types.                                                        */
/* ------------------------------------------------------------------ */

typedef char          gchar;
typedef short         gshort;
typedef long          glong;
typedef int           gint;
typedef gint          gboolean;

typedef unsigned char  guchar;
typedef unsigned short gushort;
typedef unsigned long  gulong;
typedef unsigned int   guint;

typedef float  gfloat;
typedef double gdouble;

typedef signed char        gint8;
typedef unsigned char      guint8;
typedef signed short       gint16;
typedef unsigned short     guint16;
typedef signed int         gint32;
typedef unsigned int       guint32;
typedef signed long long   gint64;
typedef unsigned long long guint64;

typedef gint64 goffset;
typedef size_t gsize;
typedef ptrdiff_t gssize;
typedef intptr_t  gintptr;
typedef uintptr_t guintptr;

typedef void       *gpointer;
typedef const void *gconstpointer;

typedef guint32 gunichar;
typedef guint16 gunichar2;
typedef gchar  **GStrv;

#define G_MAXUINT  ((guint) ~0U)
#define G_MAXINT   ((gint) (~0U >> 1))
#define G_MINSIZE  ((gsize) 0)
#define G_MAXSIZE  ((gsize) -1)
#define G_MAXSSIZE ((gssize) (((gsize) -1) >> 1))
#define G_MININT   ((gint) (~G_MAXINT))
#define G_MAXINT64 ((gint64) 0x7fffffffffffffffLL)
#define G_MININT64 ((gint64) (-G_MAXINT64 - 1))
#define G_MAXUINT64 ((guint64) 0xffffffffffffffffULL)

typedef gint  (*GCompareFunc)     (gconstpointer a, gconstpointer b);
typedef gint  (*GCompareDataFunc) (gconstpointer a, gconstpointer b,
                                   gpointer user_data);
typedef gboolean (*GEqualFunc)    (gconstpointer a, gconstpointer b);
typedef void  (*GDestroyNotify)   (gpointer data);
typedef void  (*GFunc)            (gpointer data, gpointer user_data);
typedef guint (*GHashFunc)        (gconstpointer key);
typedef void  (*GHFunc)           (gpointer key, gpointer value,
                                   gpointer user_data);
typedef gboolean (*GHRFunc)       (gpointer key, gpointer value,
                                   gpointer user_data);
typedef gpointer (*GCopyFunc)     (gconstpointer src, gpointer user_data);

/* ------------------------------------------------------------------ */
/* GLib data structures used by the QEMU QOM/device slice.              */
/* ------------------------------------------------------------------ */

typedef struct _GArray {
    gchar *data;
    guint  len;
} GArray;

typedef struct _GByteArray {
    guint8 *data;
    guint   len;
} GByteArray;

typedef struct _GPtrArray {
    gpointer *pdata;
    guint     len;
} GPtrArray;

typedef struct _GString {
    gchar *str;
    gsize  len;
    gsize  allocated_len;
} GString;

typedef struct _GSList {
    gpointer data;
    struct _GSList *next;
} GSList;

typedef struct _GList {
    gpointer data;
    struct _GList *next;
    struct _GList *prev;
} GList;

typedef struct _GQueue {
    GList *head;
    GList *tail;
    guint  length;
} GQueue;

#define G_QUEUE_INIT { NULL, NULL, 0 }

typedef struct _GHashTable GHashTable;

struct _GHashTableIter {
    GHashTable *table;
    gpointer    current;
    gpointer    next;
    guint       version;
    gboolean    current_valid;
};
typedef struct _GHashTableIter GHashTableIter;

typedef guint32 GQuark;

typedef struct _GError {
    GQuark domain;
    gint   code;
    gchar *message;
} GError;

typedef enum {
    G_FILE_ERROR_EXIST,
    G_FILE_ERROR_ISDIR,
    G_FILE_ERROR_ACCES,
    G_FILE_ERROR_NAMETOOLONG,
    G_FILE_ERROR_NOENT,
    G_FILE_ERROR_NOTDIR,
    G_FILE_ERROR_NXIO,
    G_FILE_ERROR_NODEV,
    G_FILE_ERROR_ROFS,
    G_FILE_ERROR_TXTBSY,
    G_FILE_ERROR_FAULT,
    G_FILE_ERROR_LOOP,
    G_FILE_ERROR_NOSPC,
    G_FILE_ERROR_NOMEM,
    G_FILE_ERROR_MFILE,
    G_FILE_ERROR_NFILE,
    G_FILE_ERROR_BADF,
    G_FILE_ERROR_INVAL,
    G_FILE_ERROR_PIPE,
    G_FILE_ERROR_AGAIN,
    G_FILE_ERROR_INTR,
    G_FILE_ERROR_IO,
    G_FILE_ERROR_PERM,
    G_FILE_ERROR_NOSYS,
    G_FILE_ERROR_FAILED
} GFileError;

typedef struct _GTree      GTree;
typedef struct _GTreeNode  GTreeNode;
typedef struct _GBytes     GBytes;
typedef struct _GDateTime  GDateTime;
typedef struct _GMappedFile GMappedFile;
typedef struct _GThreadPool GThreadPool;
typedef struct _GMainContext GMainContext;
typedef struct _GMainLoop   GMainLoop;
typedef struct _GPatternSpec GPatternSpec;
typedef struct _GRegex      GRegex;

typedef struct _GSource GSource;
typedef gboolean (*GSourceFunc)(gpointer user_data);
typedef void (*GSourceDummyMarshal)(void);

typedef struct _GSourceFuncs {
    gboolean (*prepare)(GSource *source, gint *timeout_);
    gboolean (*check)(GSource *source);
    gboolean (*dispatch)(GSource *source, GSourceFunc callback,
                         gpointer user_data);
    void     (*finalize)(GSource *source);
    GSourceFunc closure_callback;
    GSourceDummyMarshal closure_marshal;
} GSourceFuncs;

struct _GSource {
    gpointer       callback_data;
    GSourceFuncs  *source_funcs;
    guint          ref_count;
    GMainContext  *context;
    gint           priority;
    guint          flags;
    guint          source_id;
    GSList        *poll_fds;
    GSource       *prev;
    GSource       *next;
};

typedef struct _GPollFD {
    gint   fd;
    gushort events;
    gushort revents;
} GPollFD;

typedef enum {
    G_IO_IN   = 1 << 0,
    G_IO_OUT  = 1 << 2,
    G_IO_PRI  = 1 << 1,
    G_IO_ERR  = 1 << 3,
    G_IO_HUP  = 1 << 4,
    G_IO_NVAL = 1 << 5
} GIOCondition;

typedef enum {
    G_LOG_FLAG_RECURSION = 1 << 0,
    G_LOG_FLAG_FATAL     = 1 << 1,
    G_LOG_LEVEL_ERROR    = 1 << 2,
    G_LOG_LEVEL_CRITICAL = 1 << 3,
    G_LOG_LEVEL_WARNING  = 1 << 4,
    G_LOG_LEVEL_MESSAGE  = 1 << 5,
    G_LOG_LEVEL_INFO     = 1 << 6,
    G_LOG_LEVEL_DEBUG    = 1 << 7
} GLogLevelFlags;

#define G_LOG_LEVEL_MASK ((gint) ~(G_LOG_FLAG_RECURSION | G_LOG_FLAG_FATAL))
#define G_LOG_FATAL_MASK ((gint) (G_LOG_FLAG_RECURSION | G_LOG_LEVEL_ERROR))

typedef void (*GLogFunc)(const gchar *log_domain, GLogLevelFlags log_level,
                         const gchar *message, gpointer user_data);

#define G_PRIORITY_DEFAULT 0
#define G_SOURCE_CONTINUE  TRUE
#define G_SOURCE_REMOVE    FALSE
#define G_USEC_PER_SEC     1000000

/* ------------------------------------------------------------------ */
/* GArray / GByteArray / GPtrArray                                     */
/* ------------------------------------------------------------------ */

#define g_array_append_val(a, v)  g_array_append_vals((a), &(v), 1)
#define g_array_prepend_val(a, v) g_array_prepend_vals((a), &(v), 1)
#define g_array_insert_val(a, i, v) g_array_insert_vals((a), (i), &(v), 1)
#define g_array_index(a, t, i)    (((t *) (void *) (a)->data)[(i)])

GArray *g_array_new(gboolean zero_terminated, gboolean clear_,
                    guint element_size);
GArray *g_array_sized_new(gboolean zero_terminated, gboolean clear_,
                          guint element_size, guint reserved_size);
GArray *g_array_append_vals(GArray *array, gconstpointer data, guint len);
GArray *g_array_prepend_vals(GArray *array, gconstpointer data, guint len);
GArray *g_array_insert_vals(GArray *array, guint index_, gconstpointer data,
                            guint len);
GArray *g_array_set_size(GArray *array, guint length);
GArray *g_array_remove_index(GArray *array, guint index_);
GArray *g_array_remove_index_fast(GArray *array, guint index_);
GArray *g_array_remove_range(GArray *array, guint index_, guint length);
gchar  *g_array_free(GArray *array, gboolean free_segment);
GArray *g_array_ref(GArray *array);
void    g_array_unref(GArray *array);
guint   g_array_get_element_size(GArray *array);
void    g_array_sort(GArray *array, GCompareFunc compare_func);
void    g_array_sort_with_data(GArray *array, GCompareDataFunc compare_func,
                               gpointer user_data);
void    g_array_set_clear_func(GArray *array, GDestroyNotify clear_func);

#define g_ptr_array_index(array, index_) ((array)->pdata)[(index_)]

GPtrArray *g_ptr_array_new(void);
GPtrArray *g_ptr_array_new_with_free_func(GDestroyNotify element_free_func);
GPtrArray *g_ptr_array_sized_new(guint reserved_size);
GPtrArray *g_ptr_array_new_full(guint reserved_size,
                                GDestroyNotify element_free_func);
gpointer  *g_ptr_array_free(GPtrArray *array, gboolean free_segment);
GPtrArray *g_ptr_array_ref(GPtrArray *array);
void       g_ptr_array_unref(GPtrArray *array);
void       g_ptr_array_set_free_func(GPtrArray *array,
                                     GDestroyNotify element_free_func);
void       g_ptr_array_set_size(GPtrArray *array, gint length);
gpointer   g_ptr_array_remove_index(GPtrArray *array, guint index_);
gpointer   g_ptr_array_remove_index_fast(GPtrArray *array, guint index_);
gboolean   g_ptr_array_remove(GPtrArray *array, gpointer data);
gboolean   g_ptr_array_remove_fast(GPtrArray *array, gpointer data);
GPtrArray *g_ptr_array_remove_range(GPtrArray *array, guint index_,
                                    guint length);
void       g_ptr_array_add(GPtrArray *array, gpointer data);
void       g_ptr_array_insert(GPtrArray *array, gint index_, gpointer data);
void       g_ptr_array_sort(GPtrArray *array, GCompareFunc compare_func);
void       g_ptr_array_sort_with_data(GPtrArray *array,
                                      GCompareDataFunc compare_func,
                                      gpointer user_data);
void       g_ptr_array_foreach(GPtrArray *array, GFunc func,
                               gpointer user_data);

GByteArray *g_byte_array_new(void);
GByteArray *g_byte_array_sized_new(guint reserved_size);
GByteArray *g_byte_array_append(GByteArray *array, const guint8 *data,
                                guint len);
GByteArray *g_byte_array_set_size(GByteArray *array, guint length);
guint8     *g_byte_array_free(GByteArray *array, gboolean free_segment);
GByteArray *g_byte_array_ref(GByteArray *array);
void        g_byte_array_unref(GByteArray *array);

/* ------------------------------------------------------------------ */
/* GHashTable                                                          */
/* ------------------------------------------------------------------ */

GHashTable *g_hash_table_new(GHashFunc hash_func, GEqualFunc key_equal_func);
GHashTable *g_hash_table_new_full(GHashFunc hash_func, GEqualFunc key_equal_func,
                                  GDestroyNotify key_destroy_func,
                                  GDestroyNotify value_destroy_func);
GHashTable *g_hash_table_ref(GHashTable *hash_table);
void        g_hash_table_unref(GHashTable *hash_table);
void        g_hash_table_destroy(GHashTable *hash_table);
gboolean    g_hash_table_insert(GHashTable *hash_table, gpointer key,
                                gpointer value);
gboolean    g_hash_table_replace(GHashTable *hash_table, gpointer key,
                                 gpointer value);
gboolean    g_hash_table_add(GHashTable *hash_table, gpointer key);
gboolean    g_hash_table_remove(GHashTable *hash_table, gconstpointer key);
void        g_hash_table_remove_all(GHashTable *hash_table);
gpointer    g_hash_table_lookup(GHashTable *hash_table, gconstpointer key);
gboolean    g_hash_table_contains(GHashTable *hash_table, gconstpointer key);
gboolean    g_hash_table_lookup_extended(GHashTable *hash_table,
                                         gconstpointer lookup_key,
                                         gpointer *orig_key,
                                         gpointer *value);
void        g_hash_table_foreach(GHashTable *hash_table, GHFunc func,
                                 gpointer user_data);
gpointer    g_hash_table_find(GHashTable *hash_table, GHRFunc predicate,
                              gpointer user_data);
guint       g_hash_table_foreach_remove(GHashTable *hash_table, GHRFunc func,
                                        gpointer user_data);
guint       g_hash_table_size(GHashTable *hash_table);

void        g_hash_table_iter_init(GHashTableIter *iter,
                                   GHashTable *hash_table);
gboolean    g_hash_table_iter_next(GHashTableIter *iter, gpointer *key,
                                   gpointer *value);
GHashTable *g_hash_table_iter_get_hash_table(GHashTableIter *iter);
void        g_hash_table_iter_remove(GHashTableIter *iter);
void        g_hash_table_iter_replace(GHashTableIter *iter, gpointer value);
void        g_hash_table_iter_steal(GHashTableIter *iter);

/* ------------------------------------------------------------------ */
/* GSList / GList / GQueue                                             */
/* ------------------------------------------------------------------ */

GSList *g_slist_alloc(void) G_GNUC_MALLOC;
void    g_slist_free(GSList *list);
void    g_slist_free_1(GSList *list);
void    g_slist_free_full(GSList *list, GDestroyNotify free_func);
GSList *g_slist_append(GSList *list, gpointer data) G_GNUC_WARN_UNUSED_RESULT;
GSList *g_slist_prepend(GSList *list, gpointer data) G_GNUC_WARN_UNUSED_RESULT;
GSList *g_slist_insert(GSList *list, gpointer data, gint position)
    G_GNUC_WARN_UNUSED_RESULT;
GSList *g_slist_insert_sorted(GSList *list, gpointer data, GCompareFunc func)
    G_GNUC_WARN_UNUSED_RESULT;
GSList *g_slist_insert_sorted_with_data(GSList *list, gpointer data,
                                        GCompareDataFunc func,
                                        gpointer user_data)
    G_GNUC_WARN_UNUSED_RESULT;
GSList *g_slist_remove(GSList *list, gconstpointer data)
    G_GNUC_WARN_UNUSED_RESULT;
GSList *g_slist_remove_all(GSList *list, gconstpointer data)
    G_GNUC_WARN_UNUSED_RESULT;
GSList *g_slist_reverse(GSList *list) G_GNUC_WARN_UNUSED_RESULT;
GSList *g_slist_copy(GSList *list) G_GNUC_WARN_UNUSED_RESULT;
GSList *g_slist_nth(GSList *list, guint n);
GSList *g_slist_find(GSList *list, gconstpointer data);
GSList *g_slist_last(GSList *list);
guint   g_slist_length(GSList *list);
void    g_slist_foreach(GSList *list, GFunc func, gpointer user_data);
GSList *g_slist_sort_with_data(GSList *list, GCompareDataFunc compare_func,
                               gpointer user_data)
    G_GNUC_WARN_UNUSED_RESULT;
gpointer g_slist_nth_data(GSList *list, guint n);

#define g_slist_next(slist) ((slist) ? (((GSList *) (slist))->next) : NULL)

GList *g_list_alloc(void) G_GNUC_MALLOC;
void   g_list_free(GList *list);
void   g_list_free_full(GList *list, GDestroyNotify free_func);
GList *g_list_append(GList *list, gpointer data) G_GNUC_WARN_UNUSED_RESULT;
GList *g_list_prepend(GList *list, gpointer data) G_GNUC_WARN_UNUSED_RESULT;
GList *g_list_insert_before(GList *list, GList *sibling, gpointer data)
    G_GNUC_WARN_UNUSED_RESULT;
GList *g_list_remove(GList *list, gconstpointer data)
    G_GNUC_WARN_UNUSED_RESULT;
GList *g_list_delete_link(GList *list, GList *link_)
    G_GNUC_WARN_UNUSED_RESULT;
GList *g_list_first(GList *list);
GList *g_list_last(GList *list);
GList *g_list_next(GList *list);
guint  g_list_length(GList *list);
void   g_list_foreach(GList *list, GFunc func, gpointer user_data);
GList *g_list_sort_with_data(GList *list, GCompareDataFunc compare_func,
                             gpointer user_data)
    G_GNUC_WARN_UNUSED_RESULT;

GQueue *g_queue_new(void);
void    g_queue_free(GQueue *queue);
void    g_queue_free_full(GQueue *queue, GDestroyNotify free_func);
void    g_queue_init(GQueue *queue);
void    g_queue_clear(GQueue *queue);
void    g_queue_clear_full(GQueue *queue, GDestroyNotify free_func);
gboolean g_queue_is_empty(GQueue *queue);
void    g_queue_push_tail(GQueue *queue, gpointer data);
void    g_queue_push_head(GQueue *queue, gpointer data);
gpointer g_queue_pop_tail(GQueue *queue);
gpointer g_queue_pop_head(GQueue *queue);
gpointer g_queue_peek_tail(GQueue *queue);
gpointer g_queue_peek_head(GQueue *queue);
guint   g_queue_get_length(GQueue *queue);

/* ------------------------------------------------------------------ */
/* GString                                                             */
/* ------------------------------------------------------------------ */

GString *g_string_new(const gchar *init);
GString *g_string_new_len(const gchar *init, gssize len);
GString *g_string_sized_new(gsize dfl_size);
GString *g_string_assign(GString *string, const gchar *rval);
GString *g_string_truncate(GString *string, gsize len);
GString *g_string_set_size(GString *string, gsize len);
GString *g_string_insert_len(GString *string, gssize pos,
                             const gchar *val, gssize len);
GString *g_string_append_len(GString *string, const gchar *val, gssize len);
GString *g_string_append(GString *string, const gchar *val);
GString *g_string_append_c(GString *string, gchar c);
GString *g_string_prepend(GString *string, const gchar *val);
GString *g_string_prepend_c(GString *string, gchar c);
GString *g_string_insert(GString *string, gssize pos, const gchar *val);
GString *g_string_insert_c(GString *string, gssize pos, gchar c);
GString *g_string_append_unichar(GString *string, gunichar wc);
GString *g_string_prepend_unichar(GString *string, gunichar wc);
GString *g_string_insert_unichar(GString *string, gssize pos, gunichar wc);
GString *g_string_erase(GString *string, gssize pos, gssize len);
void     g_string_printf(GString *string, const gchar *format, ...)
    G_GNUC_PRINTF(2, 3);
void     g_string_vprintf(GString *string, const gchar *format, va_list args)
    G_GNUC_PRINTF(2, 0);
void     g_string_append_printf(GString *string, const gchar *format, ...)
    G_GNUC_PRINTF(2, 3);
void     g_string_append_vprintf(GString *string, const gchar *format,
                                 va_list args) G_GNUC_PRINTF(2, 0);
gchar   *g_string_free(GString *string, gboolean free_segment);

/* ------------------------------------------------------------------ */
/* String utilities and ASCII helpers                                  */
/* ------------------------------------------------------------------ */

gpointer g_memdup(gconstpointer mem, guint byte_size) G_GNUC_MALLOC;
gpointer g_memdup2(gconstpointer mem, gsize byte_size) G_GNUC_MALLOC;
gpointer g_malloc(gsize n_bytes) G_GNUC_MALLOC G_GNUC_ALLOC_SIZE(1);
gpointer g_malloc0(gsize n_bytes) G_GNUC_MALLOC G_GNUC_ALLOC_SIZE(1);
gpointer g_realloc(gpointer mem, gsize n_bytes)
    G_GNUC_WARN_UNUSED_RESULT;
gpointer g_malloc_n(gsize n_blocks, gsize n_block_bytes)
    G_GNUC_MALLOC G_GNUC_ALLOC_SIZE2(1, 2);
gpointer g_malloc0_n(gsize n_blocks, gsize n_block_bytes)
    G_GNUC_MALLOC G_GNUC_ALLOC_SIZE2(1, 2);
gpointer g_realloc_n(gpointer mem, gsize n_blocks, gsize n_block_bytes)
    G_GNUC_WARN_UNUSED_RESULT;
gpointer g_try_malloc(gsize n_bytes) G_GNUC_MALLOC G_GNUC_ALLOC_SIZE(1);
gpointer g_try_malloc0(gsize n_bytes) G_GNUC_MALLOC G_GNUC_ALLOC_SIZE(1);
gpointer g_try_realloc(gpointer mem, gsize n_bytes)
    G_GNUC_WARN_UNUSED_RESULT;
gpointer g_try_malloc_n(gsize n_blocks, gsize n_block_bytes)
    G_GNUC_MALLOC G_GNUC_ALLOC_SIZE2(1, 2);
gpointer g_try_malloc0_n(gsize n_blocks, gsize n_block_bytes)
    G_GNUC_MALLOC G_GNUC_ALLOC_SIZE2(1, 2);
gpointer g_try_realloc_n(gpointer mem, gsize n_blocks, gsize n_block_bytes)
    G_GNUC_WARN_UNUSED_RESULT;
void     g_free(gpointer mem);

gchar  *g_strdup(const gchar *str) G_GNUC_MALLOC;
gchar  *g_strndup(const gchar *str, gsize n) G_GNUC_MALLOC;
gchar  *g_strdup_printf(const gchar *format, ...)
    G_GNUC_MALLOC G_GNUC_PRINTF(1, 2);
gchar  *g_strdup_vprintf(const gchar *format, va_list args)
    G_GNUC_MALLOC G_GNUC_PRINTF(1, 0);
void    g_strfreev(gchar **str_array);
guint   g_strv_length(gchar **str_array);
gchar **g_strsplit(const gchar *string, const gchar *delimiter,
                   gint max_tokens) G_GNUC_MALLOC;
gchar  *g_strjoinv(const gchar *separator, gchar **str_array) G_GNUC_MALLOC;
gchar  *g_strconcat(const gchar *string1, ...)
    G_GNUC_MALLOC G_GNUC_NULL_TERMINATED;
gsize   g_strlcpy(gchar *dest, const gchar *src, gsize dest_size);
gboolean g_str_has_prefix(const gchar *str, const gchar *prefix);
gboolean g_str_has_suffix(const gchar *str, const gchar *suffix);
gint    g_strcmp0(const gchar *str1, const gchar *str2);
guint   g_str_hash(gconstpointer v) G_GNUC_PURE;
gboolean g_str_equal(gconstpointer v1, gconstpointer v2);
guint   g_direct_hash(gconstpointer v) G_GNUC_CONST;
gboolean g_direct_equal(gconstpointer v1, gconstpointer v2) G_GNUC_CONST;
gint    g_ascii_strcasecmp(const gchar *s1, const gchar *s2);
gint    g_ascii_strncasecmp(const gchar *s1, const gchar *s2, gsize n);
gchar   g_ascii_tolower(gchar c) G_GNUC_CONST;
gchar   g_ascii_toupper(gchar c) G_GNUC_CONST;
gint    g_ascii_digit_value(gchar c) G_GNUC_CONST;
gint    g_ascii_xdigit_value(gchar c) G_GNUC_CONST;
gint64  g_ascii_strtoll(const gchar *nptr, gchar **endptr, guint base);
const gchar *g_strerror(gint errnum) G_GNUC_CONST;
gchar  *g_path_get_dirname(const gchar *file_name) G_GNUC_MALLOC;

typedef enum {
    G_ASCII_ALNUM  = 1 << 0,
    G_ASCII_ALPHA  = 1 << 1,
    G_ASCII_CNTRL  = 1 << 2,
    G_ASCII_DIGIT  = 1 << 3,
    G_ASCII_GRAPH  = 1 << 4,
    G_ASCII_LOWER  = 1 << 5,
    G_ASCII_PRINT  = 1 << 6,
    G_ASCII_PUNCT  = 1 << 7,
    G_ASCII_SPACE  = 1 << 8,
    G_ASCII_UPPER  = 1 << 9,
    G_ASCII_XDIGIT = 1 << 10
} GAsciiType;

GLIB_VAR const guint16 * const g_ascii_table;

#define g_ascii_isalnum(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_ALNUM) != 0)
#define g_ascii_isalpha(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_ALPHA) != 0)
#define g_ascii_iscntrl(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_CNTRL) != 0)
#define g_ascii_isdigit(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_DIGIT) != 0)
#define g_ascii_isgraph(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_GRAPH) != 0)
#define g_ascii_islower(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_LOWER) != 0)
#define g_ascii_isprint(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_PRINT) != 0)
#define g_ascii_ispunct(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_PUNCT) != 0)
#define g_ascii_isspace(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_SPACE) != 0)
#define g_ascii_isupper(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_UPPER) != 0)
#define g_ascii_isxdigit(c) ((g_ascii_table[(guchar) (c)] & G_ASCII_XDIGIT) != 0)

/* ------------------------------------------------------------------ */
/* Assertions and logging                                              */
/* ------------------------------------------------------------------ */

void g_assertion_message_expr(const gchar *domain, const gchar *file,
                              gint line, const gchar *func,
                              const gchar *expr) G_GNUC_NORETURN;
void g_assertion_message(const gchar *domain, const gchar *file, gint line,
                         const gchar *func, const gchar *message);
void g_return_if_fail_warning(const gchar *log_domain,
                              const gchar *pretty_function,
                              const gchar *expression);
void g_warn_message(const gchar *domain, const gchar *file, gint line,
                    const gchar *func, const gchar *warnexpr);
void g_error(const gchar *format, ...) G_GNUC_NORETURN G_GNUC_PRINTF(1, 2);
void g_critical(const gchar *format, ...) G_GNUC_PRINTF(1, 2);
void g_warning(const gchar *format, ...) G_GNUC_PRINTF(1, 2);
void g_message(const gchar *format, ...) G_GNUC_PRINTF(1, 2);
void g_debug(const gchar *format, ...) G_GNUC_PRINTF(1, 2);

#define g_assert(expr)                                                         \
    G_STMT_START {                                                             \
        if (G_LIKELY(expr)) {                                                  \
        } else {                                                               \
            g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__,         \
                                     G_STRFUNC, #expr);                        \
        }                                                                      \
    } G_STMT_END

#define g_assert_not_reached()                                                 \
    g_assertion_message_expr(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC, NULL)

#define g_return_if_fail(expr)                                                 \
    G_STMT_START {                                                             \
        if (G_LIKELY(expr)) {                                                  \
        } else {                                                               \
            g_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC, #expr);          \
            return;                                                            \
        }                                                                      \
    } G_STMT_END

#define g_return_val_if_fail(expr, val)                                        \
    G_STMT_START {                                                             \
        if (G_LIKELY(expr)) {                                                  \
        } else {                                                               \
            g_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC, #expr);          \
            return (val);                                                      \
        }                                                                      \
    } G_STMT_END

#define g_return_if_reached()                                                  \
    G_STMT_START {                                                             \
        g_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC,                      \
                                 "code should not be reached");                \
        return;                                                                \
    } G_STMT_END

#define g_return_val_if_reached(val)                                           \
    G_STMT_START {                                                             \
        g_return_if_fail_warning(G_LOG_DOMAIN, G_STRFUNC,                      \
                                 "code should not be reached");                \
        return (val);                                                          \
    } G_STMT_END

#define g_warn_if_fail(expr)                                                   \
    G_STMT_START {                                                             \
        if (G_LIKELY(expr)) {                                                  \
        } else {                                                               \
            g_warn_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,        \
                           #expr);                                             \
        }                                                                      \
    } G_STMT_END

#define g_warn_if_reached()                                                    \
    G_STMT_START {                                                             \
        g_warn_message(G_LOG_DOMAIN, __FILE__, __LINE__, G_STRFUNC,            \
                       "code should not be reached");                          \
    } G_STMT_END

/* Forward declarations needed by the auto-cleanup macros below. */
void g_error_free(GError *error);
void g_main_context_unref(GMainContext *context);
void g_source_unref(GSource *source);
void g_date_time_unref(GDateTime *datetime);

/* ------------------------------------------------------------------ */
/* Autocleanups, allocation helpers, misc macros                       */
/* ------------------------------------------------------------------ */

#ifdef __GNUC__
#define _GLIB_CLEANUP(func) __attribute__((__cleanup__(func)))
#define _GLIB_AUTOPTR_FUNC_NAME(TypeName) glib_autoptr_cleanup_##TypeName
#define _GLIB_AUTOPTR_TYPENAME(TypeName) TypeName *
#define _GLIB_AUTO_FUNC_NAME(TypeName) glib_auto_cleanup_##TypeName

#define G_DEFINE_AUTOPTR_CLEANUP_FUNC(TypeName, func)                          \
    static inline void _GLIB_AUTOPTR_FUNC_NAME(TypeName)(TypeName **_ptr)      \
    {                                                                          \
        if (_ptr && *_ptr) {                                                   \
            (func)(*_ptr);                                                     \
            *_ptr = NULL;                                                      \
        }                                                                      \
    }

#define G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(TypeName, func)                       \
    static inline void _GLIB_AUTO_FUNC_NAME(TypeName)(TypeName *_ptr)          \
    {                                                                          \
        (func)(_ptr);                                                          \
    }

#define G_DEFINE_AUTO_CLEANUP_FREE_FUNC(TypeName, func, none)                  \
    static inline void _GLIB_AUTO_FUNC_NAME(TypeName)(TypeName *_ptr)          \
    {                                                                          \
        if (*_ptr != (none)) {                                                 \
            (func)(*_ptr);                                                     \
        }                                                                      \
    }

#define g_autoptr(TypeName)                                                    \
    _GLIB_CLEANUP(_GLIB_AUTOPTR_FUNC_NAME(TypeName)) _GLIB_AUTOPTR_TYPENAME(TypeName)
#define g_auto(TypeName)                                                       \
    _GLIB_CLEANUP(_GLIB_AUTO_FUNC_NAME(TypeName)) TypeName
#define g_autofree _GLIB_CLEANUP(g_autoptr_cleanup_generic_gfree)
#else
#define G_DEFINE_AUTOPTR_CLEANUP_FUNC(TypeName, func)
#define G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(TypeName, func)
#define G_DEFINE_AUTO_CLEANUP_FREE_FUNC(TypeName, func, none)
#define g_autoptr(TypeName) TypeName *
#define g_auto(TypeName) TypeName
#define g_autofree
#endif

static inline void g_autoptr_cleanup_generic_gfree(void *ptr)
{
    void **pp = (void **) ptr;

    if (pp && *pp) {
        g_free(*pp);
        *pp = NULL;
    }
}

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GArray, g_array_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GPtrArray, g_ptr_array_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GByteArray, g_byte_array_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GHashTable, g_hash_table_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GSList, g_slist_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GList, g_list_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GQueue, g_queue_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GError, g_error_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GMainContext, g_main_context_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GSource, g_source_unref)
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(GStrv, g_strfreev, NULL)

static inline void g_autoptr_cleanup_gstring_free(GString *string)
{
    g_string_free(string, TRUE);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GString, g_autoptr_cleanup_gstring_free)
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GDateTime, g_date_time_unref)

#define g_new(struct_type, n_structs)                                          \
    ((struct_type *) g_malloc_n((n_structs), sizeof(struct_type)))
#define g_new0(struct_type, n_structs)                                         \
    ((struct_type *) g_malloc0_n((n_structs), sizeof(struct_type)))
#define g_renew(struct_type, mem, n_structs)                                   \
    ((struct_type *) g_realloc_n((mem), (n_structs), sizeof(struct_type)))
#define g_try_new(struct_type, n_structs)                                      \
    ((struct_type *) g_try_malloc_n((n_structs), sizeof(struct_type)))
#define g_try_new0(struct_type, n_structs)                                     \
    ((struct_type *) g_try_malloc0_n((n_structs), sizeof(struct_type)))
#define g_try_renew(struct_type, mem, n_structs)                               \
    ((struct_type *) g_try_realloc_n((mem), (n_structs), sizeof(struct_type)))
#define g_alloca(size) __builtin_alloca(size)
#define g_newa(struct_type, n_structs)                                         \
    ((struct_type *) g_alloca(sizeof(struct_type) * (n_structs)))

#define g_clear_pointer(pp, destroy)                                           \
    G_STMT_START {                                                             \
        gpointer *_pp = (gpointer *) (pp);                                     \
        gpointer _p = *_pp;                                                    \
        if (_p) {                                                              \
            *_pp = NULL;                                                       \
            (destroy)(_p);                                                     \
        }                                                                      \
    } G_STMT_END

#define g_steal_pointer(pp)                                                    \
    G_GNUC_EXTENSION ({                                                        \
        __typeof__(*(pp)) _p = *(pp);                                          \
        *(pp) = NULL;                                                          \
        _p;                                                                    \
    })

/* ------------------------------------------------------------------ */
/* GLib entry points which QEMU headers may reference but which this   */
/* narrow layer does not implement.  They are declared to keep source  */
/* parsing possible; glib.c provides no definitions, so reachable uses */
/* fail loudly at link time.                                           */
/* ------------------------------------------------------------------ */

gboolean g_once_init_enter(volatile void *location);
void     g_once_init_leave(volatile void *location, gsize result);
gboolean g_test_slow(void);
gboolean g_test_thorough(void);
gboolean g_test_quick(void);
gboolean g_close(gint fd, GError **error);
const gchar *g_get_prgname(void);
void     g_set_prgname(const gchar *prgname);
const gchar *g_get_tmp_dir(void);
gchar   *g_getenv(const gchar *variable);
gint64   g_get_real_time(void);
void     g_usleep(gulong microseconds);
guint32  g_random_int(void);
gint32   g_random_int_range(gint32 begin, gint32 end);
void     g_log_set_default_handler(GLogFunc log_func, gpointer user_data);

gboolean g_file_get_contents(const gchar *filename, gchar **contents,
                             gsize *length, GError **error);
gboolean g_file_set_contents(const gchar *filename, const gchar *contents,
                             gssize length, GError **error);
gchar   *g_file_read_link(const gchar *filename, GError **error);

#define G_FILE_ERROR g_file_error_quark()
GQuark    g_file_error_quark(void);
GFileError g_file_error_from_errno(gint err_no);

GMainContext *g_main_context_default(void);
GMainContext *g_main_context_new(void);
GMainContext *g_main_context_ref(GMainContext *context);
gboolean      g_main_context_acquire(GMainContext *context);
void          g_main_context_release(GMainContext *context);
gboolean      g_main_context_prepare(GMainContext *context, gint *priority);
gint          g_main_context_query(GMainContext *context, gint max_priority,
                                   gint *timeout_, GPollFD *fds,
                                   gint n_fds);
gint          g_main_context_check(GMainContext *context, gint max_priority,
                                   GPollFD *fds, gint n_fds);
gboolean      g_main_context_dispatch(GMainContext *context);
GSource      *g_main_context_find_source_by_id(GMainContext *context,
                                               guint source_id);
GMainLoop    *g_main_loop_new(GMainContext *context, gboolean is_running);
void          g_main_loop_run(GMainLoop *loop);
void          g_main_loop_quit(GMainLoop *loop);
void          g_main_loop_unref(GMainLoop *loop);
gint          g_poll(GPollFD *fds, guint nfds, gint timeout_);

GSource *g_source_new(GSourceFuncs *source_funcs, guint struct_size);
GSource *g_source_ref(GSource *source);
guint    g_source_attach(GSource *source, GMainContext *context);
void     g_source_destroy(GSource *source);
gboolean g_source_is_destroyed(GSource *source);
void     g_source_set_callback(GSource *source, GSourceFunc func,
                               gpointer data, GDestroyNotify notify);
void     g_source_set_can_recurse(GSource *source, gboolean can_recurse);
void     g_source_set_name(GSource *source, const gchar *name);
void     g_source_set_priority(GSource *source, gint priority);
void     g_source_add_poll(GSource *source, GPollFD *fd);
void     g_source_remove_poll(GSource *source, GPollFD *fd);
gpointer g_source_add_unix_fd(GSource *source, gint fd, GIOCondition events);
GIOCondition g_source_query_unix_fd(GSource *source, gpointer tag);
void     g_source_remove_unix_fd(GSource *source, gpointer tag);
GSource *g_timeout_source_new(guint interval);
GSource *g_timeout_source_new_seconds(guint interval);
guint    g_timeout_add(guint interval, GSourceFunc function, gpointer data);
guint    g_timeout_add_seconds(guint interval, GSourceFunc function,
                               gpointer data);

GThreadPool *g_thread_pool_new(GFunc func, gpointer user_data,
                               gint max_threads, gboolean exclusive,
                               GError **error);
void g_thread_pool_free(GThreadPool *pool, gboolean immediate,
                        gboolean wait_);
gboolean g_thread_pool_push(GThreadPool *pool, gpointer data, GError **error);
gboolean g_thread_pool_set_max_threads(GThreadPool *pool, gint max_threads,
                                       GError **error);

GDateTime *g_date_time_new_now_utc(void);
gchar     *g_date_time_format_iso8601(GDateTime *datetime);
GMappedFile *g_mapped_file_new_from_fd(gint fd, gboolean writable,
                                       GError **error);
gsize        g_mapped_file_get_length(GMappedFile *file);
gchar       *g_mapped_file_get_contents(GMappedFile *file);
GMappedFile *g_mapped_file_ref(GMappedFile *file);
void         g_mapped_file_unref(GMappedFile *file);
GPatternSpec *g_pattern_spec_new(const gchar *pattern) G_GNUC_MALLOC;
gboolean      g_pattern_match_string(GPatternSpec *pspec,
                                     const gchar *string);
void          g_pattern_spec_free(GPatternSpec *pspec);
gboolean     g_pattern_match_simple(const gchar *pattern, const gchar *string);

gboolean g_unix_open_pipe(gint *fds, gint flags, GError **error);
gboolean g_unix_set_fd_nonblocking(gint fd, gboolean nonblock, GError **error);

#ifdef __cplusplus
}
#endif

#endif /* __GLIB_H__ */
