/* Образы дисков приставок Sony: какой это диск — PS1 или PS2. У обеих бывают одинаковые
 * .iso, .bin и .cue, поэтому смотрим на сам диск: в SYSTEM.CNF у PS2 строка BOOT2, у PS1 —
 * BOOT. Читаем ISO 9660 из образа по 2048 байт на сектор и из «сырого» по 2352. */
#ifndef BT_DISC_H
#define BT_DISC_H

enum { DISC_NONE, DISC_PS1, DISC_PS2 };

/* Похоже ли имя на образ диска (.cue .bin .iso .img .pbp .chd .cso .zso). */
int  disc_is_image(const char *path);
/* DISC_PS1 или DISC_PS2 по содержимому (.pbp — всегда PS1, .chd/.cso/.zso — PS2);
 * не прочитали — по размеру: больше 1 ГБ бывают только DVD PS2. DISC_NONE — не образ. */
int  disc_system(const char *path);

#endif
