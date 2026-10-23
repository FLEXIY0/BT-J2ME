/* Мультитап: буква → несколько нажатий цифры, как на кнопочном телефоне. Нужен играм,
 * которые сами набирают текст на Canvas (имя питомца, таблица рекордов): букв они
 * не знают, только клавиши 2 = абвг, 3 = деёжз… Зовут и сенсорная клавиатура, и ПК. */
#ifndef BT_MULTITAP_H
#define BT_MULTITAP_H

#include <stdint.h>

#include "platform.h"

typedef void (*MtEmit)(EventType type, int key);

void mt_set_emit(MtEmit emit);
/* Буква (латиница или кириллица, любой регистр) в очередь; 0 — такой на кнопках нет. */
int  mt_letter(int c);
/* Одно нажатие клавиши через ту же очередь — чтобы не обогнать буквы. */
void mt_key(int key);
/* Нажатия очереди по времени; зовётся из platform_poll. */
void mt_tick(uint32_t now);
void mt_clear(void);

#endif
