/* Бэкенд без экрана: для серверов, CI и проверки JAR-ов. */
#define _POSIX_C_SOURCE 199309L
#include "platform.h"
#include "util.h"

#include <stdlib.h>
#include <time.h>

static uint32_t *fb;

int platform_init(const PlatformConfig *cfg)
{
    fb = calloc((size_t)cfg->width * (size_t)cfg->height, sizeof(uint32_t));
    return fb ? 0 : -1;
}

void platform_shutdown(void)
{
    free(fb);
    fb = NULL;
}

uint32_t *platform_framebuffer(void) { return fb; }
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
