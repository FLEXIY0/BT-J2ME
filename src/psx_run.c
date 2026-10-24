/* Запуск диска PS1 с полки. Ядро PCSX-ReARMed вшито в программу (retro_* из
 * third_party/pcsx_rearmed/frontend/libretro.c) и идёт через общий фронтенд lr_run.
 * Здесь — только своё для PS1: поиск BIOS, встроенный OpenBIOS и опции ядра. */
#include "psx.h"
#include "lr.h"
#include "compat.h"
#include "util.h"

#include "openbios_bin.h"

#include <zlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ends_with(const char *s, const char *suf)
{
    size_t n = strlen(s), k = strlen(suf);
    size_t i;

    if (n < k)
        return 0;
    for (i = 0; i < k; i++) {
        char c = s[n - k + i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        if (c != suf[i])
            return 0;
    }
    return 1;
}

int psx_is_image(const char *path)
{
    return ends_with(path, ".cue") || ends_with(path, ".bin") || ends_with(path, ".iso") ||
           ends_with(path, ".img") || ends_with(path, ".pbp");
}

int psx_is_bios(const char *path)
{
    FILE *f;
    long n = -1;

    if (!ends_with(path, ".bin") || !(f = bt_fopen(path, "rb")))
        return 0;
    if (fseek(f, 0, SEEK_END) == 0)
        n = ftell(f);
    fclose(f);
    return n == 512 * 1024;
}

static int dir_has_bios(const char *dir)
{
    BtDir *d = bt_dir_open(dir);
    const char *name;
    char path[1400];
    int found = 0;

    if (!d)
        return 0;
    while (!found && (name = bt_dir_next(d)) != NULL) {
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        found = psx_is_bios(path);
    }
    bt_dir_close(d);
    return found;
}

int psx_bios_dir(const char *game_dir, char *out, size_t cap)
{
    char dir[1024];
    int level;

    /* игра может лежать во вложенной папке полки — идём вверх до четырёх уровней */
    snprintf(dir, sizeof(dir), "%s", game_dir);
    for (level = 0; level < 4; level++) {
        char *slash = strrchr(dir, '/');
#ifdef _WIN32
        if (strrchr(dir, '\\') > slash)
            slash = strrchr(dir, '\\');
#endif
        snprintf(out, cap, "%.1000s/bios", dir);
        if (dir_has_bios(out))
            return 1;
        if (dir_has_bios(dir)) {
            snprintf(out, cap, "%s", dir);
            return 1;
        }
        if (!slash || slash == dir)
            break;
        *slash = '\0';
    }
    snprintf(out, cap, "%s/bios", game_dir);
    return 0;
}

/* Встроенный OpenBIOS (PCSX-Redux, MIT; tools/build-openbios.sh) — в <данные>/bios/openbios.bin.
 * Файл перезаписывается, только если его нет или он отличается (новая версия программы). */
static int builtin_bios(char *dir, size_t cap)
{
    static uint8_t bin[512 * 1024], old[512 * 1024];
    uLongf len = sizeof(bin);
    char path[1200];
    FILE *f;
    size_t got = 0;

    if (uncompress(bin, &len, openbios_z, sizeof(openbios_z)) != Z_OK || len != sizeof(bin))
        return 0;
    bt_data_dir(path, sizeof(path));
    snprintf(dir, cap, "%.1000s/bios", path);
    snprintf(path, sizeof(path), "%.1000s/openbios.bin", dir);
    if ((f = bt_fopen(path, "rb")) != NULL) {
        got = fread(old, 1, sizeof(old), f);
        fclose(f);
    }
    if (got == sizeof(old) && !memcmp(old, bin, sizeof(bin)))
        return 1;
    bt_mkdirs(dir);
    if (!(f = bt_fopen(path, "wb")))
        return 0;
    got = fwrite(bin, 1, sizeof(bin), f);
    return (fclose(f) == 0) & (got == sizeof(bin));
}

/* ---------- запуск ---------- */

static const LrCore psx_core = {
    retro_set_environment, retro_set_video_refresh, retro_set_audio_sample, retro_set_audio_sample_batch,
    retro_set_input_poll, retro_set_input_state, retro_init, retro_deinit, retro_load_game, retro_unload_game,
    retro_run, retro_reset, retro_get_system_av_info, retro_set_controller_port_device, retro_serialize_size,
    retro_serialize, retro_unserialize,
};

MidpResult psx_run(const char *path, const char *save_id, ScaleMode scale, char *err, size_t errlen)
{
    /* что меняем относительно умолчаний ядра */
    static const char *opts[][2] = {
        { "pcsx_rearmed_memcard1", "shared" },          /* файл карты — в папке сохранений этой игры */
        { "pcsx_rearmed_memcard2", "none" },
        { "pcsx_rearmed_rgb32_output", "enabled" },     /* ролики и заставки в 24 битах — без полос */
        { "pcsx_rearmed_frameskip_type", "auto" },      /* не успеваем — пропускаем отрисовку, не звук */
        { "pcsx_rearmed_frameskip_interval", "3" },
        { "pcsx_rearmed_show_bios_bootlogo", "disabled" },
        { "pcsx_rearmed_drc", "enabled" },
        { "pcsx_rearmed_bios", "auto" },                /* BIOS из sys_dir: свой или встроенный */
    };
    char game_dir[1024], sys_dir[1024];
    const char *slash, *hle_env = getenv("BT_PSX_HLE");
    LrConfig cfg;
    int have_bios, hle = hle_env && *hle_env && strcmp(hle_env, "0");

    snprintf(game_dir, sizeof(game_dir), "%s", path);
    slash = strrchr(game_dir, '/');
#ifdef _WIN32
    if (strrchr(game_dir, '\\') > slash)
        slash = strrchr(game_dir, '\\');
#endif
    if (slash)
        game_dir[slash - game_dir] = '\0';
    else
        snprintf(game_dir, sizeof(game_dir), ".");
    have_bios = psx_bios_dir(game_dir, sys_dir, sizeof(sys_dir));
    if (!have_bios && !hle && !builtin_bios(sys_dir, sizeof(sys_dir)))
        hle = 1;                /* не распаковался (нет места?) — остаётся HLE ядра */
    opts[7][1] = hle ? "HLE" : "auto";
    BT_LOG("PlayStation: %s, BIOS: %s\n", path, hle ? "HLE ядра" : have_bios ? sys_dir : "встроенный OpenBIOS");

    memset(&cfg, 0, sizeof(cfg));
    cfg.tag = "psx";
    cfg.console = CONSOLE_PSX;
    cfg.sys_dir = sys_dir;
    cfg.opts = (const char *const (*)[2])opts;
    cfg.nopts = (int)(sizeof(opts) / sizeof(opts[0]));
    cfg.verbose = getenv("BT_PSX_LOG") != NULL;
    return lr_run(&psx_core, &cfg, path, save_id, scale, err, errlen);
}
