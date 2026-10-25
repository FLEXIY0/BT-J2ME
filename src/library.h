/* Полка игр: сканирование папки с JAR-ами и «оформление» — обложки для CD-боксов. */
#ifndef BT_LIBRARY_H
#define BT_LIBRARY_H

#include <stdint.h>

#include "gfx.h"

enum { GAME_J2ME, GAME_NES, GAME_PSX, GAME_PS2 };

typedef struct {
    char     path[1024];
    char     base[256];     /* имя файла без .jar/.nes/.cue — по нему ищется обложка */
    int      kind;          /* GAME_J2ME, GAME_NES, GAME_PSX или GAME_PS2 */
    char     name[128], vendor[128], version[32], profile[32];
    long long size;
    Image    icon;          /* иконка MIDlet из JAR, может быть пустой */
    Image    cover;         /* носитель с обложкой (media.h) заданной высоты, фон прозрачный */
    Image    art;           /* сама обложка (не больше 400 точек) — для фона полки */
    char     year[8], publisher[64], genre[48], region[24];   /* сведения (может быть пусто) */
    long     play_secs;     /* сколько всего играли, секунд */
    long long last_played;  /* когда запускали (время Unix), 0 — ни разу */
    int      cover_ready;
    int      cover_custom;  /* 1 — своя картинка, 2 — скачанная, 0 — собрана автоматически */
    int      cover_own;     /* своя картинка в папке игр — из интернета не качаем */
    uint32_t tone_bg, tone_fg;  /* два цвета игры по обложке: фон полки и акцент */
    uint32_t cover_time;    /* когда загрузилась — для плавного появления */
} Game;

typedef struct {
    char  dir[1024];
    Game *games;
    int   count;
    char (*covers)[256];    /* картинки из covers/ без .png: подбираются к играм по названию */
    int   ncovers;
} Library;

int  library_scan(Library *lib, const char *dir);
/* Файлы дорожек, которые называет .cue (FILE "…"): имена без пути, не больше max. */
int  library_cue_files(const char *cue_path, char (*out)[256], int max);
void library_free(Library *lib);

/* Носитель игры высотой h точек. Обложка: <имя>.png рядом с игрой или в covers/ (там и
 * похожее имя, например из пакета объёмных коробок — тогда она показывается как есть), потом
 * скачанная из libretro, потом заставка игры; иначе рисуется сама. Заодно — цвета игры. */
void library_load_cover(const Library *lib, Game *g, int h);

#endif
