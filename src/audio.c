/* Звук BT-J2ME: микшер, MIDI-синтезатор и разбор звуковых форматов телефонов. */
#include "audio.h"
#include "platform.h"
#include "compat.h"
#include "util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RATE_WANT   22050   /* как у телефонов; для слабых машин — в самый раз */
#define BLOCK       32      /* сэмплов между пересчётами огибающих и событий (~1,5 мс) */
#define MAX_SOUNDS  64
#define MAX_VOICES  32
#define MAX_EVENTS  400000
#define VOICE_SCALE 0.22f   /* одна нота — около четверти шкалы: аккорды не хрипят */
#define PCM_SCALE   0.85f
#define UNKNOWN_US  1000000u /* «играем» неизвестный формат секунду тишины */

typedef struct {
    uint32_t t;             /* микросекунды от начала */
    uint8_t  st, a, b;      /* статус MIDI и два байта данных */
} Ev;

typedef struct {
    uint8_t prog, vol, expr, pedal;
    float   bend_mul;
} Chan;

enum { K_SEQ, K_PCM };

typedef struct {
    int      used, gen, kind, temp, unknown;
    int      playing, loops, loops_left, volume;
    uint32_t last_use;
    int16_t *pcm;
    uint32_t pcm_len, pcm_rate;
    uint64_t pcm_pos;       /* 32.32 в сэмплах источника */
    Ev      *ev;
    int      ev_count, ev_i;
    uint32_t dur_us;
    uint64_t pos;           /* сэмплов вывода от начала прохода */
    Chan     ch[16];
} Sound;

typedef struct {
    int            on, snd, gen, released, held, drum, attacking;
    uint8_t        ch, note;
    const int16_t *wave;    /* NULL — шум */
    uint32_t       phase, step, base_step, min_step;
    float          env, att, dec, sus, rel, amp, sweep;
    float          samp, camp, bmul;   /* громкость звука и канала: запоминаем, если звук уже закрыт */
    int32_t        gain, lp;
    int            filt, lp_k;
    uint32_t       lfsr, age;
} Voice;

enum { W_SINE, W_TRI, W_SAW, W_SQUARE, W_PULSE, W_ORGAN, W_PIANO, W_BASS, W_FLUTE, W_COUNT };
enum { F_NONE, F_LOW, F_HIGH };

typedef struct {
    uint8_t  wave;
    uint16_t att_ms, dec_ms;
    uint8_t  sus;           /* % */
    uint16_t rel_ms;
} Instr;

/* Инструменты General MIDI по семействам (программа / 8). */
static const Instr family[16] = {
    { W_PIANO,    2, 800,  0, 150 },   /* фортепиано */
    { W_SINE,     1, 300,  0, 150 },   /* колокольчики, ксилофон */
    { W_ORGAN,   10,  60, 90,  80 },   /* орган */
    { W_SAW,      2, 600,  0, 120 },   /* гитара */
    { W_BASS,     3, 800, 40,  60 },   /* бас */
    { W_SAW,     60, 400, 85, 250 },   /* струнные */
    { W_SAW,     80, 400, 80, 300 },   /* ансамбль, хор */
    { W_SAW,     25, 300, 80, 120 },   /* медь */
    { W_PULSE,   15, 300, 85, 100 },   /* язычковые */
    { W_FLUTE,   30, 300, 90, 120 },   /* флейты */
    { W_SQUARE,   3, 200, 90,  80 },   /* синт-соло: «телефонный» квадрат */
    { W_SAW,    250, 800, 85, 500 },   /* пэды */
    { W_TRI,     50, 600, 70, 400 },   /* синт-эффекты */
    { W_SAW,      2, 400,  0, 120 },   /* этнические щипковые */
    { W_SINE,     1, 200,  0,  80 },   /* перкуссия */
    { W_SQUARE,  10, 300, 60, 200 }    /* звуковые эффекты */
};

static struct {
    int      open, tried, offline, rate, master;
    Sound    snd[MAX_SOUNDS];
    Voice    v[MAX_VOICES];
    uint32_t note_step[128];
    int16_t  waves[W_COUNT][256];
    int      ready;
    uint32_t pump_t0, use, age;
    uint64_t pump_done;     /* сэмплов, выданных по часам с pump_t0 */
    FILE    *dump;
    uint32_t dump_bytes;
} A = { .master = 100 };

/* ---------- подготовка ---------- */

static void make_wave(int16_t *w, const float *harm, int n)
{
    float buf[256], peak = 0;
    int i, k;

    for (i = 0; i < 256; i++) {
        float x = 0;
        for (k = 0; k < n; k++)
            x += harm[k] * sinf(6.2831853f * (float)((k + 1) * i) / 256.0f);
        buf[i] = x;
        peak = fabsf(x) > peak ? fabsf(x) : peak;
    }
    for (i = 0; i < 256; i++)
        w[i] = (int16_t)(buf[i] / (peak > 0 ? peak : 1) * 32000.0f);
}

static void setup(int rate)
{
    int i;

    A.rate = rate;
    for (i = 0; i < 128; i++) {
        double f = 440.0 * pow(2.0, (i - 69) / 12.0), st = f * 4294967296.0 / rate;
        A.note_step[i] = st >= 2147483647.0 ? 2147483647u : (uint32_t)st;
    }
    if (A.ready)
        return;
    {
        /* волны из гармоник: мягче «голых» пил и квадратов, меньше звона на 22 кГц */
        float h[12];
        int k;
        h[0] = 1;
        make_wave(A.waves[W_SINE], h, 1);
        for (k = 0; k < 12; k++)
            h[k] = (k % 2 == 0) ? ((k / 2) % 2 ? -1.0f : 1.0f) / (float)((k + 1) * (k + 1)) : 0;
        make_wave(A.waves[W_TRI], h, 12);
        for (k = 0; k < 12; k++)
            h[k] = 1.0f / (float)(k + 1);
        make_wave(A.waves[W_SAW], h, 12);
        for (k = 0; k < 12; k++)
            h[k] = k % 2 == 0 ? 1.0f / (float)(k + 1) : 0;
        make_wave(A.waves[W_SQUARE], h, 12);
        for (k = 0; k < 12; k++)
            h[k] = sinf(3.14159265f * (float)(k + 1) / 4.0f) / (float)(k + 1);
        make_wave(A.waves[W_PULSE], h, 12);
        {
            static const float organ[8] = { 1, 0.6f, 0.35f, 0.25f, 0, 0.12f, 0, 0.08f };
            static const float piano[8] = { 1, 0.45f, 0.25f, 0.12f, 0.08f, 0.05f, 0.03f, 0.02f };
            static const float bass[4] = { 1, 0.5f, 0.25f, 0.1f };
            static const float flute[3] = { 1, 0.15f, 0.05f };
            make_wave(A.waves[W_ORGAN], organ, 8);
            make_wave(A.waves[W_PIANO], piano, 8);
            make_wave(A.waves[W_BASS], bass, 4);
            make_wave(A.waves[W_FLUTE], flute, 3);
        }
    }
    A.ready = 1;
}

static void lock(void)   { if (A.open) platform_audio_lock(1); }
static void unlock(void) { if (A.open) platform_audio_lock(0); }

/* ---------- WAV-файл для отладки (BT_AUDIO_DUMP) и audio-render ---------- */

static void wav_header(FILE *f, int rate, uint32_t bytes)
{
    uint8_t h[44];

    memcpy(h, "RIFF", 4);
    h[4] = (uint8_t)(bytes + 36); h[5] = (uint8_t)((bytes + 36) >> 8);
    h[6] = (uint8_t)((bytes + 36) >> 16); h[7] = (uint8_t)((bytes + 36) >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = h[18] = h[19] = 0;
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;     /* PCM, моно */
    h[24] = (uint8_t)rate; h[25] = (uint8_t)(rate >> 8); h[26] = (uint8_t)(rate >> 16); h[27] = 0;
    h[28] = (uint8_t)(rate * 2); h[29] = (uint8_t)((rate * 2) >> 8); h[30] = (uint8_t)((rate * 2) >> 16); h[31] = 0;
    h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4);
    h[40] = (uint8_t)bytes; h[41] = (uint8_t)(bytes >> 8); h[42] = (uint8_t)(bytes >> 16); h[43] = (uint8_t)(bytes >> 24);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
    fseek(f, 0, SEEK_END);
}

static void wav_write(FILE *f, const int16_t *s, int n)
{
    uint8_t buf[2 * BLOCK];
    int i;

    for (i = 0; i < n; i++) {
        buf[i * 2] = (uint8_t)s[i];
        buf[i * 2 + 1] = (uint8_t)((uint16_t)s[i] >> 8);
    }
    fwrite(buf, 2, (size_t)n, f);
}

/* ---------- голоса ---------- */

static Sound *get(int h)
{
    int i = (h & 0xFF) - 1;

    if (h <= 0 || i < 0 || i >= MAX_SOUNDS || !A.snd[i].used || A.snd[i].gen != (h >> 8))
        return NULL;
    return &A.snd[i];
}

static float per_block(float ms)
{
    float blocks = ms * (float)A.rate / 1000.0f / BLOCK;
    return blocks < 1 ? 0.0f : expf(-1.0f / blocks);
}

static Voice *voice_alloc(void)
{
    Voice *best = NULL;
    int i;

    for (i = 0; i < MAX_VOICES; i++)
        if (!A.v[i].on)
            return &A.v[i];
    /* все заняты: забираем самый тихий из отпущенных, иначе самый старый */
    for (i = 0; i < MAX_VOICES; i++)
        if (A.v[i].released && (!best || A.v[i].env < best->env))
            best = &A.v[i];
    for (i = 0; !best && i < MAX_VOICES; i++)
        if (!best || A.v[i].age < best->age)
            best = &A.v[i];
    for (i = 0; i < MAX_VOICES; i++)
        if (!best->released && A.v[i].age < best->age)
            best = &A.v[i];
    return best;
}

static void drum_setup(Voice *v, int note)
{
    float dec = 100, amp = 0.5f, f0 = 0, f1 = 0, sweep_ms = 60;

    v->filt = F_LOW;
    v->lp_k = 180;
    switch (note) {
    case 35: case 36:                                   /* бочка */
        f0 = 160; f1 = 45; dec = 150; amp = 1.5f;
        break;
    case 41: case 43: case 45: case 47: case 48: case 50: /* томы */
        f0 = 90.0f + (float)(note - 41) * 18.0f;
        f1 = f0 * 0.6f; dec = 200; amp = 1.1f; sweep_ms = 120;
        break;
    case 38: case 40:                                   /* малый */
        v->lp_k = 150; dec = 120; amp = 0.9f;
        break;
    case 37: case 39:                                   /* боковой, хлопок */
        v->lp_k = 200; dec = 40; amp = 0.8f;
        break;
    case 42: case 44:                                   /* закрытая тарелка */
        v->filt = F_HIGH; v->lp_k = 60; dec = 35; amp = 0.5f;
        break;
    case 46:
        v->filt = F_HIGH; v->lp_k = 60; dec = 220; amp = 0.45f;
        break;
    case 49: case 52: case 55: case 57:                 /* крэш */
        v->filt = F_HIGH; v->lp_k = 100; dec = 700; amp = 0.5f;
        break;
    case 51: case 53: case 59:                          /* райд */
        v->filt = F_HIGH; v->lp_k = 100; dec = 400; amp = 0.35f;
        break;
    }
    v->drum = 1;
    v->amp *= amp;
    v->env = 1;
    v->sus = 0;
    v->dec = per_block(dec);
    v->rel = v->dec;
    if (f0 > 0) {
        v->wave = A.waves[W_SINE];
        v->step = (uint32_t)(f0 * 4294967296.0 / A.rate);
        v->min_step = (uint32_t)(f1 * 4294967296.0 / A.rate);
        v->sweep = expf(logf(f1 / f0) / (sweep_ms * (float)A.rate / 1000.0f / BLOCK));
    } else {
        v->wave = NULL;
        v->lfsr = 0x12345678u ^ (uint32_t)note * 2654435761u;
    }
}

static void note_on(int si, int c, int note, int vel)
{
    Sound *s = &A.snd[si];
    Voice *v = voice_alloc();
    const Instr *in = &family[s->ch[c].prog >> 3];

    memset(v, 0, sizeof(*v));
    v->on = 1;
    v->snd = si;
    v->gen = s->gen;
    v->ch = (uint8_t)c;
    v->note = (uint8_t)note;
    v->age = ++A.age;
    v->amp = (float)vel / 127.0f;
    v->samp = 1;
    v->camp = 1;
    v->bmul = 1;
    if (c == 9) {
        drum_setup(v, note);
        return;
    }
    v->wave = A.waves[in->wave];
    v->base_step = v->step = A.note_step[note & 127];
    v->att = in->att_ms * (float)A.rate / 1000.0f / BLOCK;
    v->att = v->att < 1 ? 1 : 1.0f / v->att;    /* прибавка за блок */
    v->attacking = 1;
    v->dec = per_block(in->dec_ms);
    v->sus = in->sus / 100.0f;
    v->rel = per_block(in->rel_ms);
}

static void voice_release(Voice *v, float ms)
{
    v->released = 1;
    v->held = 0;
    if (ms > 0)
        v->rel = per_block(ms);
}

static void note_off(int si, int c, int note)
{
    int i;

    if (c == 9)
        return;                 /* барабаны затухают сами */
    for (i = 0; i < MAX_VOICES; i++) {
        Voice *v = &A.v[i];
        if (v->on && !v->released && v->snd == si && v->gen == A.snd[si].gen && v->ch == c && v->note == note) {
            if (A.snd[si].ch[c].pedal)
                v->held = 1;
            else
                voice_release(v, 0);
        }
    }
}

/* Отпустить все ноты звука (или одного канала, c >= 0). */
static void release_all(int si, int c, float ms)
{
    int i;

    for (i = 0; i < MAX_VOICES; i++) {
        Voice *v = &A.v[i];
        if (v->on && v->snd == si && v->gen == A.snd[si].gen && (c < 0 || v->ch == c))
            voice_release(v, ms);
    }
}

static void chan_reset(Sound *s)
{
    int c;

    for (c = 0; c < 16; c++) {
        s->ch[c].prog = 0;
        s->ch[c].vol = 100;
        s->ch[c].expr = 127;
        s->ch[c].pedal = 0;
        s->ch[c].bend_mul = 1;
    }
}

static void seq_event(int si, const Ev *e, int notes)
{
    Sound *s = &A.snd[si];
    int c = e->st & 15, i;
    Chan *ch = &s->ch[c];

    switch (e->st & 0xF0) {
    case 0x90:
        if (e->b) {
            if (notes)
                note_on(si, c, e->a, e->b);
            break;
        }
        /* note on с нулевой силой — это note off */
        /* fall through */
    case 0x80:
        if (notes)
            note_off(si, c, e->a);
        break;
    case 0xB0:
        switch (e->a) {
        case 7:   ch->vol = e->b; break;
        case 11:  ch->expr = e->b; break;
        case 64:
            ch->pedal = e->b >= 64;
            for (i = 0; !ch->pedal && i < MAX_VOICES; i++)
                if (A.v[i].on && A.v[i].held && A.v[i].snd == si && A.v[i].ch == c)
                    voice_release(&A.v[i], 0);
            break;
        case 120: case 123:
            if (notes)
                release_all(si, c, e->a == 120 ? 5 : 0);
            break;
        case 121:
            ch->expr = 127;
            ch->pedal = 0;
            ch->bend_mul = 1;
            break;
        }
        break;
    case 0xC0:
        ch->prog = e->a;
        break;
    case 0xE0:
        ch->bend_mul = powf(2.0f, (float)((e->a | (e->b << 7)) - 8192) / 8192.0f * 2.0f / 12.0f);
        break;
    }
}

static void voice_block(Voice *v, int32_t *acc, int n)
{
    Sound *s = &A.snd[v->snd];
    int32_t g, dg, target;
    float gain;
    int i;

    /* огибающая */
    if (v->released)
        v->env *= v->rel;
    else if (v->attacking) {
        v->env += v->att;
        if (v->env >= 1) {
            v->env = 1;
            v->attacking = 0;
        }
    } else
        v->env = v->sus + (v->env - v->sus) * v->dec;
    if (v->env < 0.0004f && !v->attacking) {
        v->on = 0;
        return;
    }
    if (s->used && s->gen == v->gen) {
        Chan *ch = &s->ch[v->ch];
        float cv = ch->vol / 127.0f, ce = ch->expr / 127.0f;
        v->samp = s->volume / 100.0f;
        v->camp = cv * cv * ce * ce;
        v->bmul = ch->bend_mul;
    }
    if (v->drum) {
        if (v->wave && v->step > v->min_step)
            v->step = (uint32_t)((float)v->step * v->sweep);
    } else
        v->step = v->bmul == 1 ? v->base_step : (uint32_t)((float)v->base_step * v->bmul);
    gain = v->env * v->amp * v->samp * v->camp * (float)A.master / 100.0f * VOICE_SCALE;
    target = (int32_t)(gain * 32767.0f);
    target = target > 32767 ? 32767 : target;
    g = v->gain;
    dg = (target - g) / n;
    if (v->wave) {
        const int16_t *w = v->wave;
        uint32_t ph = v->phase, st = v->step;
        for (i = 0; i < n; i++, g += dg) {
            int a = w[ph >> 24], b = w[(uint8_t)((ph >> 24) + 1)];
            int x = a + (((b - a) * (int32_t)((ph >> 10) & 0x3FFF)) >> 14);
            acc[i] += (x * g) >> 15;
            ph += st;
        }
        v->phase = ph;
    } else {
        uint32_t x = v->lfsr;
        int32_t lp = v->lp;
        for (i = 0; i < n; i++, g += dg) {
            int smp, out;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            smp = (int16_t)(x >> 16);
            lp += ((smp - lp) * v->lp_k) >> 8;
            out = v->filt == F_HIGH ? (smp - lp) >> 1 : v->filt == F_LOW ? lp : smp;
            acc[i] += (out * g) >> 15;
        }
        v->lfsr = x;
        v->lp = lp;
    }
    v->gain = target;
}

/* ---------- звуки ---------- */

static uint64_t us2smp(uint32_t us)
{
    return (uint64_t)us * (uint64_t)A.rate / 1000000u;
}

static void sound_free_slot(int si)
{
    Sound *s = &A.snd[si];

    free(s->pcm);
    free(s->ev);
    s->pcm = NULL;
    s->ev = NULL;
    s->used = 0;
}

static void rewind_sound(Sound *s)
{
    s->pos = 0;
    s->ev_i = 0;
    s->pcm_pos = 0;
    chan_reset(s);
}

/* Дошли до конца: следующий круг или остановка. */
static void sound_end(int si)
{
    Sound *s = &A.snd[si];

    if (s->kind == K_SEQ)
        release_all(si, -1, 0);
    rewind_sound(s);
    if (s->loops_left < 0 || --s->loops_left > 0)
        return;
    s->playing = 0;
    s->loops_left = s->loops;
    if (s->temp)
        sound_free_slot(si);
}

static void pcm_block(int si, int32_t *acc, int n)
{
    Sound *s = &A.snd[si];
    uint64_t step = ((uint64_t)s->pcm_rate << 32) / (uint64_t)A.rate;
    int32_t g = (int32_t)((float)s->volume / 100.0f * (float)A.master / 100.0f * PCM_SCALE * 32767.0f);
    int i;

    for (i = 0; i < n && s->playing; i++) {
        uint64_t idx = s->pcm_pos >> 32;
        int a, b, x;
        if (idx >= s->pcm_len) {
            sound_end(si);
            continue;
        }
        a = s->pcm[idx];
        b = idx + 1 < s->pcm_len ? s->pcm[idx + 1] : a;
        x = a + (((b - a) * (int32_t)((s->pcm_pos >> 18) & 0x3FFF)) >> 14);
        acc[i] += (x * g) >> 15;
        s->pcm_pos += step;
    }
}

static void seq_block(int si, int n)
{
    Sound *s = &A.snd[si];
    uint64_t end = s->pos + (uint64_t)n;

    while (s->ev_i < s->ev_count && us2smp(s->ev[s->ev_i].t) < end)
        seq_event(si, &s->ev[s->ev_i++], 1);
    s->pos = end;
    if (s->ev_i >= s->ev_count && s->pos >= us2smp(s->dur_us))
        sound_end(si);
}

static void mix(int16_t *out, int frames)
{
    int32_t acc[BLOCK];

    while (frames > 0) {
        int n = frames < BLOCK ? frames : BLOCK, i;
        memset(acc, 0, sizeof(acc));
        for (i = 0; i < MAX_SOUNDS; i++) {
            if (!A.snd[i].used || !A.snd[i].playing)
                continue;
            if (A.snd[i].kind == K_PCM)
                pcm_block(i, acc, n);
            else
                seq_block(i, n);
        }
        for (i = 0; i < MAX_VOICES; i++)
            if (A.v[i].on)
                voice_block(&A.v[i], acc, n);
        for (i = 0; i < n; i++)
            out[i] = (int16_t)(acc[i] > 32767 ? 32767 : acc[i] < -32768 ? -32768 : acc[i]);
        if (A.dump) {
            wav_write(A.dump, out, n);
            A.dump_bytes += (uint32_t)n * 2;
        }
        out += n;
        frames -= n;
    }
}

static int busy(void)
{
    int i;

    for (i = 0; i < MAX_SOUNDS; i++)
        if (A.snd[i].used && A.snd[i].playing)
            return 1;
    for (i = 0; i < MAX_VOICES; i++)
        if (A.v[i].on)
            return 1;
    return 0;
}

static void ensure_open(void)
{
    const char *dump;

    if (!A.ready)
        setup(RATE_WANT);
    if (A.open || A.tried || A.offline || A.master == 0)
        return;
    A.tried = 1;
    dump = getenv("BT_AUDIO_DUMP");
    if (dump && *dump && (A.dump = bt_fopen(dump, "wb")) != NULL) {
        A.dump_bytes = 0;
        wav_header(A.dump, RATE_WANT, 0);
    }
    {
        int rate = platform_audio_open(RATE_WANT, mix);
        if (rate > 0) {
            setup(rate);
            A.open = 1;
            if (A.dump)
                wav_header(A.dump, rate, 0);
        }
    }
    A.pump_t0 = platform_ticks_ms();
    A.pump_done = 0;
}

void audio_set_master(int volume)
{
    A.master = volume < 0 ? 0 : volume > 100 ? 100 : volume;
}

void audio_pump(void)
{
    uint32_t now = platform_ticks_ms();
    uint64_t due, frames;
    int16_t buf[512];

    if (A.open || !A.ready)
        return;
    if (!busy()) {
        A.pump_t0 = now;
        A.pump_done = 0;
        return;
    }
    /* считаем от точки отсчёта, а не от прошлого вызова: так округление не копится */
    due = (uint64_t)(now - A.pump_t0) * (uint64_t)A.rate / 1000u;
    if (due < A.pump_done + BLOCK)
        return;
    /* целыми блоками, как у звуковой карты: огибающие считаются одинаково в обоих режимах */
    frames = (due - A.pump_done) / BLOCK * BLOCK;
    A.pump_done += frames;
    if (frames > (uint64_t)A.rate)       /* после долгой паузы не догоняем больше секунды */
        frames = (uint64_t)A.rate / BLOCK * BLOCK;
    while (frames > 0) {
        int n = frames > 512 ? 512 : (int)frames;
        mix(buf, n);
        frames -= (uint32_t)n;
    }
}

void audio_reset(void)
{
    int i;

    if (A.open)
        platform_audio_close();
    A.open = 0;
    A.tried = 0;
    for (i = 0; i < MAX_SOUNDS; i++)
        if (A.snd[i].used)
            sound_free_slot(i);
    for (i = 0; i < MAX_VOICES; i++)
        A.v[i].on = 0;
    if (A.dump) {
        wav_header(A.dump, A.rate, A.dump_bytes);
        fclose(A.dump);
        A.dump = NULL;
    }
}

/* ---------- разбор форматов ---------- */

typedef struct {
    Ev  *ev;
    int  n, cap;
} EvList;

static int ev_add(EvList *l, uint32_t t, int st, int a, int b)
{
    if (l->n == l->cap) {
        int cap = l->cap ? l->cap * 2 : 256;
        Ev *ne;
        if (cap > MAX_EVENTS || !(ne = realloc(l->ev, (size_t)cap * sizeof(Ev))))
            return -1;
        l->ev = ne;
        l->cap = cap;
    }
    l->ev[l->n].t = t;
    l->ev[l->n].st = (uint8_t)st;
    l->ev[l->n].a = (uint8_t)(a & 0x7F);
    l->ev[l->n].b = (uint8_t)(b & 0x7F);
    l->n++;
    return 0;
}

static void seq_take(Sound *s, EvList *l, uint32_t dur_us)
{
    s->kind = K_SEQ;
    s->ev = l->ev;
    s->ev_count = l->n;
    s->dur_us = dur_us;
}

/* --- Standard MIDI File --- */

typedef struct {
    uint32_t tick, order, tempo;
    uint8_t  st, a, b;
} RawEv;

static int raw_cmp(const void *x, const void *y)
{
    const RawEv *a = x, *b = y;

    if (a->tick != b->tick)
        return a->tick < b->tick ? -1 : 1;
    return a->order < b->order ? -1 : a->order > b->order;
}

static int varlen(const uint8_t **p, const uint8_t *end, uint32_t *out)
{
    uint32_t v = 0;
    int i;

    for (i = 0; i < 4; i++) {
        if (*p >= end)
            return -1;
        v = (v << 7) | (**p & 0x7F);
        if (!(*(*p)++ & 0x80)) {
            *out = v;
            return 0;
        }
    }
    return -1;
}

static int parse_midi(Sound *s, const uint8_t *d, size_t len)
{
    const uint8_t *p = NULL, *end = d + len;
    RawEv *raw = NULL;
    size_t nraw = 0, cap = 0, i;
    uint32_t max_tick = 0, order = 0, hdr, ntrk, div, t;
    EvList l = { NULL, 0, 0 };
    double us = 0, last_us = 0;
    uint32_t last_tick = 0, tempo = 500000;

    for (i = 0; i + 14 <= len && i < 128; i++)   /* RMID и прочие обёртки */
        if (!memcmp(d + i, "MThd", 4)) {
            p = d + i;
            break;
        }
    if (!p)
        return -1;
    hdr = rd_be32(p + 4);
    ntrk = rd_be16(p + 10);
    div = rd_be16(p + 12);
    if (hdr < 6 || div == 0 || (size_t)(end - p) < 8 + hdr)
        return -1;
    p += 8 + hdr;
    for (t = 0; t < ntrk && end - p >= 8; t++) {
        uint32_t clen = rd_be32(p + 4), tick = 0;
        const uint8_t *q = p + 8, *qend = (size_t)(end - q) < clen ? end : q + clen;
        uint8_t run = 0;
        if (memcmp(p, "MTrk", 4)) {
            p = qend;
            t--;
            continue;
        }
        while (q < qend) {
            uint32_t delta, ml;
            uint8_t st;
            if (varlen(&q, qend, &delta) || q >= qend)
                break;
            tick += delta;
            st = *q;
            if (st & 0x80) {
                q++;
                if (st < 0xF0)
                    run = st;
            } else if (run)
                st = run;
            else
                break;
            if (st == 0xFF) {
                uint8_t type;
                if (q >= qend)
                    break;
                type = *q++;
                if (varlen(&q, qend, &ml) || (size_t)(qend - q) < ml)
                    break;
                if (type == 0x51 && ml == 3) {
                    if (nraw == cap) {
                        RawEv *nr;
                        cap = cap ? cap * 2 : 1024;
                        if (cap > MAX_EVENTS || !(nr = realloc(raw, cap * sizeof(RawEv))))
                            goto fail;
                        raw = nr;
                    }
                    raw[nraw].tick = tick;
                    raw[nraw].order = order++;
                    raw[nraw].tempo = ((uint32_t)q[0] << 16) | ((uint32_t)q[1] << 8) | q[2];
                    raw[nraw].st = 0xFF;
                    nraw++;
                }
                q += ml;
                if (type == 0x2F)
                    break;
            } else if (st == 0xF0 || st == 0xF7) {
                run = 0;
                if (varlen(&q, qend, &ml) || (size_t)(qend - q) < ml)
                    break;
                q += ml;
            } else if (st >= 0xF0) {
                continue;               /* системные без данных */
            } else {
                int nd = (st & 0xE0) == 0xC0 ? 1 : 2;
                if (qend - q < nd)
                    break;
                if ((st & 0xF0) != 0xA0 && (st & 0xF0) != 0xD0) {
                    if (nraw == cap) {
                        RawEv *nr;
                        cap = cap ? cap * 2 : 1024;
                        if (cap > MAX_EVENTS || !(nr = realloc(raw, cap * sizeof(RawEv))))
                            goto fail;
                        raw = nr;
                    }
                    raw[nraw].tick = tick;
                    raw[nraw].order = order++;
                    raw[nraw].st = st;
                    raw[nraw].a = q[0] & 0x7F;
                    raw[nraw].b = nd == 2 ? q[1] & 0x7F : 0;
                    nraw++;
                }
                q += nd;
            }
            if (tick > max_tick)
                max_tick = tick;
        }
        if (tick > max_tick)
            max_tick = tick;
        p = qend;
    }
    if (nraw)
        qsort(raw, nraw, sizeof(RawEv), raw_cmp);
    for (i = 0; i <= nraw; i++) {
        uint32_t tk = i < nraw ? raw[i].tick : max_tick;
        if (div & 0x8000) {
            int fps = -(int8_t)(div >> 8), tpf = (int)(div & 0xFF);
            us = fps > 0 && tpf > 0 ? (double)tk * 1e6 / (fps * tpf) : 0;
        } else {
            us += (double)(tk - last_tick) * tempo / div;
            last_tick = tk;
        }
        if (us > 4.0e9)
            break;
        last_us = us;
        if (i == nraw)
            break;
        if (raw[i].st == 0xFF)
            tempo = raw[i].tempo ? raw[i].tempo : 500000;
        else if (ev_add(&l, (uint32_t)us, raw[i].st, raw[i].a, raw[i].b))
            goto fail;
    }
    free(raw);
    seq_take(s, &l, (uint32_t)last_us);
    return 0;
fail:
    free(raw);
    free(l.ev);
    return -1;
}

/* --- тоновая последовательность JSR-135 (ToneControl) --- */

enum { T_VERSION = -2, T_TEMPO = -3, T_RES = -4, T_BLOCK_START = -5, T_BLOCK_END = -6,
       T_PLAY_BLOCK = -7, T_SET_VOLUME = -8, T_REPEAT = -9, T_SILENCE = -1 };

typedef struct {
    const int8_t *b;
    size_t        len;
    int           blocks[128];
    double        unit_us, t;
    EvList        l;
} ToneSeq;

static int tone_emit(ToneSeq *ts, size_t i, int block, int depth)
{
    int rep = 1;

    if (depth > 8)
        return 0;
    for (; i + 1 < ts->len; i += 2) {
        int c = ts->b[i], v = ts->b[i + 1];
        if (c == T_BLOCK_END) {
            if (block >= 0)
                return 0;
            continue;
        }
        if (c >= 0 || c == T_SILENCE) {
            double dur = (v > 0 ? v : 0) * ts->unit_us, gap = dur / 16 < 10000 ? dur / 16 : 10000;
            int r;
            for (r = 0; r < rep; r++) {
                if (c >= 0 && dur > 0 &&
                    (ev_add(&ts->l, (uint32_t)ts->t, 0x90, c, 127) ||
                     ev_add(&ts->l, (uint32_t)(ts->t + dur - gap), 0x80, c, 0)))
                    return -1;
                ts->t += dur;
                if (ts->t > 3.6e9)
                    return -1;
            }
            rep = 1;
        } else if (c == T_PLAY_BLOCK) {
            if (v >= 0 && ts->blocks[v] >= 0 && tone_emit(ts, (size_t)ts->blocks[v], v, depth + 1))
                return -1;
        } else if (c == T_SET_VOLUME) {
            if (ev_add(&ts->l, (uint32_t)ts->t, 0xB0, 7, (v < 0 ? 0 : v > 100 ? 100 : v) * 127 / 100))
                return -1;
        } else if (c == T_REPEAT) {
            rep = v > 0 ? v : 1;
        }
    }
    return 0;
}

static int parse_tone(Sound *s, const uint8_t *d, size_t len)
{
    ToneSeq ts;
    int tempo = 30, res = 64;
    size_t i = 2;

    if (len < 2 || (int8_t)d[0] != T_VERSION)
        return -1;
    memset(&ts, 0, sizeof(ts));
    memset(ts.blocks, -1, sizeof(ts.blocks));
    ts.b = (const int8_t *)d;
    ts.len = len;
    if (i + 1 < len && ts.b[i] == T_TEMPO) {
        tempo = ts.b[i + 1] >= 5 ? ts.b[i + 1] : 30;
        i += 2;
    }
    if (i + 1 < len && ts.b[i] == T_RES) {
        res = ts.b[i + 1] >= 1 ? ts.b[i + 1] : 64;
        i += 2;
    }
    while (i + 1 < len && ts.b[i] == T_BLOCK_START) {
        int n = ts.b[i + 1];
        i += 2;
        if (n >= 0)
            ts.blocks[n] = (int)i;
        while (i + 1 < len && !(ts.b[i] == T_BLOCK_END && ts.b[i + 1] == n))
            i += 2;
        i += 2;
    }
    /* единица длительности: 1/res целой ноты; темп в байте — удары в минуту / 4 */
    ts.unit_us = 60.0e6 / ((double)tempo * res);
    if (ev_add(&ts.l, 0, 0xC0, 80, 0) || tone_emit(&ts, i, -1, 0)) {
        free(ts.l.ev);
        return -1;
    }
    seq_take(s, &ts.l, (uint32_t)ts.t);
    return 0;
}

/* --- рингтон Nokia Smart Messaging (OTT) --- */

typedef struct {
    const uint8_t *d;
    size_t         len, bit;
    int            err;
} Bits;

static int bits(Bits *b, int n)
{
    int v = 0;

    while (n--) {
        if (b->bit >= b->len * 8) {
            b->err = 1;
            return 0;
        }
        v = (v << 1) | ((b->d[b->bit >> 3] >> (7 - (b->bit & 7))) & 1);
        b->bit++;
    }
    return v;
}

static int parse_ott(Sound *s, const uint8_t *d, size_t len)
{
    static const int bpm_tab[32] = { 25, 28, 31, 35, 40, 45, 50, 56, 63, 70, 80, 90, 100, 112, 125, 140,
                                     160, 180, 200, 225, 250, 285, 320, 360, 400, 450, 500, 565, 635, 715, 800, 900 };
    Bits b = { d, len, 0, 0 };
    EvList l = { NULL, 0, 0 };
    int ncmd, cmd, k, unicode = 0, scale = 1, style = 0, bpm = 63, found = 0;
    double t = 0;

    ncmd = bits(&b, 8);
    for (k = 0; k < ncmd && !b.err && !found; k++) {
        cmd = bits(&b, 7);
        if (cmd == 0x25 || cmd == 0x22) {       /* программирование мелодии / юникод */
            unicode |= cmd == 0x22;
            bits(&b, 1);
        } else if (cmd == 0x1D) {               /* сама мелодия */
            int type = bits(&b, 3), np, pi;
            if (type == 1) {
                int tl = bits(&b, 4);
                bits(&b, tl * (unicode ? 16 : 8));
            }
            np = bits(&b, 8);
            for (pi = 0; pi < np && !b.err; pi++) {
                size_t start;
                int loops, ni, n, rep;
                bits(&b, 3);
                bits(&b, 2);
                loops = bits(&b, 4);
                ni = bits(&b, 8);
                start = b.bit;
                for (rep = 0; rep <= (loops == 15 ? 0 : loops) && !b.err; rep++) {
                    b.bit = start;
                    for (n = 0; n < ni && !b.err; n++) {
                        int id = bits(&b, 3);
                        if (id == 1) {
                            int note = bits(&b, 4), dc = bits(&b, 3), spec = bits(&b, 2);
                            double dur = 4 * 60.0e6 / bpm / (double)(1 << (dc > 5 ? 5 : dc));
                            dur *= spec == 1 ? 1.5 : spec == 2 ? 1.75 : spec == 3 ? 2.0 / 3 : 1;
                            if (note >= 1 && note <= 12) {
                                int midi = 60 + 12 * scale + note - 1;
                                double on = style == 1 ? dur : style == 2 ? dur * 0.5 : dur * 0.9;
                                if (ev_add(&l, (uint32_t)t, 0x90, midi, 127) || ev_add(&l, (uint32_t)(t + on), 0x80, midi, 0))
                                    goto fail;
                            }
                            t += dur;
                            if (t > 3.6e9)
                                goto fail;
                        } else if (id == 2) {
                            scale = bits(&b, 2);
                        } else if (id == 3) {
                            style = bits(&b, 2);
                        } else if (id == 4) {
                            bpm = bpm_tab[bits(&b, 5)];
                        } else if (id == 5) {
                            int vol = bits(&b, 4);
                            if (ev_add(&l, (uint32_t)t, 0xB0, 7, vol * 127 / 15))
                                goto fail;
                        } else
                            break;
                    }
                }
            }
            found = 1;
        } else
            break;
    }
    if (!found || !l.n)
        goto fail;
    /* программа «квадрат» в начало: события уже по времени, поэтому просто вставляем первым */
    if (ev_add(&l, 0, 0xC0, 80, 0))
        goto fail;
    memmove(l.ev + 1, l.ev, (size_t)(l.n - 1) * sizeof(Ev));
    l.ev[0].t = 0;
    l.ev[0].st = 0xC0;
    l.ev[0].a = 80;
    l.ev[0].b = 0;
    seq_take(s, &l, (uint32_t)t);
    return 0;
fail:
    free(l.ev);
    return -1;
}

/* --- WAV и Sun AU --- */

static int16_t ulaw(uint8_t u)
{
    int t;

    u = (uint8_t)~u;
    t = ((u & 0x0F) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    return (int16_t)((u & 0x80) ? 0x84 - t : t - 0x84);
}

static int16_t alaw(uint8_t a)
{
    int t, seg;

    a ^= 0x55;
    t = (a & 0x0F) << 4;
    seg = (a & 0x70) >> 4;
    if (seg == 0)
        t += 8;
    else {
        t += 0x108;
        if (seg > 1)
            t <<= seg - 1;
    }
    return (int16_t)((a & 0x80) ? t : -t);
}

static const int ima_index[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
static const int ima_step[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767
};

static int ima_nibble(int nib, int *pred, int *idx)
{
    int step = ima_step[*idx], diff = step >> 3;

    if (nib & 1) diff += step >> 2;
    if (nib & 2) diff += step >> 1;
    if (nib & 4) diff += step;
    *pred += (nib & 8) ? -diff : diff;
    *pred = *pred > 32767 ? 32767 : *pred < -32768 ? -32768 : *pred;
    *idx += ima_index[nib];
    *idx = *idx < 0 ? 0 : *idx > 88 ? 88 : *idx;
    return *pred;
}

/* IMA ADPCM (Microsoft): блоки с заголовком на канал, потом по 4 байта на канал. */
static int16_t *ima_decode(const uint8_t *d, size_t size, int chans, int align, uint32_t *frames)
{
    int per_block, blocks, b, c, i;
    int16_t *out;

    if (align < 8 * chans || chans > 2)
        return NULL;
    per_block = (align - 4 * chans) * 2 / chans + 1;
    blocks = (int)(size / (size_t)align);
    if (blocks <= 0 || !(out = calloc((size_t)blocks * (size_t)per_block, sizeof(int16_t))))
        return NULL;
    for (b = 0; b < blocks; b++) {
        const uint8_t *p = d + (size_t)b * (size_t)align;
        int16_t *o = out + (size_t)b * (size_t)per_block;
        for (c = 0; c < chans; c++) {
            int pred = (int16_t)rd_le16(p + 4 * c), idx = p[4 * c + 2] > 88 ? 88 : p[4 * c + 2];
            const uint8_t *q = p + 4 * chans + 4 * c;
            int n = 1;
            o[0] = (int16_t)(o[0] + pred / chans);
            while (n < per_block && q + 4 <= p + align) {
                for (i = 0; i < 8 && n < per_block; i++, n++) {
                    int nib = (q[i / 2] >> ((i & 1) * 4)) & 15;
                    o[n] = (int16_t)(o[n] + ima_nibble(nib, &pred, &idx) / chans);
                }
                q += 4 * chans;
            }
        }
    }
    *frames = (uint32_t)blocks * (uint32_t)per_block;
    return out;
}

static int pcm_take(Sound *s, int16_t *pcm, uint32_t frames, uint32_t rate)
{
    if (!pcm || !frames || rate < 1000 || rate > 192000) {
        free(pcm);
        return -1;
    }
    s->kind = K_PCM;
    s->pcm = pcm;
    s->pcm_len = frames;
    s->pcm_rate = rate;
    s->dur_us = (uint32_t)((uint64_t)frames * 1000000u / rate);
    return 0;
}

/* Сэмплы фиксированной ширины → моно int16. enc: 'u' 8 бит без знака, 's' 8 со знаком,
 * 'l' little-endian, 'b' big-endian (bytes — ширина), 'm' μ-law, 'a' A-law. */
static int16_t *decode_plain(const uint8_t *d, size_t size, int chans, int bytes, int enc, uint32_t *frames)
{
    size_t n = size / (size_t)(chans * bytes), i;
    int16_t *out;
    int c;

    if (!n || !(out = malloc(n * sizeof(int16_t))))
        return NULL;
    for (i = 0; i < n; i++) {
        int32_t sum = 0;
        for (c = 0; c < chans; c++) {
            const uint8_t *p = d + (i * (size_t)chans + (size_t)c) * (size_t)bytes;
            int v;
            switch (enc) {
            case 'u': v = (p[0] - 128) << 8; break;
            case 's': v = (int8_t)p[0] * 256; break;
            case 'm': v = ulaw(p[0]); break;
            case 'a': v = alaw(p[0]); break;
            case 'b': v = (int16_t)((p[0] << 8) | p[1]); break;
            default:  v = (int16_t)((p[bytes - 1] << 8) | p[bytes - 2]); break;
            }
            sum += v;
        }
        out[i] = (int16_t)(sum / chans);
    }
    *frames = (uint32_t)n;
    return out;
}

static int parse_wav(Sound *s, const uint8_t *d, size_t len)
{
    size_t p = 12;
    const uint8_t *fmt = NULL, *data = NULL;
    size_t data_len = 0;
    int tag, chans, bits, align;
    uint32_t rate, frames = 0;
    int16_t *pcm = NULL;

    while (p + 8 <= len) {
        uint32_t cl = rd_le32(d + p + 4);
        size_t avail = len - p - 8;
        if (!memcmp(d + p, "fmt ", 4) && cl >= 16 && avail >= 16)
            fmt = d + p + 8;
        else if (!memcmp(d + p, "data", 4)) {
            data = d + p + 8;
            data_len = cl < avail ? cl : avail;
        }
        if (cl > avail)
            break;
        p += 8 + cl + (cl & 1);
    }
    if (!fmt || !data)
        return -1;
    tag = rd_le16(fmt);
    chans = rd_le16(fmt + 2);
    rate = rd_le32(fmt + 4);
    align = rd_le16(fmt + 12);
    bits = rd_le16(fmt + 14);
    if (tag == 0xFFFE && rd_le16(fmt + 16) >= 22)
        tag = rd_le16(fmt + 24);
    if (chans < 1 || chans > 8)
        return -1;
    if (tag == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32))
        pcm = decode_plain(data, data_len, chans, bits / 8, bits == 8 ? 'u' : 'l', &frames);
    else if (tag == 6 || tag == 7)
        pcm = decode_plain(data, data_len, chans, 1, tag == 7 ? 'm' : 'a', &frames);
    else if (tag == 0x11)
        pcm = ima_decode(data, data_len, chans, align, &frames);
    return pcm_take(s, pcm, frames, rate);
}

static int parse_au(Sound *s, const uint8_t *d, size_t len)
{
    uint32_t off, size, enc, rate, chans, frames = 0;
    int16_t *pcm = NULL;

    if (len < 24)
        return -1;
    off = rd_be32(d + 4);
    size = rd_be32(d + 8);
    enc = rd_be32(d + 12);
    rate = rd_be32(d + 16);
    chans = rd_be32(d + 20);
    if (off >= len || chans < 1 || chans > 8)
        return -1;
    if (size > len - off)
        size = (uint32_t)(len - off);
    if (enc == 1)
        pcm = decode_plain(d + off, size, (int)chans, 1, 'm', &frames);
    else if (enc == 2)
        pcm = decode_plain(d + off, size, (int)chans, 1, 's', &frames);
    else if (enc == 3)
        pcm = decode_plain(d + off, size, (int)chans, 2, 'b', &frames);
    else if (enc == 27)
        pcm = decode_plain(d + off, size, (int)chans, 1, 'a', &frames);
    return pcm_take(s, pcm, frames, rate);
}

/* ---------- дескрипторы ---------- */

static int alloc_slot(void)
{
    int i, best = -1;

    for (i = 0; i < MAX_SOUNDS; i++)
        if (!A.snd[i].used)
            break;
    if (i == MAX_SOUNDS) {
        /* игры часто создают плеер на каждый звук и не закрывают: забираем давно не игравший */
        for (i = 0; i < MAX_SOUNDS; i++)
            if (!A.snd[i].playing && (best < 0 || A.snd[i].last_use < A.snd[best].last_use))
                best = i;
        if (best < 0)
            return -1;
        lock();
        sound_free_slot(best);
        unlock();
        i = best;
    }
    memset(&A.snd[i], 0, sizeof(Sound));
    return i;
}

static int finish_create(int i, int ok)
{
    Sound *s = &A.snd[i];
    static int gen = 0;

    if (!ok) {
        /* непонятный формат (AMR, MP3, MMF) — секунда тишины, чтобы игра шла дальше */
        free(s->ev);
        free(s->pcm);
        s->ev = NULL;
        s->pcm = NULL;
        s->kind = K_SEQ;
        s->ev_count = 0;
        s->dur_us = UNKNOWN_US;
        s->unknown = 1;
    }
    s->gen = ++gen & 0x7FFFFF;
    s->volume = 100;
    s->loops = s->loops_left = 1;
    s->last_use = ++A.use;
    chan_reset(s);
    s->used = 1;        /* последним: до этого микшер слот не видит */
    return (s->gen << 8) | (i + 1);
}

int snd_create(const uint8_t *data, size_t len, int kind)
{
    int i = alloc_slot(), ok;
    Sound *s;

    if (i < 0)
        return 0;
    if (!A.ready)
        setup(RATE_WANT);
    s = &A.snd[i];
    if (!data || !len) {
        s->kind = K_SEQ;        /* пустой тоновый плеер до setSequence */
        return finish_create(i, 1);
    }
    if (kind == SND_OTT)
        ok = !parse_ott(s, data, len);
    else if (len >= 4 && !memcmp(data, "RIFF", 4) && len >= 12 && !memcmp(data + 8, "WAVE", 4))
        ok = !parse_wav(s, data, len);
    else if (len >= 4 && !memcmp(data, ".snd", 4))
        ok = !parse_au(s, data, len);
    else if (len >= 2 && data[0] == 0xFE && data[1] == 0x01)
        ok = !parse_tone(s, data, len);
    else
        ok = !parse_midi(s, data, len);
    return finish_create(i, ok);
}

int snd_create_freq(int hz, int ms)
{
    int i = alloc_slot(), note = hz > 0 ? (int)lround(69 + 12 * log2(hz / 440.0)) : -1;
    uint32_t dur = (uint32_t)(ms > 0 ? ms : 1) * 1000u;
    EvList l = { NULL, 0, 0 };

    if (i < 0)
        return 0;
    if (!A.ready)
        setup(RATE_WANT);
    note = note > 127 ? 127 : note;
    if (ev_add(&l, 0, 0xC0, 80, 0) ||
        (note >= 0 && (ev_add(&l, 0, 0x90, note, 127) || ev_add(&l, dur, 0x80, note, 0)))) {
        free(l.ev);
        return finish_create(i, 0);
    }
    seq_take(&A.snd[i], &l, dur);
    return finish_create(i, 1);
}

void snd_free(int h)
{
    Sound *s = get(h);

    if (!s)
        return;
    lock();
    release_all((int)(s - A.snd), -1, 10);
    sound_free_slot((int)(s - A.snd));
    unlock();
}

int snd_valid(int h)
{
    return get(h) != NULL;
}

void snd_play(int h)
{
    Sound *s = get(h);

    if (!s)
        return;
    ensure_open();
    lock();
    if (!s->playing) {
        if (!A.open && !busy()) {
            A.pump_t0 = platform_ticks_ms();     /* без карты часы звука стартуют сейчас */
            A.pump_done = 0;
        }
        s->playing = 1;
        s->last_use = ++A.use;
    }
    unlock();
}

void snd_pause(int h)
{
    Sound *s = get(h);

    if (!s)
        return;
    lock();
    s->playing = 0;
    if (s->kind == K_SEQ)
        release_all((int)(s - A.snd), -1, 15);
    unlock();
}

int snd_playing(int h)
{
    Sound *s = get(h);

    return s && s->playing;
}

void snd_set_loops(int h, int loops)
{
    Sound *s = get(h);

    if (!s)
        return;
    lock();
    s->loops = s->loops_left = loops < 0 ? -1 : loops == 0 ? 1 : loops;
    unlock();
}

void snd_set_volume(int h, int volume)
{
    Sound *s = get(h);

    if (s)
        s->volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
}

int64_t snd_time_us(int h)
{
    Sound *s = get(h);
    int64_t t;

    if (!s)
        return 0;
    lock();
    t = s->kind == K_PCM ? (int64_t)((s->pcm_pos >> 32) * 1000000u / s->pcm_rate)
                         : (int64_t)(s->pos * 1000000u / (uint64_t)A.rate);
    unlock();
    return t > (int64_t)s->dur_us ? (int64_t)s->dur_us : t;
}

int64_t snd_seek_us(int h, int64_t us)
{
    Sound *s = get(h);
    int si;

    if (!s)
        return 0;
    si = (int)(s - A.snd);
    us = us < 0 ? 0 : us > (int64_t)s->dur_us ? (int64_t)s->dur_us : us;
    lock();
    if (s->kind == K_PCM)
        s->pcm_pos = ((uint64_t)us * s->pcm_rate / 1000000u) << 32;
    else {
        release_all(si, -1, 10);
        rewind_sound(s);
        /* программы и громкости каналов должны быть как в этой точке песни */
        while (s->ev_i < s->ev_count && s->ev[s->ev_i].t < (uint64_t)us)
            seq_event(si, &s->ev[s->ev_i++], 0);
        s->pos = us2smp((uint32_t)us);
    }
    unlock();
    return us;
}

int64_t snd_duration_us(int h)
{
    Sound *s = get(h);

    return !s || s->unknown ? -1 : (int64_t)s->dur_us;
}

void snd_tone(int note, int ms, int volume)
{
    int h = snd_create_freq((int)lround(440.0 * pow(2.0, (note - 69) / 12.0)), ms);
    Sound *s = get(h);

    if (!s)
        return;
    s->temp = 1;
    snd_set_volume(h, volume);
    snd_play(h);
}

/* ---------- audio-render ---------- */

int audio_render_file(const char *in, const char *out, int kind)
{
    FILE *f = bt_fopen(in, "rb"), *o;
    uint8_t *data;
    long len;
    int16_t buf[BLOCK];
    uint32_t bytes = 0, tail = 0, limit;
    int h;

    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) || (len = ftell(f)) <= 0 || fseek(f, 0, SEEK_SET) ||
        !(data = malloc((size_t)len))) {
        fclose(f);
        return -1;
    }
    if (fread(data, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(data);
        return -1;
    }
    fclose(f);
    A.offline = 1;
    setup(RATE_WANT);
    h = snd_create(data, (size_t)len, kind);
    free(data);
    if (!get(h) || get(h)->unknown || !(o = bt_fopen(out, "wb"))) {
        snd_free(h);
        return -1;
    }
    wav_header(o, A.rate, 0);
    snd_play(h);
    limit = (uint32_t)A.rate * 600;    /* не больше 10 минут */
    while (bytes / 2 < limit && tail < (uint32_t)A.rate / 2) {
        mix(buf, BLOCK);
        wav_write(o, buf, BLOCK);
        bytes += BLOCK * 2;
        if (!busy())
            tail += BLOCK;
    }
    wav_header(o, A.rate, bytes);
    fclose(o);
    printf("%s: %.2f с, %d Гц\n", out, bytes / 2.0 / A.rate, A.rate);
    snd_free(h);
    A.offline = 0;
    return 0;
}
