#include "multitap.h"

/* Раскладки как на телефонах: E.161 и русская Nokia. */
static const char *const mt_en[10] = { " ", ".,?!1", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz" };
static const char *const mt_ru[10] = { " ", ".,?!1", "абвг", "деёжз", "ийкл", "мноп", "рсту", "фхцч", "шщъы", "ьэюя" };

static struct {
    MtEmit   emit;
    struct { int key, taps; } q[64];
    int      head, len, phase, done, last_key;
    uint32_t next, last_end;
} M;

static int utf8_next(const char **p)
{
    const unsigned char *u = (const unsigned char *)*p;
    int c = u[0], n = 1;

    if ((c & 0xE0) == 0xC0 && u[1]) {
        c = ((c & 0x1F) << 6) | (u[1] & 0x3F);
        n = 2;
    } else if ((c & 0xF0) == 0xE0 && u[1] && u[2]) {
        c = ((c & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
        n = 3;
    }
    *p += n;
    return c;
}

static int lower(int c)
{
    if ((c >= 'A' && c <= 'Z') || (c >= 0x410 && c <= 0x42F))
        return c + 32;
    return c == 0x401 ? 0x451 : c;
}

void mt_set_emit(MtEmit emit)
{
    M.emit = emit;
}

static void push(int key, int taps)
{
    if (M.len == (int)(sizeof(M.q) / sizeof(M.q[0])))
        return;
    M.q[(M.head + M.len) % 64].key = key;
    M.q[(M.head + M.len) % 64].taps = taps;
    M.len++;
}

int mt_letter(int c)
{
    int d, k;

    c = lower(c);
    for (d = 0; d < 10; d++) {
        const char *tabs[2] = { mt_en[d], mt_ru[d] };
        for (k = 0; k < 2; k++) {
            const char *p = tabs[k];
            int i = 1;
            while (*p) {
                if (utf8_next(&p) == c) {
                    push('0' + d, i);
                    return 1;
                }
                i++;
            }
        }
    }
    return 0;
}

void mt_key(int key)
{
    push(key, 1);
}

void mt_clear(void)
{
    M.len = 0;
    M.phase = 0;
    M.done = 0;
}

void mt_tick(uint32_t now)
{
    int key;

    if (!M.len || !M.emit || (int32_t)(now - M.next) < 0)
        return;
    key = M.q[M.head].key;
    if (M.phase) {
        M.emit(EV_KEY_UP, key);
        M.phase = 0;
        if (++M.done >= M.q[M.head].taps) {
            M.head = (M.head + 1) % 64;
            M.len--;
            M.done = 0;
            M.last_key = key;
            M.last_end = now;
            M.next = now + 250;         /* следующая буква на другой цифре */
            return;
        }
        M.next = now + 160;             /* пауза между нажатиями одной буквы */
        return;
    }
    /* та же цифра у следующей буквы — ждём, пока телефон «примет» прошлую */
    if (M.done == 0 && key == M.last_key && now - M.last_end < 1500) {
        M.next = M.last_end + 1500;
        return;
    }
    M.emit(EV_KEY_DOWN, key);
    M.phase = 1;
    M.next = now + 150;                 /* игры опрашивают клавиши раз в кадр — держим подольше */
}
