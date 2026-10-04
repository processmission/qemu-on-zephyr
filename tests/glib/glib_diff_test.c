/*
 * Differential GLib compatibility fixture.
 *
 * This file is compiled twice by glib-checks/run_diff_test.sh:
 *   - against the host's real GLib
 *   - against qemu/ports/zephyr/glib
 * The outputs must be byte-for-byte identical for the covered semantics.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#ifdef GLIB_DIFF_HAVE_PTHREAD
#include <pthread.h>
#endif

static int key_destroy_count;
static int value_destroy_count;
static int ptr_free_count;

static void count_key_destroy(gpointer data)
{
    (void) data;
    key_destroy_count++;
}

static void count_value_destroy(gpointer data)
{
    (void) data;
    value_destroy_count++;
}

static void count_ptr_free(gpointer data)
{
    (void) data;
    ptr_free_count++;
}

static void print_hex(const char *label, const unsigned char *data, size_t len)
{
    size_t i;

    printf("%s len=%lu bytes=", label, (unsigned long) len);
    for (i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
    printf("\n");
}

static void print_strv(const char *label, gchar **strv)
{
    guint i;

    printf("%s:", label);
    if (!strv) {
        printf(" NULL\n");
        return;
    }
    for (i = 0; strv[i] != NULL; i++) {
        printf(" [%s]", strv[i]);
    }
    printf(" len=%u\n", g_strv_length(strv));
}

static gint cmp_string_ptr(gconstpointer a, gconstpointer b)
{
    const char *sa = *(const char * const *) a;
    const char *sb = *(const char * const *) b;

    return strcmp(sa, sb);
}

static gint cmp_string_data(gconstpointer a, gconstpointer b)
{
    return strcmp((const char *) a, (const char *) b);
}

static guint hash_constant(gconstpointer key)
{
    (void) key;
    return 1;
}

static gboolean remove_value_two(gpointer key, gpointer value, gpointer data)
{
    (void) key;
    (void) data;
    return value == (gpointer) (gintptr) 2;
}

static void test_memory(void)
{
    gchar *dup;
    gpointer p;
    gboolean zero_alloc;
    gboolean zero_alloc_n;
    gboolean zero_alloc0_n;
    gboolean memdup_zero;
    gboolean memdup_null;
    gboolean realloc_nonnull;
    gboolean realloc_zero;

    zero_alloc = g_malloc(0) == NULL;
    zero_alloc_n = g_malloc_n(0, 4) == NULL;
    zero_alloc0_n = g_malloc0_n(0, 4) == NULL;
    printf("memory malloc0=%d malloc_n0=%d malloc0_n0=%d\n",
           zero_alloc, zero_alloc_n, zero_alloc0_n);

    memdup_zero = g_memdup("abc", 0) == NULL;
    memdup_null = g_memdup(NULL, 4) == NULL;
    printf("memory memdup0=%d memdupNULL=%d\n", memdup_zero, memdup_null);

    p = g_realloc(NULL, 8);
    realloc_nonnull = p != NULL;
    realloc_zero = g_realloc(p, 0) == NULL;
    printf("memory reallocNULL=%d realloc0=%d\n",
           realloc_nonnull, realloc_zero);

    dup = g_strndup("abc", 0);
    printf("memory strndup0='%s'\n", dup);
    g_free(dup);
    dup = g_strndup("abc", 2);
    printf("memory strndup2='%s'\n", dup);
    g_free(dup);
    printf("memory strdupNULL=%d\n", g_strdup(NULL) == NULL);
}

static void test_hash_ownership(void)
{
    GHashTable *h;
    gpointer key_alpha = g_strdup("alpha");
    gpointer key_alpha2 = g_strdup("alpha");
    gpointer key_beta = g_strdup("beta");
    gpointer key_beta2 = g_strdup("beta");
    gboolean r;
    gpointer lookup;
    guint size;
    gboolean removed;

    key_destroy_count = 0;
    value_destroy_count = 0;

    h = g_hash_table_new_full(g_str_hash, g_str_equal,
                              count_key_destroy, count_value_destroy);

    r = g_hash_table_insert(h, key_alpha, (gpointer) (gintptr) 1);
    printf("hash insert-new=%d kd=%d vd=%d\n",
           r, key_destroy_count, value_destroy_count);

    r = g_hash_table_insert(h, key_alpha2, (gpointer) (gintptr) 2);
    printf("hash insert-dup=%d kd=%d vd=%d\n",
           r, key_destroy_count, value_destroy_count);

    lookup = g_hash_table_lookup(h, "alpha");
    printf("hash lookup=%ld\n", (long) (gintptr) lookup);

    r = g_hash_table_replace(h, g_strdup("alpha"), (gpointer) (gintptr) 3);
    printf("hash replace=%d kd=%d vd=%d\n",
           r, key_destroy_count, value_destroy_count);

    r = g_hash_table_add(h, key_beta);
    printf("hash add-new=%d kd=%d vd=%d\n",
           r, key_destroy_count, value_destroy_count);

    r = g_hash_table_add(h, key_beta2);
    printf("hash add-dup=%d kd=%d vd=%d\n",
           r, key_destroy_count, value_destroy_count);

    size = g_hash_table_size(h);
    printf("hash size=%u\n", size);

    removed = g_hash_table_remove(h, "alpha");
    printf("hash remove=%d kd=%d vd=%d\n",
           removed, key_destroy_count, value_destroy_count);

    removed = g_hash_table_remove(h, "missing");
    printf("hash remove-missing=%d\n", removed);
    printf("hash contains-beta=%d\n", g_hash_table_contains(h, "beta"));

    g_hash_table_unref(h);
    printf("hash unref kd=%d vd=%d\n", key_destroy_count,
           value_destroy_count);
}

static void test_hash_collision_iterator(void)
{
    GHashTable *h = g_hash_table_new_full(hash_constant, g_str_equal,
                                          count_key_destroy,
                                          count_value_destroy);
    GHashTableIter iter;
    gpointer key;
    gpointer value;
    int removed = 0;

    key_destroy_count = 0;
    value_destroy_count = 0;

    g_hash_table_insert(h, g_strdup("one"), g_strdup("1"));
    g_hash_table_insert(h, g_strdup("two"), g_strdup("2"));
    g_hash_table_insert(h, g_strdup("three"), g_strdup("3"));
    g_hash_table_insert(h, g_strdup("four"), g_strdup("4"));
    g_hash_table_insert(h, g_strdup("five"), g_strdup("5"));

    g_hash_table_iter_init(&iter, h);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        if (strcmp((const char *) key, "three") == 0) {
            g_hash_table_iter_remove(&iter);
            removed++;
        }
    }

    printf("iter collision removed=%d size=%u kd=%d vd=%d\n",
           removed, g_hash_table_size(h), key_destroy_count,
           value_destroy_count);
    printf("iter collision lookup one=%s three=%d five=%s\n",
           (const char *) g_hash_table_lookup(h, "one"),
           g_hash_table_lookup(h, "three") == NULL,
           (const char *) g_hash_table_lookup(h, "five"));

    g_hash_table_unref(h);
    printf("iter collision unref kd=%d vd=%d\n",
           key_destroy_count, value_destroy_count);
}

static void test_hash_foreach_remove(void)
{
    GHashTable *h = g_hash_table_new_full(hash_constant, g_str_equal,
                                          count_key_destroy,
                                          count_value_destroy);
    guint removed;

    key_destroy_count = 0;
    value_destroy_count = 0;

    g_hash_table_insert(h, g_strdup("one"), (gpointer) (gintptr) 1);
    g_hash_table_insert(h, g_strdup("two"), (gpointer) (gintptr) 2);
    g_hash_table_insert(h, g_strdup("three"), (gpointer) (gintptr) 3);

    removed = g_hash_table_foreach_remove(h, remove_value_two, NULL);
    printf("foreach-remove removed=%u size=%u kd=%d vd=%d\n",
           removed, g_hash_table_size(h), key_destroy_count,
           value_destroy_count);
    g_hash_table_unref(h);
}

static gint cmp_uint32(gconstpointer a, gconstpointer b)
{
    guint32 va = *(const guint32 *) a;
    guint32 vb = *(const guint32 *) b;

    return (va > vb) - (va < vb);
}

static void test_array(void)
{
    guint32 values[2] = { 0x11223344u, 0x55667788u };
    guint32 extra = 0xaabbccddu;
    guint32 unsorted[5] = { 5, 1, 3, 2, 4 };
    guint32 sorted_value;
    GArray *array;
    gchar *segment;
    guint32 *words;
    guint i;

    array = g_array_new(TRUE, TRUE, sizeof(guint32));
    g_array_append_vals(array, values, 2);
    words = (guint32 *) (void *) array->data;
    printf("array append len=%u v0=%08x v1=%08x term=%08x\n",
           array->len, words[0], words[1], words[2]);

    g_array_set_size(array, 5);
    words = (guint32 *) (void *) array->data;
    printf("array grow len=%u v2=%08x v4=%08x term=%08x\n",
           array->len, words[2], words[4], words[5]);

    g_array_set_size(array, 1);
    words = (guint32 *) (void *) array->data;
    printf("array shrink len=%u v0=%08x term=%08x\n",
           array->len, words[0], words[1]);

    g_array_append_vals(array, &extra, 1);
    words = (guint32 *) (void *) array->data;
    printf("array append-again len=%u v1=%08x term=%08x\n",
           array->len, words[1], words[2]);

    segment = g_array_free(array, FALSE);
    words = (guint32 *) (void *) segment;
    printf("array free-false v0=%08x v1=%08x term=%08x\n",
           words[0], words[1], words[2]);
    g_free(segment);

    array = g_array_sized_new(TRUE, TRUE, sizeof(guint32), 4);
    words = (guint32 *) (void *) array->data;
    printf("array sized len=%u term=%08x data-nonnull=%d\n",
           array->len, words[0], array->data != NULL);
    g_array_set_size(array, 3);
    words = (guint32 *) (void *) array->data;
    printf("array sized-grow len=%u v0=%08x v2=%08x term=%08x\n",
           array->len, words[0], words[2], words[3]);
    g_array_free(array, TRUE);

    array = g_array_new(FALSE, FALSE, sizeof(guint32));
    g_array_append_vals(array, unsorted, G_N_ELEMENTS(unsorted));
    g_array_sort(array, cmp_uint32);
    printf("array sort len=%u:", array->len);
    for (i = 0; i < array->len; i++) {
        sorted_value = g_array_index(array, guint32, i);
        printf(" %u", sorted_value);
    }
    printf("\n");
    g_array_free(array, TRUE);
}

static int clear_count;

static void count_clear(gpointer data)
{
    (void) data;
    clear_count++;
}

static void test_refcounts(void)
{
    guint32 values[2] = { 0x01020304u, 0x05060708u };
    guint8 bytes[3] = { 0xaa, 0xbb, 0xcc };
    GArray *array;
    GArray *array_ref;
    GPtrArray *ptr_array;
    GPtrArray *ptr_array_ref;
    GByteArray *byte_array;
    GByteArray *byte_array_ref;
    GHashTable *hash;
    GHashTable *hash_ref;
    gchar *array_data;
    gpointer *ptr_data;
    guint8 *byte_data;
    gpointer old_array_data;
    gpointer old_ptr_data;
    guint i;

    clear_count = 0;
    array = g_array_new(FALSE, FALSE, sizeof(guint32));
    g_array_set_clear_func(array, count_clear);
    g_array_append_vals(array, values, 2);
    array_ref = g_array_ref(array);
    old_array_data = array->data;
    array_data = g_array_free(array, FALSE);
    printf("array ref-free-false same-data=%d len=%u data-null=%d "
           "other-len=%u other-data-null=%d\n",
           array_data == old_array_data,
           array->len, array->data == NULL,
           array_ref->len, array_ref->data == NULL);
    printf("array ref-free-false values=%08x %08x clear=%d\n",
           ((guint32 *) (void *) array_data)[0],
           ((guint32 *) (void *) array_data)[1], clear_count);
    g_free(array_data);
    g_array_unref(array_ref);

    clear_count = 0;
    array = g_array_new(FALSE, FALSE, sizeof(guint32));
    g_array_set_clear_func(array, count_clear);
    g_array_append_vals(array, values, 2);
    array_ref = g_array_ref(array);
    array_data = g_array_free(array, TRUE);
    printf("array ref-free-true ret-null=%d len=%u data-null=%d "
           "other-len=%u other-data-null=%d clear=%d\n",
           array_data == NULL, array->len, array->data == NULL,
           array_ref->len, array_ref->data == NULL, clear_count);
    g_array_unref(array_ref);
    printf("array ref-free-true after-unref clear=%d\n", clear_count);

    ptr_free_count = 0;
    ptr_array = g_ptr_array_new();
    g_ptr_array_set_free_func(ptr_array, count_ptr_free);
    g_ptr_array_add(ptr_array, (gpointer) "one");
    g_ptr_array_add(ptr_array, (gpointer) "two");
    ptr_array_ref = g_ptr_array_ref(ptr_array);
    old_ptr_data = ptr_array->pdata;
    ptr_data = g_ptr_array_free(ptr_array, FALSE);
    printf("ptr-array ref-free-false same-data=%d len=%u data-null=%d "
           "other-len=%u other-data-null=%d free-count=%d\n",
           ptr_data == old_ptr_data,
           ptr_array->len, ptr_array->pdata == NULL,
           ptr_array_ref->len, ptr_array_ref->pdata == NULL,
           ptr_free_count);
    printf("ptr-array ref-free-false values=%s %s\n",
           (const char *) ptr_data[0], (const char *) ptr_data[1]);
    g_free(ptr_data);
    g_ptr_array_unref(ptr_array_ref);

    ptr_free_count = 0;
    ptr_array = g_ptr_array_new();
    g_ptr_array_set_free_func(ptr_array, count_ptr_free);
    g_ptr_array_add(ptr_array, (gpointer) "one");
    g_ptr_array_add(ptr_array, (gpointer) "two");
    ptr_array_ref = g_ptr_array_ref(ptr_array);
    ptr_data = g_ptr_array_free(ptr_array, TRUE);
    printf("ptr-array ref-free-true ret-null=%d len=%u data-null=%d "
           "other-len=%u other-data-null=%d free-count=%d\n",
           ptr_data == NULL, ptr_array->len, ptr_array->pdata == NULL,
           ptr_array_ref->len, ptr_array_ref->pdata == NULL,
           ptr_free_count);
    g_ptr_array_unref(ptr_array_ref);
    printf("ptr-array ref-free-true after-unref free-count=%d\n",
           ptr_free_count);

    byte_array = g_byte_array_new();
    g_byte_array_append(byte_array, bytes, 3);
    byte_array_ref = g_byte_array_ref(byte_array);
    byte_data = g_byte_array_free(byte_array, FALSE);
    printf("byte-array ref-free-false len=%u data-null=%d other-len=%u "
           "other-data-null=%d values=%02x%02x%02x\n",
           byte_array->len, byte_array->data == NULL,
           byte_array_ref->len, byte_array_ref->data == NULL,
           byte_data[0], byte_data[1], byte_data[2]);
    g_free(byte_data);
    g_byte_array_unref(byte_array_ref);

    key_destroy_count = 0;
    value_destroy_count = 0;
    hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                 count_key_destroy, count_value_destroy);
    g_hash_table_insert(hash, g_strdup("a"), g_strdup("A"));
    g_hash_table_insert(hash, g_strdup("b"), g_strdup("B"));
    hash_ref = g_hash_table_ref(hash);
    g_hash_table_destroy(hash);
    printf("hash ref-destroy size=%u lookup-null=%d kd=%d vd=%d\n",
           g_hash_table_size(hash), g_hash_table_lookup(hash, "a") == NULL,
           key_destroy_count, value_destroy_count);
    g_hash_table_unref(hash_ref);
    printf("hash ref-destroy after-unref kd=%d vd=%d\n",
           key_destroy_count, value_destroy_count);

    clear_count = 0;
    array = g_array_new(FALSE, FALSE, sizeof(guint32));
    g_array_set_clear_func(array, count_clear);
    g_array_append_vals(array, values, 2);
    g_array_set_size(array, 1);
    printf("array clear-shrink len=%u clear=%d\n",
           array->len, clear_count);
    g_array_free(array, TRUE);
    printf("array clear-shrink after-free clear=%d\n", clear_count);

    array = g_array_new(FALSE, FALSE, sizeof(guint32));
    for (i = 0; i < 10000; i++) {
        array_ref = g_array_ref(array);
        g_array_unref(array_ref);
    }
    g_array_unref(array);
    printf("array ref-loop ok\n");
}

#ifdef GLIB_DIFF_HAVE_PTHREAD
#define REF_THREAD_COUNT 4
#define REF_THREAD_ITERS 20000

static void *array_ref_thread(void *opaque)
{
    GArray *array = opaque;
    int i;

    for (i = 0; i < REF_THREAD_ITERS; i++) {
        GArray *ref = g_array_ref(array);

        g_array_unref(ref);
    }
    return NULL;
}

static void test_atomic_refcount_threads(void)
{
    GArray *array = g_array_new(FALSE, FALSE, sizeof(guint32));
    pthread_t threads[REF_THREAD_COUNT];
    int i;

    for (i = 0; i < REF_THREAD_COUNT; i++) {
        if (pthread_create(&threads[i], NULL, array_ref_thread, array) != 0) {
            fprintf(stderr, "pthread_create failed\n");
            exit(2);
        }
    }
    for (i = 0; i < REF_THREAD_COUNT; i++) {
        pthread_join(threads[i], NULL);
    }
    g_array_unref(array);
    printf("array ref threads ok\n");
}
#endif

static void test_ptr_array(void)
{
    GPtrArray *array;
    gpointer *pdata;
    guint i;
    gboolean removed;

    ptr_free_count = 0;
    array = g_ptr_array_new();
    g_ptr_array_set_free_func(array, count_ptr_free);
    g_ptr_array_add(array, (gpointer) "delta");
    g_ptr_array_add(array, (gpointer) "alpha");
    g_ptr_array_add(array, (gpointer) "charlie");
    g_ptr_array_add(array, (gpointer) "bravo");
    g_ptr_array_sort(array, cmp_string_ptr);
    printf("ptr-array sorted:");
    for (i = 0; i < array->len; i++) {
        printf(" %s", (const char *) array->pdata[i]);
    }
    printf("\n");

    removed = g_ptr_array_remove(array, (gpointer) "charlie");
    printf("ptr-array remove=%d free-count=%d len=%u\n",
           removed, ptr_free_count, array->len);

    pdata = g_ptr_array_free(array, FALSE);
    printf("ptr-array free-false free-count=%d first=%s\n",
           ptr_free_count, (const char *) pdata[0]);
    g_free(pdata);

    ptr_free_count = 0;
    array = g_ptr_array_new_with_free_func(count_ptr_free);
    g_ptr_array_add(array, NULL);
    g_ptr_array_add(array, (gpointer) "x");
    g_ptr_array_free(array, TRUE);
    printf("ptr-array free-true-null free-count=%d\n", ptr_free_count);
}

static void test_slist(void)
{
    GSList *list = NULL;
    GSList *node;
    static const char *keys[] = { "b", "a", "d" };

    list = g_slist_append(list, (gpointer) keys[0]);
    list = g_slist_prepend(list, (gpointer) keys[1]);
    list = g_slist_append(list, (gpointer) keys[2]);
    list = g_slist_insert_sorted(list, (gpointer) "c", cmp_string_data);
    printf("slist insert-sorted len=%u:", g_slist_length(list));
    for (node = list; node; node = node->next) {
        printf(" %s", (const char *) node->data);
    }
    printf("\n");
    list = g_slist_remove(list, (gpointer) "b");
    printf("slist remove-b len=%u:", g_slist_length(list));
    for (node = list; node; node = node->next) {
        printf(" %s", (const char *) node->data);
    }
    printf("\n");
    g_slist_free(list);
}

struct sort_item {
    const char *key;
    int sequence;
};

static gint cmp_sort_item(gconstpointer a, gconstpointer b, gpointer data)
{
    const struct sort_item *ia = a;
    const struct sort_item *ib = b;

    (void) data;
    return strcmp(ia->key, ib->key);
}

static void test_slist_stable_sort(void)
{
    static struct sort_item items[] = {
        { "a", 0 }, { "b", 1 }, { "a", 2 }, { "b", 3 }, { "a", 4 }
    };
    GSList *list = NULL;
    GSList *node;
    guint i;

    for (i = 0; i < G_N_ELEMENTS(items); i++) {
        list = g_slist_append(list, &items[i]);
    }
    list = g_slist_sort_with_data(list, cmp_sort_item, NULL);
    printf("slist stable:");
    for (node = list; node; node = node->next) {
        const struct sort_item *item = node->data;

        printf(" %s%d", item->key, item->sequence);
    }
    printf("\n");
    g_slist_free(list);
}

static void test_queue(void)
{
    GQueue stack_queue;
    GQueue init_queue = G_QUEUE_INIT;
    GQueue *queue;
    gpointer data;
    gchar *heap_one;
    gchar *heap_two;

    printf("queue init-macro empty=%d len=%u head-null=%d tail-null=%d\n",
           g_queue_is_empty(&init_queue), g_queue_get_length(&init_queue),
           init_queue.head == NULL, init_queue.tail == NULL);

    g_queue_init(&stack_queue);
    printf("queue init empty=%d len=%u head-null=%d tail-null=%d\n",
           g_queue_is_empty(&stack_queue),
           g_queue_get_length(&stack_queue),
           stack_queue.head == NULL, stack_queue.tail == NULL);
    g_queue_push_tail(&stack_queue, (gpointer) "stack-tail");
    g_queue_push_head(&stack_queue, (gpointer) "stack-head");
    printf("queue stack len=%u head=%s tail=%s head-next=%s tail-prev=%s\n",
           g_queue_get_length(&stack_queue),
           (const char *) stack_queue.head->data,
           (const char *) stack_queue.tail->data,
           (const char *) stack_queue.head->next->data,
           (const char *) stack_queue.tail->prev->data);
    g_queue_clear(&stack_queue);
    printf("queue stack clear empty=%d len=%u head-null=%d tail-null=%d\n",
           g_queue_is_empty(&stack_queue),
           g_queue_get_length(&stack_queue),
           stack_queue.head == NULL, stack_queue.tail == NULL);

    queue = g_queue_new();
    printf("queue new empty=%d len=%u head-null=%d tail-null=%d\n",
           g_queue_is_empty(queue), g_queue_get_length(queue),
           queue->head == NULL, queue->tail == NULL);

    g_queue_push_tail(queue, (gpointer) "a");
    g_queue_push_tail(queue, (gpointer) "b");
    g_queue_push_tail(queue, (gpointer) "c");
    printf("queue push-tail len=%u head=%s tail=%s head-next=%s "
           "tail-prev=%s head-prev-null=%d tail-next-null=%d\n",
           g_queue_get_length(queue),
           (const char *) queue->head->data,
           (const char *) queue->tail->data,
           (const char *) queue->head->next->data,
           (const char *) queue->tail->prev->data,
           queue->head->prev == NULL, queue->tail->next == NULL);

    g_queue_push_head(queue, (gpointer) "z");
    printf("queue push-head len=%u head=%s tail=%s head-next=%s "
           "tail-prev=%s\n",
           g_queue_get_length(queue),
           (const char *) queue->head->data,
           (const char *) queue->tail->data,
           (const char *) queue->head->next->data,
           (const char *) queue->tail->prev->data);

    printf("queue peek head=%s tail=%s\n",
           (const char *) g_queue_peek_head(queue),
           (const char *) g_queue_peek_tail(queue));

    data = g_queue_pop_tail(queue);
    printf("queue pop-tail=%s len=%u tail=%s tail-next-null=%d\n",
           (const char *) data, g_queue_get_length(queue),
           (const char *) queue->tail->data, queue->tail->next == NULL);

    data = g_queue_pop_head(queue);
    printf("queue pop-head=%s len=%u head=%s head-prev-null=%d\n",
           (const char *) data, g_queue_get_length(queue),
           (const char *) queue->head->data, queue->head->prev == NULL);

    printf("queue drain:");
    while (!g_queue_is_empty(queue)) {
        printf(" %s", (const char *) g_queue_pop_head(queue));
    }
    printf(" len=%u\n", g_queue_get_length(queue));
    printf("queue empty-pop head-null=%d tail-null=%d\n",
           g_queue_pop_head(queue) == NULL, g_queue_pop_tail(queue) == NULL);
    g_queue_free(queue);

    ptr_free_count = 0;
    queue = g_queue_new();
    g_queue_push_tail(queue, (gpointer) "free-full-one");
    g_queue_push_tail(queue, (gpointer) "free-full-two");
    g_queue_free_full(queue, count_ptr_free);
    printf("queue free-full count=%d\n", ptr_free_count);

    ptr_free_count = 0;
    queue = g_queue_new();
    g_queue_push_tail(queue, (gpointer) "clear-full-one");
    g_queue_push_tail(queue, (gpointer) "clear-full-two");
    g_queue_clear_full(queue, count_ptr_free);
    printf("queue clear-full count=%d empty=%d len=%u\n",
           ptr_free_count, g_queue_is_empty(queue),
           g_queue_get_length(queue));
    g_queue_free(queue);

    ptr_free_count = 0;
    queue = g_queue_new();
    g_queue_push_tail(queue, (gpointer) "free-no-data");
    g_queue_free(queue);
    printf("queue free-no-data count=%d\n", ptr_free_count);

    heap_one = g_strdup("heap-one");
    heap_two = g_strdup("heap-two");
    queue = g_queue_new();
    g_queue_push_tail(queue, heap_one);
    g_queue_push_head(queue, heap_two);
    g_queue_clear(queue);
    printf("queue clear heap empty=%d len=%u head-null=%d tail-null=%d\n",
           g_queue_is_empty(queue), g_queue_get_length(queue),
           queue->head == NULL, queue->tail == NULL);
    g_free(heap_one);
    g_free(heap_two);
    g_queue_free(queue);
    printf("queue clear data-owned\n");
}

static void test_string(void)
{
    GString *string;
    gchar *segment;

    string = g_string_new("abc");
    g_string_insert_len(string, -1, "XY", 2);
    printf("string insert-end len=%lu str=%s\n",
           (unsigned long) string->len, string->str);
    g_string_insert_len(string, -100, "QQ", 2);
    printf("string insert-negative len=%lu str=%s\n",
           (unsigned long) string->len, string->str);
    g_string_insert_len(string, 1, "a\0b", 3);
    print_hex("string insert-embedded", (const unsigned char *) string->str,
              string->len);
    g_string_append_len(string, "c\0d", 3);
    print_hex("string append-embedded", (const unsigned char *) string->str,
              string->len);
    g_string_append(string, "e");
    g_string_append_printf(string, "-%d", 7);
    printf("string append-printf len=%lu str=%s\n",
           (unsigned long) string->len, string->str);
    g_string_printf(string, "%s:%d", "fmt", 9);
    printf("string printf len=%lu str=%s\n",
           (unsigned long) string->len, string->str);
    g_string_erase(string, 0, 2);
    printf("string erase len=%lu str=%s\n",
           (unsigned long) string->len, string->str);
    g_string_truncate(string, 2);
    printf("string truncate len=%lu str=%s\n",
           (unsigned long) string->len, string->str);
    g_string_set_size(string, 5);
    printf("string set-size len=%lu prefix=%c%c nul=%d\n",
           (unsigned long) string->len, string->str[0], string->str[1],
           string->str[5] == '\0');
    memset(string->str + 2, 'X', 3);
    print_hex("string set-size-initialized",
              (const unsigned char *) string->str, string->len);
    segment = g_string_free(string, FALSE);
    print_hex("string free-false", (const unsigned char *) segment, 6);
    g_free(segment);

    string = g_string_new_len("x\0y", 3);
    print_hex("string new-len", (const unsigned char *) string->str,
              string->len);
    g_string_free(string, TRUE);
}

static void test_string_helpers(void)
{
    gchar *s;
    gchar **strv;
    int i;

    strv = g_strsplit("a,b,c", ",", 0);
    print_strv("split max0", strv);
    g_strfreev(strv);
    strv = g_strsplit("a,b,c", ",", 1);
    print_strv("split max1", strv);
    g_strfreev(strv);
    strv = g_strsplit("a,b,c", ",", 2);
    print_strv("split max2", strv);
    g_strfreev(strv);
    strv = g_strsplit("a,,b", ",", 0);
    print_strv("split empty-middle", strv);
    g_strfreev(strv);
    strv = g_strsplit(",", ",", 0);
    print_strv("split trailing", strv);
    g_strfreev(strv);
    strv = g_strsplit("", ",", 0);
    print_strv("split empty-string", strv);
    g_strfreev(strv);
    strv = g_strsplit("abc", ",", 0);
    print_strv("split no-delim", strv);
    g_strfreev(strv);

    s = g_strdup_printf("%s=%d", "x", 7);
    printf("strdup-printf '%s'\n", s);
    g_free(s);

    printf("strcmp0 nn=%d n1=%d 1n=%d eq=%d\n",
           g_strcmp0(NULL, NULL), g_strcmp0(NULL, "a"),
           g_strcmp0("a", NULL), g_strcmp0("a", "a"));
    printf("str-has-prefix=%d suffix=%d\n",
           g_str_has_prefix("abcdef", "abc"),
           g_str_has_suffix("abcdef", "def"));

    printf("ascii casecmp A_a=%d a_B=%d high=%d\n",
           g_ascii_strcasecmp("A_", "a_"),
           g_ascii_strcasecmp("a", "B"),
           g_ascii_strcasecmp("\x80", "\x81"));

    for (i = 0; i < 256; i += 17) {
        printf("ascii-table %d=%04x\n", i, g_ascii_table[i]);
    }
    printf("ascii classify=%d%d%d%d%d%d%d%d\n",
           g_ascii_isalnum('A'), g_ascii_isalpha('a'),
           g_ascii_isdigit('0'), g_ascii_isspace(' '),
           g_ascii_isspace('\v'), g_ascii_isxdigit('f'),
           g_ascii_isprint('\x7f'), g_ascii_iscntrl('\x7f'));

    printf("str-hash hello=%u empty=%u abc=%u\n",
           g_str_hash("hello"), g_str_hash(""), g_str_hash("abc"));
}

static void print_pattern_result(const char *pattern, const char *string)
{
    GPatternSpec *pspec = g_pattern_spec_new(pattern);
    gboolean matched = g_pattern_match_string(pspec, string);

    printf("pattern '%s' vs '%s' -> %d\n", pattern, string, matched);
    g_pattern_spec_free(pspec);
}

static void test_concat_and_pattern(void)
{
    gchar *concat;

    concat = g_strconcat("aa", "bb", "cc", NULL);
    printf("concat three='%s'\n", concat);
    g_free(concat);

    concat = g_strconcat("", "x", "", NULL);
    printf("concat empties='%s'\n", concat);
    g_free(concat);

    concat = g_strconcat("", NULL);
    printf("concat empty-only='%s'\n", concat);
    g_free(concat);

    concat = g_strconcat(NULL, NULL);
    printf("concat null=%s\n", concat ? concat : "(null)");
    g_free(concat);

    concat = g_strconcat("a", NULL, "b", NULL);
    printf("concat early-null='%s'\n", concat);
    g_free(concat);

    print_pattern_result("", "");
    print_pattern_result("", "a");
    print_pattern_result("*", "");
    print_pattern_result("*", "abc");
    print_pattern_result("**", "abc");
    print_pattern_result("a*b*c", "abc");
    print_pattern_result("a*b*c", "aXbYc");
    print_pattern_result("a*b*c", "aXbYcZ");
    print_pattern_result("?", "a");
    print_pattern_result("?", "");
    print_pattern_result("?", "ab");
    print_pattern_result("??", "ab");
    print_pattern_result("?", "\xc3\xa9");
    print_pattern_result("??", "\xc3\xa9");
    print_pattern_result("a?b", "a\xc3\xa9" "b");
    print_pattern_result("?", "\xe2\x82\xac");
    print_pattern_result("??", "\xe2\x82\xac" "\xe2\x82\xac");
    print_pattern_result("?", "\xf0\x9f\x98\x80");
    print_pattern_result("??", "\xf0\x9f\x98\x80" "\xf0\x9f\x98\x80");
    print_pattern_result("*?*", "\xc3\xa9");
    print_pattern_result("*?*", "");
    print_pattern_result("[a]", "a");
    print_pattern_result("[a]", "[a]");
    print_pattern_result("a[]b", "a[]b");
    print_pattern_result("a[?]b", "a[?]b");
    print_pattern_result("a[?]b", "aXb");
    print_pattern_result("*a*", "xxaxx");
    print_pattern_result("a**b", "ab");
    print_pattern_result("a**b", "aXb");
    print_pattern_result("a**b", "aXYb");
    print_pattern_result("A", "a");
    print_pattern_result("a", "A");
}

static void test_file_error_mapping(void)
{
    printf("file-error domain-nonzero=%d stable=%d\n",
           G_FILE_ERROR != 0, G_FILE_ERROR == G_FILE_ERROR);

#define PRINT_FILE_ERROR(e) \
    printf("file-error %s=%d\n", #e, (gint) g_file_error_from_errno(e))

#ifdef EEXIST
    PRINT_FILE_ERROR(EEXIST);
#endif
#ifdef EISDIR
    PRINT_FILE_ERROR(EISDIR);
#endif
#ifdef EACCES
    PRINT_FILE_ERROR(EACCES);
#endif
#ifdef ENAMETOOLONG
    PRINT_FILE_ERROR(ENAMETOOLONG);
#endif
#ifdef ENOENT
    PRINT_FILE_ERROR(ENOENT);
#endif
#ifdef ENOTDIR
    PRINT_FILE_ERROR(ENOTDIR);
#endif
#ifdef ENXIO
    PRINT_FILE_ERROR(ENXIO);
#endif
#ifdef ENODEV
    PRINT_FILE_ERROR(ENODEV);
#endif
#ifdef EROFS
    PRINT_FILE_ERROR(EROFS);
#endif
#ifdef ETXTBSY
    PRINT_FILE_ERROR(ETXTBSY);
#endif
#ifdef EFAULT
    PRINT_FILE_ERROR(EFAULT);
#endif
#ifdef ELOOP
    PRINT_FILE_ERROR(ELOOP);
#endif
#ifdef ENOSPC
    PRINT_FILE_ERROR(ENOSPC);
#endif
#ifdef ENOMEM
    PRINT_FILE_ERROR(ENOMEM);
#endif
#ifdef EMFILE
    PRINT_FILE_ERROR(EMFILE);
#endif
#ifdef ENFILE
    PRINT_FILE_ERROR(ENFILE);
#endif
#ifdef EBADF
    PRINT_FILE_ERROR(EBADF);
#endif
#ifdef EINVAL
    PRINT_FILE_ERROR(EINVAL);
#endif
#ifdef EPIPE
    PRINT_FILE_ERROR(EPIPE);
#endif
#ifdef EAGAIN
    PRINT_FILE_ERROR(EAGAIN);
#endif
#ifdef EINTR
    PRINT_FILE_ERROR(EINTR);
#endif
#ifdef EIO
    PRINT_FILE_ERROR(EIO);
#endif
#ifdef EPERM
    PRINT_FILE_ERROR(EPERM);
#endif
#ifdef ENOSYS
    PRINT_FILE_ERROR(ENOSYS);
#endif

#undef PRINT_FILE_ERROR

    printf("file-error zero=%d unknown=%d\n",
           (gint) g_file_error_from_errno(0),
           (gint) g_file_error_from_errno(123456));
}

static void test_random_range(void)
{
    gboolean range_ok = TRUE;
    gboolean negative_ok = TRUE;
    gboolean full_span_ok = TRUE;
    gboolean narrow_min_ok = TRUE;
    gboolean narrow_max_ok = TRUE;
    gboolean saw_zero = FALSE;
    gboolean saw_one = FALSE;
    gint32 value;
    int i;

    for (i = 0; i < 10000; i++) {
        value = g_random_int_range(0, 1000000);
        if (value < 0 || value >= 1000000) {
            range_ok = FALSE;
        }
    }

    for (i = 0; i < 10000; i++) {
        value = g_random_int_range(-10, -5);
        if (value < -10 || value >= -5) {
            negative_ok = FALSE;
        }
    }

    for (i = 0; i < 10000; i++) {
        value = g_random_int_range(INT32_MIN, INT32_MAX);
        if (value < INT32_MIN || value >= INT32_MAX) {
            full_span_ok = FALSE;
        }
    }

    for (i = 0; i < 1000; i++) {
        value = g_random_int_range(INT32_MIN, INT32_MIN + 1);
        if (value != INT32_MIN) {
            narrow_min_ok = FALSE;
        }
    }

    for (i = 0; i < 1000; i++) {
        value = g_random_int_range(INT32_MAX - 1, INT32_MAX);
        if (value != INT32_MAX - 1) {
            narrow_max_ok = FALSE;
        }
    }

    for (i = 0; i < 1000; i++) {
        value = g_random_int_range(0, 2);
        if (value == 0) {
            saw_zero = TRUE;
        } else if (value == 1) {
            saw_one = TRUE;
        }
    }

    printf("random range=%d negative=%d full-span=%d narrow-min=%d "
           "narrow-max=%d nonconstant=%d\n",
           range_ok, negative_ok, full_span_ok, narrow_min_ok, narrow_max_ok,
           saw_zero && saw_one);
}

static void test_prgname(void)
{
    char buffer[] = "prgname-one";
    const gchar *name;

    g_set_prgname(NULL);
    name = g_get_prgname();
    printf("prgname default=%s\n", name ? name : "(null)");

    g_set_prgname(buffer);
    buffer[0] = 'X';
    name = g_get_prgname();
    printf("prgname copy=%s\n", name ? name : "(null)");

    g_set_prgname("prgname-two");
    name = g_get_prgname();
    printf("prgname replace=%s\n", name ? name : "(null)");

    g_set_prgname(NULL);
    name = g_get_prgname();
    printf("prgname clear=%s\n", name ? name : "(null)");
}

static void test_overflow_mode(void)
{
    volatile gpointer p;

    p = g_malloc_n((gsize) -1, 2);
    printf("overflow returned %p\n", p);
}

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--overflow") == 0) {
        test_overflow_mode();
        return 0;
    }

    test_memory();
    test_hash_ownership();
    test_hash_collision_iterator();
    test_hash_foreach_remove();
    test_array();
    test_refcounts();
#ifdef GLIB_DIFF_HAVE_PTHREAD
    test_atomic_refcount_threads();
#endif
    test_ptr_array();
    test_slist();
    test_slist_stable_sort();
    test_queue();
    test_string();
    test_string_helpers();
    test_concat_and_pattern();
    test_file_error_mapping();
    test_random_range();
    test_prgname();
    return 0;
}
