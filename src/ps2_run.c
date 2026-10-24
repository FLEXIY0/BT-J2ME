/* Запуск диска PS2: ядро ParaLLEl PS2 (PCSX2) скачивается при первом запуске, BIOS — свой или
 * встроенный OpenPS2BIOS, дальше — общий фронтенд lr_run. Графика — программный рендер ядра
 * («Software (SW)»): кадр приходит готовой картинкой, как у PS1, без OpenGL и Vulkan. */
#include "ps2.h"
#include "lr.h"
#include "compat.h"
#include "gfx.h"
#include "jar.h"
#include "update.h"
#include "util.h"

#include "openps2bios_bin.h"

#include <SDL.h>
#include <zlib.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Сборки ядра с buildbot.libretro.com — официальные ночные, тот же код, что в RetroArch. */
#if defined(_WIN32) && defined(_WIN64)
#define CORE_FILE "pcsx2_libretro.dll"
#define CORE_URL  "https://buildbot.libretro.com/nightly/windows/x86_64/latest/pcsx2_libretro.dll.zip"
#elif defined(__linux__) && defined(__x86_64__) && !defined(__ANDROID__)
#define CORE_FILE "pcsx2_libretro.so"
#define CORE_URL  "https://buildbot.libretro.com/nightly/linux/x86_64/latest/pcsx2_libretro.so.zip"
#endif

#define BIOS_SIZE (4 << 20)

#ifdef CORE_FILE

/* ---------- ядро: скачать, распаковать, подгрузить ---------- */

static struct {
    volatile int kb, done, ok;
    char         zip[1200];
} dl;

static int SDLCALL fetch_thread(void *u)
{
    (void)u;
    dl.ok = update_fetch(CORE_URL, dl.zip, &dl.kb);
    dl.done = 1;
    return 0;
}

static void say(const char *l1, const char *l2)
{
    Image *s = platform_screen();
    int w;

    gfx_fill(s, 0, 0, s->w, s->h, 0xFF101320u);
    w = gfx_text_width(&font_big, l1, 1);
    gfx_text(s, &font_big, (s->w - w) / 2, s->h / 2 - 22, l1, 0xFFEDE8F2u, 1);
    w = gfx_text_width(&font_small, l2, 1);
    gfx_text(s, &font_small, (s->w - w) / 2, s->h / 2 + 4, l2, 0xFF9C97AAu, 1);
    platform_present();
}

/* Ядро в <данные>/cores; нет — качаем zip (~4 МБ) с экраном прогресса. 0 — нет ядра (отменили,
 * нет сети). */
static int fetch_core(const char *core_path, char *err, size_t errlen)
{
    SDL_Thread *t;
    Jar jar;
    const JarEntry *e;
    uint8_t *so;
    size_t len;
    FILE *f;
    char tmp[1200], line[96];

    if (bt_file_size(core_path) > 0)
        return 1;
    snprintf(dl.zip, sizeof(dl.zip), "%.1100s.zip.part", core_path);
    dl.kb = dl.done = dl.ok = 0;
    platform_set_screen(400, 240, SCALE_INTEGER);
    platform_set_aspect(0, 0);
    if (!(t = SDL_CreateThread(fetch_thread, "ps2-core", NULL))) {
        snprintf(err, errlen, "не удалось начать загрузку ядра PS2");
        return 0;
    }
    SDL_DetachThread(t);
    BT_LOG("скачиваю ядро PlayStation 2: %s\n", CORE_URL);
    while (!dl.done) {
        Event ev;
        while (platform_poll(&ev))
            if (ev.type == EV_BACK || ev.type == EV_QUIT) {
                snprintf(err, errlen, "загрузку ядра PS2 отменили");
                return 0;           /* поток докачает в .part и бросит — в следующий раз заново */
            }
        snprintf(line, sizeof(line), "Один раз, из сборок libretro: %d КБ", dl.kb);
        say("Скачиваю ядро PlayStation 2", line);
        platform_sleep_ms(50);
    }
    if (!dl.ok) {
        snprintf(err, errlen, "не удалось скачать ядро PS2 — нужна сеть");
        return 0;
    }
    say("Распаковываю ядро PlayStation 2", "");
    so = NULL;
    if (jar_open(&jar, dl.zip) == 0) {
        if ((e = jar_find(&jar, CORE_FILE)) != NULL)
            so = jar_read(&jar, e, &len);
        jar_close(&jar);
    }
    bt_remove(dl.zip);
    if (!so) {
        snprintf(err, errlen, "в архиве нет ядра PS2");
        return 0;
    }
    snprintf(tmp, sizeof(tmp), "%.1100s.tmp", core_path);
    if (!(f = bt_fopen(tmp, "wb")) || fwrite(so, 1, len, f) != len || fclose(f) != 0 || !bt_replace(tmp, core_path)) {
        free(so);
        bt_remove(tmp);
        snprintf(err, errlen, "не удалось сохранить ядро PS2");
        return 0;
    }
    free(so);
    BT_LOG("ядро PlayStation 2 скачано: %s\n", core_path);
    return 1;
}

/* Ядро подгружается один раз и остаётся в памяти: повторный запуск — через retro_init. */
static int load_core(const char *core_path, LrCore *c, char *err, size_t errlen)
{
    static void *lib;
    static LrCore core;
    static const char *const names[] = {
        "retro_set_environment", "retro_set_video_refresh", "retro_set_audio_sample", "retro_set_audio_sample_batch",
        "retro_set_input_poll", "retro_set_input_state", "retro_init", "retro_deinit", "retro_load_game",
        "retro_unload_game", "retro_run", "retro_reset", "retro_get_system_av_info",
        "retro_set_controller_port_device", "retro_serialize_size", "retro_serialize", "retro_unserialize",
    };
    void **slot = (void **)&core;
    size_t i;

    if (!lib) {
        if (!(lib = SDL_LoadObject(core_path))) {
            snprintf(err, errlen, "ядро PS2 не загрузилось: %s", SDL_GetError());
            return 0;
        }
        for (i = 0; i < sizeof(names) / sizeof(names[0]); i++)
            if (!(slot[i] = SDL_LoadFunction(lib, names[i]))) {
                snprintf(err, errlen, "в ядре PS2 нет %s", names[i]);
                SDL_UnloadObject(lib);
                lib = NULL;
                return 0;
            }
    }
    *c = core;
    return 1;
}

/* ---------- BIOS ---------- */

static int is_ps2_bios(const char *path)
{
    size_t n = strlen(path);

    return n > 4 && (!strcasecmp(path + n - 4, ".bin") || !strcasecmp(path + n - 4, ".rom")) &&
           bt_file_size(path) == BIOS_SIZE;
}

/* Свой BIOS PS2 (4 МБ) — в bios рядом с игрой или выше по папкам, как у PS1. */
static int find_bios(const char *game_dir, char *out, size_t cap)
{
    char dir[1024], sub[1100];
    int level, k;

    snprintf(dir, sizeof(dir), "%s", game_dir);
    for (level = 0; level < 4; level++) {
        char *slash = strrchr(dir, '/');
#ifdef _WIN32
        if (strrchr(dir, '\\') > slash)
            slash = strrchr(dir, '\\');
#endif
        for (k = 0; k < 2; k++) {
            BtDir *d;
            const char *name;
            snprintf(sub, sizeof(sub), k ? "%.1000s" : "%.1000s/bios", dir);
            if (!(d = bt_dir_open(sub)))
                continue;
            while ((name = bt_dir_next(d)) != NULL) {
                snprintf(out, cap, "%.1000s/%.255s", sub, name);
                if (is_ps2_bios(out)) {
                    bt_dir_close(d);
                    return 1;
                }
            }
            bt_dir_close(d);
        }
        if (!slash || slash == dir)
            break;
        *slash = '\0';
    }
    return 0;
}

static int copy_file(const char *from, const char *to)
{
    FILE *in = bt_fopen(from, "rb"), *out;
    char buf[65536];
    size_t n;
    int ok = 1;

    if (!in)
        return 0;
    if (!(out = bt_fopen(to, "wb"))) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
        ok &= fwrite(buf, 1, n, out) == n;
    fclose(in);
    return (fclose(out) == 0) & ok;
}

/* Ядро ищет BIOS в <sys>/pcsx2/bios и берёт файл из опции pcsx2_bios. Свой BIOS копируем туда,
 * встроенный распаковываем (переписываем, только если отличается). name — имя файла. */
static int prepare_bios(const char *game_dir, const char *sys_dir, char *name, size_t cap)
{
    char dir[1100], user[1400], path[1400];
    const char *base;
    static uint8_t bin[BIOS_SIZE], old[BIOS_SIZE];
    uLongf len = sizeof(bin);
    FILE *f;
    size_t got = 0;

    snprintf(dir, sizeof(dir), "%.1000s/pcsx2/bios", sys_dir);
    bt_mkdirs(dir);
    if (find_bios(game_dir, user, sizeof(user))) {
        base = strrchr(user, '/');
        snprintf(name, cap, "%.255s", base ? base + 1 : user);
        snprintf(path, sizeof(path), "%.1000s/%.255s", dir, name);
        if (bt_file_size(path) == BIOS_SIZE || copy_file(user, path)) {
            BT_LOG("PlayStation 2: свой BIOS %s\n", user);
            return 1;
        }
    }
    if (uncompress(bin, &len, openps2bios_z, sizeof(openps2bios_z)) != Z_OK || len != sizeof(bin))
        return 0;
    snprintf(name, cap, "OpenPS2BIOS.bin");
    snprintf(path, sizeof(path), "%.1000s/%s", dir, name);
    if ((f = bt_fopen(path, "rb")) != NULL) {
        got = fread(old, 1, sizeof(old), f);
        fclose(f);
    }
    if (got == sizeof(old) && !memcmp(old, bin, sizeof(bin)))
        return 1;
    if (!(f = bt_fopen(path, "wb")))
        return 0;
    got = fwrite(bin, 1, sizeof(bin), f);
    return (fclose(f) == 0) & (got == sizeof(bin));
}

/* ---------- запуск ---------- */

static void set_opt(const char *(*opts)[2], int n, const char *key, const char *val)
{
    int i;

    for (i = 0; i < n; i++)
        if (!strcmp(opts[i][0], key))
            opts[i][1] = val;
}

MidpResult ps2_run(const char *path, const char *save_id, ScaleMode scale, char *err, size_t errlen)
{
    static char bios[256], threads[8];
    static const char *opts[][2] = {
        { "pcsx2_renderer", "Software (SW)" },      /* картинка готовой — без OpenGL/Vulkan */
        { "pcsx2_sw_renderer_threads", "2" },
        { "pcsx2_fastboot", "enabled" },            /* без заставки BIOS — сразу игра */
        { "pcsx2_fastmem", "enabled" },
        { "pcsx2_bios", "" },
    };
    char data[1024], sys_dir[1100], cores[1100], core_path[1200], game_dir[1024];
    const char *slash;
    LrCore core;
    LrConfig cfg;
    int cpus = SDL_GetCPUCount();

    err[0] = '\0';
    bt_data_dir(data, sizeof(data));
    snprintf(cores, sizeof(cores), "%.1000s/cores", data);
    snprintf(core_path, sizeof(core_path), "%.1000s/%s", cores, CORE_FILE);
    snprintf(sys_dir, sizeof(sys_dir), "%.1000s/ps2", data);
    bt_mkdirs(cores);
    if (!fetch_core(core_path, err, errlen) || !load_core(core_path, &core, err, errlen))
        return MIDP_ERROR;

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
    if (!prepare_bios(game_dir, sys_dir, bios, sizeof(bios))) {
        snprintf(err, errlen, "не удалось подготовить BIOS PS2");
        return MIDP_ERROR;
    }
    /* программному рендеру — потоки: все ядра, кроме двух под процессоры приставки */
    snprintf(threads, sizeof(threads), "%d", cpus > 4 ? (cpus - 2 > 11 ? 11 : cpus - 2) : 2);
    set_opt(opts, (int)(sizeof(opts) / sizeof(opts[0])), "pcsx2_sw_renderer_threads", threads);
    set_opt(opts, (int)(sizeof(opts) / sizeof(opts[0])), "pcsx2_bios", bios);
#ifdef _WIN32
    /* под wine/Proton (Linux, Steam Deck) окно fastmem не резервируется, а без него эта сборка
     * ядра падает, если fastmem включён, — выключаем сами */
    if (GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version"))
        set_opt(opts, (int)(sizeof(opts) / sizeof(opts[0])), "pcsx2_fastmem", "disabled");
#endif
    BT_LOG("PlayStation 2: %s, BIOS: %s\n", path, bios);

    memset(&cfg, 0, sizeof(cfg));
    cfg.tag = "ps2";
    cfg.console = CONSOLE_PS2;
    cfg.sys_dir = sys_dir;
    cfg.opts = (const char *const (*)[2])opts;
    cfg.nopts = (int)(sizeof(opts) / sizeof(opts[0]));
    cfg.analog = 1;
    cfg.verbose = getenv("BT_PS2_LOG") != NULL;
    return lr_run(&core, &cfg, path, save_id, scale, err, errlen);
}

#else

MidpResult ps2_run(const char *path, const char *save_id, ScaleMode scale, char *err, size_t errlen)
{
    (void)path;
    (void)save_id;
    (void)scale;
    snprintf(err, errlen, "PlayStation 2 пока работает на ПК: Windows и Linux x86-64");
    return MIDP_ERROR;
}

#endif
