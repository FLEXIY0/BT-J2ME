/* Эмулятор NES / Famicom / Денди: процессор 6502 с точностью до такта, видеочип по точкам,
 * звук APU и популярные мапперы картриджей. Ядро не знает про экран и звук ОС —
 * снаружи ему дают кнопки, забирают кадр 256×240 и звук. */
#ifndef BT_NES_H
#define BT_NES_H

#include <stddef.h>
#include <stdint.h>

#include "midp.h"

enum { NES_A = 1, NES_B = 2, NES_SELECT = 4, NES_START = 8, NES_UP = 16, NES_DOWN = 32, NES_LEFT = 64, NES_RIGHT = 128 };
enum { NES_NTSC, NES_PAL, NES_DENDY };

typedef struct Nes Nes;

/* Образ .nes (iNES / NES 2.0). region < 0 — по заголовку и имени файла. */
Nes        *nes_create(const uint8_t *rom, size_t len, int region, char *err, size_t errlen);
void        nes_destroy(Nes *n);
void        nes_reset(Nes *n);                  /* кнопка Reset на приставке */
void        nes_frame(Nes *n);                  /* один кадр */
const uint32_t *nes_pixels(const Nes *n);       /* 256×240, 0xAARRGGBB */
void        nes_set_buttons(Nes *n, int port, int buttons);
void        nes_set_rate(Nes *n, double rate);  /* частота звука, Гц */
int         nes_take_audio(Nes *n, int16_t *out, int max);
int         nes_region(const Nes *n);
double      nes_fps(const Nes *n);
/* Память картриджа с батарейкой (рекорды, сохранения) или NULL. */
uint8_t    *nes_battery(Nes *n, size_t *len);
/* Быстрое сохранение: снимок всего состояния. */
size_t      nes_state_size(const Nes *n);
void        nes_state_save(const Nes *n, uint8_t *out);
int         nes_state_load(Nes *n, const uint8_t *in, size_t len);  /* 0 — успех */
/* Отладка: печатать каждую команду в формате журнала nestest. */
void        nes_debug_trace(Nes *n, uint16_t start_pc, void (*line)(const char *s));

/* Запуск с полки: как midp_run, только для картриджа. */
MidpResult  nes_run(const char *path, const char *save_id, ScaleMode scale, char *err, size_t errlen);

#endif
