/* Подбор обложки по названию (boxart_match) и носитель (media_render) без сети. */
#include "../src/boxart.h"
#include "../src/media.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void match(const char *base, const char *name, const char *want)
{
    static const char *const names[] = {
        "Castlevania - Symphony of the Night (USA)",
        "Castlevania - Symphony of the Night (Europe)",
        "Castlevania - Symphony of the Night (Japan) (Demo)",
        "Crash Bandicoot (USA)",
        "Crash Bandicoot 2 - Cortex Strikes Back (USA)",
        "Final Fantasy VII (USA) (Disc 1)",
        "Final Fantasy VII (USA) (Disc 2)",
        "Final Fantasy VIII (USA) (Disc 1)",
        "Legend of Zelda, The (USA)",
        "Tom & Jerry (USA)",
    };
    int i = boxart_match(names, (int)(sizeof(names) / sizeof(names[0])), base, name);
    const char *got = i < 0 ? "-" : names[i];
    int ok = !strcmp(got, want);

    printf("%s boxart %s -> %s\n", ok ? "ok  " : "FAIL", base, got);
    fails += !ok;
}

int main(void)
{
    Media m;
    Image img;
    int kind, x, y, opaque;

    match("Castlevania - Symphony of the Night (USA) (Track 1)", "", "Castlevania - Symphony of the Night (USA)");
    match("Castlevania - Symphony of the Night (Europe)", "", "Castlevania - Symphony of the Night (Europe)");
    match("castlevania_symphony_of_the_night", "", "Castlevania - Symphony of the Night (USA)");
    match("Crash Bandicoot", "", "Crash Bandicoot (USA)");
    match("Final Fantasy 7 (Disc 2)", "", "Final Fantasy VII (USA) (Disc 2)");
    match("Final Fantasy VII", "", "Final Fantasy VII (USA) (Disc 1)");
    match("The Legend of Zelda", "", "Legend of Zelda, The (USA)");
    match("Tom and Jerry", "", "Tom & Jerry (USA)");
    match("Spyro the Dragon", "", "-");
    match("Final Fantasy IX", "", "-");

    /* носитель: угол прозрачный (пиксельное скругление), остальное закрашено */
    for (kind = GAME_J2ME; kind <= GAME_PS2; kind++) {
        memset(&m, 0, sizeof(m));
        m.kind = kind;
        m.title = "Test";
        m.bg = 0xFF336699u;
        m.fg = 0xFFFFCC00u;
        img = media_render(&m, 120);
        opaque = 0;
        for (y = 0; y < img.h; y++)
            for (x = 0; x < img.w; x++)
                opaque += (img.px[y * img.w + x] >> 24) > 200;
        if (!img.px || (img.px[0] >> 24) > 10 || opaque < img.w * img.h / 2) {
            printf("FAIL media kind %d\n", kind);
            fails++;
        } else
            printf("ok   media kind %d: %dx%d\n", kind, img.w, img.h);
        image_free(&img);
    }
    printf("BOXART DONE fails=%d\n", fails);
    return fails != 0;
}
