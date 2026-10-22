/* Разбор META-INF/MANIFEST.MF и .jad — у обоих формат "Ключ: значение". */
#ifndef BT_MANIFEST_H
#define BT_MANIFEST_H

#include <stddef.h>

typedef struct {
    char *key;
    char *value;
} ManifestAttr;

typedef struct {
    ManifestAttr *attrs;
    size_t        count;
} Manifest;

/* Описание одного мидлета из атрибута "MIDlet-N: Имя, Иконка, Класс". */
typedef struct {
    char name[128];
    char icon[128];
    char class_name[256];
} MidletInfo;

int         manifest_parse(Manifest *m, const char *text, size_t len);
void        manifest_free(Manifest *m);
const char *manifest_get(const Manifest *m, const char *key);
int         manifest_midlet(const Manifest *m, int n, MidletInfo *out);

#endif
