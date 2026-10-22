/* Ядро Java-машины: имена, классы, объекты, строки, сборщик мусора. */
#define _POSIX_C_SOURCE 199309L
#include "jvm_internal.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

Vm *vm;

/* ---------- время ---------- */

static uint32_t mono_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

uint32_t jvm_now(void)
{
    return mono_ms() - vm->start_ms;
}

/* ---------- символы ---------- */

static uint32_t hash_bytes(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint32_t h = 2166136261u;

    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}

const char *jvm_sym(const char *s, size_t n)
{
    size_t i;

    if (vm->sym_count * 2 >= vm->sym_cap) {
        size_t ncap = vm->sym_cap ? vm->sym_cap * 2 : 4096, k;
        char **nt = calloc(ncap, sizeof(char *));
        if (!nt)
            return NULL;
        for (k = 0; k < vm->sym_cap; k++)
            if (vm->syms[k]) {
                size_t j = hash_bytes(vm->syms[k], strlen(vm->syms[k])) & (ncap - 1);
                while (nt[j])
                    j = (j + 1) & (ncap - 1);
                nt[j] = vm->syms[k];
            }
        free(vm->syms);
        vm->syms = nt;
        vm->sym_cap = ncap;
    }
    for (i = hash_bytes(s, n) & (vm->sym_cap - 1); vm->syms[i]; i = (i + 1) & (vm->sym_cap - 1))
        if (strncmp(vm->syms[i], s, n) == 0 && vm->syms[i][n] == '\0')
            return vm->syms[i];
    vm->syms[i] = malloc(n + 1);
    if (!vm->syms[i])
        return NULL;
    memcpy(vm->syms[i], s, n);
    vm->syms[i][n] = '\0';
    vm->sym_count++;
    return vm->syms[i];
}

static const char *cp_sym(const ClassFile *cf, uint16_t idx)
{
    if (!idx || idx >= cf->cp_count || cf->cp[idx].tag != CP_UTF8)
        return NULL;
    return jvm_sym(cf->cp[idx].u.utf8.str, cf->cp[idx].u.utf8.len);
}

/* ---------- таблица классов ---------- */

static size_t ptr_hash(const void *p, size_t cap)
{
    uintptr_t v = (uintptr_t)p;

    v ^= v >> 17;
    v *= 0x9E3779B1u;
    return (size_t)(v ^ (v >> 13)) & (cap - 1);
}

static Class *ctab_get(const char *name)
{
    size_t i;

    if (!vm->ctab_cap)
        return NULL;
    for (i = ptr_hash(name, vm->ctab_cap); vm->ctab[i]; i = (i + 1) & (vm->ctab_cap - 1))
        if (vm->ctab[i]->name == name)
            return vm->ctab[i];
    return NULL;
}

static void ctab_put(Class *c)
{
    size_t i;

    if (vm->ctab_count * 2 >= vm->ctab_cap) {
        size_t ncap = vm->ctab_cap ? vm->ctab_cap * 2 : 256, k;
        Class **nt = calloc(ncap, sizeof(Class *));
        for (k = 0; k < vm->ctab_cap; k++)
            if (vm->ctab[k]) {
                size_t j = ptr_hash(vm->ctab[k]->name, ncap);
                while (nt[j])
                    j = (j + 1) & (ncap - 1);
                nt[j] = vm->ctab[k];
            }
        free(vm->ctab);
        vm->ctab = nt;
        vm->ctab_cap = ncap;
    }
    for (i = ptr_hash(c->name, vm->ctab_cap); vm->ctab[i]; i = (i + 1) & (vm->ctab_cap - 1))
        ;
    vm->ctab[i] = c;
    vm->ctab_count++;
    c->next = vm->classes;
    vm->classes = c;
}

/* ---------- дескрипторы ---------- */

/* Сколько слотов занимают аргументы по дескриптору "(IJLjava/lang/String;)V". */
int jvm_desc_slots(const char *d)
{
    int n = 0;

    for (d++; *d && *d != ')'; d++) {
        n += (*d == 'J' || *d == 'D') ? 2 : 1;
        while (*d == '[')
            d++;
        if (*d == 'L')
            while (*d && *d != ';')
                d++;
    }
    return n;
}

/* ---------- нативные методы ---------- */

void jvm_register_natives(const NativeDef *defs)
{
    if (vm->native_tables < MAX_NATIVE_TABLES)
        vm->natives[vm->native_tables++] = defs;
}

static NativeFn find_native(const char *cls, const char *name, const char *desc)
{
    int t;
    const NativeDef *d;

    for (t = 0; t < vm->native_tables; t++)
        for (d = vm->natives[t]; d->cls; d++)
            if (!strcmp(d->cls, cls) && !strcmp(d->name, name) && (!d->desc || !strcmp(d->desc, desc)))
                return d->fn;
    return NULL;
}

/* ---------- загрузка и связывание ---------- */

static uint8_t *read_class_bytes(const char *name, size_t *len)
{
    char path[512];
    const JarEntry *e;

    snprintf(path, sizeof(path), "%s.class", name);
    if ((e = jar_find(&vm->rt, path)) != NULL)
        return jar_read(&vm->rt, e, len);
    if (vm->have_app && (e = jar_find(&vm->app, path)) != NULL)
        return jar_read(&vm->app, e, len);
    return NULL;
}

Class *jvm_array_class(char type, Class *elem)
{
    char buf[512];
    const char *name;
    Class *c;

    if (type == 'L' && elem && elem->array_of)
        return elem->array_of;
    if (type == 'L' && elem)
        snprintf(buf, sizeof(buf), elem->is_array ? "[%s" : "[L%s;", elem->name);
    else
        snprintf(buf, sizeof(buf), "[%c", type);
    name = jvm_sym(buf, strlen(buf));
    if ((c = ctab_get(name)) != NULL)
        return c;

    c = calloc(1, sizeof(Class));
    if (!c)
        return NULL;
    c->name = name;
    c->super = vm->c_object;
    c->access = ACC_PUBLIC | ACC_FINAL;
    c->is_array = 1;
    c->elem_type = type == 'L' && elem && elem->is_array ? '[' : type;
    c->elem_class = elem;
    switch (type) {
    case 'Z': case 'B': c->elem_size = 1; break;
    case 'C': case 'S': c->elem_size = 2; break;
    case 'I': case 'F': c->elem_size = 4; break;
    case 'J': case 'D': c->elem_size = 8; break;
    default:  c->elem_size = (int)sizeof(Object *); break;
    }
    c->vtable = vm->c_object->vtable;
    c->vtable_len = vm->c_object->vtable_len;
    c->state = CLS_INITIALIZED;
    ctab_put(c);
    if (type == 'L' && elem)
        elem->array_of = c;
    return c;
}

static Class *load_array(const char *name)
{
    char t = name[1];

    if (t == '[') {
        Class *e = jvm_load(name + 1);
        return e ? jvm_array_class('L', e) : NULL;
    }
    if (t == 'L') {
        size_t n = strlen(name);
        char buf[512];
        Class *e;
        if (n < 4 || n - 3 >= sizeof(buf))
            return NULL;
        memcpy(buf, name + 2, n - 3);
        buf[n - 3] = '\0';
        e = jvm_load(buf);
        return e ? jvm_array_class('L', e) : NULL;
    }
    return t && strchr("ZBCSIFJD", t) ? jvm_array_class(t, NULL) : NULL;
}

static void build_vtable(Class *c)
{
    int i, j, n = c->super ? c->super->vtable_len : 0;

    c->vtable = malloc(sizeof(Method *) * (size_t)(n + c->method_count + 1));
    if (n)
        memcpy(c->vtable, c->super->vtable, sizeof(Method *) * (size_t)n);
    for (i = 0; i < c->method_count; i++) {
        Method *m = &c->methods[i];

        m->vindex = -1;
        if ((m->access & (ACC_STATIC | ACC_PRIVATE)) || m->name[0] == '<' || (c->access & ACC_INTERFACE))
            continue;
        for (j = 0; j < n; j++)
            if (c->vtable[j]->name == m->name && c->vtable[j]->desc == m->desc) {
                m->vindex = j;
                c->vtable[j] = m;
                break;
            }
        if (m->vindex < 0) {
            m->vindex = n;
            c->vtable[n++] = m;
        }
    }
    c->vtable_len = n;
}

static void free_class(Class *c)
{
    if (!c->is_array) {
        classfile_free(&c->cf);
        free(c->data);
        free(c->fields);
        free(c->methods);
        free(c->ref_map);
        free(c->statics);
        free(c->static_ref_map);
        free(c->vtable);
        free(c->rcp);
        free(c->ifaces);
    }
    free(c);
}

Class *jvm_load(const char *name_in)
{
    const char *name = jvm_sym(name_in, strlen(name_in));
    Class *c = ctab_get(name), *super = NULL;
    ClassFile cf;
    uint8_t *data;
    size_t len;
    char err[128], buf[512];
    int i, islot, sslot;

    if (c)
        return c;
    if (name[0] == '[')
        return load_array(name);
    if (!(data = read_class_bytes(name, &len)))
        return NULL;
    if (classfile_parse(&cf, data, len, err, sizeof(err))) {
        BT_LOG("класс %s: %s\n", name, err);
        free(data);
        return NULL;
    }
    if (cf.super_class) {
        if (classfile_class_name(&cf, cf.super_class, buf, sizeof(buf)) || !(super = jvm_load(buf))) {
            BT_LOG("класс %s: нет суперкласса %s\n", name, buf);
            classfile_free(&cf);
            free(data);
            return NULL;
        }
    }
    /* суперкласс мог загрузить нас самих (не бывает в корректном коде, но проверим) */
    if ((c = ctab_get(name)) != NULL) {
        classfile_free(&cf);
        free(data);
        return c;
    }

    c = calloc(1, sizeof(Class));
    c->name = name;
    c->cf = cf;
    c->data = data;
    c->super = super;
    c->access = cf.access;
    c->rcp = calloc(cf.cp_count, sizeof(void *));

    c->ifaces = calloc(cf.iface_count ? cf.iface_count : 1, sizeof(Class *));
    for (i = 0; i < cf.iface_count; i++)
        if (!classfile_class_name(&cf, cf.ifaces[i], buf, sizeof(buf)) && (c->ifaces[c->iface_count] = jvm_load(buf)))
            c->iface_count++;

    /* поля: экземплярные продолжают раскладку суперкласса */
    c->field_count = cf.field_count;
    c->fields = calloc(cf.field_count ? cf.field_count : 1, sizeof(Field));
    islot = super ? super->inst_slots : 0;
    sslot = 0;
    for (i = 0; i < cf.field_count; i++) {
        Field *f = &c->fields[i];
        f->name = cp_sym(&cf, cf.fields[i].name_idx);
        f->desc = cp_sym(&cf, cf.fields[i].desc_idx);
        f->access = cf.fields[i].access;
        f->owner = c;
        f->const_idx = cf.fields[i].const_idx;
        f->slot = (f->access & ACC_STATIC) ? sslot++ : islot++;
    }
    c->inst_slots = islot;
    c->static_count = sslot;
    c->ref_map = calloc((size_t)islot + 1, 1);
    if (super && super->inst_slots)
        memcpy(c->ref_map, super->ref_map, (size_t)super->inst_slots);
    c->statics = calloc((size_t)sslot + 1, sizeof(Value));
    c->static_ref_map = calloc((size_t)sslot + 1, 1);
    for (i = 0; i < cf.field_count; i++) {
        Field *f = &c->fields[i];
        int ref = f->desc && (f->desc[0] == 'L' || f->desc[0] == '[');
        if (f->access & ACC_STATIC)
            c->static_ref_map[f->slot] = (uint8_t)ref;
        else
            c->ref_map[f->slot] = (uint8_t)ref;
    }

    /* методы */
    c->method_count = cf.method_count;
    c->methods = calloc(cf.method_count ? cf.method_count : 1, sizeof(Method));
    for (i = 0; i < cf.method_count; i++) {
        Method *m = &c->methods[i];
        const Member *mm = &cf.methods[i];
        const char *r;

        m->name = cp_sym(&cf, mm->name_idx);
        m->desc = cp_sym(&cf, mm->desc_idx);
        m->access = mm->access;
        m->owner = c;
        m->max_stack = mm->max_stack;
        m->max_locals = mm->max_locals;
        m->code_len = mm->code_len;
        m->code = mm->code;
        m->exc_count = mm->exc_count;
        m->exc_table = mm->exc_table;
        m->line_count = mm->line_count;
        m->line_table = mm->line_table;
        m->arg_slots = jvm_desc_slots(m->desc) + ((m->access & ACC_STATIC) ? 0 : 1);
        r = strchr(m->desc, ')');
        m->ret = r ? r[1] : 'V';
        if (m->access & ACC_NATIVE)
            m->native = find_native(name, m->name, m->desc);   /* нет — UnsatisfiedLinkError при вызове */
        if (m->max_locals < m->arg_slots)
            m->max_locals = (uint16_t)m->arg_slots;
    }
    build_vtable(c);
    c->state = CLS_LINKED;
    ctab_put(c);
    return c;
}

Class *jvm_class(Thread *t, const char *name)
{
    Class *c = jvm_load(name);

    if (!c && t) {
        char buf[600];
        int k;
        snprintf(buf, sizeof(buf), "%s", name);
        for (k = 0; buf[k]; k++)
            if (buf[k] == '/')
                buf[k] = '.';
        jvm_throw(t, "java/lang/NoClassDefFoundError", buf);
    }
    return c;
}

/* ---------- поиск полей и методов ---------- */

static Field *find_field_here(Class *c, const char *name, const char *desc)
{
    int i;

    for (i = 0; i < c->field_count; i++)
        if (c->fields[i].name == name && (!desc || c->fields[i].desc == desc))
            return &c->fields[i];
    return NULL;
}

Field *jvm_resolve_field(Class *c, const char *name, const char *desc)
{
    for (; c; c = c->super) {
        Field *f = find_field_here(c, name, desc);
        int i;
        if (f)
            return f;
        for (i = 0; i < c->iface_count; i++)
            if ((f = jvm_resolve_field(c->ifaces[i], name, desc)) != NULL)
                return f;
    }
    return NULL;
}

static Method *find_method_here(Class *c, const char *name, const char *desc)
{
    int i;

    for (i = 0; i < c->method_count; i++)
        if (c->methods[i].name == name && c->methods[i].desc == desc)
            return &c->methods[i];
    return NULL;
}

static Method *find_in_ifaces(Class *c, const char *name, const char *desc)
{
    int i;
    Method *m;

    for (; c; c = c->super)
        for (i = 0; i < c->iface_count; i++) {
            if ((m = find_method_here(c->ifaces[i], name, desc)) != NULL)
                return m;
            if ((m = find_in_ifaces(c->ifaces[i], name, desc)) != NULL)
                return m;
        }
    return NULL;
}

Method *jvm_resolve_method(Class *c, const char *name, const char *desc)
{
    Class *k;
    Method *m;

    if (c->is_array)
        c = vm->c_object;
    for (k = c; k; k = k->super)
        if ((m = find_method_here(k, name, desc)) != NULL)
            return m;
    return find_in_ifaces(c, name, desc);
}

Method *jvm_find_method(Class *c, const char *name, const char *desc)
{
    return jvm_resolve_method(c, jvm_sym(name, strlen(name)), jvm_sym(desc, strlen(desc)));
}

int jvm_field_slot(Class *c, const char *name)
{
    const char *n = jvm_sym(name, strlen(name));

    for (; c; c = c->super) {
        Field *f = find_field_here(c, n, NULL);
        if (f && !(f->access & ACC_STATIC))
            return f->slot;
    }
    return -1;
}

Value *jvm_static(Class *c, const char *name)
{
    Field *f = find_field_here(c, jvm_sym(name, strlen(name)), NULL);

    return f && (f->access & ACC_STATIC) ? &c->statics[f->slot] : NULL;
}

static int implements(Class *s, Class *iface)
{
    int i;

    for (; s; s = s->super)
        for (i = 0; i < s->iface_count; i++)
            if (s->ifaces[i] == iface || implements(s->ifaces[i], iface))
                return 1;
    return 0;
}

int jvm_instanceof(Class *s, Class *t)
{
    if (s == t || t == vm->c_object)
        return 1;
    if (s->is_array) {
        if (!t->is_array)
            return 0;
        if (s->elem_type != 'L' && s->elem_type != '[')
            return s->elem_type == t->elem_type;
        if (t->elem_type != 'L' && t->elem_type != '[')
            return 0;
        return jvm_instanceof(s->elem_class, t->elem_class);
    }
    if (t->access & ACC_INTERFACE)
        return implements(s, t);
    for (s = s->super; s; s = s->super)
        if (s == t)
            return 1;
    return 0;
}

/* ---------- инициализация классов ---------- */

static void apply_constants(Thread *t, Class *c)
{
    int i;

    for (i = 0; i < c->field_count; i++) {
        Field *f = &c->fields[i];
        const CpEntry *e;
        if (!(f->access & ACC_STATIC) || !f->const_idx || f->const_idx >= c->cf.cp_count)
            continue;
        e = &c->cf.cp[f->const_idx];
        switch (e->tag) {
        case CP_INTEGER: c->statics[f->slot].i = e->u.i; break;
        case CP_FLOAT:   memcpy(&c->statics[f->slot].f, &e->u.f_bits, 4); break;
        case CP_LONG:    c->statics[f->slot].j = (int64_t)e->u.l_bits; break;
        case CP_DOUBLE:  memcpy(&c->statics[f->slot].d, &e->u.l_bits, 8); break;
        case CP_STRING: {
            char buf[1024];
            if (!classfile_utf8(&c->cf, e->u.ref.a, buf, sizeof(buf)))
                c->statics[f->slot].l = jvm_intern(t, jvm_new_string(t, buf));
            break;
        }
        }
    }
}

int jvm_init_class(Thread *t, Class *c)
{
    Method *clinit;
    int r;

    if (c->state == CLS_INITIALIZED)
        return 1;
    if (c->state == CLS_ERROR) {
        jvm_throw(t, "java/lang/NoClassDefFoundError", c->name);
        return -1;
    }
    if (c->state == CLS_INITIALIZING) {
        if (c->init_thread == t)
            return 1;
        t->yield = 1;          /* другой поток ещё инициализирует — подождём */
        return 0;
    }
    if (c->super && c->super->state != CLS_INITIALIZED) {
        r = jvm_init_class(t, c->super);
        if (r != 1)
            return r;
    }
    apply_constants(t, c);
    clinit = find_method_here(c, jvm_sym("<clinit>", 8), jvm_sym("()V", 3));
    if (!clinit || !clinit->code) {
        c->state = CLS_INITIALIZED;
        return 1;
    }
    c->state = CLS_INITIALIZING;
    c->init_thread = t;
    {
        Frame *f = t->depth ? &t->frames[t->depth - 1] : NULL;
        Frame *nf = jvm_push_frame(t, clinit, f ? f->sp : t->stack, 1);
        if (!nf) {
            c->state = CLS_ERROR;
            return -1;
        }
        nf->clinit_of = c;
    }
    return 0;
}

/* ---------- куча ---------- */

static void oset_insert(Object *o)
{
    size_t i;

    if (vm->oset_count * 2 >= vm->oset_cap) {
        size_t ncap = vm->oset_cap ? vm->oset_cap * 2 : 4096, k;
        Object **nt = calloc(ncap, sizeof(Object *));
        if (!nt)
            return;
        for (k = 0; k < vm->oset_cap; k++)
            if (vm->oset[k]) {
                size_t j = ptr_hash(vm->oset[k], ncap);
                while (nt[j])
                    j = (j + 1) & (ncap - 1);
                nt[j] = vm->oset[k];
            }
        free(vm->oset);
        vm->oset = nt;
        vm->oset_cap = ncap;
    }
    for (i = ptr_hash(o, vm->oset_cap); vm->oset[i]; i = (i + 1) & (vm->oset_cap - 1))
        ;
    vm->oset[i] = o;
    vm->oset_count++;
}

static int oset_has(const void *p)
{
    size_t i;

    if (!p || !vm->oset_cap || ((uintptr_t)p & 7))
        return 0;
    for (i = ptr_hash(p, vm->oset_cap); vm->oset[i]; i = (i + 1) & (vm->oset_cap - 1))
        if (vm->oset[i] == p)
            return 1;
    return 0;
}

Object *jvm_alloc(Class *c, size_t payload)
{
    size_t size = OBJ_HDR + payload;
    Object *o;

    if (vm->bytes + size > vm->limit)
        return NULL;
    o = calloc(1, size);
    if (!o)
        return NULL;
    o->cls = c;
    o->next = vm->objects;
    vm->objects = o;
    vm->obj_count++;
    vm->bytes += size;
    vm->since_gc += size;
    if (vm->since_gc > vm->threshold)
        vm->gc_requested = 1;
    oset_insert(o);
    return o;
}

Object *jvm_new(Thread *t, Class *c)
{
    Object *o = jvm_alloc(c, (size_t)c->inst_slots * sizeof(Value));

    if (!o && t)
        jvm_throw(t, "java/lang/OutOfMemoryError", NULL);
    return o;
}

Object *jvm_new_array(Thread *t, char type, Class *elem, int len)
{
    Class *ac;
    Object *o;

    if (len < 0) {
        if (t)
            jvm_throw(t, "java/lang/NegativeArraySizeException", NULL);
        return NULL;
    }
    ac = jvm_array_class(type, elem);
    o = ac ? jvm_alloc(ac, (size_t)len * (size_t)ac->elem_size) : NULL;
    if (!o) {
        if (t)
            jvm_throw(t, "java/lang/OutOfMemoryError", NULL);
        return NULL;
    }
    o->length = len;
    return o;
}

size_t jvm_heap_used(void)
{
    return vm->bytes;
}

size_t jvm_heap_limit(void)
{
    return vm->limit;
}

void jvm_gc_request(void)
{
    vm->gc_requested = 1;
}

/* ---------- строки ---------- */

Object *jvm_new_string_utf16(Thread *t, const uint16_t *s, int len)
{
    Object *chars, *str;

    if (!(chars = jvm_new_array(t, 'C', NULL, len)))
        return NULL;
    if (len)
        memcpy(ARR_DATA(chars), s, (size_t)len * 2);
    if (!(str = jvm_new(t, vm->c_string)))
        return NULL;
    OBJ_FIELDS(str)[vm->s_value].l = chars;
    OBJ_FIELDS(str)[vm->s_offset].i = 0;
    OBJ_FIELDS(str)[vm->s_count].i = len;
    return str;
}

/* Модифицированный UTF-8 из class-файлов и обычный UTF-8 — оба понимаем. */
Object *jvm_new_string(Thread *t, const char *utf8)
{
    size_t n = strlen(utf8), o = 0;
    const uint8_t *p = (const uint8_t *)utf8;
    uint16_t stackbuf[256] = { 0 }, *buf = n < 256 ? stackbuf : malloc(n * 2 + 2);
    Object *r;

    if (!buf)
        return NULL;
    while (*p) {
        if (p[0] < 0x80) {
            buf[o++] = p[0];
            p++;
        } else if ((p[0] & 0xE0) == 0xC0 && p[1]) {
            buf[o++] = (uint16_t)(((p[0] & 0x1F) << 6) | (p[1] & 0x3F));
            p += 2;
        } else if ((p[0] & 0xF0) == 0xE0 && p[1] && p[2]) {
            buf[o++] = (uint16_t)(((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F));
            p += 3;
        } else if ((p[0] & 0xF8) == 0xF0 && p[1] && p[2] && p[3]) {
            uint32_t cp = ((uint32_t)(p[0] & 7) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) |
                          ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            cp -= 0x10000;
            buf[o++] = (uint16_t)(0xD800 + (cp >> 10));
            buf[o++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
            p += 4;
        } else {
            buf[o++] = '?';
            p++;
        }
    }
    r = jvm_new_string_utf16(t, buf, (int)o);
    if (buf != stackbuf)
        free(buf);
    return r;
}

const uint16_t *jvm_string_chars(Object *s, int *len)
{
    Object *v = OBJ_FIELDS(s)[vm->s_value].l;

    *len = OBJ_FIELDS(s)[vm->s_count].i;
    return v ? ARR_OF(v, uint16_t) + OBJ_FIELDS(s)[vm->s_offset].i : NULL;
}

static const uint16_t *str_chars(Object *s, int *len)
{
    Object *v = OBJ_FIELDS(s)[vm->s_value].l;

    *len = OBJ_FIELDS(s)[vm->s_count].i;
    return v ? ARR_OF(v, uint16_t) + OBJ_FIELDS(s)[vm->s_offset].i : NULL;
}

int jvm_string_utf8(Object *s, char *out, size_t cap)
{
    int len, i;
    const uint16_t *c;
    size_t o = 0;

    if (!cap)
        return 0;
    if (!s || !(c = str_chars(s, &len))) {
        out[0] = '\0';
        return 0;
    }
    for (i = 0; i < len; i++) {
        uint16_t ch = c[i];
        if (ch < 0x80 && ch) {
            if (o + 1 >= cap)
                break;
            out[o++] = (char)ch;
        } else if (ch < 0x800) {
            if (o + 2 >= cap)
                break;
            out[o++] = (char)(0xC0 | (ch >> 6));
            out[o++] = (char)(0x80 | (ch & 0x3F));
        } else {
            if (o + 3 >= cap)
                break;
            out[o++] = (char)(0xE0 | (ch >> 12));
            out[o++] = (char)(0x80 | ((ch >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (ch & 0x3F));
        }
    }
    out[o] = '\0';
    return (int)o;
}

static uint32_t str_hash(Object *s)
{
    int len;
    const uint16_t *c = str_chars(s, &len);

    return c ? hash_bytes(c, (size_t)len * 2) : 0;
}

static int str_equal(Object *a, Object *b)
{
    int la, lb;
    const uint16_t *ca = str_chars(a, &la), *cb = str_chars(b, &lb);

    return la == lb && (!la || memcmp(ca, cb, (size_t)la * 2) == 0);
}

Object *jvm_intern(Thread *t, Object *s)
{
    size_t i;

    (void)t;
    if (!s)
        return NULL;
    if (vm->intern_count * 2 >= vm->intern_cap) {
        size_t ncap = vm->intern_cap ? vm->intern_cap * 2 : 1024, k;
        Object **nt = calloc(ncap, sizeof(Object *));
        if (!nt)
            return s;
        for (k = 0; k < vm->intern_cap; k++)
            if (vm->intern[k]) {
                size_t j = str_hash(vm->intern[k]) & (ncap - 1);
                while (nt[j])
                    j = (j + 1) & (ncap - 1);
                nt[j] = vm->intern[k];
            }
        free(vm->intern);
        vm->intern = nt;
        vm->intern_cap = ncap;
    }
    for (i = str_hash(s) & (vm->intern_cap - 1); vm->intern[i]; i = (i + 1) & (vm->intern_cap - 1))
        if (str_equal(vm->intern[i], s))
            return vm->intern[i];
    vm->intern[i] = s;
    vm->intern_count++;
    return s;
}

/* ---------- объекты-классы ---------- */

Object *jvm_class_object(Thread *t, Class *c)
{
    if (!c->class_obj) {
        Object *o = jvm_new(t, vm->c_class);
        if (!o)
            return NULL;
        OBJ_FIELDS(o)[vm->class_ref_slot].j = (int64_t)(intptr_t)c;
        c->class_obj = o;
    }
    return c->class_obj;
}

Class *jvm_class_from_object(Object *o)
{
    return o ? (Class *)(intptr_t)OBJ_FIELDS(o)[vm->class_ref_slot].j : NULL;
}

/* ---------- исключения из C ---------- */

int jvm_throw(Thread *t, const char *cls, const char *msg)
{
    Class *c = jvm_load(cls);
    Object *e;

    if (!c)
        c = jvm_load("java/lang/Error");
    e = c ? jvm_alloc(c, (size_t)c->inst_slots * sizeof(Value)) : NULL;
    if (e && msg && vm->throwable_msg_slot >= 0)
        OBJ_FIELDS(e)[vm->throwable_msg_slot].l = jvm_new_string(NULL, msg);
    t->exception = e;
    if (!e)
        BT_LOG("не удалось создать исключение %s (%s)\n", cls, msg ? msg : "");
    return NATIVE_EXCEPTION;
}

/* ---------- сборщик мусора ---------- */

static void mark(Object *o)
{
    if (!o || (o->flags & 1))
        return;
    o->flags |= 1;
    if (vm->mark_top == vm->mark_cap) {
        size_t ncap = vm->mark_cap ? vm->mark_cap * 2 : 1024;
        Object **ns = realloc(vm->mark_stack, ncap * sizeof(Object *));
        if (!ns) {
            vm->mark_failed = 1;   /* поля объекта не обойдём — подметать нельзя */
            return;
        }
        vm->mark_stack = ns;
        vm->mark_cap = ncap;
    }
    vm->mark_stack[vm->mark_top++] = o;
}

/* Консервативно: значение считается ссылкой, если это адрес живого объекта. */
static void mark_maybe(const Value *v)
{
    if (oset_has(v->l))
        mark(v->l);
}

static void drain(void)
{
    while (vm->mark_top) {
        Object *o = vm->mark_stack[--vm->mark_top];
        Class *c = o->cls;
        int i;

        if (c->is_array) {
            if (c->elem_type == 'L' || c->elem_type == '[') {
                Object **a = ARR_OF(o, Object *);
                for (i = 0; i < o->length; i++)
                    mark(a[i]);
            }
        } else {
            Value *f = OBJ_FIELDS(o);
            for (i = 0; i < c->inst_slots; i++)
                if (c->ref_map[i])
                    mark(f[i].l);
        }
    }
}

void jvm_gc(void)
{
    Class *c;
    Thread *t;
    Object **pp, *o;
    size_t i, live = 0;
    int k;

    for (c = vm->classes; c; c = c->next) {
        mark(c->class_obj);
        for (k = 0; k < c->static_count; k++)
            if (c->static_ref_map[k])
                mark(c->statics[k].l);
        drain();
    }
    for (i = 0; i < vm->intern_cap; i++)
        mark(vm->intern[i]);
    for (t = vm->threads; t; t = t->next) {
        Value *v, *top;
        mark(t->jthread);
        mark(t->exception);
        mark(t->wait_obj);
        if (t->state == TH_DEAD || !t->depth)
            continue;
        top = t->frames[t->depth - 1].sp;
        for (v = t->stack; v < top; v++)
            mark_maybe(v);
        drain();
    }
    drain();

    if (vm->mark_failed) {
        /* разметка неполная: удалять ничего нельзя, только снять метки */
        for (o = vm->objects; o; o = o->next)
            o->flags &= ~1u;
        vm->mark_failed = 0;
        vm->mark_top = 0;
        vm->since_gc = 0;
        vm->gc_requested = 0;
        return;
    }

    /* подметаем и заново строим множество живых объектов */
    memset(vm->oset, 0, vm->oset_cap * sizeof(Object *));
    vm->oset_count = 0;
    vm->bytes = 0;
    for (pp = &vm->objects; (o = *pp) != NULL;) {
        size_t size = OBJ_HDR + (o->cls->is_array ? (size_t)o->length * (size_t)o->cls->elem_size
                                                   : (size_t)o->cls->inst_slots * sizeof(Value));
        if (o->flags & 1) {
            o->flags &= ~1u;
            vm->bytes += size;
            live++;
            oset_insert(o);
            pp = &o->next;
        } else {
            *pp = o->next;
            free(o->mon);
            free(o);
        }
    }
    vm->obj_count = live;
    vm->since_gc = 0;
    vm->threshold = vm->bytes > (1u << 20) ? vm->bytes : (1u << 20);
    vm->gc_requested = 0;
    /* кэш интерфейсных вызовов хранит только указатели на классы — их не удаляем */
}

/* ---------- создание и уничтожение ---------- */

int jvm_create(const uint8_t *rt, size_t rt_size, const char *app_jar)
{
    jvm_destroy();
    vm = calloc(1, sizeof(Vm));
    if (!vm)
        return -1;
    vm->start_ms = mono_ms();
    vm->limit = 48u << 20;
    vm->threshold = 1u << 20;
    vm->trace = getenv("BT_J2ME_TRACE") != NULL;
    jvm_register_natives(jvm_lib_natives);

    if (jar_open_mem(&vm->rt, rt, rt_size)) {
        BT_LOG("встроенная библиотека повреждена\n");
        return -1;
    }
    if (app_jar) {
        if (jar_open(&vm->app, app_jar))
            return -1;
        vm->have_app = 1;
    }
    vm->c_object = jvm_load("java/lang/Object");
    vm->c_string = jvm_load("java/lang/String");
    vm->c_class = jvm_load("java/lang/Class");
    vm->c_thread = jvm_load("java/lang/Thread");
    if (!vm->c_object || !vm->c_string || !vm->c_class || !vm->c_thread) {
        BT_LOG("в библиотеке нет базовых классов\n");
        return -1;
    }
    vm->s_value = jvm_field_slot(vm->c_string, "value");
    vm->s_offset = jvm_field_slot(vm->c_string, "offset");
    vm->s_count = jvm_field_slot(vm->c_string, "count");
    vm->class_ref_slot = jvm_field_slot(vm->c_class, "vmref");
    vm->throwable_msg_slot = jvm_field_slot(jvm_load("java/lang/Throwable"), "message");
    if (vm->s_value < 0 || vm->s_offset < 0 || vm->s_count < 0 || vm->class_ref_slot < 0) {
        BT_LOG("у String/Class в библиотеке не те поля\n");
        return -1;
    }
    /* эти классы трогает сама машина — инициализировать их нечего */
    vm->c_object->state = CLS_INITIALIZED;
    vm->c_string->state = CLS_INITIALIZED;
    vm->c_class->state = CLS_INITIALIZED;
    return 0;
}

Jar *jvm_app_jar(void)
{
    return vm && vm->have_app ? &vm->app : NULL;
}

void jvm_destroy(void)
{
    Object *o, *on;
    Class *c, *cn;
    Thread *t, *tn;
    size_t i;

    if (!vm)
        return;
    for (o = vm->objects; o; o = on) {
        on = o->next;
        free(o->mon);
        free(o);
    }
    for (c = vm->classes; c; c = cn) {
        cn = c->next;
        free_class(c);
    }
    for (t = vm->threads; t; t = tn) {
        tn = t->next;
        free(t->stack);
        free(t->frames);
        free(t);
    }
    for (i = 0; i < vm->sym_cap; i++)
        free(vm->syms[i]);
    free(vm->syms);
    free(vm->ctab);
    free(vm->oset);
    free(vm->intern);
    free(vm->mark_stack);
    jar_close(&vm->rt);
    if (vm->have_app)
        jar_close(&vm->app);
    free(vm);
    vm = NULL;
}
