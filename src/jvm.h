/* Java-машина BT-J2ME: CLDC 1.1 без верификатора и JIT.
 *
 * Устройство:
 *  - классы грузятся из встроенной библиотеки (rt.jar в бинарнике) и из JAR игры;
 *  - все значения — 8-байтовые слоты Value; long/double занимают два слота, как в спецификации;
 *  - аргументы вызова прямо становятся локальными переменными вызываемого метода (без копирования);
 *  - потоки «зелёные»: один системный поток, переключение по счётчику инструкций;
 *  - сборщик mark&sweep: кучу обходит точно (по типам полей), стеки — консервативно;
 *  - сборка мусора идёт только между квантами, когда состояние всех потоков сохранено. */
#ifndef BT_JVM_H
#define BT_JVM_H

#include <stddef.h>
#include <stdint.h>

#include "classfile.h"
#include "jar.h"

typedef struct Object Object;
typedef struct Class  Class;
typedef struct Method Method;
typedef struct Field  Field;
typedef struct Thread Thread;
typedef struct Frame  Frame;

typedef union {
    int32_t i;
    int64_t j;
    float   f;
    double  d;
    Object *l;
} Value;

typedef struct {
    Thread *owner;
    int     count;
} Monitor;

struct Object {
    Class   *cls;
    Object  *next;      /* список всех объектов — для сборщика */
    Monitor *mon;       /* заводится при первом synchronized */
    uint32_t flags;     /* бит 0 — метка сборщика */
    int32_t  length;    /* длина массива */
};

#define OBJ_HDR          ((sizeof(Object) + 7) & ~(size_t)7)
#define OBJ_FIELDS(o)    ((Value *)((uint8_t *)(o) + OBJ_HDR))
#define ARR_DATA(o)      ((void *)((uint8_t *)(o) + OBJ_HDR))
#define ARR_OF(o, type)  ((type *)ARR_DATA(o))

enum {
    ACC_PUBLIC = 0x0001, ACC_PRIVATE = 0x0002, ACC_PROTECTED = 0x0004, ACC_STATIC = 0x0008,
    ACC_FINAL = 0x0010, ACC_SYNCHRONIZED = 0x0020, ACC_SUPER = 0x0020, ACC_NATIVE = 0x0100,
    ACC_INTERFACE = 0x0200, ACC_ABSTRACT = 0x0400
};

struct Field {
    const char *name, *desc;   /* интернированные строки: сравниваем указатели */
    uint16_t    access;
    int         slot;          /* в OBJ_FIELDS(obj) или в cls->statics */
    Class      *owner;
    uint16_t    const_idx;
};

/* Нативный метод: args — аргументы (args[0] — this у нестатических).
 * Возвращает одно из NATIVE_*. */
typedef int (*NativeFn)(Thread *t, Value *args, Value *ret);

enum {
    NATIVE_OK,          /* результат в *ret */
    NATIVE_EXCEPTION,   /* в t->exception исключение */
    NATIVE_YIELD,       /* результат готов, но поток должен уступить (sleep, wait) */
    NATIVE_REEXEC,      /* повторить вызов позже (ждём события или инициализации класса) */
    NATIVE_TAILCALL     /* вместо возврата вызвать t->tail с теми же аргументами */
};

struct Method {
    const char    *name, *desc;
    uint16_t       access;
    Class         *owner;
    uint16_t       max_stack, max_locals;
    uint32_t       code_len;
    const uint8_t *code;
    uint16_t       exc_count, line_count;
    const uint8_t *exc_table, *line_table;
    int            arg_slots;  /* вместе с this */
    char           ret;        /* первый символ типа результата */
    int            vindex;     /* место в vtable или -1 */
    NativeFn       native;
};

enum { CLS_LINKED, CLS_INITIALIZING, CLS_INITIALIZED, CLS_ERROR };

struct Class {
    const char *name;           /* "java/lang/String", "[I" */
    Class      *super;
    Class     **ifaces;
    int         iface_count;
    uint16_t    access;
    ClassFile   cf;
    uint8_t    *data;           /* байты class-файла: cf ссылается на них */
    Field      *fields;
    int         field_count;
    Method     *methods;
    int         method_count;
    int         inst_slots;
    uint8_t    *ref_map;        /* слоты экземпляра, где лежат ссылки */
    Value      *statics;
    int         static_count;
    uint8_t    *static_ref_map;
    Method    **vtable;
    int         vtable_len;
    void      **rcp;            /* разрешённые элементы пула констант */
    int         state;
    Thread     *init_thread;
    Object     *class_obj;      /* java.lang.Class */
    int         is_array;
    char        elem_type;      /* для массивов: 'I', 'B', 'L', '[' ... */
    Class      *elem_class;     /* для массивов ссылок */
    int         elem_size;
    Class      *array_of;       /* кэш: класс массива из этих элементов */
    Class      *next;
};

struct Frame {
    Method        *m;
    const uint8_t *pc;          /* начало текущей инструкции */
    Value         *locals, *sp, *stack_base;
    Object        *sync;        /* монитор synchronized-метода */
    Class         *clinit_of;   /* это <clinit>: по возврату класс готов */
    uint8_t        reexec;      /* по возврату не продвигать pc вызывающего */
};

enum { TH_RUNNABLE, TH_SLEEPING, TH_WAITING, TH_BLOCKED, TH_REACQUIRE, TH_WAIT_EVENT, TH_DEAD };

struct Thread {
    Object  *jthread;           /* java.lang.Thread */
    Value   *stack;
    int      stack_size;
    Frame   *frames;
    int      depth, max_depth;
    int      state;
    int      timed;             /* у sleep/wait есть срок wake_at */
    uint32_t wake_at;
    Object  *wait_obj;          /* на чём ждём или чей монитор хотим */
    int      saved_count;       /* глубина монитора до wait() */
    Object  *exception;
    Method  *tail;
    int      yield;
    int      interrupted;
    int      id;
    Thread  *next;
};

/* ---- жизненный цикл ---- */

/* Создаёт машину с библиотекой rt (в памяти) и JAR игры. */
int  jvm_create(const uint8_t *rt, size_t rt_size, const char *app_jar);
void jvm_destroy(void);
Jar *jvm_app_jar(void);

/* Запускает статический метод cls.name(desc) в новом потоке; args — строковый аргумент или NULL. */
int  jvm_start_main(const char *cls, const char *name, const char *desc, const char *arg);

/* Выполняет потоки до budget_ms миллисекунд. Возвращает:
 *  1 — есть работа, 0 — все ждут (в *sleep_ms — сколько можно спать), -1 — машина завершилась. */
int  jvm_step(uint32_t budget_ms, uint32_t *sleep_ms);
void jvm_request_exit(int code);
int  jvm_exited(void);
/* Будит потоки, ждущие события (TH_WAIT_EVENT). */
void jvm_wake_event_waiters(void);

/* ---- для нативных методов ---- */

Class   *jvm_class(Thread *t, const char *name);           /* загрузить (без инициализации) */
int      jvm_init_class(Thread *t, Class *c);              /* 1 готов, 0 — повторить, -1 исключение */
Object  *jvm_new(Thread *t, Class *c);
Object  *jvm_new_array(Thread *t, char type, Class *elem, int len);  /* type: 'I','B',..,'L' */
Object  *jvm_new_string(Thread *t, const char *utf8);
Object  *jvm_new_string_utf16(Thread *t, const uint16_t *s, int len);
int      jvm_string_utf8(Object *s, char *out, size_t cap);   /* длина без \0 */
const uint16_t *jvm_string_chars(Object *s, int *len);
Object  *jvm_intern(Thread *t, Object *s);
Object  *jvm_class_object(Thread *t, Class *c);
Class   *jvm_class_from_object(Object *o);
int      jvm_field_slot(Class *c, const char *name);         /* -1 если нет */
Value   *jvm_static(Class *c, const char *name);
Method  *jvm_find_method(Class *c, const char *name, const char *desc);
int      jvm_instanceof(Class *s, Class *t);
int      jvm_throw(Thread *t, const char *cls, const char *msg);  /* всегда NATIVE_EXCEPTION */
Thread  *jvm_new_thread(Object *jthread, Object *receiver, Method *m);
Thread  *jvm_current(void);
uint32_t jvm_now(void);
void     jvm_gc_request(void);
size_t   jvm_heap_used(void);
size_t   jvm_heap_limit(void);
int      jvm_thread_alive(Object *jthread);

typedef struct {
    const char *cls, *name, *desc;
    NativeFn    fn;
} NativeDef;

void jvm_register_natives(const NativeDef *defs);

/* Подмена ресурсов игры на лету (широкоэкранный режим правит config.ini): получает данные
 * ресурса и может вернуть другой буфер (malloc, старый тогда освобождает сам). NULL — без подмены. */
typedef uint8_t *(*ResourceFilter)(const char *path, uint8_t *data, size_t *len);
void jvm_set_resource_filter(ResourceFilter f);

/* Нативы java.* (jvm_lib.c) и javax.microedition.* (midp.c) */
extern const NativeDef jvm_lib_natives[];

#endif
