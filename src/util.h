/* Общие мелочи: логирование и чтение чисел без привязки к порядку байт хоста. */
#ifndef BT_UTIL_H
#define BT_UTIL_H

#include <stdint.h>
#include <stdio.h>

#ifdef __ANDROID__
#include <android/log.h>
#define BT_LOG(...)  __android_log_print(ANDROID_LOG_INFO, "bt-j2me", __VA_ARGS__)
#else
#define BT_LOG(...)  fprintf(stderr, "bt-j2me: " __VA_ARGS__)
#endif

/* ZIP хранит числа в little-endian, class-файлы — в big-endian.
 * Читаем побайтно, чтобы код одинаково работал на x86, ARM и big-endian MIPS/PPC. */
static inline uint16_t rd_le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t rd_be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

#endif
