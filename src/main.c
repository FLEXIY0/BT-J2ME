#include "audio.h"
#ifdef __ANDROID__
#include <SDL_system.h>
#elif defined(_WIN32)
#include <SDL_filesystem.h>
#include <SDL_stdinc.h>
#endif
#include "classfile.h"
#include "jar.h"
#include "jvm.h"
#include "launcher.h"
#include "midp.h"
#include "nes.h"
#include "disc.h"
#include "ps2.h"
#include "psx.h"
#include "rt.h"
#include "manifest.h"
#include "platform.h"
#include "compat.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "update.h"
#include "version.h"

/* Минималистичная палитра: тёмный фон, один акцентный цвет. */
#define COL_BG     0xFF101214u
#define COL_CELL   0xFF1E2228u
#define COL_ACCENT 0xFF4FC3F7u

static void usage(void)
{
    fprintf(stderr,
        "BT-J2ME " BT_VERSION " — лёгкий запускатель J2ME-игр, NES, PlayStation и PlayStation 2\n\n"
        "  bt-j2me [папка] [опции]          меню-полка с играми (по умолчанию\n"
        "                                   $BT_J2ME_GAMES или ~/.local/share/bt-j2me/games)\n"
        "  bt-j2me info <игра.jar>          сведения о MIDlet и проверка классов\n"
        "  bt-j2me run <игра.jar> [опции]   запустить игру сразу, без меню\n"        "                                   (также .nes и диски PS1/PS2: .cue .bin .iso .img .pbp .chd .cso)\n"
        "  bt-j2me display-test [опции]     проверка экрана и клавиш\n"
        "  bt-j2me audio-render <звук> <out.wav> [--ott]\n"
        "                                   сыграть MIDI/WAV/AU/тоны/OTT в WAV-файл\n\n"
        "Опции:\n"
        "  -s WxH        виртуальный экран (по умолчанию 240x320)\n"
        "  --windowed    в окне вместо полного экрана\n"
        "  --smooth      дробное масштабирование (по умолчанию — авто)\n"
        "  --sharp       только целое масштабирование\n"
        "  --gpu         рисовать через видеокарту (OpenGL); по умолчанию — программно,\n"
        "                это легче по памяти и надёжнее на старых ПК\n"
        "  --mute        без звука\n"
        "  --touch       сенсорные кнопки поверх игры, как на Android (мышь — палец)\n\n"
        "BT_AUDIO_DUMP=файл.wav — записывать весь звук игры в файл.\n");
}

static int cmd_info(const char *path)
{
    Jar jar;
    Manifest mf;
    const JarEntry *e;
    MidletInfo mi;
    size_t i, classes = 0, bad = 0, len;
    unsigned max_major = 0;
    char *text;
    int n;

    if (jar_open(&jar, path))
        return 1;

    e = jar_find(&jar, "META-INF/MANIFEST.MF");
    text = e ? (char *)jar_read(&jar, e, &len) : NULL;
    if (!text || manifest_parse(&mf, text, len)) {
        BT_LOG("%s: нет или повреждён META-INF/MANIFEST.MF\n", path);
        free(text);
        jar_close(&jar);
        return 1;
    }

    printf("Файл:         %s (%zu байт, %zu записей)\n", path, jar.size, jar.count);
    printf("Название:     %s\n", manifest_get(&mf, "MIDlet-Name") ? manifest_get(&mf, "MIDlet-Name") : "?");
    printf("Издатель:     %s\n", manifest_get(&mf, "MIDlet-Vendor") ? manifest_get(&mf, "MIDlet-Vendor") : "?");
    printf("Версия:       %s\n", manifest_get(&mf, "MIDlet-Version") ? manifest_get(&mf, "MIDlet-Version") : "?");
    printf("Профиль:      %s\n", manifest_get(&mf, "MicroEdition-Profile") ? manifest_get(&mf, "MicroEdition-Profile") : "?");
    printf("Конфигурация: %s\n", manifest_get(&mf, "MicroEdition-Configuration") ? manifest_get(&mf, "MicroEdition-Configuration") : "?");
    for (n = 1; manifest_midlet(&mf, n, &mi) == 0; n++)
        printf("MIDlet-%d:     %s -> %s%s%s%s\n", n, mi.name, mi.class_name,
               mi.icon[0] ? "  [иконка " : "", mi.icon, mi.icon[0] ? "]" : "");
    if (n == 1)
        printf("MIDlet:       в манифесте не найден ни один MIDlet-N\n");

    for (i = 0; i < jar.count; i++) {
        size_t nlen = strlen(jar.entries[i].name), blen;
        uint8_t *buf;
        ClassFile cf;
        char err[128];

        if (nlen < 6 || strcmp(jar.entries[i].name + nlen - 6, ".class") != 0)
            continue;
        classes++;
        buf = jar_read(&jar, &jar.entries[i], &blen);
        if (!buf) {
            printf("  ! %s: не удалось распаковать\n", jar.entries[i].name);
            bad++;
            continue;
        }
        if (classfile_parse(&cf, buf, blen, err, sizeof(err))) {
            printf("  ! %s: %s\n", jar.entries[i].name, err);
            bad++;
        } else {
            if (cf.major > max_major)
                max_major = cf.major;
            classfile_free(&cf);
        }
        free(buf);
    }
    printf("Классы:       %zu, с ошибками: %zu, макс. версия class-файлов: %u%s\n", classes, bad,
           max_major, max_major > 49 ? " (новее CLDC 1.1 — может не запуститься)" : "");

    manifest_free(&mf);
    free(text);
    jar_close(&jar);
    return bad ? 1 : 0;
}

static void fill(uint32_t *fb, int w, int h, int x, int y, int rw, int rh, uint32_t c)
{
    int i, j;

    for (j = y < 0 ? 0 : y; j < y + rh && j < h; j++)
        for (i = x < 0 ? 0 : x; i < x + rw && i < w; i++)
            fb[j * w + i] = c;
}

/* Тестовый экран: цифровая клавиатура 3x4 и крестовина; нажатые клавиши подсвечиваются. */
static int cmd_display_test(PlatformConfig *cfg)
{
    Image *scr;
    static const int pad[12] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', KEY_STAR, '0', KEY_POUND };
    int held[12] = { 0 }, dir[5] = { 0 }, soft[2] = { 0 }, running = 1, px = -1, py = -1;
    int w = cfg->width, h = cfg->height;

    if (platform_init(cfg))
        return 1;
    scr = platform_screen();
    while (running) {
        uint32_t *fb = scr->px;
        int cw = w / 5, ch = h / 10, ox = (w - cw * 3 - 8) / 2, oy = h / 2;
        int i, k;
        Event ev;

        while (platform_poll(&ev)) {
            int down = ev.type == EV_KEY_DOWN;

            if (ev.type == EV_QUIT || ev.type == EV_BACK)
                running = 0;
            if (ev.type == EV_POINTER_DOWN || ev.type == EV_POINTER_MOVE) {
                px = ev.x;
                py = ev.y;
            }
            if (ev.type != EV_KEY_DOWN && ev.type != EV_KEY_UP)
                continue;
            for (i = 0; i < 12; i++)
                if (pad[i] == ev.key)
                    held[i] = down;
            if (ev.key <= KEY_UP && ev.key >= KEY_FIRE)
                dir[-ev.key - 1] = down;
            if (ev.key == KEY_SOFT_LEFT || ev.key == KEY_SOFT_RIGHT)
                soft[-ev.key - 6] = down;
        }

        fill(fb, w, h, 0, 0, w, h, COL_BG);
        fill(fb, w, h, 0, 0, w, 2, COL_ACCENT);
        /* Софт-клавиши в нижних углах */
        fill(fb, w, h, 4, h - 10, w / 4, 6, soft[0] ? COL_ACCENT : COL_CELL);
        fill(fb, w, h, w - 4 - w / 4, h - 10, w / 4, 6, soft[1] ? COL_ACCENT : COL_CELL);
        /* Крестовина: вверх, вниз, влево, вправо, огонь */
        k = ch;
        fill(fb, w, h, w / 2 - k / 2, h / 6, k, k, dir[0] ? COL_ACCENT : COL_CELL);
        fill(fb, w, h, w / 2 - k / 2, h / 6 + 2 * k, k, k, dir[1] ? COL_ACCENT : COL_CELL);
        fill(fb, w, h, w / 2 - k / 2 - k, h / 6 + k, k, k, dir[2] ? COL_ACCENT : COL_CELL);
        fill(fb, w, h, w / 2 + k / 2, h / 6 + k, k, k, dir[3] ? COL_ACCENT : COL_CELL);
        fill(fb, w, h, w / 2 - k / 2 + 2, h / 6 + k + 2, k - 4, k - 4, dir[4] ? COL_ACCENT : COL_CELL);
        /* Цифровой блок */
        for (i = 0; i < 12; i++)
            fill(fb, w, h, ox + (i % 3) * (cw + 4), oy + (i / 3) * (ch + 4), cw, ch,
                 held[i] ? COL_ACCENT : COL_CELL);
        if (px >= 0)
            fill(fb, w, h, px - 2, py - 2, 5, 5, COL_ACCENT);

        platform_present();
        platform_sleep_ms(16);
    }
    platform_shutdown();
    return 0;
}

static int parse_opts(int argc, char **argv, int start, PlatformConfig *cfg)
{
    int i;

    for (i = start; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &cfg->width, &cfg->height) != 2 ||
                cfg->width < 64 || cfg->height < 64 || cfg->width > 1024 || cfg->height > 1024) {
                BT_LOG("неверный размер экрана: %s\n", argv[i]);
                return -1;
            }
        } else if (strcmp(argv[i], "--windowed") == 0) {
            cfg->fullscreen = 0;
        } else if (strcmp(argv[i], "--smooth") == 0) {
            cfg->scale = SCALE_FIT;
        } else if (strcmp(argv[i], "--sharp") == 0) {
            cfg->scale = SCALE_INTEGER;
        } else if (strcmp(argv[i], "--soft") == 0) {
            cfg->software = 1;
        } else if (strcmp(argv[i], "--gpu") == 0) {
            cfg->software = 0;
        } else if (strcmp(argv[i], "--touch") == 0) {
            cfg->touch = 1;
        } else if (strcmp(argv[i], "--mute") == 0) {
            cfg->mute = 1;
            audio_set_master(0);
        } else {
            BT_LOG("неизвестная опция: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

/* Служебное: запуск cls.main(String) без экрана — для тестов Java-машины. */
static int cmd_jvm_run(const char *jar, const char *cls)
{
    uint32_t sleep_ms;
    int r;

    if (jvm_create(bt_rt_jar, bt_rt_jar_len, jar[0] ? jar : NULL) || jvm_start_main(cls, "main", "(Ljava/lang/String;)V", "")) {
        jvm_destroy();
        return 1;
    }
    while ((r = jvm_step(50, &sleep_ms)) >= 0)
        if (r == 0)
            platform_sleep_ms(sleep_ms ? sleep_ms : 1);
    jvm_destroy();
    return 0;
}

static int cmd_dump_rt(const char *path)
{
    FILE *f = bt_fopen(path, "wb");

    if (!f || fwrite(bt_rt_jar, 1, bt_rt_jar_len, f) != bt_rt_jar_len) {
        BT_LOG("не удалось записать %s\n", path);
        if (f)
            fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
}

static void default_games_dir(char *out, size_t cap)
{
    const char *env = getenv("BT_J2ME_GAMES"), *xdg = getenv("XDG_DATA_HOME");

    if (env && *env)
        snprintf(out, cap, "%s", env);
    else if (xdg && *xdg)
        snprintf(out, cap, "%s/bt-j2me/games", xdg);
    else
#ifdef __ANDROID__
    {
        /* Android/data/ru.flexiy0.btj2me/files/games: сюда же копирует импорт из MainActivity */
        const char *ext = SDL_AndroidGetExternalStoragePath();
        snprintf(out, cap, "%s/games", ext ? ext : bt_home());
        bt_mkdir(out);
    }
#elif defined(_WIN32)
    {
        /* на Windows — папка games рядом с bt-j2me.exe, откуда бы его ни запустили
         * (при перетаскивании JAR на exe текущая папка — чужая) */
        char *base = SDL_GetBasePath();
        snprintf(out, cap, "%sgames", base ? base : "");
        SDL_free(base);
    }
#else
        snprintf(out, cap, "%s/.local/share/bt-j2me/games", bt_home());
#endif
}

static int is_dir(const char *path)
{
    return bt_is_dir(path);
}

/* Существующий .jar, .jad, .nes или образ диска PS1. */
static int is_game_file(const char *path)
{
    size_t n = strlen(path);
    FILE *f;

    if (n < 5 || path[n - 4] != '.' || (strcmp(path + n - 3, "jar") && strcmp(path + n - 3, "jad") &&
                                         strcmp(path + n - 3, "JAR") && strcmp(path + n - 3, "JAD") &&
                                         strcmp(path + n - 3, "nes") && strcmp(path + n - 3, "NES") &&
                                         !disc_is_image(path)))
        return 0;
    if (!(f = bt_fopen(path, "rb")))
        return 0;
    fclose(f);
    return 1;
}

int main(int argc, char **argv)
{
    PlatformConfig cfg = { 240, 320, 1, SCALE_AUTO, 1, "BT-J2ME", 0, 0 };
    char dir[1024];

#ifdef __ANDROID__
    cfg.software = 0;   /* на телефоне рисует GPU (OpenGL ES): кнопки с прозрачностью — без нагрузки на CPU */
    cfg.touch = 1;
#endif

    if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage();
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "jvm-run") == 0)
        return cmd_jvm_run(argv[2], argv[3]);
    if ((argc == 4 || argc == 5) && strcmp(argv[1], "audio-render") == 0) {
        int ott = argc == 5 && strcmp(argv[4], "--ott") == 0;
        if (audio_render_file(argv[2], argv[3], ott ? SND_OTT : SND_AUTO)) {
            BT_LOG("%s: не удалось разобрать или записать звук\n", argv[2]);
            return 1;
        }
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "dump-rt") == 0)
        return cmd_dump_rt(argv[2]);
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("BT-J2ME " BT_VERSION);
        return 0;
    }
    if (argc >= 2 && is_game_file(argv[1])) {
        /* JAR перетащили на bt-j2me.exe (или «Открыть с помощью»): кладём в папку игр
         * и открываем полку прямо на нём */
        int i;
        default_games_dir(dir, sizeof(dir));
        for (i = 1; i < argc && is_game_file(argv[i]); i++)
            launcher_import(dir, argv[i]);
        if (parse_opts(argc, argv, i, &cfg))
            return 2;
        i = launcher_run(dir, &cfg);
        update_relaunch(1, argv);     /* после обновления — новая версия, просто полка */
        return i;
    }
    if (argc < 2 || argv[1][0] == '-' || is_dir(argv[1])) {
        int first_opt = argc >= 2 && argv[1][0] != '-' ? 2 : 1;

        if (first_opt == 2)
            snprintf(dir, sizeof(dir), "%s", argv[1]);
        else
            default_games_dir(dir, sizeof(dir));
        if (parse_opts(argc, argv, first_opt, &cfg))
            return 2;
        first_opt = launcher_run(dir, &cfg);
        update_relaunch(argc, argv);    /* обновились — запускаем новую версию */
        return first_opt;
    }
    if (strcmp(argv[1], "info") == 0 && argc == 3)
        return cmd_info(argv[2]);
    if (strcmp(argv[1], "display-test") == 0)
        return parse_opts(argc, argv, 2, &cfg) ? 2 : cmd_display_test(&cfg);
    if (strcmp(argv[1], "run") == 0 && argc >= 3) {
        char err[200], base[256];
        const char *slash = strrchr(argv[2], '/');
        size_t n;
        MidpResult r;

        if (parse_opts(argc, argv, 3, &cfg))
            return 2;
        snprintf(base, sizeof(base), "%s", slash ? slash + 1 : argv[2]);
        n = strlen(base);
        if (n > 4 && (strcmp(base + n - 4, ".jar") == 0 || strcmp(base + n - 4, ".nes") == 0 ||
                      disc_is_image(base)))
            base[n - 4] = '\0';
        if (platform_init(&cfg))
            return 1;
        if (n > 4 && strcmp(argv[2] + strlen(argv[2]) - 4, ".nes") == 0)
            r = nes_run(argv[2], base, cfg.scale, err, sizeof(err));
        else if (disc_system(argv[2]) == DISC_PS2)
            r = ps2_run(argv[2], base, cfg.scale, err, sizeof(err));
        else if (disc_is_image(argv[2]))
            r = psx_run(argv[2], base, cfg.scale, err, sizeof(err));
        else
            r = midp_run(argv[2], base, cfg.scale, err, sizeof(err));
        platform_shutdown();
        if (r == MIDP_ERROR) {
            BT_LOG("%s: %s\n", argv[2], err);
            return 1;
        }
        return 0;
    }
    usage();
    return 2;
}
