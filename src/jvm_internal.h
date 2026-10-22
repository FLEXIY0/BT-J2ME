/* Внутренности Java-машины, общие для jvm_core.c и jvm_interp.c. */
#ifndef BT_JVM_INTERNAL_H
#define BT_JVM_INTERNAL_H

#include "jvm.h"

#define ICACHE_SIZE  1024
#define MAX_NATIVE_TABLES 8

typedef struct {
    Class  *cls;
    Method *im, *impl;
} ICacheEntry;

typedef struct {
    Jar        rt, app;
    int        have_app;

    char     **syms;            /* интернированные имена */
    size_t     sym_cap, sym_count;

    Class    **ctab;            /* классы по имени (ключ — указатель-символ) */
    size_t     ctab_cap, ctab_count;
    Class     *classes;

    Object    *objects;         /* куча */
    size_t     obj_count, bytes, since_gc, threshold, limit;
    Object   **oset;            /* множество живых объектов для консервативного сканирования */
    size_t     oset_cap, oset_count;
    int        gc_requested;
    Object   **mark_stack;
    size_t     mark_cap, mark_top;
    int        mark_failed;     /* не хватило памяти под стек меток — эту сборку отменяем */

    Object   **intern;          /* интернированные строки */
    size_t     intern_cap, intern_count;

    Thread    *threads, *current;
    int        next_tid;
    int        exit, exit_code;

    const NativeDef *natives[MAX_NATIVE_TABLES];
    int        native_tables;

    Class     *c_object, *c_string, *c_class, *c_thread;
    int        s_value, s_offset, s_count, class_ref_slot, throwable_msg_slot;

    ICacheEntry icache[ICACHE_SIZE];
    uint32_t   start_ms;
    int        trace;           /* BT_J2ME_TRACE: печатать брошенные исключения */
} Vm;

extern Vm *vm;

const char *jvm_sym(const char *s, size_t n);
Class      *jvm_load(const char *name);            /* NULL, если нет такого класса */
Class      *jvm_array_class(char type, Class *elem);
Method     *jvm_resolve_method(Class *c, const char *name, const char *desc);
Field      *jvm_resolve_field(Class *c, const char *name, const char *desc);
Object     *jvm_alloc(Class *c, size_t payload);
void        jvm_gc(void);
int         jvm_desc_slots(const char *desc);

/* jvm_interp.c */
Frame      *jvm_push_frame(Thread *t, Method *m, Value *locals, int reexec);
void        jvm_print_stack(Thread *t);
int         jvm_monitor_wait(Thread *t, Object *o, int64_t ms);
int         jvm_monitor_notify(Thread *t, Object *o, int all);

#endif
