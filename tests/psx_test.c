/* Проверка запуска PS1 без экрана: psx_run с ядром PCSX-ReARMed и встроенным HLE-BIOS грузит
 * tests/psx/psxtest.iso, а платформа-заглушка по кадрам жмёт кнопки, делает быстрое сохранение
 * и загрузку и смотрит на картинку: квадрат на синем фоне едет вправо, краснеет от ×,
 * зеленеет от ○ и снова красный после загрузки сохранения. Звук должен идти. */
#include "../src/psx.h"
#include "../src/audio.h"
#include "../src/compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Image    screen;
static int      frame, aspect_w, fails;
static uint32_t now_ms;
static AudioMixFn mixer;
static long     audio_frames;
static const char *out_dir;

static int sq_x(void)   /* левый край квадрата в средней строке или -1 */
{
    int x, y = -1, k;
    for (k = 0; k < screen.h && y < 0; k++)
        for (x = 0; x < screen.w; x++)
            if (screen.px[k * screen.w + x] != screen.px[0] && k < screen.h - 10) {
                y = k + 16;
                break;
            }
    if (y < 0 || y >= screen.h)
        return -1;
    for (x = 0; x < screen.w; x++)
        if (screen.px[y * screen.w + x] != screen.px[0])
            return x;
    return -1;
}

static uint32_t sq_color(void)
{
    int x = sq_x(), y;
    if (x < 0)
        return 0;
    for (y = 0; y < screen.h - 10; y++)
        if (screen.px[y * screen.w + x + 4] != screen.px[0])
            return screen.px[(y + 4) * screen.w + x + 4] & 0xFFFFFF;
    return 0;
}

/* 15-битный цвет приставки при переводе в 24 бита чуть округляется */
static int near(uint32_t c, uint32_t want)
{
    int k;
    for (k = 0; k < 24; k += 8) {
        int d = (int)((c >> k) & 255) - (int)((want >> k) & 255);
        if (d < -12 || d > 12)
            return 0;
    }
    return 1;
}

static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
}

/* ---------- заглушка платформы ---------- */

int platform_set_screen(int w, int h, ScaleMode mode)
{
    (void)mode;
    image_free(&screen);
    screen = image_new(w, h);
    return screen.px ? 0 : -1;
}
void platform_set_aspect(int w, int h) { (void)h; aspect_w = w; }
void platform_analog(int stick, float *x, float *y) { (void)stick; *x = *y = 0; }
Image *platform_screen(void) { return &screen; }
uint32_t platform_ticks_ms(void) { return now_ms; }
void platform_sleep_ms(uint32_t ms) { now_ms += ms; }
void platform_touch_profile(const char *id) { (void)id; }
void platform_console_mode(int mode) { (void)mode; }
int platform_paused(void) { return 0; }
int platform_audio_open_stereo(int rate, AudioMixFn mix) { (void)rate; mixer = mix; return 48000; }
void platform_audio_lock(int on) { (void)on; }
void platform_audio_close(void) { mixer = NULL; }
void audio_reset(void) {}
int audio_master(void) { return 100; }

void midp_save_dir(const char *id, char *out, size_t cap) { snprintf(out, cap, "%s/save-%s", out_dir, id); }
int midp_shot(const char *id, const char *name) { (void)id; (void)name; return 0; }
int midp_title_shot(const char *id) { (void)id; return 0; }

static int saved_x;

/* Файл карты памяти этой игры называет сохранение name (в каталоге карты — его имя). */
static int card_has(const char *name)
{
    char path[1200], buf[8192];
    size_t n, i, k = strlen(name);
    FILE *f;

    snprintf(path, sizeof(path), "%s/save-Тест/pcsx-card1.mcd", out_dir);
    if (!(f = bt_fopen(path, "rb")))
        return 0;
    n = fread(buf, 1, sizeof(buf), f);      /* каталог — в первом блоке */
    fclose(f);
    for (i = 0; i + k <= n; i++)
        if (!memcmp(buf + i, name, k))
            return 1;
    return 0;
}

void platform_present(void)
{
    int16_t buf[800 * 2];

    frame++;
    now_ms += 16;
    if (mixer) {            /* устройство «играет» 800 кадров звука за кадр — как 48 кГц */
        mixer(buf, 800);
        audio_frames += 800;
    }
}

static int done = -1;

int platform_poll(Event *ev)
{
    memset(ev, 0, sizeof(*ev));
    if (done == frame)
        return 0;
    done = frame;
    switch (frame) {
    case 150:
        expect(screen.w == 320 && screen.h == 240 && aspect_w == 4, "экран 320×240, показ 4:3");
        expect(near(screen.px[0], 0x003080), "синий фон");
        expect(sq_x() >= 140 && sq_x() <= 148, "квадрат посередине");
        expect(near(sq_color(), 0xFFFFFF), "квадрат белый");
        ev->type = EV_KEY_DOWN; ev->key = KEY_RIGHT; return 1;
    case 170:
        ev->type = EV_KEY_UP; ev->key = KEY_RIGHT; return 1;
    case 180:
        expect(sq_x() > 160, "крестовина вправо двигает квадрат");
        ev->type = EV_KEY_DOWN; ev->key = KEY_GAME_A; return 1;
    case 185:
        ev->type = EV_KEY_UP; ev->key = KEY_GAME_A; return 1;
    case 195:
        expect(near(sq_color(), 0xFF0000), "× красит в красный");
        saved_x = sq_x();
        ev->type = EV_STATE_SAVE; return 1;
    case 200:
        ev->type = EV_KEY_DOWN; ev->key = KEY_GAME_B; return 1;
    case 205:
        ev->type = EV_KEY_UP; ev->key = KEY_GAME_B; return 1;
    case 206:
        ev->type = EV_KEY_DOWN; ev->key = KEY_LEFT; return 1;
    case 216:
        ev->type = EV_KEY_UP; ev->key = KEY_LEFT; return 1;
    case 220:
        expect(near(sq_color(), 0x00FF00), "○ красит в зелёный");
        ev->type = EV_STATE_LOAD; return 1;
    case 230:
        expect(near(sq_color(), 0xFF0000) && sq_x() == saved_x, "загрузка сохранения вернула красный квадрат на место");
        expect(audio_frames > 48000 * 3, "звук идёт");
        ev->type = EV_KEY_DOWN; ev->key = KEY_GAME_Y; return 1;
    case 235:
        ev->type = EV_KEY_UP; ev->key = KEY_GAME_Y; return 1;
    case 330:
        expect(near(sq_color(), 0x0000FF), "△: игра записала файл на карту памяти через BIOS");
        expect(card_has("BTTEST"), "запись есть в файле карты памяти");
        ev->type = EV_BACK; return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char err[200];
    MidpResult r;

    if (argc != 3) {
        fprintf(stderr, "usage: psx_test psxtest.iso out_dir\n");
        return 2;
    }
    {
        /* образ — в папку с русским именем: так проверяются пути UTF-8 (на Windows — тоже) */
        static char dir[1024], iso[1100];
        FILE *in = bt_fopen(argv[1], "rb"), *out;
        char buf[65536];
        size_t n;
        snprintf(dir, sizeof(dir), "%s/Игры", argv[2]);
        snprintf(iso, sizeof(iso), "%s/Тест (Диск).iso", dir);
        bt_mkdirs(dir);
        if (!in || !(out = bt_fopen(iso, "wb"))) {
            printf("FAIL не скопировать образ в %s\n", iso);
            return 1;
        }
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
            fwrite(buf, 1, n, out);
        fclose(in);
        fclose(out);
        out_dir = dir;
        r = psx_run(iso, "Тест", SCALE_AUTO, err, sizeof(err));
        if (r != MIDP_ERROR) {
            /* второй запуск подряд: с полки игру открывают снова, ядро должно подняться заново */
            printf("-- второй запуск\n");
            frame = 0;
            done = -1;
            audio_frames = 0;
            r = psx_run(iso, "Тест", SCALE_AUTO, err, sizeof(err));
        }
    }
    expect(r == MIDP_BACK, "выход на полку");
    if (!getenv("BT_PSX_HLE")) {
        char data[1024], ob[1100];
        bt_data_dir(data, sizeof(data));
        snprintf(ob, sizeof(ob), "%s/bios/openbios.bin", data);
        expect(psx_is_bios(ob), "встроенный OpenBIOS распакован в папку данных");
    }
    {
        char card[1200];
        FILE *f;
        snprintf(card, sizeof(card), "%s/save-Тест/pcsx-card1.mcd", out_dir);
        f = bt_fopen(card, "rb");
        expect(f != NULL, "карта памяти в папке сохранений игры");
        if (f)
            fclose(f);
    }
    if (r == MIDP_ERROR)
        printf("ошибка: %s\n", err);
    {
        /* игра во вложенной папке полки, BIOS — в bios у корня: находится, поднимаясь вверх */
        char root[1024], deep[1100], bios[1200], found[1200];
        FILE *f;
        snprintf(root, sizeof(root), "%s/полка", argv[2]);
        snprintf(deep, sizeof(deep), "%s/PS1/Диск", root);
        snprintf(bios, sizeof(bios), "%s/bios", root);
        bt_mkdirs(deep);
        bt_mkdirs(bios);
        expect(!psx_bios_dir(deep, found, sizeof(found)), "своего BIOS нет — нашёлся только встроенный");
        snprintf(bios, sizeof(bios), "%s/bios/scph1001.bin", root);
        if ((f = bt_fopen(bios, "wb")) != NULL) {
            fseek(f, 512 * 1024 - 1, SEEK_SET);
            fputc(0, f);
            fclose(f);
        }
        snprintf(bios, sizeof(bios), "%s/bios", root);
        expect(psx_bios_dir(deep, found, sizeof(found)) && !strcmp(found, bios),
               "BIOS из bios у корня полки для игры во вложенной папке");
    }
    printf("PSX DONE fails=%d\n", fails);
    return fails != 0;
}
