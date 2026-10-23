/* Интерпретатор байткода, вызовы, исключения, мониторы и планировщик зелёных потоков. */
#include "jvm_internal.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STACK_SLOTS 16384   /* 128 КБ стека на поток — с запасом для рекурсивных ИИ в играх */
#define MAX_FRAMES  1024
#define QUANTUM     8000    /* инструкций за один заход в поток */

enum { GO, STOP, EXC };

/* ---------- кадры ---------- */

Frame *jvm_push_frame(Thread *t, Method *m, Value *locals, int reexec)
{
    Frame *f;

    if (t->depth >= t->max_depth || locals + m->max_locals + m->max_stack + 4 > t->stack + t->stack_size) {
        jvm_throw(t, "java/lang/StackOverflowError", NULL);
        return NULL;
    }
    f = &t->frames[t->depth++];
    f->m = m;
    f->pc = m->code;
    f->locals = locals;
    f->stack_base = locals + m->max_locals;
    f->sp = f->stack_base;
    f->sync = NULL;
    f->clinit_of = NULL;
    f->reexec = (uint8_t)reexec;
    if (m->max_locals > m->arg_slots)
        memset(locals + m->arg_slots, 0, (size_t)(m->max_locals - m->arg_slots) * sizeof(Value));
    return f;
}

static int line_of(const Method *m, const uint8_t *pc)
{
    uint32_t off = (uint32_t)(pc - m->code);
    int i, line = -1;

    for (i = 0; i < m->line_count; i++)
        if (rd_be16(m->line_table + i * 4) <= off)
            line = rd_be16(m->line_table + i * 4 + 2);
    return line;
}

static void frame_name(const Frame *f, char *buf, size_t cap)
{
    snprintf(buf, cap, "%s.%s:%d", f->m->owner->name, f->m->name, f->m->code ? line_of(f->m, f->pc) : -1);
}

void jvm_print_stack(Thread *t)
{
    int i;
    char buf[300];

    for (i = t->depth - 1; i >= 0; i--) {
        frame_name(&t->frames[i], buf, sizeof(buf));
        BT_LOG("    в %s\n", buf);
    }
}

/* ---------- потоки ---------- */

Thread *jvm_new_thread(Object *jthread, Object *receiver, Method *m)
{
    Thread *t = calloc(1, sizeof(Thread)), **pp;

    if (!t)
        return NULL;
    t->stack_size = STACK_SLOTS;
    t->stack = calloc(STACK_SLOTS, sizeof(Value));
    t->max_depth = MAX_FRAMES;
    t->frames = calloc(MAX_FRAMES, sizeof(Frame));
    if (!t->stack || !t->frames) {
        free(t->stack);
        free(t->frames);
        free(t);
        return NULL;
    }
    t->jthread = jthread;
    t->state = TH_RUNNABLE;
    t->id = ++vm->next_tid;
    if (receiver)
        t->stack[0].l = receiver;
    if (!jvm_push_frame(t, m, t->stack, 0)) {
        free(t->stack);
        free(t->frames);
        free(t);
        return NULL;
    }
    for (pp = &vm->threads; *pp; pp = &(*pp)->next)
        ;
    *pp = t;
    return t;
}

Thread *jvm_current(void)
{
    return vm->current;
}

int jvm_thread_alive(Object *jthread)
{
    Thread *t;

    for (t = vm->threads; t; t = t->next)
        if (t->jthread == jthread && t->state != TH_DEAD)
            return 1;
    return 0;
}

int jvm_start_main(const char *cls, const char *name, const char *desc, const char *arg)
{
    Class *c = jvm_load(cls);
    Method *m = c ? jvm_find_method(c, name, desc) : NULL;
    Thread *t;

    if (!m) {
        BT_LOG("нет точки входа %s.%s%s\n", cls, name, desc);
        return -1;
    }
    if (!(t = jvm_new_thread(NULL, NULL, m)))
        return -1;
    if (arg)
        t->stack[0].l = jvm_new_string(t, arg);
    return 0;
}

void jvm_request_exit(int code)
{
    vm->exit = 1;
    vm->exit_code = code;
}

int jvm_exited(void)
{
    return !vm || vm->exit;
}

void jvm_wake_event_waiters(void)
{
    Thread *t;

    for (t = vm->threads; t; t = t->next)
        if (t->state == TH_WAIT_EVENT)
            t->state = TH_RUNNABLE;
}

/* ---------- мониторы ---------- */

static int monitor_enter(Thread *t, Object *o)
{
    if (!o->mon && !(o->mon = calloc(1, sizeof(Monitor))))
        return 1;
    if (!o->mon->owner) {
        o->mon->owner = t;
        o->mon->count = 1;
        return 1;
    }
    if (o->mon->owner == t) {
        o->mon->count++;
        return 1;
    }
    t->state = TH_BLOCKED;
    t->wait_obj = o;
    return 0;
}

static int monitor_exit(Thread *t, Object *o)
{
    if (!o->mon || o->mon->owner != t)
        return 0;
    if (--o->mon->count == 0)
        o->mon->owner = NULL;
    return 1;
}

int jvm_monitor_wait(Thread *t, Object *o, int64_t ms)
{
    if (!o->mon || o->mon->owner != t)
        return jvm_throw(t, "java/lang/IllegalMonitorStateException", NULL);
    t->saved_count = o->mon->count;
    o->mon->owner = NULL;
    o->mon->count = 0;
    t->wait_obj = o;
    t->state = TH_WAITING;
    t->timed = ms > 0;
    t->wake_at = jvm_now() + (uint32_t)(ms > 0x7FFFFFFF ? 0x7FFFFFFF : ms);
    return NATIVE_YIELD;
}

int jvm_monitor_notify(Thread *t, Object *o, int all)
{
    Thread *w;

    if (!o->mon || o->mon->owner != t)
        return jvm_throw(t, "java/lang/IllegalMonitorStateException", NULL);
    for (w = vm->threads; w; w = w->next)
        if (w->state == TH_WAITING && w->wait_obj == o) {
            w->state = TH_REACQUIRE;
            if (!all)
                break;
        }
    return NATIVE_OK;
}

/* ---------- разрешение пула констант ---------- */

static Class *res_class(Thread *t, Class *cls, uint16_t idx)
{
    char buf[512];

    if (cls->rcp[idx])
        return cls->rcp[idx];
    if (classfile_class_name(&cls->cf, idx, buf, sizeof(buf))) {
        jvm_throw(t, "java/lang/VerifyError", "плохой индекс класса");
        return NULL;
    }
    return (Class *)(cls->rcp[idx] = jvm_class(t, buf));
}

static void nametype(Class *cls, uint16_t nt, const char **name, const char **desc)
{
    const CpEntry *e = &cls->cf.cp[nt];
    const CpEntry *n = &cls->cf.cp[e->u.ref.a], *d = &cls->cf.cp[e->u.ref.b];

    *name = jvm_sym(n->u.utf8.str, n->u.utf8.len);
    *desc = jvm_sym(d->u.utf8.str, d->u.utf8.len);
}

static Field *res_field(Thread *t, Class *cls, uint16_t idx)
{
    const CpEntry *e = &cls->cf.cp[idx];
    const char *name, *desc;
    Class *c;
    Field *f;

    if (cls->rcp[idx])
        return cls->rcp[idx];
    if (!(c = res_class(t, cls, e->u.ref.a)))
        return NULL;
    nametype(cls, e->u.ref.b, &name, &desc);
    if (!(f = jvm_resolve_field(c, name, desc))) {
        char msg[300];
        snprintf(msg, sizeof(msg), "%s.%s", c->name, name);
        jvm_throw(t, "java/lang/NoSuchFieldError", msg);
        return NULL;
    }
    return (Field *)(cls->rcp[idx] = f);
}

static Method *res_method(Thread *t, Class *cls, uint16_t idx)
{
    const CpEntry *e = &cls->cf.cp[idx];
    const char *name, *desc;
    Class *c;
    Method *m;

    if (cls->rcp[idx])
        return cls->rcp[idx];
    if (!(c = res_class(t, cls, e->u.ref.a)))
        return NULL;
    nametype(cls, e->u.ref.b, &name, &desc);
    if (!(m = jvm_resolve_method(c, name, desc))) {
        char msg[400];
        snprintf(msg, sizeof(msg), "%s.%s%s", c->name, name, desc);
        jvm_throw(t, "java/lang/NoSuchMethodError", msg);
        return NULL;
    }
    return (Method *)(cls->rcp[idx] = m);
}

/* Реализация метода im для объекта класса c (интерфейсы и «потерянные» виртуальные вызовы). */
static Method *find_impl(Class *c, Method *im)
{
    size_t h = (((uintptr_t)c >> 4) ^ ((uintptr_t)im >> 3)) & (ICACHE_SIZE - 1);
    ICacheEntry *ce = &vm->icache[h];
    Method *m;

    if (ce->cls == c && ce->im == im)
        return ce->impl;
    m = jvm_resolve_method(c, im->name, im->desc);
    ce->cls = c;
    ce->im = im;
    ce->impl = m;
    return m;
}

/* ---------- вызовы и возвраты ---------- */

static void push_ret(Frame *f, char type, Value v)
{
    if (type == 'V')
        return;
    f->sp[0] = v;
    f->sp += (type == 'J' || type == 'D') ? 2 : 1;
}

static int invoke(Thread *t, Method *m)
{
    Frame *f = &t->frames[t->depth - 1];
    int len = *f->pc == 0xB9 ? 5 : 3;
    Value *args = f->sp - m->arg_slots;
    Object *lock = NULL;
    Frame *nf;

    if (m->access & ACC_SYNCHRONIZED) {
        lock = (m->access & ACC_STATIC) ? jvm_class_object(t, m->owner) : args[0].l;
        if (lock && !monitor_enter(t, lock))
            return STOP;             /* повторим вызов, когда монитор освободится */
    }
    if (m->access & ACC_NATIVE) {
        Value ret;
        int r, depth = t->depth;

        if (!m->native) {
            char msg[300];
            if (lock)
                monitor_exit(t, lock);
            snprintf(msg, sizeof(msg), "%s.%s%s", m->owner->name, m->name, m->desc);
            jvm_throw(t, "java/lang/UnsatisfiedLinkError", msg);
            return EXC;
        }
        ret.j = 0;
        r = m->native(t, args, &ret);
        if (lock)
            monitor_exit(t, lock);
        switch (r) {
        case NATIVE_OK:
        case NATIVE_YIELD:
            f->sp = args;
            push_ret(f, m->ret, ret);
            f->pc += len;
            return (r == NATIVE_YIELD || t->state != TH_RUNNABLE || t->yield) ? STOP : GO;
        case NATIVE_EXCEPTION:
            return EXC;
        case NATIVE_REEXEC:
            (void)depth;
            return (t->state != TH_RUNNABLE || t->yield) ? STOP : GO;
        case NATIVE_TAILCALL:
            m = t->tail;
            t->tail = NULL;
            lock = NULL;
            break;
        }
    }
    if (!m->code) {
        char msg[300];
        if (lock)
            monitor_exit(t, lock);
        snprintf(msg, sizeof(msg), "%s.%s%s", m->owner->name, m->name, m->desc);
        jvm_throw(t, "java/lang/AbstractMethodError", msg);
        return EXC;
    }
    if (!(nf = jvm_push_frame(t, m, args, 0))) {
        if (lock)
            monitor_exit(t, lock);
        return EXC;
    }
    nf->sync = lock;
    return GO;
}

static int do_return(Thread *t, char type, Value v)
{
    Frame *f = &t->frames[--t->depth], *c;

    if (f->sync)
        monitor_exit(t, f->sync);
    if (f->clinit_of) {
        f->clinit_of->state = CLS_INITIALIZED;
        f->clinit_of->init_thread = NULL;
    }
    if (t->depth == 0) {
        t->state = TH_DEAD;
        return STOP;
    }
    c = &t->frames[t->depth - 1];
    c->sp = f->locals;
    push_ret(c, type, v);
    if (!f->reexec)
        c->pc += *c->pc == 0xB9 ? 5 : 3;
    return GO;
}

/* Ищем обработчик вверх по стеку; если его нет — поток умирает с сообщением. */
static int handle_exception(Thread *t)
{
    Object *ex = t->exception;
    char trace[8][300];
    int traced = 0;

    if (!ex) {
        jvm_throw(t, "java/lang/NullPointerException", NULL);
        ex = t->exception;
    }
    if (vm->trace && t->depth) {
        char where[300], msg[200] = "";
        frame_name(&t->frames[t->depth - 1], where, sizeof(where));
        if (vm->throwable_msg_slot >= 0)
            jvm_string_utf8(OBJ_FIELDS(ex)[vm->throwable_msg_slot].l, msg, sizeof(msg));
        BT_LOG("исключение %s (%s) в %s\n", ex->cls->name, msg, where);
    }
    while (t->depth > 0) {
        Frame *f = &t->frames[t->depth - 1];
        Method *m = f->m;
        int i;

        if (m->code) {
            uint32_t off = (uint32_t)(f->pc - m->code);
            for (i = 0; i < m->exc_count; i++) {
                const uint8_t *e = m->exc_table + i * 8;
                uint16_t start = rd_be16(e), end = rd_be16(e + 2), handler = rd_be16(e + 4), ct = rd_be16(e + 6);
                int match = 0;

                if (off < start || off >= end)
                    continue;
                if (!ct) {
                    match = 1;
                } else {
                    Object *saved = t->exception;
                    Class *cc = res_class(t, m->owner, ct);
                    t->exception = saved;
                    match = cc && jvm_instanceof(ex->cls, cc);
                }
                if (match) {
                    f->pc = m->code + handler;
                    f->sp = f->stack_base;
                    f->sp[0].l = ex;
                    f->sp++;
                    t->exception = NULL;
                    return GO;
                }
            }
        }
        if (traced < 8)
            frame_name(f, trace[traced++], sizeof(trace[0]));
        if (f->sync)
            monitor_exit(t, f->sync);
        if (f->clinit_of)
            f->clinit_of->state = CLS_ERROR;
        t->depth--;
    }
    {
        char msg[200] = "";
        int i;
        if (vm->throwable_msg_slot >= 0)
            jvm_string_utf8(OBJ_FIELDS(ex)[vm->throwable_msg_slot].l, msg, sizeof(msg));
        BT_LOG("необработанное исключение в потоке %d: %s%s%s\n", t->id, ex->cls->name, msg[0] ? ": " : "", msg);
        for (i = 0; i < traced; i++)
            BT_LOG("    в %s\n", trace[i]);
    }
    t->exception = NULL;
    t->state = TH_DEAD;
    return STOP;
}

static Object *multi_array(Thread *t, Class *ac, const int32_t *counts, int dims)
{
    Object *a;
    int i;

    if (ac->elem_type != '[' || dims == 1) {
        if (ac->elem_type == 'L' || ac->elem_type == '[')
            return jvm_new_array(t, 'L', ac->elem_class, counts[0]);
        return jvm_new_array(t, ac->elem_type, NULL, counts[0]);
    }
    if (!(a = jvm_new_array(t, 'L', ac->elem_class, counts[0])))
        return NULL;
    for (i = 0; i < counts[0]; i++)
        if (!(ARR_OF(a, Object *)[i] = multi_array(t, ac->elem_class, counts + 1, dims - 1)))
            return NULL;
    return a;
}

static int32_t f2i(double v)
{
    if (v != v)
        return 0;
    if (v >= 2147483647.0)
        return INT32_MAX;
    if (v <= -2147483648.0)
        return INT32_MIN;
    return (int32_t)v;
}

static int64_t f2l(double v)
{
    if (v != v)
        return 0;
    if (v >= 9223372036854775807.0)
        return INT64_MAX;
    if (v <= -9223372036854775808.0)
        return INT64_MIN;
    return (int64_t)v;
}

/* ---------- интерпретатор ---------- */

#define U2(n)  ((uint16_t)((pc[n] << 8) | pc[(n) + 1]))
#define S2(n)  ((int16_t)U2(n))
#define S4(n)  ((int32_t)(((uint32_t)pc[n] << 24) | ((uint32_t)pc[(n) + 1] << 16) | ((uint32_t)pc[(n) + 2] << 8) | pc[(n) + 3]))

#define SAVE()  do { f->pc = pc; f->sp = sp; } while (0)
#define LOAD()  do { f = &t->frames[t->depth - 1]; pc = f->pc; sp = f->sp; lv = f->locals; cls = f->m->owner; } while (0)
#define NEXT(n) do { pc += (n); goto next; } while (0)
#define THROW(c, msg) do { SAVE(); jvm_throw(t, c, msg); goto exc; } while (0)
#define NPE()   THROW("java/lang/NullPointerException", NULL)
#define AFTER_CALL(r) do { \
        if ((r) == EXC) goto exc; \
        if ((r) == STOP || t->depth == 0) return; \
        LOAD(); goto next; } while (0)

#define IPUSH(v) (sp++->i = (v))
#define IPOP()   ((--sp)->i)
#define FPUSH(v) (sp++->f = (v))
#define FPOP()   ((--sp)->f)
#define APUSH(v) (sp++->l = (v))
#define APOP()   ((--sp)->l)
#define LPUSH(v) (sp->j = (v), sp += 2)
#define LPOP()   (sp -= 2, sp->j)
#define DPUSH(v) (sp->d = (v), sp += 2)
#define DPOP()   (sp -= 2, sp->d)

#define ARR_CHECK(a, i) do { \
        if (!(a)) NPE(); \
        if ((uint32_t)(i) >= (uint32_t)(a)->length) { \
            char ib[24]; snprintf(ib, sizeof(ib), "%d", (int)(i)); \
            THROW("java/lang/ArrayIndexOutOfBoundsException", ib); } } while (0)

#define IADD(a, b) ((int32_t)((uint32_t)(a) + (uint32_t)(b)))
#define ISUB(a, b) ((int32_t)((uint32_t)(a) - (uint32_t)(b)))
#define IMUL(a, b) ((int32_t)((uint32_t)(a) * (uint32_t)(b)))
#define LADD(a, b) ((int64_t)((uint64_t)(a) + (uint64_t)(b)))
#define LSUB(a, b) ((int64_t)((uint64_t)(a) - (uint64_t)(b)))
#define LMUL(a, b) ((int64_t)((uint64_t)(a) * (uint64_t)(b)))

static void run(Thread *t, int budget)
{
    Frame *f;
    const uint8_t *pc;
    Value *sp, *lv;
    Class *cls;
    int r;

    if (t->exception) {
        f = &t->frames[t->depth - 1];
        goto exc_saved;
    }
    LOAD();

next:
    if (--budget <= 0) {
        SAVE();
        return;
    }
    switch (*pc) {
    case 0x00: NEXT(1);
    case 0x01: APUSH(NULL); NEXT(1);
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x08:
        IPUSH(*pc - 0x03); NEXT(1);
    case 0x09: case 0x0A: LPUSH(*pc - 0x09); NEXT(1);
    case 0x0B: case 0x0C: case 0x0D: FPUSH((float)(*pc - 0x0B)); NEXT(1);
    case 0x0E: case 0x0F: DPUSH((double)(*pc - 0x0E)); NEXT(1);
    case 0x10: IPUSH((int8_t)pc[1]); NEXT(2);
    case 0x11: IPUSH(S2(1)); NEXT(3);
    case 0x12: case 0x13: {
        uint16_t idx = *pc == 0x12 ? pc[1] : U2(1);
        const CpEntry *e = &cls->cf.cp[idx];
        if (e->tag == CP_INTEGER) {
            IPUSH(e->u.i);
        } else if (e->tag == CP_FLOAT) {
            memcpy(&sp->f, &e->u.f_bits, 4);
            sp++;
        } else if (e->tag == CP_STRING) {
            if (!cls->rcp[idx]) {
                char stack_buf[512], *buf = stack_buf;
                uint16_t ulen = cls->cf.cp[e->u.ref.a].u.utf8.len;
                Object *s;
                SAVE();
                if (ulen >= sizeof(stack_buf) && !(buf = malloc(ulen + 1u)))
                    THROW("java/lang/OutOfMemoryError", NULL);
                classfile_utf8(&cls->cf, e->u.ref.a, buf, (size_t)ulen + 1);
                s = jvm_new_string(t, buf);
                if (buf != stack_buf)
                    free(buf);
                if (!s)
                    goto exc;
                cls->rcp[idx] = jvm_intern(t, s);
            }
            APUSH((Object *)cls->rcp[idx]);
        } else if (e->tag == CP_CLASS) {
            Class *c;
            SAVE();
            if (!(c = res_class(t, cls, idx)))
                goto exc;
            APUSH(jvm_class_object(t, c));
        } else {
            THROW("java/lang/VerifyError", "ldc");
        }
        NEXT(*pc == 0x12 ? 2 : 3);
    }
    case 0x14: {
        const CpEntry *e = &cls->cf.cp[U2(1)];
        sp->j = (int64_t)e->u.l_bits;   /* и long, и double — те же 8 байт */
        sp += 2;
        NEXT(3);
    }
    case 0x15: case 0x17: case 0x19: *sp++ = lv[pc[1]]; NEXT(2);
    case 0x16: case 0x18: sp[0] = lv[pc[1]]; sp += 2; NEXT(2);
    case 0x1A: case 0x1B: case 0x1C: case 0x1D: *sp++ = lv[*pc - 0x1A]; NEXT(1);
    case 0x1E: case 0x1F: case 0x20: case 0x21: sp[0] = lv[*pc - 0x1E]; sp += 2; NEXT(1);
    case 0x22: case 0x23: case 0x24: case 0x25: *sp++ = lv[*pc - 0x22]; NEXT(1);
    case 0x26: case 0x27: case 0x28: case 0x29: sp[0] = lv[*pc - 0x26]; sp += 2; NEXT(1);
    case 0x2A: case 0x2B: case 0x2C: case 0x2D: *sp++ = lv[*pc - 0x2A]; NEXT(1);

    /* загрузка из массивов */
    case 0x2E: case 0x2F: case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: {
        int32_t i = sp[-1].i;
        Object *a = sp[-2].l;
        ARR_CHECK(a, i);
        sp -= 2;
        switch (*pc) {
        case 0x2E: IPUSH(ARR_OF(a, int32_t)[i]); break;
        case 0x2F: LPUSH(ARR_OF(a, int64_t)[i]); break;
        case 0x30: FPUSH(ARR_OF(a, float)[i]); break;
        case 0x31: DPUSH(ARR_OF(a, double)[i]); break;
        case 0x32: APUSH(ARR_OF(a, Object *)[i]); break;
        case 0x33: IPUSH(ARR_OF(a, int8_t)[i]); break;
        case 0x34: IPUSH(ARR_OF(a, uint16_t)[i]); break;
        default:   IPUSH(ARR_OF(a, int16_t)[i]); break;
        }
        NEXT(1);
    }
    case 0x36: case 0x38: case 0x3A: lv[pc[1]] = *--sp; NEXT(2);
    case 0x37: case 0x39: sp -= 2; lv[pc[1]] = sp[0]; NEXT(2);
    case 0x3B: case 0x3C: case 0x3D: case 0x3E: lv[*pc - 0x3B] = *--sp; NEXT(1);
    case 0x3F: case 0x40: case 0x41: case 0x42: sp -= 2; lv[*pc - 0x3F] = sp[0]; NEXT(1);
    case 0x43: case 0x44: case 0x45: case 0x46: lv[*pc - 0x43] = *--sp; NEXT(1);
    case 0x47: case 0x48: case 0x49: case 0x4A: sp -= 2; lv[*pc - 0x47] = sp[0]; NEXT(1);
    case 0x4B: case 0x4C: case 0x4D: case 0x4E: lv[*pc - 0x4B] = *--sp; NEXT(1);

    /* запись в массивы */
    case 0x4F: case 0x51: case 0x53: case 0x54: case 0x55: case 0x56: {
        Value v = sp[-1];
        int32_t i = sp[-2].i;
        Object *a = sp[-3].l;
        ARR_CHECK(a, i);
        switch (*pc) {
        case 0x4F: ARR_OF(a, int32_t)[i] = v.i; break;
        case 0x51: ARR_OF(a, float)[i] = v.f; break;
        case 0x53:
            if (v.l && !jvm_instanceof(v.l->cls, a->cls->elem_class))
                THROW("java/lang/ArrayStoreException", v.l->cls->name);
            ARR_OF(a, Object *)[i] = v.l;
            break;
        case 0x54: ARR_OF(a, int8_t)[i] = (int8_t)(a->cls->elem_type == 'Z' ? (v.i & 1) : v.i); break;
        case 0x55: ARR_OF(a, uint16_t)[i] = (uint16_t)v.i; break;
        default:   ARR_OF(a, int16_t)[i] = (int16_t)v.i; break;
        }
        sp -= 3;
        NEXT(1);
    }
    case 0x50: case 0x52: {
        Value v = sp[-2];
        int32_t i = sp[-3].i;
        Object *a = sp[-4].l;
        ARR_CHECK(a, i);
        ARR_OF(a, int64_t)[i] = v.j;
        sp -= 4;
        NEXT(1);
    }

    /* операции со стеком: long/double — два слота, поэтому всё сводится к перестановке слотов */
    case 0x57: sp--; NEXT(1);
    case 0x58: sp -= 2; NEXT(1);
    case 0x59: sp[0] = sp[-1]; sp++; NEXT(1);
    case 0x5A: { Value a = sp[-1], b = sp[-2]; sp[-2] = a; sp[-1] = b; sp[0] = a; sp++; NEXT(1); }
    case 0x5B: { Value a = sp[-1], b = sp[-2], c = sp[-3]; sp[-3] = a; sp[-2] = c; sp[-1] = b; sp[0] = a; sp++; NEXT(1); }
    case 0x5C: sp[0] = sp[-2]; sp[1] = sp[-1]; sp += 2; NEXT(1);
    case 0x5D: { Value a = sp[-1], b = sp[-2], c = sp[-3];
        sp[-3] = b; sp[-2] = a; sp[-1] = c; sp[0] = b; sp[1] = a; sp += 2; NEXT(1); }
    case 0x5E: { Value a = sp[-1], b = sp[-2], c = sp[-3], d = sp[-4];
        sp[-4] = b; sp[-3] = a; sp[-2] = d; sp[-1] = c; sp[0] = b; sp[1] = a; sp += 2; NEXT(1); }
    case 0x5F: { Value a = sp[-1]; sp[-1] = sp[-2]; sp[-2] = a; NEXT(1); }

    /* арифметика */
    case 0x60: sp--; sp[-1].i = IADD(sp[-1].i, sp[0].i); NEXT(1);
    case 0x61: { int64_t b = LPOP(); sp[-2].j = LADD(sp[-2].j, b); NEXT(1); }
    case 0x62: sp--; sp[-1].f += sp[0].f; NEXT(1);
    case 0x63: { double b = DPOP(); sp[-2].d += b; NEXT(1); }
    case 0x64: sp--; sp[-1].i = ISUB(sp[-1].i, sp[0].i); NEXT(1);
    case 0x65: { int64_t b = LPOP(); sp[-2].j = LSUB(sp[-2].j, b); NEXT(1); }
    case 0x66: sp--; sp[-1].f -= sp[0].f; NEXT(1);
    case 0x67: { double b = DPOP(); sp[-2].d -= b; NEXT(1); }
    case 0x68: sp--; sp[-1].i = IMUL(sp[-1].i, sp[0].i); NEXT(1);
    case 0x69: { int64_t b = LPOP(); sp[-2].j = LMUL(sp[-2].j, b); NEXT(1); }
    case 0x6A: sp--; sp[-1].f *= sp[0].f; NEXT(1);
    case 0x6B: { double b = DPOP(); sp[-2].d *= b; NEXT(1); }
    case 0x6C: case 0x70: {
        int32_t b = sp[-1].i, a = sp[-2].i;
        if (b == 0)
            THROW("java/lang/ArithmeticException", "/ by zero");
        sp--;
        if (*pc == 0x6C)
            sp[-1].i = (b == -1) ? ISUB(0, a) : a / b;
        else
            sp[-1].i = (b == -1) ? 0 : a % b;
        NEXT(1);
    }
    case 0x6D: case 0x71: {
        int64_t b = sp[-2].j, a = sp[-4].j;
        if (b == 0)
            THROW("java/lang/ArithmeticException", "/ by zero");
        sp -= 2;
        if (*pc == 0x6D)
            sp[-2].j = (b == -1) ? LSUB(0, a) : a / b;
        else
            sp[-2].j = (b == -1) ? 0 : a % b;
        NEXT(1);
    }
    case 0x6E: sp--; sp[-1].f /= sp[0].f; NEXT(1);
    case 0x6F: { double b = DPOP(); sp[-2].d /= b; NEXT(1); }
    case 0x72: sp--; sp[-1].f = fmodf(sp[-1].f, sp[0].f); NEXT(1);
    case 0x73: { double b = DPOP(); sp[-2].d = fmod(sp[-2].d, b); NEXT(1); }
    case 0x74: sp[-1].i = ISUB(0, sp[-1].i); NEXT(1);
    case 0x75: sp[-2].j = LSUB(0, sp[-2].j); NEXT(1);
    case 0x76: sp[-1].f = -sp[-1].f; NEXT(1);
    case 0x77: sp[-2].d = -sp[-2].d; NEXT(1);
    case 0x78: sp--; sp[-1].i = (int32_t)((uint32_t)sp[-1].i << (sp[0].i & 31)); NEXT(1);
    case 0x79: { int32_t s = IPOP(); sp[-2].j = (int64_t)((uint64_t)sp[-2].j << (s & 63)); NEXT(1); }
    case 0x7A: sp--; sp[-1].i = sp[-1].i >> (sp[0].i & 31); NEXT(1);
    case 0x7B: { int32_t s = IPOP(); sp[-2].j = sp[-2].j >> (s & 63); NEXT(1); }
    case 0x7C: sp--; sp[-1].i = (int32_t)((uint32_t)sp[-1].i >> (sp[0].i & 31)); NEXT(1);
    case 0x7D: { int32_t s = IPOP(); sp[-2].j = (int64_t)((uint64_t)sp[-2].j >> (s & 63)); NEXT(1); }
    case 0x7E: sp--; sp[-1].i &= sp[0].i; NEXT(1);
    case 0x7F: { int64_t b = LPOP(); sp[-2].j &= b; NEXT(1); }
    case 0x80: sp--; sp[-1].i |= sp[0].i; NEXT(1);
    case 0x81: { int64_t b = LPOP(); sp[-2].j |= b; NEXT(1); }
    case 0x82: sp--; sp[-1].i ^= sp[0].i; NEXT(1);
    case 0x83: { int64_t b = LPOP(); sp[-2].j ^= b; NEXT(1); }
    case 0x84: lv[pc[1]].i = IADD(lv[pc[1]].i, (int8_t)pc[2]); NEXT(3);

    /* преобразования */
    case 0x85: { int32_t v = sp[-1].i; sp[-1].j = v; sp++; NEXT(1); }
    case 0x86: sp[-1].f = (float)sp[-1].i; NEXT(1);
    case 0x87: { int32_t v = sp[-1].i; sp[-1].d = v; sp++; NEXT(1); }
    case 0x88: { int64_t v = LPOP(); IPUSH((int32_t)(uint32_t)(uint64_t)v); NEXT(1); }
    case 0x89: { int64_t v = LPOP(); FPUSH((float)v); NEXT(1); }
    case 0x8A: sp[-2].d = (double)sp[-2].j; NEXT(1);
    case 0x8B: sp[-1].i = f2i(sp[-1].f); NEXT(1);
    case 0x8C: { float v = sp[-1].f; sp[-1].j = f2l(v); sp++; NEXT(1); }
    case 0x8D: { float v = sp[-1].f; sp[-1].d = v; sp++; NEXT(1); }
    case 0x8E: { double v = DPOP(); IPUSH(f2i(v)); NEXT(1); }
    case 0x8F: sp[-2].j = f2l(sp[-2].d); NEXT(1);
    case 0x90: { double v = DPOP(); FPUSH((float)v); NEXT(1); }
    case 0x91: sp[-1].i = (int8_t)sp[-1].i; NEXT(1);
    case 0x92: sp[-1].i = (uint16_t)sp[-1].i; NEXT(1);
    case 0x93: sp[-1].i = (int16_t)sp[-1].i; NEXT(1);

    /* сравнения */
    case 0x94: { int64_t b = LPOP(), a = LPOP(); IPUSH((a > b) - (a < b)); NEXT(1); }
    case 0x95: case 0x96: {
        float b = FPOP(), a = FPOP();
        IPUSH(a > b ? 1 : a < b ? -1 : a == b ? 0 : (*pc == 0x95 ? -1 : 1));
        NEXT(1);
    }
    case 0x97: case 0x98: {
        double b = DPOP(), a = DPOP();
        IPUSH(a > b ? 1 : a < b ? -1 : a == b ? 0 : (*pc == 0x97 ? -1 : 1));
        NEXT(1);
    }
    case 0x99: if (IPOP() == 0) NEXT(S2(1)); NEXT(3);
    case 0x9A: if (IPOP() != 0) NEXT(S2(1)); NEXT(3);
    case 0x9B: if (IPOP() < 0) NEXT(S2(1)); NEXT(3);
    case 0x9C: if (IPOP() >= 0) NEXT(S2(1)); NEXT(3);
    case 0x9D: if (IPOP() > 0) NEXT(S2(1)); NEXT(3);
    case 0x9E: if (IPOP() <= 0) NEXT(S2(1)); NEXT(3);
    case 0x9F: case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: {
        int32_t b = sp[-1].i, a = sp[-2].i, jump;
        sp -= 2;
        switch (*pc) {
        case 0x9F: jump = a == b; break;
        case 0xA0: jump = a != b; break;
        case 0xA1: jump = a < b; break;
        case 0xA2: jump = a >= b; break;
        case 0xA3: jump = a > b; break;
        default:   jump = a <= b; break;
        }
        NEXT(jump ? S2(1) : 3);
    }
    case 0xA5: sp -= 2; NEXT(sp[0].l == sp[1].l ? S2(1) : 3);
    case 0xA6: sp -= 2; NEXT(sp[0].l != sp[1].l ? S2(1) : 3);
    case 0xA7: NEXT(S2(1));
    case 0xA8: IPUSH((int32_t)(pc + 3 - f->m->code)); NEXT(S2(1));
    case 0xA9: pc = f->m->code + lv[pc[1]].i; goto next;
    case 0xAA: {
        const uint8_t *base = pc;
        int32_t key = IPOP(), def, lo, hi;
        pc += 1 + ((4 - ((pc + 1 - f->m->code) & 3)) & 3);
        def = S4(0);
        lo = S4(4);
        hi = S4(8);
        if (key < lo || key > hi)
            pc = base + def;
        else
            pc = base + S4(12 + 4 * (key - lo));
        goto next;
    }
    case 0xAB: {
        const uint8_t *base = pc;
        int32_t key = IPOP(), def, n, i;
        pc += 1 + ((4 - ((pc + 1 - f->m->code) & 3)) & 3);
        def = S4(0);
        n = S4(4);
        for (i = 0; i < n; i++)
            if (S4(8 + i * 8) == key) {
                pc = base + S4(12 + i * 8);
                goto next;
            }
        pc = base + def;
        goto next;
    }

    /* возвраты */
    case 0xAC: case 0xAE: case 0xB0: {
        Value v = sp[-1];
        SAVE();
        r = do_return(t, *pc == 0xAC ? 'I' : *pc == 0xAE ? 'F' : 'L', v);
        AFTER_CALL(r);
    }
    case 0xAD: case 0xAF: {
        Value v = sp[-2];
        SAVE();
        r = do_return(t, 'J', v);
        AFTER_CALL(r);
    }
    case 0xB1: {
        Value v;
        v.j = 0;
        SAVE();
        r = do_return(t, 'V', v);
        AFTER_CALL(r);
    }

    /* поля */
    case 0xB2: case 0xB3: {
        Field *fl;
        Class *oc;
        SAVE();
        if (!(fl = res_field(t, cls, U2(1))))
            goto exc;
        oc = fl->owner;
        if (oc->state != CLS_INITIALIZED) {
            int depth = t->depth;
            r = jvm_init_class(t, oc);
            if (r < 0)
                goto exc;
            if (r == 0) {
                if (t->depth != depth && !t->yield) {
                    LOAD();
                    goto next;
                }
                return;
            }
        }
        if (*pc == 0xB2) {
            sp[0] = oc->statics[fl->slot];
            sp += (fl->desc[0] == 'J' || fl->desc[0] == 'D') ? 2 : 1;
        } else {
            sp -= (fl->desc[0] == 'J' || fl->desc[0] == 'D') ? 2 : 1;
            oc->statics[fl->slot] = sp[0];
        }
        NEXT(3);
    }
    case 0xB4: {
        Field *fl;
        Object *o;
        SAVE();
        if (!(fl = res_field(t, cls, U2(1))))
            goto exc;
        if (!(o = sp[-1].l))
            NPE();
        sp--;
        sp[0] = OBJ_FIELDS(o)[fl->slot];
        sp += (fl->desc[0] == 'J' || fl->desc[0] == 'D') ? 2 : 1;
        NEXT(3);
    }
    case 0xB5: {
        Field *fl;
        Object *o;
        int wide;
        SAVE();
        if (!(fl = res_field(t, cls, U2(1))))
            goto exc;
        wide = fl->desc[0] == 'J' || fl->desc[0] == 'D';
        if (!(o = sp[wide ? -3 : -2].l))
            NPE();
        OBJ_FIELDS(o)[fl->slot] = sp[wide ? -2 : -1];
        sp -= wide ? 3 : 2;
        NEXT(3);
    }

    /* вызовы */
    case 0xB6: case 0xB7: case 0xB8: case 0xB9: {
        Method *rm, *tm;
        Object *recv = NULL;
        uint8_t op = *pc;
        SAVE();
        if (!(rm = res_method(t, cls, U2(1))))
            goto exc;
        if (op != 0xB8) {
            recv = sp[-rm->arg_slots].l;
            if (!recv)
                NPE();
        }
        switch (op) {
        case 0xB6:
            if (rm->access & ACC_PRIVATE)
                tm = rm;
            else if (rm->vindex >= 0 && rm->vindex < recv->cls->vtable_len &&
                     recv->cls->vtable[rm->vindex]->name == rm->name)
                tm = recv->cls->vtable[rm->vindex];
            else
                tm = find_impl(recv->cls, rm);
            break;
        case 0xB7:
            tm = rm;
            if (rm->name[0] != '<' && (cls->access & ACC_SUPER) && rm->owner != cls &&
                !(rm->owner->access & ACC_INTERFACE) && cls->super && jvm_instanceof(cls, rm->owner))
                tm = jvm_resolve_method(cls->super, rm->name, rm->desc);
            break;
        case 0xB8:
            tm = rm;
            if (rm->owner->state != CLS_INITIALIZED) {
                int depth = t->depth;
                r = jvm_init_class(t, rm->owner);
                if (r < 0)
                    goto exc;
                if (r == 0) {
                    if (t->depth != depth && !t->yield) {
                        LOAD();
                        goto next;
                    }
                    return;
                }
            }
            break;
        default:
            tm = find_impl(recv->cls, rm);
            break;
        }
        if (!tm) {
            char msg[300];
            snprintf(msg, sizeof(msg), "%s.%s%s", rm->owner->name, rm->name, rm->desc);
            THROW("java/lang/AbstractMethodError", msg);
        }
        r = invoke(t, tm);
        AFTER_CALL(r);
    }
    case 0xBA:
        THROW("java/lang/UnsupportedOperationException", "invokedynamic");

    /* объекты */
    case 0xBB: {
        Class *c;
        Object *o;
        SAVE();
        if (!(c = res_class(t, cls, U2(1))))
            goto exc;
        if (c->access & (ACC_INTERFACE | ACC_ABSTRACT))
            THROW("java/lang/InstantiationError", c->name);
        if (c->state != CLS_INITIALIZED) {
            int depth = t->depth;
            r = jvm_init_class(t, c);
            if (r < 0)
                goto exc;
            if (r == 0) {
                if (t->depth != depth && !t->yield) {
                    LOAD();
                    goto next;
                }
                return;
            }
        }
        if (!(o = jvm_new(t, c)))
            goto exc;
        APUSH(o);
        NEXT(3);
    }
    case 0xBC: {
        static const char types[] = "????ZCFDBSIJ";
        Object *a;
        SAVE();
        if (pc[1] < 4 || pc[1] > 11)
            THROW("java/lang/VerifyError", "newarray");
        if (!(a = jvm_new_array(t, types[pc[1]], NULL, sp[-1].i)))
            goto exc;
        sp[-1].l = a;
        NEXT(2);
    }
    case 0xBD: {
        Class *c;
        Object *a;
        SAVE();
        if (!(c = res_class(t, cls, U2(1))) || !(a = jvm_new_array(t, 'L', c, sp[-1].i)))
            goto exc;
        sp[-1].l = a;
        NEXT(3);
    }
    case 0xBE:
        if (!sp[-1].l)
            NPE();
        sp[-1].i = sp[-1].l->length;
        NEXT(1);
    case 0xBF: {
        Object *e = sp[-1].l;
        if (!e)
            NPE();
        SAVE();
        t->exception = e;
        goto exc;
    }
    case 0xC0: case 0xC1: {
        Class *c;
        Object *o = sp[-1].l;
        SAVE();
        if (!(c = res_class(t, cls, U2(1))))
            goto exc;
        if (*pc == 0xC0) {
            if (o && !jvm_instanceof(o->cls, c)) {
                char msg[300];
                snprintf(msg, sizeof(msg), "%s -> %s", o->cls->name, c->name);
                THROW("java/lang/ClassCastException", msg);
            }
        } else {
            sp[-1].i = o && jvm_instanceof(o->cls, c);
        }
        NEXT(3);
    }
    case 0xC2: {
        Object *o = sp[-1].l;
        if (!o)
            NPE();
        if (!monitor_enter(t, o)) {
            SAVE();
            return;
        }
        sp--;
        NEXT(1);
    }
    case 0xC3: {
        Object *o = sp[-1].l;
        if (!o)
            NPE();
        if (!monitor_exit(t, o))
            THROW("java/lang/IllegalMonitorStateException", NULL);
        sp--;
        NEXT(1);
    }
    case 0xC4: {
        uint16_t idx = U2(2);
        switch (pc[1]) {
        case 0x15: case 0x17: case 0x19: *sp++ = lv[idx]; NEXT(4);
        case 0x16: case 0x18: sp[0] = lv[idx]; sp += 2; NEXT(4);
        case 0x36: case 0x38: case 0x3A: lv[idx] = *--sp; NEXT(4);
        case 0x37: case 0x39: sp -= 2; lv[idx] = sp[0]; NEXT(4);
        case 0x84: lv[idx].i = IADD(lv[idx].i, S2(4)); NEXT(6);
        case 0xA9: pc = f->m->code + lv[idx].i; goto next;
        default: THROW("java/lang/VerifyError", "wide");
        }
    }
    case 0xC5: {
        Class *c;
        int dims = pc[3], i;
        int32_t counts[255];
        Object *a;
        SAVE();
        if (!(c = res_class(t, cls, U2(1))))
            goto exc;
        for (i = 0; i < dims; i++) {
            counts[i] = sp[i - dims].i;
            if (counts[i] < 0)
                THROW("java/lang/NegativeArraySizeException", NULL);
        }
        if (!(a = multi_array(t, c, counts, dims)))
            goto exc;
        sp -= dims;
        APUSH(a);
        NEXT(4);
    }
    case 0xC6: NEXT((--sp)->l == NULL ? S2(1) : 3);
    case 0xC7: NEXT((--sp)->l != NULL ? S2(1) : 3);
    case 0xC8: NEXT(S4(1));
    case 0xC9: IPUSH((int32_t)(pc + 5 - f->m->code)); NEXT(S4(1));
    default: {
        char msg[32];
        snprintf(msg, sizeof(msg), "опкод 0x%02X", *pc);
        THROW("java/lang/VerifyError", msg);
    }
    }

exc:
    /* состояние кадра уже сохранено, t->exception заполнен */
exc_saved:
    r = handle_exception(t);
    if (r == STOP || t->depth == 0)
        return;
    LOAD();
    goto next;
}

/* ---------- планировщик ---------- */

static void update_states(uint32_t now)
{
    Thread *t, **pp;

    for (pp = &vm->threads; (t = *pp) != NULL;) {
        if (t->state == TH_DEAD) {
            *pp = t->next;
            if (vm->current == t)
                vm->current = NULL;
            free(t->stack);
            free(t->frames);
            free(t);
            continue;
        }
        switch (t->state) {
        case TH_SLEEPING:
            if ((int32_t)(now - t->wake_at) >= 0)
                t->state = TH_RUNNABLE;
            break;
        case TH_WAITING:
            if (t->timed && (int32_t)(now - t->wake_at) >= 0)
                t->state = TH_REACQUIRE;
            break;
        case TH_WAIT_EVENT:
            if (t->timed && (int32_t)(now - t->wake_at) >= 0)
                t->state = TH_RUNNABLE;
            break;
        case TH_BLOCKED:
            if (!t->wait_obj->mon || !t->wait_obj->mon->owner) {
                t->state = TH_RUNNABLE;
                t->wait_obj = NULL;
            }
            break;
        }
        if (t->state == TH_REACQUIRE && (!t->wait_obj->mon->owner || t->wait_obj->mon->owner == t)) {
            t->wait_obj->mon->owner = t;
            t->wait_obj->mon->count = t->saved_count;
            t->wait_obj = NULL;
            t->state = TH_RUNNABLE;
        }
        pp = &t->next;
    }
}

int jvm_step(uint32_t budget_ms, uint32_t *sleep_ms)
{
    uint32_t start = jvm_now();

    for (;;) {
        uint32_t now = jvm_now();
        Thread *t, *pick = NULL;

        if (vm->exit)
            return -1;
        update_states(now);
        if (!vm->threads)
            return -1;
        /* по кругу: начинаем со следующего за последним выполненным */
        t = vm->current && vm->current->next ? vm->current->next : vm->threads;
        do {
            if (t->state == TH_RUNNABLE) {
                pick = t;
                break;
            }
            t = t->next ? t->next : vm->threads;
        } while (t != (vm->current && vm->current->next ? vm->current->next : vm->threads));

        if (!pick) {
            uint32_t wait = 10;
            for (t = vm->threads; t; t = t->next)
                if ((t->state == TH_SLEEPING || ((t->state == TH_WAITING || t->state == TH_WAIT_EVENT) && t->timed))) {
                    int32_t d = (int32_t)(t->wake_at - now);
                    if (d < 0)
                        d = 0;
                    if ((uint32_t)d < wait)
                        wait = (uint32_t)d;
                }
            *sleep_ms = wait;
            return 0;
        }
        vm->current = pick;
        pick->yield = 0;
        run(pick, QUANTUM);
        if (vm->gc_requested) {
            static int perf = -1;
            uint32_t g0 = jvm_now();
            jvm_gc();
            if (perf < 0)
                perf = getenv("BT_PERF") != NULL;
            if (perf)
                fprintf(stderr, "perf: gc %u мс, живых %zu КБ\n", (unsigned)(jvm_now() - g0), vm->bytes / 1024);
        }
        if (jvm_now() - start >= budget_ms)
            return 1;
    }
}
