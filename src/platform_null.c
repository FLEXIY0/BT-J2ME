/* Бэкенд без экрана: для серверов, CI и проверки JAR-ов. */
#define _POSIX_C_SOURCE 199309L
#include "platform.h"
#include "util.h"

#include <time.h>

static Image screen;

int platform_init(const PlatformConfig *cfg)
{
    return platform_set_screen(cfg->width, cfg->height, cfg->scale);
}

void platform_shutdown(void)
{
    image_free(&screen);
}

void platform_display_size(int *w, int *h)
{
    *w = 1024;
    *h = 768;
}

int platform_set_screen(int w, int h, ScaleMode mode)
{
    image_free(&screen);
    screen = image_new(w, h);
    return screen.px ? 0 : -1;
}

Image *platform_screen(void) { return &screen; }
void platform_present(void) {}

int platform_poll(Event *ev)
{
    ev->type = EV_QUIT;
    return 1;
}

uint32_t platform_ticks_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000);
}

void platform_sleep_ms(uint32_t ms)
{
    struct timespec ts;

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* Звуковой карты нет: audio.c сам ведёт время звуков по часам (audio_pump). */
int platform_audio_open(int rate, AudioMixFn mix)
{
    (void)rate;
    (void)mix;
    return 0;
}

void platform_audio_lock(int on) { (void)on; }
void platform_audio_close(void) {}

void platform_touch_profile(const char *game_id) { (void)game_id; }
int platform_can_import(void) { return 0; }
void platform_import_games(void) {}
void platform_text_input(int on) { (void)on; }
void platform_vibrate(int ms) { (void)ms; }
void platform_fade_in(int ms) { (void)ms; }
void platform_cursor_mode(int on) { (void)on; }
int platform_paused(void) { return 0; }
int platform_latch(void) { return 0; }
int platform_hints(void) { return 0; }
int platform_fullscreen(void) { return -1; }
void platform_set_fullscreen(int on) { (void)on; }
void platform_stick(float *x, float *y) { *x = *y = 0; }
int platform_open_url(const char *url) { (void)url; return 0; }
int platform_touch_wide(void) { return 0; }
