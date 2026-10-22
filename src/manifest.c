#include "manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_range(const char *s, size_t n)
{
    char *r = malloc(n + 1);

    if (r) {
        memcpy(r, s, n);
        r[n] = '\0';
    }
    return r;
}

static void trim(char *s)
{
    size_t n = strlen(s), i = 0;

    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        s[--n] = '\0';
    while (s[i] == ' ' || s[i] == '\t')
        i++;
    memmove(s, s + i, n - i + 1);
}

/* Строка, начинающаяся с пробела, продолжает значение предыдущей (лимит 72 байта в JAR). */
static int append(char **dst, const char *s, size_t n)
{
    size_t old = strlen(*dst);
    char *r = realloc(*dst, old + n + 1);

    if (!r)
        return -1;
    memcpy(r + old, s, n);
    r[old + n] = '\0';
    *dst = r;
    return 0;
}

int manifest_parse(Manifest *m, const char *text, size_t len)
{
    const char *p = text, *end = text + len;
    size_t cap = 0;

    memset(m, 0, sizeof(*m));
    /* UTF-8 BOM встречается в самодельных .jad */
    if (len >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;

    while (p < end) {
        const char *eol = p, *colon;
        size_t n;

        while (eol < end && *eol != '\n' && *eol != '\r')
            eol++;
        n = (size_t)(eol - p);

        if (n && p[0] == ' ' && m->count) {
            if (append(&m->attrs[m->count - 1].value, p + 1, n - 1))
                return -1;
        } else if (n && (colon = memchr(p, ':', n)) != NULL) {
            ManifestAttr *a;

            if (m->count == cap) {
                size_t ncap = cap ? cap * 2 : 16;
                ManifestAttr *na = realloc(m->attrs, ncap * sizeof(*na));
                if (!na)
                    return -1;
                m->attrs = na;
                cap = ncap;
            }
            a = &m->attrs[m->count];
            a->key   = dup_range(p, (size_t)(colon - p));
            a->value = dup_range(colon + 1, (size_t)(eol - colon - 1));
            if (!a->key || !a->value)
                return -1;
            m->count++;
        }

        p = eol;
        if (p < end && *p == '\r')
            p++;
        if (p < end && *p == '\n')
            p++;
    }

    for (cap = 0; cap < m->count; cap++) {
        trim(m->attrs[cap].key);
        trim(m->attrs[cap].value);
    }
    return 0;
}

void manifest_free(Manifest *m)
{
    size_t i;

    for (i = 0; i < m->count; i++) {
        free(m->attrs[i].key);
        free(m->attrs[i].value);
    }
    free(m->attrs);
    memset(m, 0, sizeof(*m));
}

const char *manifest_get(const Manifest *m, const char *key)
{
    size_t i;

    for (i = 0; i < m->count; i++)
        if (strcmp(m->attrs[i].key, key) == 0)
            return m->attrs[i].value;
    return NULL;
}

static void copy_field(char *dst, size_t cap, const char *s, size_t n)
{
    if (n >= cap)
        n = cap - 1;
    memcpy(dst, s, n);
    dst[n] = '\0';
    trim(dst);
}

int manifest_midlet(const Manifest *m, int n, MidletInfo *out)
{
    char key[32];
    const char *v, *c1, *c2;

    snprintf(key, sizeof(key), "MIDlet-%d", n);
    v = manifest_get(m, key);
    if (!v || !(c1 = strchr(v, ',')) || !(c2 = strchr(c1 + 1, ',')))
        return -1;
    copy_field(out->name, sizeof(out->name), v, (size_t)(c1 - v));
    copy_field(out->icon, sizeof(out->icon), c1 + 1, (size_t)(c2 - c1 - 1));
    copy_field(out->class_name, sizeof(out->class_name), c2 + 1, strlen(c2 + 1));
    return 0;
}
