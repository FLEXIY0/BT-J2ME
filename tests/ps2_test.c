/* Проверка запуска PS2 без экрана: ps2_run скачивает ядро ParaLLEl PS2 (или берёт уже
 * лежащее в <данные>/cores), распаковывает встроенный OpenPS2BIOS и грузит tests/ps2/ps2test.iso;
 * платформа-заглушка смотрит на картинку (красный фон, синий квадрат), делает быстрое
 * сохранение и загрузку и выходит. Нужен SDL (подгрузка ядра и поток загрузки). */
#include "../src/ps2.h"
#include "../src/audio.h"
#include "../src/compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Image    screen;
static int      frame, aspect_w, fails, red_frames, blue_seen;
static uint32_t now_ms;
static AudioMixFn mixer;
static long     audio_frames;
static const char *out_dir;

static void expect(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        fails++;
}

/* Кадр игры: фон красный, где-то есть синий квадрат. */
static void look(void)
{
    int i, n = screen.w * screen.h, red = 0, blue = 0;

    if (!screen.px || screen.w < 600)
        return;
    for (i = 0; i < n; i++) {
        uint32_t c = screen.px[i], r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
        red += r > 0xE0 && g < 0x20 && b < 0x20;      /* цвет ядро округляет: 0xFE0000 */
        blue += b > 0xE0 && g < 0x20 && r < 0x20;
    }
    if (red > n / 2)
        red_frames++;
    if (blue > 1000)
        blue_seen = 1;
}

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

void platform_present(void)
{
    int16_t buf[800 * 2];

    now_ms += 16;
    if (screen.w < 600)
        return;             /* экран загрузки ядра (400×240) — ещё не игра */
    frame++;
    look();
    if (mixer) {
        mixer(buf, 800);
        audio_frames += 800;
    }
}

int platform_poll(Event *ev)
{
    static int done = -1;
    char st[1200];

    memset(ev, 0, sizeof(*ev));
    if (done == frame)
        return 0;
    done = frame;
    switch (frame) {
    case 240:
        expect(screen.w == 640 && aspect_w == 4, "экран 640 точек, показ 4:3");
        expect(red_frames > 30, "кадры игры: красный фон");
        expect(blue_seen, "синий квадрат");
        ev->type = EV_STATE_SAVE; return 1;
    case 250:
        snprintf(st, sizeof(st), "%s/save-ps2test/state.sav", out_dir);
        expect(bt_file_size(st) > 1000000, "быстрое сохранение записано");
        ev->type = EV_STATE_LOAD; return 1;
    case 300:
        expect(red_frames > 100, "после загрузки сохранения игра идёт");
        ev->type = EV_BACK; return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    char err[200];
    MidpResult r;

    if (argc != 3) {
        fprintf(stderr, "usage: ps2_test ps2test.iso out_dir\n");
        return 2;
    }
    out_dir = argv[2];
    bt_mkdirs(out_dir);
    r = ps2_run(argv[1], "ps2test", SCALE_AUTO, err, sizeof(err));
    if (r == MIDP_ERROR)
        printf("ошибка: %s\n", err);
    expect(r == MIDP_BACK, "выход на полку");
    printf("PS2 DONE fails=%d\n", fails);
    return fails != 0;
}
