#include "classfile.h"
#include "util.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *p, *end;
    char          *err;
    size_t         errlen;
    int            failed;
} Reader;

static void fail(Reader *r, const char *fmt, ...)
{
    va_list ap;

    if (r->failed)
        return;
    r->failed = 1;
    va_start(ap, fmt);
    vsnprintf(r->err, r->errlen, fmt, ap);
    va_end(ap);
}

static int need(Reader *r, size_t n)
{
    if (r->failed)
        return 0;
    if ((size_t)(r->end - r->p) < n) {
        fail(r, "неожиданный конец файла");
        return 0;
    }
    return 1;
}

static uint8_t u1(Reader *r)
{
    return need(r, 1) ? *r->p++ : 0;
}

static uint16_t u2(Reader *r)
{
    uint16_t v;

    if (!need(r, 2))
        return 0;
    v = rd_be16(r->p);
    r->p += 2;
    return v;
}

static uint32_t u4(Reader *r)
{
    uint32_t v;

    if (!need(r, 4))
        return 0;
    v = rd_be32(r->p);
    r->p += 4;
    return v;
}

static void skip(Reader *r, size_t n)
{
    if (need(r, n))
        r->p += n;
}

static int utf8_is(const ClassFile *cf, uint16_t idx, const char *s)
{
    size_t n = strlen(s);

    return idx && idx < cf->cp_count && cf->cp[idx].tag == CP_UTF8 &&
           cf->cp[idx].u.utf8.len == n && memcmp(cf->cp[idx].u.utf8.str, s, n) == 0;
}

static void parse_cp(Reader *r, ClassFile *cf)
{
    uint16_t i;

    cf->cp_count = u2(r);
    if (r->failed || cf->cp_count == 0) {
        fail(r, "пустой пул констант");
        return;
    }
    cf->cp = calloc(cf->cp_count, sizeof(CpEntry));
    if (!cf->cp) {
        fail(r, "нет памяти");
        return;
    }
    for (i = 1; i < cf->cp_count && !r->failed; i++) {
        CpEntry *e = &cf->cp[i];

        e->tag = u1(r);
        switch (e->tag) {
        case CP_UTF8:
            e->u.utf8.len = u2(r);
            if (need(r, e->u.utf8.len)) {
                e->u.utf8.str = (const char *)r->p;
                r->p += e->u.utf8.len;
            }
            break;
        case CP_INTEGER: e->u.i = (int32_t)u4(r); break;
        case CP_FLOAT:   e->u.f_bits = u4(r); break;
        case CP_LONG:
        case CP_DOUBLE:
            e->u.l_bits = (uint64_t)u4(r) << 32;
            e->u.l_bits |= u4(r);
            i++; /* long/double занимают два слота */
            break;
        case CP_CLASS:
        case CP_STRING:
        case CP_METHODTYPE:
            e->u.ref.a = u2(r);
            break;
        case CP_FIELDREF:
        case CP_METHODREF:
        case CP_IMETHODREF:
        case CP_NAMETYPE:
        case CP_INVOKEDYNAMIC:
            e->u.ref.a = u2(r);
            e->u.ref.b = u2(r);
            break;
        case CP_METHODHANDLE:
            e->u.ref.a = u1(r);
            e->u.ref.b = u2(r);
            break;
        default:
            fail(r, "неизвестный тег %u в пуле констант (#%u)", e->tag, i);
        }
    }
}

static void parse_members(Reader *r, ClassFile *cf, uint16_t *count, Member **out, int is_method)
{
    uint16_t i, j;

    *count = u2(r);
    if (r->failed)
        return;
    *out = calloc(*count ? *count : 1, sizeof(Member));
    if (!*out) {
        fail(r, "нет памяти");
        return;
    }
    for (i = 0; i < *count && !r->failed; i++) {
        Member *m = &(*out)[i];
        uint16_t nattr;

        m->access   = u2(r);
        m->name_idx = u2(r);
        m->desc_idx = u2(r);
        nattr       = u2(r);
        for (j = 0; j < nattr && !r->failed; j++) {
            uint16_t aname = u2(r);
            uint32_t alen  = u4(r);

            if (!need(r, alen))
                return;
            if (is_method && utf8_is(cf, aname, "Code") && alen >= 12) {
                const uint8_t *a = r->p, *end = r->p + alen, *q;
                uint16_t k, n;

                m->max_stack  = rd_be16(a);
                m->max_locals = rd_be16(a + 2);
                m->code_len   = rd_be32(a + 4);
                if (m->code_len > alen - 12) {
                    fail(r, "атрибут Code длиннее, чем объявлено");
                    return;
                }
                m->code = a + 8;
                q = m->code + m->code_len;
                m->exc_count = rd_be16(q);
                m->exc_table = q + 2;
                q += 2 + (size_t)m->exc_count * 8;
                if (q + 2 > end) {
                    fail(r, "повреждена таблица исключений");
                    return;
                }
                /* вложенные атрибуты Code: ищем LineNumberTable */
                n = rd_be16(q);
                q += 2;
                for (k = 0; k < n && q + 6 <= end; k++) {
                    uint32_t sl = rd_be32(q + 2);
                    if (sl > (size_t)(end - q - 6))
                        break;
                    if (utf8_is(cf, rd_be16(q), "LineNumberTable") && sl >= 2) {
                        m->line_count = rd_be16(q + 6);
                        m->line_table = q + 8;
                        if ((size_t)m->line_count * 4 > sl - 2)
                            m->line_count = 0;
                    }
                    q += 6 + sl;
                }
            } else if (!is_method && utf8_is(cf, aname, "ConstantValue") && alen >= 2) {
                m->const_idx = rd_be16(r->p);
            }
            r->p += alen;
        }
    }
}

int classfile_parse(ClassFile *cf, const uint8_t *buf, size_t len, char *err, size_t errlen)
{
    Reader r;
    uint16_t i, nattr;

    memset(cf, 0, sizeof(*cf));
    r.p = buf;
    r.end = buf + len;
    r.err = err;
    r.errlen = errlen;
    r.failed = 0;
    cf->buf = buf;

    if (u4(&r) != 0xCAFEBABEu) {
        fail(&r, "нет сигнатуры CAFEBABE");
        goto out;
    }
    cf->minor = u2(&r);
    cf->major = u2(&r);
    parse_cp(&r, cf);
    cf->access      = u2(&r);
    cf->this_class  = u2(&r);
    cf->super_class = u2(&r);
    cf->iface_count = u2(&r);
    if (!r.failed) {
        cf->ifaces = calloc(cf->iface_count ? cf->iface_count : 1, sizeof(uint16_t));
        if (!cf->ifaces)
            fail(&r, "нет памяти");
        for (i = 0; i < cf->iface_count && !r.failed; i++)
            cf->ifaces[i] = u2(&r);
    }
    parse_members(&r, cf, &cf->field_count, &cf->fields, 0);
    parse_members(&r, cf, &cf->method_count, &cf->methods, 1);
    nattr = u2(&r);
    for (i = 0; i < nattr && !r.failed; i++) {
        u2(&r);
        skip(&r, u4(&r));
    }
    if (!r.failed && (cf->this_class == 0 || cf->this_class >= cf->cp_count ||
                      cf->cp[cf->this_class].tag != CP_CLASS))
        fail(&r, "некорректный this_class");

out:
    if (r.failed) {
        classfile_free(cf);
        return -1;
    }
    return 0;
}

void classfile_free(ClassFile *cf)
{
    free(cf->cp);
    free(cf->ifaces);
    free(cf->fields);
    free(cf->methods);
    memset(cf, 0, sizeof(*cf));
}

int classfile_utf8(const ClassFile *cf, uint16_t idx, char *out, size_t cap)
{
    size_t n;

    if (!idx || idx >= cf->cp_count || cf->cp[idx].tag != CP_UTF8 || cap == 0)
        return -1;
    n = cf->cp[idx].u.utf8.len;
    if (n >= cap)
        n = cap - 1;
    memcpy(out, cf->cp[idx].u.utf8.str, n);
    out[n] = '\0';
    return 0;
}

int classfile_class_name(const ClassFile *cf, uint16_t idx, char *out, size_t cap)
{
    if (!idx || idx >= cf->cp_count || cf->cp[idx].tag != CP_CLASS)
        return -1;
    return classfile_utf8(cf, cf->cp[idx].u.ref.a, out, cap);
}
