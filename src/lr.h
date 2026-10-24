/* Общий libretro-фронтенд для ядер приставок: PS1 (ядро вшито в программу) и PS2 (ядро
 * подгружается из файла). Экран, звук, кнопки DualShock и аналоговые стики, меню паузы,
 * быстрые сохранения — одинаково для всех; приставка задаёт только ядро и его опции. */
#ifndef BT_LR_H
#define BT_LR_H

#include <stddef.h>

#include "libretro.h"
#include "midp.h"

/* Функции ядра: у вшитого — прямо retro_*, у подгружаемого — найденные в .dll/.so. */
typedef struct {
    void   (*set_environment)(retro_environment_t);
    void   (*set_video_refresh)(retro_video_refresh_t);
    void   (*set_audio_sample)(retro_audio_sample_t);
    void   (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void   (*set_input_poll)(retro_input_poll_t);
    void   (*set_input_state)(retro_input_state_t);
    void   (*init)(void);
    void   (*deinit)(void);
    bool   (*load_game)(const struct retro_game_info *);
    void   (*unload_game)(void);
    void   (*run)(void);
    void   (*reset)(void);
    void   (*get_system_av_info)(struct retro_system_av_info *);
    void   (*set_controller_port_device)(unsigned, unsigned);
    size_t (*serialize_size)(void);
    bool   (*serialize)(void *, size_t);
    bool   (*unserialize)(const void *, size_t);
} LrCore;

typedef struct {
    const char *tag;            /* метка в журнале: «psx», «ps2» */
    int         console;        /* CONSOLE_PSX, CONSOLE_PS2 */
    const char *sys_dir;        /* «системная» папка ядра (BIOS) */
    const char *const (*opts)[2];   /* опции поверх умолчаний ядра: { ключ, значение } */
    int         nopts;
    int         analog;         /* отдавать игре аналоговые стики (DualShock 2) */
    int         verbose;        /* в журнал — и сообщения ядра уровня «инфо» */
} LrConfig;

/* Запуск образа path: как nes_run. Ядро инициализируется и выгружается внутри. */
MidpResult lr_run(const LrCore *core, const LrConfig *cfg, const char *path, const char *save_id,
                  ScaleMode scale, char *err, size_t errlen);

#endif
