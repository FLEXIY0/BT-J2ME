/* Чтение JAR (ZIP) целиком в память: игры J2ME весят от 30 КБ до ~2 МБ. */
#ifndef BT_JAR_H
#define BT_JAR_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char     *name;
    uint16_t  method;       /* 0 = stored, 8 = deflate */
    uint32_t  csize;
    uint32_t  usize;
    uint32_t  local_offset;
} JarEntry;

typedef struct {
    uint8_t  *data;
    size_t    size;
    JarEntry *entries;
    size_t    count;
    int       borrowed;     /* data принадлежит не нам */
} Jar;

int  jar_open(Jar *jar, const char *path);
/* То же для архива в памяти (встроенная библиотека): данные не копируются и не освобождаются. */
int  jar_open_mem(Jar *jar, const uint8_t *data, size_t size);
void jar_close(Jar *jar);

/* Поиск записи по точному имени (например "META-INF/MANIFEST.MF"). */
const JarEntry *jar_find(const Jar *jar, const char *name);

/* Распаковывает запись в новый буфер (освобождать free()). NULL при ошибке. */
uint8_t *jar_read(const Jar *jar, const JarEntry *e, size_t *out_size);

#endif
