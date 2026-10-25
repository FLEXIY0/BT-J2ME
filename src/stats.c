/* Файл маленький: читаем целиком, правим строку игры, пишем через временный файл. */
#include "stats.h"
#include "compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void stats_path(char *out, size_t cap)
{
    char data[800];

    bt_data_dir(data, sizeof(data));
    snprintf(out, cap, "%s/playtime.txt", data);
}

void stats_load(Library *lib)
{
    char path[900], line[400];
    FILE *f;
    int i;

    for (i = 0; i < lib->count; i++) {
        lib->games[i].play_secs = 0;
        lib->games[i].last_played = 0;
    }
    stats_path(path, sizeof(path));
    if (!(f = bt_fopen(path, "r")))
        return;
    while (fgets(line, sizeof(line), f)) {
        char *t1 = strchr(line, '\t'), *t2;
        if (!t1 || !(t2 = strchr(t1 + 1, '\t')))
            continue;
        *t1 = '\0';
        for (i = 0; i < lib->count; i++)
            if (!strcmp(lib->games[i].base, line)) {
                lib->games[i].play_secs = atol(t1 + 1);
                lib->games[i].last_played = atoll(t2 + 1);
            }
    }
    fclose(f);
}

void stats_add(Game *g, long secs)
{
    char path[900], tmp[920], line[400];
    FILE *in, *out;
    size_t n = strlen(g->base);

    g->play_secs += secs > 0 ? secs : 0;
    g->last_played = (long long)time(NULL);
    stats_path(path, sizeof(path));
    bt_data_dir(tmp, sizeof(tmp));
    bt_mkdirs(tmp);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (!(out = bt_fopen(tmp, "w")))
        return;
    if ((in = bt_fopen(path, "r")) != NULL) {
        while (fgets(line, sizeof(line), in))
            if (strncmp(line, g->base, n) || line[n] != '\t')
                fputs(line, out);
        fclose(in);
    }
    fprintf(out, "%s\t%ld\t%lld\n", g->base, g->play_secs, g->last_played);
    if (fclose(out) == 0)
        bt_replace(tmp, path);
}
