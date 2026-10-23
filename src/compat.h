/* Файлы и папки без привязки к ОС. Все пути — UTF-8: на Linux они идут в libc как есть,
 * на Windows переводятся в UTF-16 для «широких» функций — иначе не открыть «Мяусим.jar»
 * и не попасть в профиль пользователя с русским именем. */
#ifndef BT_COMPAT_H
#define BT_COMPAT_H

#include <stdio.h>

typedef struct BtDir BtDir;

FILE       *bt_fopen(const char *path, const char *mode);
int         bt_mkdir(const char *path);
int         bt_replace(const char *from, const char *to);   /* rename поверх существующего; 1 — успех */
int         bt_remove(const char *path);                    /* 1 — успех */
int         bt_is_dir(const char *path);
BtDir      *bt_dir_open(const char *path);
const char *bt_dir_next(BtDir *d);                          /* имя в UTF-8 или NULL */
void        bt_dir_close(BtDir *d);

/* Домашняя папка (на Windows — профиль пользователя). */
const char *bt_home(void);
/* %APPDATA% на Windows (настройки и сохранения), на остальных — NULL. */
const char *bt_appdata(void);

/* Папка данных bt-j2me (сохранения, скриншоты): $XDG_DATA_HOME/bt-j2me, %APPDATA%\bt-j2me
 * или ~/.local/share/bt-j2me. */
void        bt_data_dir(char *out, size_t cap);
/* Создать все папки по пути. */
void        bt_mkdirs(const char *path);

#endif
