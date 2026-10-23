/* Нативные методы java.lang / java.util: то, что нельзя или невыгодно писать на Java. */
#define _POSIX_C_SOURCE 200112L
#include "jvm_internal.h"
#include "util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define THIS (args[0].l)

/* ---------- java.lang.Object ---------- */

static int obj_getClass(Thread *t, Value *args, Value *ret)
{
    ret->l = jvm_class_object(t, THIS->cls);
    return ret->l ? NATIVE_OK : NATIVE_EXCEPTION;
}

static int obj_hashCode(Thread *t, Value *args, Value *ret)
{
    uintptr_t p = (uintptr_t)THIS;

    (void)t;
    ret->i = (int32_t)((uint32_t)(p >> 3) ^ (uint32_t)((uint64_t)p >> 32));
    return NATIVE_OK;
}

static int obj_wait(Thread *t, Value *args, Value *ret)
{
    (void)ret;
    if (args[1].j < 0)
        return jvm_throw(t, "java/lang/IllegalArgumentException", "timeout < 0");
    return jvm_monitor_wait(t, THIS, args[1].j);
}

static int obj_notify(Thread *t, Value *args, Value *ret)
{
    (void)ret;
    return jvm_monitor_notify(t, THIS, 0);
}

static int obj_notifyAll(Thread *t, Value *args, Value *ret)
{
    (void)ret;
    return jvm_monitor_notify(t, THIS, 1);
}

/* ---------- java.lang.Class ---------- */

static void dots_to_slashes(char *s)
{
    for (; *s; s++)
        if (*s == '.')
            *s = '/';
}

static int class_forName(Thread *t, Value *args, Value *ret)
{
    char name[512];
    Class *c;
    int r;

    if (!args[0].l)
        return jvm_throw(t, "java/lang/NullPointerException", NULL);
    jvm_string_utf8(args[0].l, name, sizeof(name));
    dots_to_slashes(name);
    if (!(c = jvm_load(name)))
        return jvm_throw(t, "java/lang/ClassNotFoundException", name);
    r = jvm_init_class(t, c);
    if (r < 0)
        return NATIVE_EXCEPTION;
    if (r == 0)
        return NATIVE_REEXEC;          /* сначала отработает <clinit>, потом вызов повторится */
    ret->l = jvm_class_object(t, c);
    return NATIVE_OK;
}

static int class_getName(Thread *t, Value *args, Value *ret)
{
    char name[512];
    Class *c = jvm_class_from_object(THIS);

    snprintf(name, sizeof(name), "%s", c->name);
    {
        char *p;
        for (p = name; *p; p++)
            if (*p == '/')
                *p = '.';
    }
    ret->l = jvm_new_string(t, name);
    return ret->l ? NATIVE_OK : NATIVE_EXCEPTION;
}

static int class_isInstance(Thread *t, Value *args, Value *ret)
{
    (void)t;
    ret->i = args[1].l && jvm_instanceof(args[1].l->cls, jvm_class_from_object(THIS));
    return NATIVE_OK;
}

static int class_isAssignableFrom(Thread *t, Value *args, Value *ret)
{
    if (!args[1].l)
        return jvm_throw(t, "java/lang/NullPointerException", NULL);
    ret->i = jvm_instanceof(jvm_class_from_object(args[1].l), jvm_class_from_object(THIS));
    return NATIVE_OK;
}

static int class_isArray(Thread *t, Value *args, Value *ret)
{
    (void)t;
    ret->i = jvm_class_from_object(THIS)->is_array;
    return NATIVE_OK;
}

static int class_isInterface(Thread *t, Value *args, Value *ret)
{
    (void)t;
    ret->i = (jvm_class_from_object(THIS)->access & ACC_INTERFACE) != 0;
    return NATIVE_OK;
}

static int class_newInstance0(Thread *t, Value *args, Value *ret)
{
    Class *c = jvm_class_from_object(THIS);
    int r;

    if (c->is_array || (c->access & (ACC_INTERFACE | ACC_ABSTRACT)))
        return jvm_throw(t, "java/lang/InstantiationException", c->name);
    if (!jvm_find_method(c, "<init>", "()V"))
        return jvm_throw(t, "java/lang/InstantiationException", c->name);
    r = jvm_init_class(t, c);
    if (r < 0)
        return NATIVE_EXCEPTION;
    if (r == 0)
        return NATIVE_REEXEC;
    ret->l = jvm_new(t, c);
    return ret->l ? NATIVE_OK : NATIVE_EXCEPTION;
}

/* static native void init0(Object o): хвостовой вызов o.<init>() */
static int class_init0(Thread *t, Value *args, Value *ret)
{
    Method *m;

    (void)ret;
    if (!args[0].l)
        return jvm_throw(t, "java/lang/NullPointerException", NULL);
    m = jvm_find_method(args[0].l->cls, "<init>", "()V");
    if (!m || m->owner != args[0].l->cls)
        return jvm_throw(t, "java/lang/InstantiationException", args[0].l->cls->name);
    t->tail = m;
    return NATIVE_TAILCALL;
}

static ResourceFilter res_filter;

void jvm_set_resource_filter(ResourceFilter f)
{
    res_filter = f;
}

/* byte[] getResource0(String name): ресурс из JAR игры (или библиотеки) */
static int class_getResource0(Thread *t, Value *args, Value *ret)
{
    char name[512], path[600];
    Class *c = jvm_class_from_object(THIS);
    const JarEntry *e = NULL;
    Jar *app = jvm_app_jar();
    uint8_t *data = NULL;
    size_t len = 0;

    if (!args[1].l)
        return jvm_throw(t, "java/lang/NullPointerException", NULL);
    jvm_string_utf8(args[1].l, name, sizeof(name));
    if (name[0] == '/') {
        snprintf(path, sizeof(path), "%s", name + 1);
    } else {
        const char *slash = strrchr(c->name, '/');
        if (slash)
            snprintf(path, sizeof(path), "%.*s/%s", (int)(slash - c->name), c->name, name);
        else
            snprintf(path, sizeof(path), "%s", name);
    }
    if (app && (e = jar_find(app, path)) != NULL)
        data = jar_read(app, e, &len);
    if (data && res_filter)
        data = res_filter(path, data, &len);
    if (!data) {
        ret->l = NULL;
        return NATIVE_OK;
    }
    ret->l = jvm_new_array(t, 'B', NULL, (int)len);
    if (ret->l)
        memcpy(ARR_DATA(ret->l), data, len);
    free(data);
    return ret->l ? NATIVE_OK : NATIVE_EXCEPTION;
}

/* ---------- java.lang.System / Runtime ---------- */

/* Смещение местного времени от UTC (с летним временем, если оно сейчас действует). */
static int tz_localOffset(Thread *t, Value *args, Value *ret)
{
    time_t now = time(NULL);
    struct tm g = *gmtime(&now);

    (void)t;
    (void)args;
    g.tm_isdst = localtime(&now)->tm_isdst;   /* mktime читает «UTC» как местное — разница и есть смещение */
    ret->i = (int32_t)difftime(now, mktime(&g)) * 1000;
    return NATIVE_OK;
}

static int sys_currentTimeMillis(Thread *t, Value *args, Value *ret)
{
    struct timeval tv;

    (void)t;
    (void)args;
    gettimeofday(&tv, NULL);
    ret->j = (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    return NATIVE_OK;
}

static int sys_arraycopy(Thread *t, Value *args, Value *ret)
{
    Object *src = args[0].l, *dst = args[2].l;
    int32_t sp = args[1].i, dp = args[3].i, n = args[4].i;
    Class *sc, *dc;

    (void)ret;
    if (!src || !dst)
        return jvm_throw(t, "java/lang/NullPointerException", NULL);
    sc = src->cls;
    dc = dst->cls;
    if (!sc->is_array || !dc->is_array)
        return jvm_throw(t, "java/lang/ArrayStoreException", NULL);
    if (sc->elem_type != dc->elem_type &&
        !((sc->elem_type == 'L' || sc->elem_type == '[') && (dc->elem_type == 'L' || dc->elem_type == '[')))
        return jvm_throw(t, "java/lang/ArrayStoreException", NULL);
    if (n < 0 || sp < 0 || dp < 0 || (int64_t)sp + n > src->length || (int64_t)dp + n > dst->length)
        return jvm_throw(t, "java/lang/ArrayIndexOutOfBoundsException", NULL);
    if (n == 0)
        return NATIVE_OK;
    if ((sc->elem_type == 'L' || sc->elem_type == '[') && !jvm_instanceof(sc, dc)) {
        /* поэлементная проверка типов */
        Object **s = ARR_OF(src, Object *) + sp, **d = ARR_OF(dst, Object *) + dp;
        int32_t i;
        for (i = 0; i < n; i++) {
            if (s[i] && !jvm_instanceof(s[i]->cls, dc->elem_class))
                return jvm_throw(t, "java/lang/ArrayStoreException", NULL);
            d[i] = s[i];
        }
        return NATIVE_OK;
    }
    memmove((uint8_t *)ARR_DATA(dst) + (size_t)dp * (size_t)dc->elem_size,
            (uint8_t *)ARR_DATA(src) + (size_t)sp * (size_t)sc->elem_size, (size_t)n * (size_t)sc->elem_size);
    return NATIVE_OK;
}

static int sys_identityHashCode(Thread *t, Value *args, Value *ret)
{
    if (!args[0].l) {
        ret->i = 0;
        return NATIVE_OK;
    }
    return obj_hashCode(t, args, ret);
}

static int rt_freeMemory(Thread *t, Value *args, Value *ret)
{
    (void)t;
    (void)args;
    ret->j = (int64_t)(jvm_heap_limit() - jvm_heap_used());
    return NATIVE_OK;
}

static int rt_totalMemory(Thread *t, Value *args, Value *ret)
{
    (void)t;
    (void)args;
    ret->j = (int64_t)jvm_heap_limit();
    return NATIVE_OK;
}

static int rt_gc(Thread *t, Value *args, Value *ret)
{
    (void)args;
    (void)ret;
    jvm_gc_request();
    t->yield = 1;
    return NATIVE_YIELD;
}

static int rt_exit(Thread *t, Value *args, Value *ret)
{
    (void)ret;
    jvm_request_exit(args[0].i);
    t->yield = 1;
    return NATIVE_YIELD;
}

/* ---------- java.lang.Thread ---------- */

static int th_currentThread(Thread *t, Value *args, Value *ret)
{
    (void)args;
    if (!t->jthread) {
        Class *c = jvm_load("java/lang/Thread");
        t->jthread = c ? jvm_new(t, c) : NULL;
    }
    ret->l = t->jthread;
    return ret->l ? NATIVE_OK : NATIVE_EXCEPTION;
}

static int th_start0(Thread *t, Value *args, Value *ret)
{
    Method *m = jvm_find_method(THIS->cls, "run", "()V");

    (void)ret;
    if (!m || !jvm_new_thread(THIS, THIS, m))
        return jvm_throw(t, "java/lang/OutOfMemoryError", "thread");
    return NATIVE_OK;
}

static int th_sleep(Thread *t, Value *args, Value *ret)
{
    int64_t ms = args[0].j;

    (void)ret;
    if (ms < 0)
        return jvm_throw(t, "java/lang/IllegalArgumentException", "timeout < 0");
    t->state = TH_SLEEPING;
    t->wake_at = jvm_now() + (uint32_t)(ms > 0x7FFFFFFF ? 0x7FFFFFFF : ms);
    return NATIVE_YIELD;
}

static int th_yield(Thread *t, Value *args, Value *ret)
{
    (void)args;
    (void)ret;
    t->yield = 1;
    return NATIVE_YIELD;
}

static int th_isAlive(Thread *t, Value *args, Value *ret)
{
    (void)t;
    ret->i = jvm_thread_alive(THIS);
    return NATIVE_OK;
}

static int th_activeCount(Thread *t, Value *args, Value *ret)
{
    (void)t;
    (void)args;
    ret->i = 1;
    return NATIVE_OK;
}

/* ---------- строки и числа ---------- */

static int str_intern(Thread *t, Value *args, Value *ret)
{
    ret->l = jvm_intern(t, THIS);
    return NATIVE_OK;
}

static int f_floatToIntBits(Thread *t, Value *args, Value *ret)
{
    (void)t;
    if (args[0].f != args[0].f)
        ret->i = 0x7FC00000;
    else
        memcpy(&ret->i, &args[0].f, 4);
    return NATIVE_OK;
}

static int f_intBitsToFloat(Thread *t, Value *args, Value *ret)
{
    (void)t;
    memcpy(&ret->f, &args[0].i, 4);
    return NATIVE_OK;
}

static int d_doubleToLongBits(Thread *t, Value *args, Value *ret)
{
    (void)t;
    if (args[0].d != args[0].d)
        ret->j = 0x7FF8000000000000LL;
    else
        memcpy(&ret->j, &args[0].d, 8);
    return NATIVE_OK;
}

static int d_longBitsToDouble(Thread *t, Value *args, Value *ret)
{
    (void)t;
    memcpy(&ret->d, &args[0].j, 8);
    return NATIVE_OK;
}

/* Кратчайшая запись, которая читается обратно в то же число (как в Java). */
static int d_toString0(Thread *t, Value *args, Value *ret)
{
    double v = args[0].d;
    char buf[64];
    int p;

    if (v != v)
        strcpy(buf, "NaN");
    else if (v == HUGE_VAL)
        strcpy(buf, "Infinity");
    else if (v == -HUGE_VAL)
        strcpy(buf, "-Infinity");
    else if (v == 0)
        strcpy(buf, signbit(v) ? "-0.0" : "0.0");
    else {
        for (p = 1; p <= 17; p++) {
            snprintf(buf, sizeof(buf), "%.*g", p, v);
            if (strtod(buf, NULL) == v)
                break;
        }
        if (fabs(v) >= 1e-3 && fabs(v) < 1e7) {
            if (!strchr(buf, '.') && !strchr(buf, 'e'))
                strcat(buf, ".0");
            else if (strchr(buf, 'e'))
                snprintf(buf, sizeof(buf), "%.1f", v);
        }
    }
    ret->l = jvm_new_string(t, buf);
    return ret->l ? NATIVE_OK : NATIVE_EXCEPTION;
}

static int d_parse0(Thread *t, Value *args, Value *ret)
{
    char buf[128], *end;

    jvm_string_utf8(args[0].l, buf, sizeof(buf));
    ret->d = strtod(buf, &end);
    if (end == buf || *end)
        return jvm_throw(t, "java/lang/NumberFormatException", buf);
    return NATIVE_OK;
}

/* ---------- java.lang.Math ---------- */

#define MATH1(name, expr) \
    static int math_##name(Thread *t, Value *args, Value *ret) { (void)t; double x = args[0].d; ret->d = (expr); return NATIVE_OK; }
MATH1(sqrt, sqrt(x))
MATH1(sin, sin(x))
MATH1(cos, cos(x))
MATH1(tan, tan(x))
MATH1(floor, floor(x))
MATH1(ceil, ceil(x))

static int math_atan2(Thread *t, Value *args, Value *ret)
{
    (void)t;
    ret->d = atan2(args[0].d, args[2].d);
    return NATIVE_OK;
}

static int math_pow(Thread *t, Value *args, Value *ret)
{
    (void)t;
    ret->d = pow(args[0].d, args[2].d);
    return NATIVE_OK;
}

/* ---------- вывод для System.out ---------- */

static int native_print(Thread *t, Value *args, Value *ret)
{
    char buf[2048];

    (void)t;
    (void)ret;
    jvm_string_utf8(args[0].l, buf, sizeof(buf));
    fprintf(stdout, "%s", buf);
    fflush(stdout);
    return NATIVE_OK;
}

const NativeDef jvm_lib_natives[] = {
    { "java/lang/Object", "getClass", "()Ljava/lang/Class;", obj_getClass },
    { "java/lang/Object", "hashCode", "()I", obj_hashCode },
    { "java/lang/Object", "wait", "(J)V", obj_wait },
    { "java/lang/Object", "notify", "()V", obj_notify },
    { "java/lang/Object", "notifyAll", "()V", obj_notifyAll },
    { "java/lang/Class", "forName", "(Ljava/lang/String;)Ljava/lang/Class;", class_forName },
    { "java/lang/Class", "getName", "()Ljava/lang/String;", class_getName },
    { "java/lang/Class", "isInstance", "(Ljava/lang/Object;)Z", class_isInstance },
    { "java/lang/Class", "isAssignableFrom", "(Ljava/lang/Class;)Z", class_isAssignableFrom },
    { "java/lang/Class", "isArray", "()Z", class_isArray },
    { "java/lang/Class", "isInterface", "()Z", class_isInterface },
    { "java/lang/Class", "newInstance0", "()Ljava/lang/Object;", class_newInstance0 },
    { "java/lang/Class", "init0", "(Ljava/lang/Object;)V", class_init0 },
    { "java/lang/Class", "getResource0", "(Ljava/lang/String;)[B", class_getResource0 },
    { "java/lang/System", "currentTimeMillis", "()J", sys_currentTimeMillis },
    { "java/util/TimeZone", "localOffset0", "()I", tz_localOffset },
    { "java/lang/System", "arraycopy", "(Ljava/lang/Object;ILjava/lang/Object;II)V", sys_arraycopy },
    { "java/lang/System", "identityHashCode", "(Ljava/lang/Object;)I", sys_identityHashCode },
    { "java/lang/Runtime", "freeMemory", "()J", rt_freeMemory },
    { "java/lang/Runtime", "totalMemory", "()J", rt_totalMemory },
    { "java/lang/Runtime", "gc", "()V", rt_gc },
    { "java/lang/Runtime", "exit", "(I)V", rt_exit },
    { "java/lang/Thread", "currentThread", "()Ljava/lang/Thread;", th_currentThread },
    { "java/lang/Thread", "start0", "()V", th_start0 },
    { "java/lang/Thread", "sleep", "(J)V", th_sleep },
    { "java/lang/Thread", "yield", "()V", th_yield },
    { "java/lang/Thread", "isAlive", "()Z", th_isAlive },
    { "java/lang/Thread", "activeCount", "()I", th_activeCount },
    { "java/lang/String", "intern", "()Ljava/lang/String;", str_intern },
    { "java/lang/Float", "floatToIntBits", "(F)I", f_floatToIntBits },
    { "java/lang/Float", "intBitsToFloat", "(I)F", f_intBitsToFloat },
    { "java/lang/Double", "doubleToLongBits", "(D)J", d_doubleToLongBits },
    { "java/lang/Double", "longBitsToDouble", "(J)D", d_longBitsToDouble },
    { "java/lang/Double", "toString0", "(D)Ljava/lang/String;", d_toString0 },
    { "java/lang/Double", "parse0", "(Ljava/lang/String;)D", d_parse0 },
    { "java/lang/Math", "sqrt", "(D)D", math_sqrt },
    { "java/lang/Math", "sin", "(D)D", math_sin },
    { "java/lang/Math", "cos", "(D)D", math_cos },
    { "java/lang/Math", "tan", "(D)D", math_tan },
    { "java/lang/Math", "floor", "(D)D", math_floor },
    { "java/lang/Math", "ceil", "(D)D", math_ceil },
    { "java/lang/Math", "atan2", "(DD)D", math_atan2 },
    { "java/lang/Math", "pow", "(DD)D", math_pow },
    { "btvm/Native", "print", "(Ljava/lang/String;)V", native_print },
    { NULL, NULL, NULL, NULL }
};
