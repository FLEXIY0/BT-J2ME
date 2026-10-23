/* Звук: свой микшер и синтезатор без внешних библиотек.
 *
 *  - MIDI (SMF 0/1) играет простой синтезатор: таблицы волн по семействам General MIDI,
 *    огибающие и шумовые барабаны — звучит «как телефон», а не как оркестр, и почти не ест CPU;
 *  - WAV (PCM 8/16 бит, IMA ADPCM, A-law, μ-law) и Sun AU — с передискретизацией на лету;
 *  - тоновые последовательности JSR-135 (ToneControl) и рингтоны Nokia (Smart Messaging / OTT);
 *  - AMR, MP3, MMF не декодируются: такие плееры «играют» тишину, чтобы игры не падали.
 *
 * Вывод — моно 16 бит. Устройство открывается при первом звуке, так что тихие игры его не трогают.
 * Звуки адресуются дескрипторами: индекс слота + поколение, устаревший дескриптор безопасен. */
#ifndef BT_AUDIO_H
#define BT_AUDIO_H

#include <stddef.h>
#include <stdint.h>

enum { SND_AUTO, SND_OTT };

void    audio_set_master(int volume);   /* 0..100, 0 — без звука */
int     audio_master(void);
/* Без звуковой карты время звуков идёт по часам: зовётся из главного цикла. */
void    audio_pump(void);
/* Остановить и освободить все звуки и закрыть устройство (игра закрылась). */
void    audio_reset(void);

int     snd_create(const uint8_t *data, size_t len, int kind);  /* 0 — нет свободных слотов */
int     snd_create_freq(int hz, int ms);                        /* тон Nokia Sound(freq, duration) */
void    snd_free(int h);
int     snd_valid(int h);
void    snd_play(int h);
void    snd_pause(int h);
int     snd_playing(int h);
void    snd_set_loops(int h, int loops);    /* сколько раз играть, -1 — бесконечно */
void    snd_set_volume(int h, int volume);  /* 0..100 */
int64_t snd_time_us(int h);
int64_t snd_seek_us(int h, int64_t us);
int64_t snd_duration_us(int h);             /* -1 — неизвестна */
/* Manager.playTone: сыграть ноту и забыть. */
void    snd_tone(int note, int ms, int volume);
/* Сыграть звук один раз и забыть (звуки лаунчера). */
void    snd_play_once(const uint8_t *data, size_t len, int volume);

/* Отрисовать звуковой файл в WAV без устройства (проверки и отладка). 0 — успех. */
int     audio_render_file(const char *in, const char *out, int kind);

#endif
