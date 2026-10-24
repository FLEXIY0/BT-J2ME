/* Обновление программы из GitHub Releases: полка спрашивает последний выпуск и, если он новее,
 * предлагает обновиться. На ПК новый файл подменяет старый и программа перезапускается,
 * на Android скачанный APK открывается в системной установке. */
#ifndef BT_UPDATE_H
#define BT_UPDATE_H

typedef enum {
    UPD_IDLE,           /* не проверяли, обновлений нет или они здесь не поддерживаются */
    UPD_CHECKING,
    UPD_AVAILABLE,      /* есть новая версия: update_version() */
    UPD_DOWNLOADING,    /* update_progress() — 0..100 */
    UPD_DONE,           /* ПК: файл заменён, нужен перезапуск; Android: открыта установка */
    UPD_FAILED
} UpdState;

/* Убрать хвосты прошлого обновления (bt-j2me.exe.old, update.apk) — в фоне, при каждом запуске,
 * даже если проверка обновлений выключена. */
void        update_cleanup(void);
void        update_check(void);         /* фоновая проверка, один раз за запуск */
UpdState    update_state(void);
const char *update_version(void);
int         update_progress(void);
void        update_install(void);       /* скачать и поставить (после UPD_AVAILABLE) */
int         update_need_restart(void);  /* 1 — файл программы заменён, полку пора закрыть */
/* После выхода из полки: если файл заменён — запустить новую версию с теми же аргументами. */
void        update_relaunch(int argc, char **argv);

#endif
