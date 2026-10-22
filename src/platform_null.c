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
