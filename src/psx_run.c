/* Запуск диска PS1 с полки. Ядро PCSX-ReARMed подключено через его libretro-интерфейс
 * (retro_* из third_party/pcsx_rearmed/frontend/libretro.c): мы для него — «фронтенд».
 * Темп держим по часам (59,94 кадра у NTSC, 50 у PAL), звук 44,1 кГц стерео переводим
 * в частоту устройства и подстраиваем под заполнение буфера, как у NES. Отстаём — ядро
 * само пропускает отрисовку кадров (frameskip «auto»), чтобы игра не тормозила и звук не рвался. */
#include "psx.h"
#include "audio.h"
#include "compat.h"
#include "util.h"

#include "libretro.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RING 16384      /* стерео-кадров: ~0,34 с при 48 кГц */

static int16_t ring[RING * 2];
static int     ring_r, ring_w;
static int16_t last[2];

static struct {
    char      sys_dir[1024], save_dir[1024];
    int       fmt;              /* RETRO_PIXEL_FORMAT_* */
    unsigned  buttons;          /* RETRO_DEVICE_ID_JOYPAD_* битами */
    unsigned  fresh, late_up;   /* нажаты после прошлого кадра; отпущены, не дождавшись кадра */
    ScaleMode scale;
    int       sw, sh;           /* размер экрана, который сейчас выставлен */
    int       rate;             /* частота устройства, 0 — без звука */
    double    in_rate, fps, adj;
    double    rs_pos;
    int16_t   rs_prev[2];
    int       frames;
    int       verbose;
    retro_audio_buffer_status_callback_t buf_cb;
    /* опции ядра: умолчания приходят из SET_VARIABLES, поверх — наши */
    char      keys[128][48], vals[128][32];
    int       nopts;
} P;

/* Что меняем относительно умолчаний ядра. */
static const char *const overrides[][2] = {
    { "pcsx_rearmed_memcard1", "shared" },          /* файл карты — в папке сохранений этой игры */
    { "pcsx_rearmed_memcard2", "none" },
    { "pcsx_rearmed_rgb32_output", "enabled" },     /* ролики и заставки в 24 битах — без полос */
    { "pcsx_rearmed_frameskip_type", "auto" },      /* не успеваем — пропускаем отрисовку, не звук */
    { "pcsx_rearmed_frameskip_interval", "3" },
    { "pcsx_rearmed_bios", "auto" },                /* настоящий BIOS, если есть, иначе HLE */
    { "pcsx_rearmed_show_bios_bootlogo", "disabled" },
    { "pcsx_rearmed_drc", "enabled" },
};

static int ring_fill(void)
{
    return (ring_w - ring_r + RING) % RING;
}

static void mix(int16_t *out, int frames)
{
    int i;

    for (i = 0; i < frames; i++) {
        if (ring_r != ring_w) {
            last[0] = ring[ring_r * 2];
            last[1] = ring[ring_r * 2 + 1];
            ring_r = (ring_r + 1) % RING;
        }
        out[i * 2] = last[0];       /* буфер пуст — тянем последний сэмпл, а не щёлкаем нулём */
        out[i * 2 + 1] = last[1];
    }
}

/* ---------- вызовы из ядра ---------- */

static void RETRO_CALLCONV log_cb(enum retro_log_level level, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    size_t n;

    if (level < RETRO_LOG_WARN && !P.verbose)
        return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    n = strlen(buf);
    BT_LOG("psx: %s%s", buf, n && buf[n - 1] == '\n' ? "" : "\n");
}

static const char *opt_get(const char *key)
{
    size_t i;
    int k;

    for (i = 0; i < sizeof(overrides) / sizeof(overrides[0]); i++)
        if (!strcmp(overrides[i][0], key))
            return overrides[i][1];
    for (k = 0; k < P.nopts; k++)
        if (!strcmp(P.keys[k], key))
            return P.vals[k];
    return NULL;
}

/* «Описание; умолчание|вариант|…» — запоминаем умолчание. */
static void opt_defaults(const struct retro_variable *v)
{
    for (P.nopts = 0; v && v->key && P.nopts < 128; v++) {
        const char *s = v->value ? strstr(v->value, "; ") : NULL;
        size_t n;
        if (!s)
            continue;
        s += 2;
        n = strcspn(s, "|");
        snprintf(P.keys[P.nopts], sizeof(P.keys[0]), "%s", v->key);
        snprintf(P.vals[P.nopts], sizeof(P.vals[0]), "%.*s", (int)(n < 31 ? n : 31), s);
        P.nopts++;
    }
}

static bool RETRO_CALLCONV environment(unsigned cmd, void *data)
{
    switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        *(const char **)data = P.sys_dir;
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        *(const char **)data = P.save_dir;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        opt_defaults((const struct retro_variable *)data);
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        struct retro_variable *v = (struct retro_variable *)data;
        v->value = v->key ? opt_get(v->key) : NULL;
        return v->value != NULL;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        *(bool *)data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        int f = (int)*(const enum retro_pixel_format *)data;
        if (f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565)
            return false;
        P.fmt = f;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        ((struct retro_log_callback *)data)->log = log_cb;
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        if (data)
            *(bool *)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_AUDIO_BUFFER_STATUS_CALLBACK: {
        const struct retro_audio_buffer_status_callback *cb = data;
        P.buf_cb = cb ? cb->callback : NULL;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const struct retro_system_av_info *av = data;
        if (av->timing.fps > 1)
            P.fps = av->timing.fps;
        if (av->timing.sample_rate > 1000)
            P.in_rate = av->timing.sample_rate;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        BT_LOG("psx: %s\n", ((const struct retro_message *)data)->msg);
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        BT_LOG("psx: %s\n", ((const struct retro_message_ext *)data)->msg);
        return true;
    default:
        return false;
    }
}

static void RETRO_CALLCONV video_cb(const void *data, unsigned w, unsigned h, size_t pitch)
{
    Image *s;
    unsigned x, y;

    if (!data || w < 16 || h < 16)
        return;                 /* кадр не менялся (или пропущен) — на экране остаётся прошлый */
    if ((int)w != P.sw || (int)h != P.sh) {
        /* у приставки разрешение меняется на лету (меню 512×480, игра 320×240) — экран за ним,
         * а показываем всегда 4:3, как телевизор */
        platform_set_screen((int)w, (int)h, P.scale);
        platform_set_aspect(4, 3);
        P.sw = (int)w;
        P.sh = (int)h;
    }
    s = platform_screen();
    if (!s->px || s->w != (int)w || s->h != (int)h)
        return;
    for (y = 0; y < h; y++) {
        uint32_t *d = s->px + (size_t)y * w;
        if (P.fmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            const uint32_t *p = (const uint32_t *)((const uint8_t *)data + y * pitch);
            for (x = 0; x < w; x++)
                d[x] = p[x] | 0xFF000000u;
        } else {
            const uint16_t *p = (const uint16_t *)((const uint8_t *)data + y * pitch);
            for (x = 0; x < w; x++) {
                uint32_t c = p[x], r = c >> 11, g = (c >> 5) & 63, b = c & 31;
                d[x] = 0xFF000000u | (r << 19 | (r >> 2) << 16) | (g << 10 | (g >> 4) << 8) | (b << 3 | b >> 2);
            }
        }
    }
    platform_present();
}

/* Звук ядра (стерео, обычно 44100 Гц) → частота устройства, линейной интерполяцией. */
static size_t RETRO_CALLCONV audio_batch_cb(const int16_t *in, size_t frames)
{
    double step, p;

    if (P.rate <= 0 || !frames)
        return frames;
    step = P.in_rate / (double)P.rate * (1 - P.adj);
    p = P.rs_pos;
    platform_audio_lock(1);
    while (p < (double)frames - 1) {
        int i = p < 0 ? -1 : (int)p;
        double t = p - (double)i;
        const int16_t *a = i < 0 ? P.rs_prev : in + i * 2, *b = in + (i + 1) * 2;
        if (ring_fill() < RING - 1) {
            ring[ring_w * 2] = (int16_t)(a[0] + (b[0] - a[0]) * t);
            ring[ring_w * 2 + 1] = (int16_t)(a[1] + (b[1] - a[1]) * t);
            ring_w = (ring_w + 1) % RING;
        }
        p += step;
    }
    platform_audio_lock(0);
    P.rs_pos = p - (double)frames;
    P.rs_prev[0] = in[(frames - 1) * 2];
    P.rs_prev[1] = in[(frames - 1) * 2 + 1];
    return frames;
}

static void RETRO_CALLCONV audio_cb(int16_t l, int16_t r)
{
    int16_t f[2] = { l, r };
    audio_batch_cb(f, 1);
}

static void RETRO_CALLCONV input_poll_cb(void)
{
}

static int16_t RETRO_CALLCONV input_state_cb(unsigned port, unsigned device, unsigned index, unsigned id)
{
    if (port != 0 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD)
        return 0;
    if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        return (int16_t)P.buttons;
    return id < 16 ? (int16_t)((P.buttons >> id) & 1) : 0;
}

/* ---------- кнопки ---------- */

/* Кнопка нашей платформы → кнопка DualShock (номер бита libretro). Коды KEY_GAME_* — по
 * месту на геймпаде: A — нижняя (×), B — правая (○), X — левая (□), Y — верхняя (△).
 * Клавиши телефона — на случай сенсорной раскладки от J2ME-игры. */
static int psx_button(int key)
{
    switch (key) {
    case KEY_UP: case KEY_NUM0 + 2: return RETRO_DEVICE_ID_JOYPAD_UP;
    case KEY_DOWN: case KEY_NUM0 + 8: return RETRO_DEVICE_ID_JOYPAD_DOWN;
    case KEY_LEFT: case KEY_NUM0 + 4: return RETRO_DEVICE_ID_JOYPAD_LEFT;
    case KEY_RIGHT: case KEY_NUM0 + 6: return RETRO_DEVICE_ID_JOYPAD_RIGHT;
    case KEY_GAME_A: case KEY_FIRE: case KEY_NUM0 + 5: return RETRO_DEVICE_ID_JOYPAD_B;
    case KEY_GAME_B: case KEY_SOFT_RIGHT: case KEY_NUM0 + 3: return RETRO_DEVICE_ID_JOYPAD_A;
    case KEY_GAME_X: case KEY_NUM0 + 1: return RETRO_DEVICE_ID_JOYPAD_Y;
    case KEY_GAME_Y: case KEY_NUM0 + 7: return RETRO_DEVICE_ID_JOYPAD_X;
    case KEY_GAME_L1: case KEY_STAR: return RETRO_DEVICE_ID_JOYPAD_L;
    case KEY_GAME_R1: case KEY_POUND: return RETRO_DEVICE_ID_JOYPAD_R;
    case KEY_GAME_L2: return RETRO_DEVICE_ID_JOYPAD_L2;
    case KEY_GAME_R2: return RETRO_DEVICE_ID_JOYPAD_R2;
    case KEY_GAME_L3: return RETRO_DEVICE_ID_JOYPAD_L3;
    case KEY_GAME_R3: return RETRO_DEVICE_ID_JOYPAD_R3;
    case KEY_GAME_START: case KEY_SOFT_LEFT: return RETRO_DEVICE_ID_JOYPAD_START;
    case KEY_GAME_SELECT: case KEY_NUM0: return RETRO_DEVICE_ID_JOYPAD_SELECT;
    default: return -1;
    }
}

/* ---------- файлы ---------- */

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

int psx_bios_dir(const char *games_dir, char *out, size_t cap)
{
    snprintf(out, cap, "%s/bios", games_dir);
    if (dir_has_bios(out))
        return 1;
    if (dir_has_bios(games_dir)) {
        snprintf(out, cap, "%s", games_dir);
        return 1;
    }
    snprintf(out, cap, "%s/bios", games_dir);
    return 0;
}

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = bt_fopen(path, "rb");
    uint8_t *buf = NULL;
    long n;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && n < (64L << 20) && fseek(f, 0, SEEK_SET) == 0 &&
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

/* ---------- запуск ---------- */

MidpResult psx_run(const char *path, const char *save_id, ScaleMode scale, char *err, size_t errlen)
{
    char games_dir[1024], st_path[1100];
    const char *slash;
    struct retro_game_info info;
    struct retro_system_av_info av;
    MidpResult result = MIDP_BACK;
    double next, frame_ms, want;
    uint32_t t0;
    int title_done = 0, have_bios;

    err[0] = '\0';
    memset(&P, 0, sizeof(P));
    P.verbose = getenv("BT_PSX_LOG") != NULL;
    P.scale = scale;
    P.fmt = RETRO_PIXEL_FORMAT_RGB565;

    snprintf(games_dir, sizeof(games_dir), "%s", path);
    slash = strrchr(games_dir, '/');
#ifdef _WIN32
    if (strrchr(games_dir, '\\') > slash)
        slash = strrchr(games_dir, '\\');
#endif
    if (slash)
        games_dir[slash - games_dir] = '\0';
    else
        snprintf(games_dir, sizeof(games_dir), ".");
    have_bios = psx_bios_dir(games_dir, P.sys_dir, sizeof(P.sys_dir));
    midp_save_dir(save_id, P.save_dir, sizeof(P.save_dir));
    bt_mkdirs(P.save_dir);
    snprintf(st_path, sizeof(st_path), "%s/state.sav", P.save_dir);
    BT_LOG("PlayStation: %s, BIOS: %s\n", path, have_bios ? P.sys_dir : "нет, встроенный HLE");

    retro_set_environment(environment);
    retro_set_video_refresh(video_cb);
    retro_set_audio_sample(audio_cb);
    retro_set_audio_sample_batch(audio_batch_cb);
    retro_set_input_poll(input_poll_cb);
    retro_set_input_state(input_state_cb);
    retro_init();

    memset(&info, 0, sizeof(info));
    info.path = path;
    if (!retro_load_game(&info)) {
        retro_deinit();
        snprintf(err, errlen, "не удалось открыть образ диска");
        return MIDP_ERROR;
    }
    retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
    memset(&av, 0, sizeof(av));
    retro_get_system_av_info(&av);
    P.fps = av.timing.fps > 1 ? av.timing.fps : 60;
    P.in_rate = av.timing.sample_rate > 1000 ? av.timing.sample_rate : 44100;

    platform_touch_profile(save_id);
    platform_console_mode(CONSOLE_PSX);
    platform_set_screen(320, 240, scale);
    platform_set_aspect(4, 3);
    P.sw = 320;
    P.sh = 240;
    memset(platform_screen()->px, 0, 320 * 240 * 4);
    audio_reset();          /* звук полки (аккорд запуска) отдаёт устройство приставке */
    ring_r = ring_w = 0;
    P.rs_pos = -1;
    if (audio_master() > 0)
        P.rate = platform_audio_open_stereo(48000, mix);
    frame_ms = 1000.0 / P.fps;
    t0 = platform_ticks_ms();
    next = t0;

    for (;;) {
        Event ev;
        int quit = 0, back = 0, b;

        while (platform_poll(&ev)) {
            switch (ev.type) {
            case EV_QUIT: quit = 1; break;
            case EV_BACK: back = 1; break;
            case EV_RESTART: retro_reset(); break;
            case EV_SCREENSHOT: midp_shot(save_id, NULL); break;
            case EV_STATE_SAVE: {
                size_t sz = retro_serialize_size();
                uint8_t *buf = sz ? malloc(sz) : NULL;
                if (buf && retro_serialize(buf, sz) && write_file(st_path, buf, sz))
                    BT_LOG("сохранено: %s\n", st_path);
                free(buf);
                break;
            }
            case EV_STATE_LOAD: {
                size_t got;
                uint8_t *buf = read_file(st_path, &got);
                if (!buf || !retro_unserialize(buf, got))
                    BT_LOG("нет быстрого сохранения для этой игры\n");
                free(buf);
                break;
            }
            case EV_KEY_DOWN:
                if (!title_done && platform_ticks_ms() - t0 > 2500 && P.frames > 5) {
                    midp_title_shot(save_id);   /* первое нажатие — заставка, она станет обложкой */
                    title_done = 1;
                }
                if ((b = psx_button(ev.key)) >= 0) {
                    P.buttons |= 1u << b;
                    P.fresh |= 1u << b;
                    P.late_up &= ~(1u << b);
                }
                break;
            case EV_KEY_UP:
                /* короткое нажатие игра должна увидеть хотя бы в одном кадре */
                if ((b = psx_button(ev.key)) >= 0) {
                    if (P.fresh & (1u << b))
                        P.late_up |= 1u << b;
                    else
                        P.buttons &= ~(1u << b);
                }
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
            P.buttons = P.fresh = P.late_up = 0;
            platform_sleep_ms(16);
            next = platform_ticks_ms();
            continue;
        }
        if (P.buf_cb) {
            /* заполнение буфера звука подсказывает ядру, пропускать ли отрисовку кадра */
            int fill, cap = (int)(P.rate * 4 / P.fps);
            platform_audio_lock(1);
            fill = ring_fill();
            platform_audio_lock(0);
            P.buf_cb(P.rate > 0, (unsigned)(cap > 0 && fill < cap ? fill * 100 / cap : 100),
                     P.rate > 0 && fill < P.rate / P.fps);
        }
        retro_run();
        P.frames++;
        P.buttons &= ~P.late_up;
        P.fresh = P.late_up = 0;
        if (P.rate > 0) {
            /* держим в буфере ~3 кадра звука: чуть меньше — растягиваем, больше — сжимаем */
            int fill;
            platform_audio_lock(1);
            fill = ring_fill();
            platform_audio_lock(0);
            want = P.rate * 3 / P.fps;
            P.adj = (want - fill) / want * 0.005;
            P.adj = P.adj > 0.005 ? 0.005 : P.adj < -0.005 ? -0.005 : P.adj;
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

    if (P.rate > 0)
        platform_audio_close();
    retro_unload_game();
    retro_deinit();
    platform_set_aspect(0, 0);
    platform_console_mode(CONSOLE_OFF);
    platform_touch_profile(NULL);
    return result;
}
