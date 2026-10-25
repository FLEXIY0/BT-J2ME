/* Подбор обложки по названию: «Castlevania - Symphony of the Night (USA) (Track 1)» и
 * «castlevania_symphony_of_the_night» находят «Castlevania - Symphony of the Night (USA)». */
#include "boxart.h"
#include "compat.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char *boxart_system(int kind, const char **cache_dir)
{
    const char *sys = NULL, *dir = NULL;

    switch (kind) {
    case GAME_PSX: sys = "Sony - PlayStation";                       dir = "ps1"; break;
    case GAME_PS2: sys = "Sony - PlayStation 2";                     dir = "ps2"; break;
    case GAME_NES: sys = "Nintendo - Nintendo Entertainment System"; dir = "nes"; break;
    }
    if (cache_dir)
        *cache_dir = dir;
    return sys;
}

int boxart_cached(int kind, const char *base, char *out, size_t cap)
{
    const char *dir;
    char data[800];

    if (!boxart_system(kind, &dir))
        return 0;
    bt_data_dir(data, sizeof(data));
    snprintf(out, cap, "%s/boxart/%s/%s.png", data, dir, base);
    return bt_file_size(out) > 0;
}

/* Римские цифры в номерах частей — в арабские: «Final Fantasy VII» = «Final Fantasy 7». */
static const char *roman(const char *w)
{
    static const char *const r[] = { "ii", "iii", "iv", "vi", "vii", "viii", "ix", "xi", "xii", "xiii" };
    static const char *const a[] = { "2", "3", "4", "6", "7", "8", "9", "11", "12", "13" };
    size_t i;

    for (i = 0; i < sizeof(r) / sizeof(r[0]); i++)
        if (!strcmp(w, r[i]))
            return a[i];
    return w;
}

/* Ключ для сравнения: строчные латинские буквы и цифры через пробел, без скобок,
 * артиклей «the» и знаков; «&» — это «and». */
static void norm(const char *s, char *out, size_t cap)
{
    char word[64];
    size_t n = 0, wl = 0;
    int depth = 0;

    for (;; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '(' || c == '[')
            depth++;
        else if ((c == ')' || c == ']') && depth)
            depth--;
        if (!depth && (isalnum(c) || c >= 0x80) && wl + 1 < sizeof(word)) {
            word[wl++] = (char)tolower(c);
            continue;
        }
        if (wl) {
            const char *w;
            word[wl] = '\0';
            w = roman(word);
            if (strcmp(w, "the")) {
                size_t k = strlen(w);
                if (n && n + 1 < cap)
                    out[n++] = ' ';
                if (n + k < cap) {
                    memcpy(out + n, w, k);
                    n += k;
                }
            }
            wl = 0;
        }
        if (!depth && c == '&' && n + 5 < cap) {
            memcpy(out + n, n ? " and" : "and", n ? 4 : 3);
            n += n ? 4 : 3;
        }
        if (!c)
            break;
    }
    out[n] = '\0';
}

/* Сходство строк по парам букв (коэффициент Дайса), 0..100. */
static int similar(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b), i, j, hit = 0;
    char used[256];

    if (la < 2 || lb < 2 || lb >= sizeof(used))
        return !strcmp(a, b) ? 100 : 0;
    memset(used, 0, lb);
    for (i = 0; i + 1 < la; i++)
        for (j = 0; j + 1 < lb; j++)
            if (!used[j] && a[i] == b[j] && a[i + 1] == b[j + 1]) {
                used[j] = 1;
                hit++;
                break;
            }
    return (int)(200 * hit / (la - 1 + lb - 1));
}

/* Числа из ключа подряд: у «Crash Bandicoot 2» и «Crash Bandicoot» они разные — разные игры. */
static void digits(const char *s, char *out, size_t cap)
{
    size_t n = 0;

    for (; *s && n + 1 < cap; s++)
        if (isdigit((unsigned char)*s) || (*s == ' ' && n && out[n - 1] != ' '))
            out[n++] = isdigit((unsigned char)*s) ? *s : ' ';
    while (n && out[n - 1] == ' ')
        n--;
    out[n] = '\0';
}

/* Есть ли в имени метка в скобках (без регистра): «(USA)», «(Disc 1)». */
static int has_tag(const char *s, const char *tag)
{
    size_t k = strlen(tag);

    for (; (s = strchr(s, '(')) != NULL; s++) {
        const char *p = s + 1;
        /* «(USA, Europe)» — смотрим каждое слово через запятую */
        while (*p && *p != ')') {
            while (*p == ' ' || *p == ',')
                p++;
            if (!strncasecmp(p, tag, k) && (p[k] == ')' || p[k] == ','))
                return 1;
            while (*p && *p != ',' && *p != ')')
                p++;
        }
    }
    return 0;
}

static int tag_score(const char *cand, const char *base)
{
    static const char *const regions[] = { "USA", "World", "Europe", "Japan" };
    static const char *const bad[] = { "Demo", "Beta", "Proto", "Sample", "Kiosk", "Unl" };
    char disc[16];
    int i, sc = 0, want_disc = 0;

    for (i = 0; i < 4; i++) {
        if (has_tag(base, regions[i]) && has_tag(cand, regions[i]))
            sc += 40;
        if (has_tag(cand, regions[i]))
            sc += 8 - 2 * i;
    }
    for (i = 0; i < 6; i++)
        if (has_tag(cand, bad[i]) || strstr(cand, bad[i]))
            sc -= 50;
    for (i = 1; i <= 9; i++) {
        snprintf(disc, sizeof(disc), "Disc %d", i);
        if (has_tag(base, disc))
            want_disc = i;
    }
    for (i = 1; i <= 9; i++) {
        snprintf(disc, sizeof(disc), "Disc %d", i);
        if (has_tag(cand, disc))
            sc += i == (want_disc ? want_disc : 1) ? 10 : -20;
    }
    return sc;
}

int boxart_match(const char *const *names, int n, const char *base, const char *name)
{
    char q[2][256], qd[2][64], k[256], kd[64];
    int i, t, best = -1, best_sim = 0, best_tag = 0;

    norm(base, q[0], sizeof(q[0]));
    norm(name ? name : "", q[1], sizeof(q[1]));
    for (t = 0; t < 2; t++)
        digits(q[t], qd[t], sizeof(qd[t]));
    for (i = 0; i < n; i++) {
        int sim = 0, tag;
        norm(names[i], k, sizeof(k));
        digits(k, kd, sizeof(kd));
        for (t = 0; t < 2; t++) {
            int s, lq = (int)strlen(q[t]), lk = (int)strlen(k);
            if (!lq || strcmp(qd[t], kd) || lq * 4 < lk * 3 || lk * 4 < lq * 3)
                continue;
            s = similar(q[t], k);
            sim = s > sim ? s : sim;
        }
        if (sim < 84)           /* «Final Fantasy 7» и «Final Fantasy 8» — уже разные игры */
            continue;
        tag = tag_score(names[i], base);
        if (sim > best_sim || (sim == best_sim && tag > best_tag)) {
            best = i;
            best_sim = sim;
            best_tag = tag;
        }
    }
    return best;
}
