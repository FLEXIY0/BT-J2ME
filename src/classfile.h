/* Загрузчик class-файлов. CLDC 1.0/1.1 — это версии 45.3–49.0,
 * но парсер принимает и более новые, чтобы давать понятную ошибку на этапе верификации. */
#ifndef BT_CLASSFILE_H
#define BT_CLASSFILE_H

#include <stddef.h>
#include <stdint.h>

enum {
    CP_UTF8 = 1, CP_INTEGER = 3, CP_FLOAT = 4, CP_LONG = 5, CP_DOUBLE = 6,
    CP_CLASS = 7, CP_STRING = 8, CP_FIELDREF = 9, CP_METHODREF = 10,
    CP_IMETHODREF = 11, CP_NAMETYPE = 12, CP_METHODHANDLE = 15,
    CP_METHODTYPE = 16, CP_INVOKEDYNAMIC = 18
};

typedef struct {
    uint8_t tag;
    union {
        struct { const char *str; uint16_t len; } utf8; /* указывает в буфер класса, не 0-терминирована */
        int32_t  i;
        uint32_t f_bits;
        uint64_t l_bits;
        struct { uint16_t a, b; } ref;                  /* индексы класса/имени/типа */
    } u;
} CpEntry;

typedef struct {
    uint16_t       access;
    uint16_t       name_idx;
    uint16_t       desc_idx;
    /* Только для методов с атрибутом Code */
    uint16_t       max_stack;
    uint16_t       max_locals;
    uint32_t       code_len;
    const uint8_t *code;
} Member;

typedef struct {
    const uint8_t *buf;          /* владелец — вызывающий код */
    uint16_t       minor, major;
    uint16_t       cp_count;
    CpEntry       *cp;
    uint16_t       access;
    uint16_t       this_class, super_class;
    uint16_t       iface_count;
    uint16_t      *ifaces;
    uint16_t       field_count, method_count;
    Member        *fields, *methods;
} ClassFile;

/* 0 — успех; при ошибке err получает текст причины. */
int  classfile_parse(ClassFile *cf, const uint8_t *buf, size_t len, char *err, size_t errlen);
void classfile_free(ClassFile *cf);

/* Имя класса по индексу CONSTANT_Class в виде "java/lang/Object". */
int  classfile_class_name(const ClassFile *cf, uint16_t idx, char *out, size_t cap);
int  classfile_utf8(const ClassFile *cf, uint16_t idx, char *out, size_t cap);

#endif
