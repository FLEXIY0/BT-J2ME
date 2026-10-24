/* Полка игр: сканирование папки с JAR-ами и «оформление» — обложки для CD-боксов. */
#ifndef BT_LIBRARY_H
#define BT_LIBRARY_H

#include <stdint.h>

#include "gfx.h"

enum { GAME_J2ME, GAME_NES, GAME_PSX };

typedef struct {
    char     path[1024];
    char     base[256];     /* имя файла без .jar/.nes/.cue — по нему ищется обложка */
    int      kind;          /* GAME_J2ME, GAME_NES или GAME_PSX */
    char     name[128], vendor[128], version[32], profile[32];
    long     size;
    Image    icon;          /* иконка MIDlet из JAR, может быть пустой */
    Image    cover;         /* готовая обложка размера cover_w × cover_h */
    Image    cover_small;   /* она же для боковых боксов: уменьшена с усреднением, без «рваного» текста */
    int      cover_ready;
    int      cover_custom;  /* 1 — из файла, 0 — собрана автоматически */
    uint32_t cover_time;    /* когда загрузилась — для плавного появления */
} Game;

typedef struct {
    char  dir[1024];
    Game *games;
    int   count;
} Library;

int  library_scan(Library *lib, const char *dir);
/* Файлы дорожек, которые называет .cue (FILE "…"): имена без пути, не больше max. */
int  library_cue_files(const char *cue_path, char (*out)[256], int max);
void library_free(Library *lib);

/* Ищет <имя>.png рядом с JAR или в covers/<имя>.png; если нет — рисует обложку сама. */
void library_load_cover(const Library *lib, Game *g, int w, int h);

#endif
