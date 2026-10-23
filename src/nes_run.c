/* Запуск картриджа NES с полки: экран, звук, кнопки, память с батарейкой и быстрые сохранения.
 * Темп держим по часам (60,1 кадра у NTSC, 50 у PAL и Денди), звук подстраиваем под заполнение
 * буфера, чтобы не было ни щелчков, ни растущей задержки. */
#include "nes.h"
#include "audio.h"
#include "compat.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RING 16384

static int16_t ring[RING];
static int     ring_r, ring_w;
static int16_t last;

static int ring_fill(void)
{
    return (ring_w - ring_r + RING) % RING;
}

static void mix(int16_t *out, int frames)
{
    int i;

    for (i = 0; i < frames; i++) {
        if (ring_r != ring_w) {
            last = ring[ring_r];
            ring_r = (ring_r + 1) % RING;
        }
        out[i] = last;          /* буфер пуст — тянем последний сэмпл, а не щёлкаем нулём */
    }
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = bt_fopen(path, "rb");
    uint8_t *buf = NULL;
    long n;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 16 && n < (8L << 20) && fseek(f, 0, SEEK_SET) == 0 &&
        (buf = malloc((size_t)n)) != NULL && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    if (buf)
        *len = (size_t)n;
    return buf;
}

static int write_file(const char *path, const void *data, size_t len)
{
    char tmp[1200];
    FILE *f;
    int ok;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (!(f = bt_fopen(tmp, "wb")))
        return 0;
    ok = fwrite(data, 1, len, f) == len;
    ok &= fclose(f) == 0;
    return ok && bt_replace(tmp, path);
}

/* Регион по имени дампа: «(E)», «(Europe)», «(PAL)» — PAL; иначе — по заголовку. */
static int region_from_name(const char *path)
{
    if (strstr(path, "(E)") || strstr(path, "(Europe)") || strstr(path, "(PAL)") || strstr(path, "(EU)"))
        return NES_PAL;
    return -1;
}

/* Кнопка MIDP → кнопка NES: свои коды в режиме приставки, а сенсорные кнопки Android шлют
 * обычные клавиши телефона. */
static int nes_button(int key)
{
    switch (key) {
    case KEY_UP: case KEY_NUM0 + 2: return NES_UP;
    case KEY_DOWN: case KEY_NUM0 + 8: return NES_DOWN;
    case KEY_LEFT: case KEY_NUM0 + 4: return NES_LEFT;
    case KEY_RIGHT: case KEY_NUM0 + 6: return NES_RIGHT;
    case KEY_GAME_A: case KEY_FIRE: case KEY_NUM0 + 5: case KEY_NUM0 + 3: return NES_A;
    case KEY_GAME_B: case KEY_SOFT_RIGHT: case KEY_NUM0 + 1: case KEY_NUM0 + 7: return NES_B;
    case KEY_GAME_START: case KEY_SOFT_LEFT: case KEY_STAR: return NES_START;
    case KEY_GAME_SELECT: case KEY_POUND: case KEY_NUM0: return NES_SELECT;
    default: return 0;
    }
}

static void show_frame(const Nes *n, int top)
{
    Image *s = platform_screen();
    const uint32_t *px = nes_pixels(n);
    int y;

    for (y = 0; y < s->h; y++)
        memcpy(s->px + (size_t)y * (size_t)s->w, px + (size_t)(y + top) * 256, 256 * 4);
    platform_present();
}

MidpResult nes_run(const char *path, const char *save_id, ScaleMode scale, char *err, size_t errlen)
{
    char dir[1024], bat_path[1100], st_path[1100];
    uint8_t *rom, *bat;
    size_t len, bat_len = 0;
    Nes *n;
    int rate = 0, buttons = 0, top, frames = 0, title_done = 0;
    double next, base_rate = 0, frame_ms;
    MidpResult result = MIDP_BACK;
    uint32_t t0;

    err[0] = '\0';
    if (!(rom = read_file(path, &len))) {
        snprintf(err, errlen, "не удалось прочитать картридж");
        return MIDP_ERROR;
    }
    n = nes_create(rom, len, region_from_name(path), err, errlen);
    free(rom);
    if (!n)
        return MIDP_ERROR;

    midp_save_dir(save_id, dir, sizeof(dir));
    snprintf(bat_path, sizeof(bat_path), "%s/battery.sav", dir);
    snprintf(st_path, sizeof(st_path), "%s/state.sav", dir);
    if ((bat = nes_battery(n, &bat_len)) != NULL) {
        size_t got;
        uint8_t *saved = read_file(bat_path, &got);
        if (saved && got == bat_len)
            memcpy(bat, saved, bat_len);
        free(saved);
    }

    platform_touch_profile(save_id);
    platform_console_mode(1);
    top = nes_region(n) == NES_NTSC ? 8 : 0;    /* NTSC-телевизор не показывал верх и низ по 8 строк */
    platform_set_screen(256, 240 - 2 * top, scale);
    audio_reset();          /* звук полки (аккорд запуска) отдаёт устройство картриджу */
    if (audio_master() > 0 && (rate = platform_audio_open(48000, mix)) > 0) {
        base_rate = rate;
        nes_set_rate(n, rate);
    }
    ring_r = ring_w = 0;
    frame_ms = 1000.0 / nes_fps(n);
    t0 = platform_ticks_ms();
    next = t0;

    for (;;) {
        Event ev;
        int quit = 0, back = 0;

        while (platform_poll(&ev)) {
            switch (ev.type) {
            case EV_QUIT: quit = 1; break;
            case EV_BACK: back = 1; break;
            case EV_RESTART: nes_reset(n); break;
            case EV_SCREENSHOT: midp_shot(save_id, NULL); break;
            case EV_STATE_SAVE: {
                size_t sz = nes_state_size(n);
                uint8_t *buf = malloc(sz);
                if (buf) {
                    nes_state_save(n, buf);
                    bt_mkdirs(dir);
                    if (write_file(st_path, buf, sz))
                        BT_LOG("сохранено: %s\n", st_path);
                    free(buf);
                }
                break;
            }
            case EV_STATE_LOAD: {
                size_t got;
                uint8_t *buf = read_file(st_path, &got);
                if (!buf || nes_state_load(n, buf, got))
                    BT_LOG("нет быстрого сохранения для этой игры\n");
                free(buf);
                break;
            }
            case EV_KEY_DOWN:
                if (!title_done && platform_ticks_ms() - t0 > 2500 && frames > 5) {
                    midp_title_shot(save_id);   /* первое нажатие — заставка, она станет обложкой */
                    title_done = 1;
                }
                buttons |= nes_button(ev.key);
                break;
            case EV_KEY_UP:
                buttons &= ~nes_button(ev.key);
                break;
            default:
                break;
            }
        }
        if (quit || back) {
            result = quit ? MIDP_QUIT : MIDP_BACK;
            break;
        }
        if (platform_paused()) {
            buttons = 0;
            platform_sleep_ms(16);
            next = platform_ticks_ms();
            continue;
        }
        nes_set_buttons(n, 0, buttons);
        nes_frame(n);
        frames++;
        show_frame(n, top);
        {
            int16_t buf[4096];
            int k = nes_take_audio(n, buf, 4096), i, fill;
            if (rate > 0) {
                platform_audio_lock(1);
                for (i = 0; i < k && ring_fill() < RING - 1; i++) {
                    ring[ring_w] = buf[i];
                    ring_w = (ring_w + 1) % RING;
                }
                fill = ring_fill();
                platform_audio_lock(0);
                /* держим в буфере ~3 кадра звука: чуть меньше — ускоряем выдачу, больше — замедляем */
                {
                    double want = base_rate * 3 / nes_fps(n), adj = (want - fill) / want * 0.005;
                    adj = adj > 0.005 ? 0.005 : adj < -0.005 ? -0.005 : adj;
                    nes_set_rate(n, base_rate * (1 + adj));
                }
            }
        }
        next += frame_ms;
        {
            double now = platform_ticks_ms();
            if (next > now)
                platform_sleep_ms((uint32_t)(next - now));
            else if (now - next > 100)
                next = now;     /* отстали (слабая машина, перетаскивали окно) — не догоняем рывком */
        }
    }

    if ((bat = nes_battery(n, &bat_len)) != NULL) {
        bt_mkdirs(dir);
        write_file(bat_path, bat, bat_len);
    }
    if (rate > 0)
        platform_audio_close();
    platform_console_mode(0);
    platform_touch_profile(NULL);
    nes_destroy(n);
    return result;
}
