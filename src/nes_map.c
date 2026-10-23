/* Мапперы картриджей NES: как картридж переключает банки программы и графики,
 * а некоторые ещё и считают строки (MMC3, MMC5) или такты (Konami VRC, Sunsoft FME-7). */
#include "nes_internal.h"

#include <stdio.h>
#include <string.h>

#define S (&n->st)

static void irq_on(Nes *n) { S->irq |= IRQ_MAP; }
static void irq_off(Nes *n) { S->irq &= ~IRQ_MAP; }

/* ---------- простые: 0, 2, 3, 7, 11, 15, 34, 66, 71, 79, 87, 113, 140, 180, 232 ---------- */

static void w_uxrom(Nes *n, uint16_t a, uint8_t v)
{
    if (a >= 0x8000)
        nes_prg16(n, 0, v);
}

static void w_cnrom(Nes *n, uint16_t a, uint8_t v)
{
    if (a >= 0x8000)
        nes_chr8(n, v);
}

static void w_axrom(Nes *n, uint16_t a, uint8_t v)
{
    if (a < 0x8000)
        return;
    nes_prg32(n, v & 15);
    nes_mirror(n, (v & 0x10) ? MIR_B : MIR_A);
}

static void w_color_dreams(Nes *n, uint16_t a, uint8_t v)
{
    if (a < 0x8000)
        return;
    nes_prg32(n, v & 3);
    nes_chr8(n, v >> 4);
}

static void w_gxrom(Nes *n, uint16_t a, uint8_t v)
{
    if (a < 0x8000)
        return;
    nes_prg32(n, (v >> 4) & 3);
    nes_chr8(n, v & 3);
}

static void w_bnrom(Nes *n, uint16_t a, uint8_t v)
{
    if (n->chr_size > 0x2000) {     /* NINA-001 */
        if (a == 0x7FFD)
            nes_prg32(n, v & 1);
        else if (a == 0x7FFE)
            nes_chr4(n, 0, v & 15);
        else if (a == 0x7FFF)
            nes_chr4(n, 4, v & 15);
    } else if (a >= 0x8000) {
        nes_prg32(n, v);
    }
}

static void w_camerica(Nes *n, uint16_t a, uint8_t v)
{
    if (a >= 0xC000)
        nes_prg16(n, 0, v);
    else if (a >= 0x9000 && a < 0xA000)
        nes_mirror(n, (v & 0x10) ? MIR_B : MIR_A);      /* Fire Hawk */
}

static void w_nina03(Nes *n, uint16_t a, uint8_t v)
{
    if ((a & 0xE100) == 0x4100) {
        nes_prg32(n, (v >> 3) & 1);
        nes_chr8(n, v & 7);
    }
}

static void w_m87(Nes *n, uint16_t a, uint8_t v)
{
    if (a >= 0x6000 && a < 0x8000)
        nes_chr8(n, ((v & 1) << 1) | ((v & 2) >> 1));
}

static void w_m113(Nes *n, uint16_t a, uint8_t v)
{
    if (a < 0x6000 && (a & 0x4100) == 0x4100) {
        nes_prg32(n, (v >> 3) & 7);
        nes_chr8(n, (v & 7) | ((v >> 3) & 8));
        nes_mirror(n, (v & 0x80) ? MIR_V : MIR_H);
    }
}

static void w_m140(Nes *n, uint16_t a, uint8_t v)
{
    if (a >= 0x6000 && a < 0x8000) {
        nes_prg32(n, (v >> 4) & 3);
        nes_chr8(n, v & 15);
    }
}

static void w_m180(Nes *n, uint16_t a, uint8_t v)
{
    if (a >= 0x8000)
        nes_prg16(n, 2, v & 7);
}

static void w_m232(Nes *n, uint16_t a, uint8_t v)
{
    if (a >= 0x8000 && a < 0xC000)
        S->r[0] = (v >> 3) & 3;
    else if (a >= 0xC000)
        S->r[1] = v & 3;
    else
        return;
    nes_prg16(n, 0, S->r[0] * 4 + S->r[1]);
    nes_prg16(n, 2, S->r[0] * 4 + 3);
}

/* 15: пиратские «100 в 1» с Contra Function 16 — частые на Денди. */
static void w_m15(Nes *n, uint16_t a, uint8_t v)
{
    int bank = v & 0x3F, i;

    if (a < 0x8000)
        return;
    nes_mirror(n, (v & 0x40) ? MIR_H : MIR_V);
    switch (a & 3) {
    case 0:
        nes_prg16(n, 0, bank);
        nes_prg16(n, 2, bank | 1);
        break;
    case 1:
        nes_prg16(n, 0, bank);
        nes_prg16(n, 2, bank | 7);
        break;
    case 2:
        for (i = 0; i < 4; i++)
            nes_prg8(n, i, bank * 2 + (v >> 7));
        break;
    default:
        nes_prg16(n, 0, bank);
        nes_prg16(n, 2, bank);
        break;
    }
}

/* ---------- MMC1 (SxROM): Zelda, Metroid, Mega Man 2, Чип и Дейл ---------- */

static void mmc1_update(Nes *n)
{
    uint8_t ctl = S->r[2], c0 = S->r[3], c1 = S->r[4], prg = S->r[5] & 15;
    int base = n->prg_size > 0x40000 ? (c0 & 0x10) : 0;   /* SUROM: 512 КБ, старший бит — в регистре CHR */
    static const uint8_t mir[4] = { MIR_A, MIR_B, MIR_V, MIR_H };

    nes_mirror(n, mir[ctl & 3]);
    if (ctl & 0x10) {
        nes_chr4(n, 0, c0);
        nes_chr4(n, 4, c1);
    } else {
        nes_chr8(n, c0 >> 1);
    }
    switch ((ctl >> 2) & 3) {
    case 0: case 1:
        nes_prg16(n, 0, base + (prg & 14));
        nes_prg16(n, 2, base + (prg & 14) + 1);
        break;
    case 2:
        nes_prg16(n, 0, base);
        nes_prg16(n, 2, base + prg);
        break;
    default:
        nes_prg16(n, 0, base + prg);
        nes_prg16(n, 2, base + 15);
        break;
    }
}

static void w_mmc1(Nes *n, uint16_t a, uint8_t v)
{
    if (a < 0x8000)
        return;
    /* запись два такта подряд (команды «чтение-изменение-запись») считается одной */
    if (S->cycles - S->last_write < 2) {
        S->last_write = S->cycles;
        return;
    }
    S->last_write = S->cycles;
    if (v & 0x80) {
        S->r[0] = S->r[1] = 0;
        S->r[2] |= 0x0C;
    } else {
        S->r[0] |= (uint8_t)((v & 1) << S->r[1]);
        if (++S->r[1] < 5)
            return;
        S->r[2 + ((a >> 13) & 3)] = S->r[0];
        S->r[0] = S->r[1] = 0;
    }
    mmc1_update(n);
}

/* ---------- MMC3 (TxROM) и Namco 108: Mario 3, Мега Мен 3-6, Черепашки 2-3 ---------- */

static void mmc3_update(Nes *n)
{
    uint8_t sel = S->r[0], *R = &S->r[8];
    int inv = (sel & 0x80) ? 4 : 0;

    nes_chr2(n, 0 ^ inv, R[0] >> 1);
    nes_chr2(n, 2 ^ inv, R[1] >> 1);
    nes_chr1(n, 4 ^ inv, R[2]);
    nes_chr1(n, 5 ^ inv, R[3]);
    nes_chr1(n, 6 ^ inv, R[4]);
    nes_chr1(n, 7 ^ inv, R[5]);
    if (n->mapper == 206) {
        nes_prg8(n, 0, R[6] & 15);
        nes_prg8(n, 1, R[7] & 15);
        nes_prg8(n, 2, -2);
    } else if (sel & 0x40) {
        nes_prg8(n, 0, -2);
        nes_prg8(n, 1, R[7]);
        nes_prg8(n, 2, R[6]);
    } else {
        nes_prg8(n, 0, R[6]);
        nes_prg8(n, 1, R[7]);
        nes_prg8(n, 2, -2);
    }
    nes_prg8(n, 3, -1);
}

static void w_mmc3(Nes *n, uint16_t a, uint8_t v)
{
    if (a < 0x8000)
        return;
    if (n->mapper == 206)
        a &= 0x8001;
    switch (a & 0xE001) {
    case 0x8000: S->r[0] = v; break;
    case 0x8001: S->r[8 + (S->r[0] & 7)] = v; break;
    case 0xA000:
        if (S->mirror != MIR_4)
            nes_mirror(n, (v & 1) ? MIR_H : MIR_V);
        return;
    case 0xC000: S->irq_latch = v; return;
    case 0xC001: S->irq_cnt = 0; S->irq_reload = 1; return;
    case 0xE000: S->irq_en = 0; irq_off(n); return;
    case 0xE001: S->irq_en = 1; return;
    default: return;
    }
    mmc3_update(n);
}

/* Счётчик строк считает подъёмы линии A12 шины видеочипа (смена таблицы узоров
 * фон → спрайты), если перед этим она была внизу хоть пару тактов. */
static void p_mmc3(Nes *n, uint16_t a)
{
    int hi = (a & 0x1000) != 0;

    if (hi && !S->a12) {
        if (S->dots - S->a12_low >= 9) {
            if (S->irq_cnt == 0 || S->irq_reload) {
                S->irq_cnt = S->irq_latch;
                S->irq_reload = 0;
            } else {
                S->irq_cnt--;
            }
            if (S->irq_cnt == 0 && S->irq_en)
                irq_on(n);
        }
    } else if (!hi && S->a12) {
        S->a12_low = S->dots;
    }
    S->a12 = (uint8_t)hi;
}

/* ---------- MMC2 / MMC4: Punch-Out!!, Fire Emblem — защёлки CHR по тайлам $FD/$FE ---------- */

static void mmc2_chr(Nes *n)
{
    nes_chr4(n, 0, S->latch[0] == 0xFD ? S->r[1] : S->r[2]);
    nes_chr4(n, 4, S->latch[1] == 0xFD ? S->r[3] : S->r[4]);
}

static void w_mmc2(Nes *n, uint16_t a, uint8_t v)
{
    switch (a & 0xF000) {
    case 0xA000:
        if (n->mapper == 9)
            nes_prg8(n, 0, v & 15);
        else
            nes_prg16(n, 0, v & 15);
        break;
    case 0xB000: S->r[1] = v & 31; mmc2_chr(n); break;
    case 0xC000: S->r[2] = v & 31; mmc2_chr(n); break;
    case 0xD000: S->r[3] = v & 31; mmc2_chr(n); break;
    case 0xE000: S->r[4] = v & 31; mmc2_chr(n); break;
    case 0xF000: nes_mirror(n, (v & 1) ? MIR_H : MIR_V); break;
    }
}

static void p_mmc2(Nes *n, uint16_t a)
{
    int lo = n->mapper == 9 ? (a == 0x0FD8 || a == 0x0FE8) : ((a & 0xFFF8) == 0x0FD8 || (a & 0xFFF8) == 0x0FE8);

    if (lo) {
        S->latch[0] = (a & 0xFFF0) == 0x0FD0 ? 0xFD : 0xFE;
        mmc2_chr(n);
    } else if ((a & 0xFFF8) == 0x1FD8 || (a & 0xFFF8) == 0x1FE8) {
        S->latch[1] = (a & 0xFFF0) == 0x1FD0 ? 0xFD : 0xFE;
        mmc2_chr(n);
    }
}

/* ---------- Konami VRC2 / VRC4: японские Contra, Черепашки, Gradius II ---------- */

static void vrc_irq_write(Nes *n, int reg, uint8_t v)
{
    switch (reg) {
    case 0: S->irq_latch = (S->irq_latch & 0xF0) | (v & 0x0F); break;
    case 1: S->irq_latch = (S->irq_latch & 0x0F) | (v << 4); break;
    case 2:
        S->r[30] = v & 1;               /* включить снова после подтверждения */
        S->irq_en = (v >> 1) & 1;
        S->irq_mode = (v >> 2) & 1;
        if (S->irq_en) {
            S->irq_cnt = S->irq_latch;
            S->irq_pre = 341;
        }
        irq_off(n);
        break;
    case 3:
        S->irq_en = S->r[30];
        irq_off(n);
        break;
    }
}

/* VRC-таймер: раз в строку (341/3 такта) или каждый такт. */
static void c_vrc(Nes *n)
{
    if (!S->irq_en)
        return;
    if (!S->irq_mode) {
        S->irq_pre -= 3;
        if (S->irq_pre > 0)
            return;
        S->irq_pre += 341;
    }
    if (S->irq_cnt == 0xFF) {
        S->irq_cnt = S->irq_latch;
        irq_on(n);
    } else {
        S->irq_cnt++;
    }
}

static void vrc4_update(Nes *n)
{
    int i;

    if (S->r[2]) {
        nes_prg8(n, 0, -2);
        nes_prg8(n, 2, S->r[0]);
    } else {
        nes_prg8(n, 0, S->r[0]);
        nes_prg8(n, 2, -2);
    }
    nes_prg8(n, 1, S->r[1]);
    nes_prg8(n, 3, -1);
    for (i = 0; i < 8; i++) {
        int b = S->r[8 + i * 2] | S->r[9 + i * 2] << 4;
        nes_chr1(n, i, n->mapper == 22 ? b >> 1 : b);
    }
}

static void w_vrc4(Nes *n, uint16_t a, uint8_t v)
{
    int a0, a1, reg;

    if (a < 0x8000)
        return;
    /* у разных плат регистры сидят на разных линиях адреса */
    switch (n->mapper) {
    case 21: a0 = ((a >> 1) | (a >> 6)) & 1; a1 = ((a >> 2) | (a >> 7)) & 1; break;
    case 22: a0 = (a >> 1) & 1; a1 = a & 1; break;
    case 23: a0 = (a | (a >> 2)) & 1; a1 = ((a >> 1) | (a >> 3)) & 1; break;
    default: a0 = ((a >> 1) | (a >> 3)) & 1; a1 = (a | (a >> 2)) & 1; break;
    }
    reg = a1 << 1 | a0;
    switch (a & 0xF000) {
    case 0x8000: S->r[0] = v & 0x1F; break;
    case 0x9000:
        if (n->mapper == 22 || reg < 2) {
            static const uint8_t mir[4] = { MIR_V, MIR_H, MIR_A, MIR_B };
            nes_mirror(n, n->mapper == 22 ? ((v & 1) ? MIR_H : MIR_V) : mir[v & 3]);
        } else if (reg == 2) {
            S->r[2] = (v >> 1) & 1;
        }
        break;
    case 0xA000: S->r[1] = v & 0x1F; break;
    case 0xB000: case 0xC000: case 0xD000: case 0xE000: {
        int idx = ((a >> 12) - 0xB) * 2 + (reg >> 1);
        if (reg & 1)
            S->r[9 + idx * 2] = v & 0x1F;
        else
            S->r[8 + idx * 2] = v & 0x0F;
        break;
    }
    case 0xF000:
        vrc_irq_write(n, reg, v);
        return;
    }
    vrc4_update(n);
}

/* ---------- Konami VRC6: Akumajou Densetsu, Madara — со своим звуком ---------- */

static void w_vrc6(Nes *n, uint16_t a, uint8_t v)
{
    int reg;

    if (a < 0x8000)
        return;
    if (n->mapper == 26)
        a = (uint16_t)((a & 0xFFFC) | ((a & 1) << 1) | ((a & 2) >> 1));
    reg = a & 3;
    switch (a & 0xF000) {
    case 0x8000: nes_prg16(n, 0, v & 15); break;
    case 0x9000: case 0xA000: {
        NesPulse *p = &S->xsq[(a >> 12) == 9 ? 0 : 1];
        if (reg == 0) {
            p->cvol = v >> 7;
            p->duty = (v >> 4) & 7;
            p->vol = v & 15;
        } else if (reg == 1) {
            p->period = (uint16_t)((p->period & 0xF00) | v);
        } else if (reg == 2) {
            p->period = (uint16_t)((p->period & 0xFF) | ((v & 15) << 8));
            p->on = v >> 7;
            if (!p->on)
                p->pos = 15;
        }
        break;
    }
    case 0xB000:
        if (reg == 0) {
            S->saw_rate = v & 0x3F;
        } else if (reg == 1) {
            S->saw_period = (uint16_t)((S->saw_period & 0xF00) | v);
        } else if (reg == 2) {
            S->saw_period = (uint16_t)((S->saw_period & 0xFF) | ((v & 15) << 8));
            S->saw_on = v >> 7;
            if (!S->saw_on)
                S->saw_acc = S->saw_step = 0;
        } else {
            static const uint8_t mir[4] = { MIR_V, MIR_H, MIR_A, MIR_B };
            nes_mirror(n, mir[(v >> 2) & 3]);
        }
        break;
    case 0xC000: nes_prg8(n, 2, v & 0x1F); break;
    case 0xD000: nes_chr1(n, reg, v); break;
    case 0xE000: nes_chr1(n, 4 + reg, v); break;
    case 0xF000:
        if (reg == 0)
            S->irq_latch = v;
        else if (reg == 1)
            vrc_irq_write(n, 2, v);
        else if (reg == 2)
            vrc_irq_write(n, 3, v);
        break;
    }
}

static void c_vrc6(Nes *n)
{
    int i;

    c_vrc(n);
    for (i = 0; i < 2; i++) {
        NesPulse *p = &S->xsq[i];
        if (!p->on)
            continue;
        if (p->timer == 0) {
            p->timer = p->period;
            p->pos = (uint8_t)((p->pos + 15) & 15);
        } else {
            p->timer--;
        }
    }
    if (S->saw_on) {
        if (S->saw_timer == 0) {
            S->saw_timer = S->saw_period;
            if (++S->saw_step == 14) {
                S->saw_step = 0;
                S->saw_acc = 0;
            } else if (!(S->saw_step & 1)) {
                S->saw_acc = (uint8_t)(S->saw_acc + S->saw_rate);
            }
        } else {
            S->saw_timer--;
        }
    }
}

static int a_vrc6(Nes *n)
{
    int i, out = 0;

    for (i = 0; i < 2; i++) {
        const NesPulse *p = &S->xsq[i];
        if (p->on && (p->cvol || p->pos <= p->duty))
            out += p->vol;
    }
    if (S->saw_on)
        out += S->saw_acc >> 3;
    return out * 140;
}

/* ---------- Sunsoft FME-7: Batman: Return of the Joker, Gimmick! ---------- */

static void w_fme7(Nes *n, uint16_t a, uint8_t v)
{
    if (a >= 0x8000 && a < 0xA000) {
        S->r[0] = v & 15;
        return;
    }
    if (a < 0xA000 || a >= 0xC000)
        return;
    switch (S->r[0]) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        nes_chr1(n, S->r[0], v);
        break;
    case 8:
        if (v & 0x40) {
            S->wram_rom = -1;
            S->wram_off = !(v & 0x80);
        } else {
            S->wram_rom = ((v & 0x3F) % (n->prg_size / 0x2000)) * 0x2000;
        }
        break;
    case 9: case 10: case 11:
        nes_prg8(n, S->r[0] - 9, v & 0x3F);
        break;
    case 12: {
        static const uint8_t mir[4] = { MIR_V, MIR_H, MIR_A, MIR_B };
        nes_mirror(n, mir[v & 3]);
        break;
    }
    case 13:
        S->irq_en = v & 1;
        S->irq_mode = (v >> 7) & 1;
        irq_off(n);
        break;
    case 14: S->irq_cnt = (S->irq_cnt & 0xFF00) | v; break;
    case 15: S->irq_cnt = (S->irq_cnt & 0x00FF) | v << 8; break;
    }
}

static void c_fme7(Nes *n)
{
    if (!S->irq_mode)
        return;
    S->irq_cnt = (S->irq_cnt - 1) & 0xFFFF;
    if (S->irq_cnt == 0xFFFF && S->irq_en)
        irq_on(n);
}

/* ---------- MMC5: Castlevania III, Uncharted Waters ---------- */

static void mmc5_prg(Nes *n)
{
    uint8_t *P = &S->r[6];          /* $5114-$5117 */

    switch (S->r[0] & 3) {
    case 0:
        nes_prg32(n, (P[3] & 0x7F) >> 2);
        break;
    case 1:
        nes_prg16(n, 0, (P[1] & 0x7F) >> 1);
        nes_prg16(n, 2, (P[3] & 0x7F) >> 1);
        break;
    case 2:
        nes_prg16(n, 0, (P[1] & 0x7F) >> 1);
        nes_prg8(n, 2, P[2] & 0x7F);
        nes_prg8(n, 3, P[3] & 0x7F);
        break;
    default:
        nes_prg8(n, 0, P[0] & 0x7F);
        nes_prg8(n, 1, P[1] & 0x7F);
        nes_prg8(n, 2, P[2] & 0x7F);
        nes_prg8(n, 3, P[3] & 0x7F);
        break;
    }
}

/* Набор CHR: A ($5120-$5127) — спрайты, B ($5128-$512B) — фон при спрайтах 8×16. */
static void mmc5_chr(Nes *n, int set_b)
{
    uint8_t *C = &S->r[10];
    int i;

    if (set_b) {
        switch (S->r[1] & 3) {
        case 0: nes_chr8(n, C[11]); break;
        case 1: nes_chr4(n, 0, C[11]); nes_chr4(n, 4, C[11]); break;
        case 2: nes_chr2(n, 0, C[9]); nes_chr2(n, 2, C[11]); nes_chr2(n, 4, C[9]); nes_chr2(n, 6, C[11]); break;
        default:
            for (i = 0; i < 8; i++)
                nes_chr1(n, i, C[8 + (i & 3)]);
            break;
        }
        return;
    }
    switch (S->r[1] & 3) {
    case 0: nes_chr8(n, C[7]); break;
    case 1: nes_chr4(n, 0, C[3]); nes_chr4(n, 4, C[7]); break;
    case 2: nes_chr2(n, 0, C[1]); nes_chr2(n, 2, C[3]); nes_chr2(n, 4, C[5]); nes_chr2(n, 6, C[7]); break;
    default:
        for (i = 0; i < 8; i++)
            nes_chr1(n, i, C[i]);
        break;
    }
}

static void s_mmc5(Nes *n, int on)
{
    if (S->ctrl & 0x20)
        mmc5_chr(n, !on);   /* спрайты 8×16: на время спрайтов — набор A, потом снова B */
}

static void mmc5_nt(Nes *n)
{
    int i;

    for (i = 0; i < 4; i++)
        S->nt[i] = (uint8_t)(((S->r[3] >> (i * 2)) & 3) == 2 ? 4 : ((S->r[3] >> (i * 2)) & 3) == 3 ? 5
                                                                 : ((S->r[3] >> (i * 2)) & 1));
}

static uint8_t nt_mmc5(Nes *n, int page, int off, int write, uint8_t v)
{
    if (page == 4) {
        if (write) {
            if ((S->r[2] & 3) < 2)
                S->exram[off] = v;
            return 0;
        }
        return (S->r[2] & 3) < 2 ? S->exram[off] : 0;
    }
    if (off < 0x3C0)
        return S->r[4];
    return (uint8_t)((S->r[5] & 3) * 0x55);
}

static void w_mmc5(Nes *n, uint16_t a, uint8_t v)
{
    if (a >= 0x5C00 && a < 0x6000) {
        if ((S->r[2] & 3) != 3)
            S->exram[a - 0x5C00] = v;
        return;
    }
    switch (a) {
    case 0x5100: S->r[0] = v; mmc5_prg(n); break;
    case 0x5101: S->r[1] = v; mmc5_chr(n, 0); break;
    case 0x5104: S->r[2] = v; break;
    case 0x5105: S->r[3] = v; mmc5_nt(n); break;
    case 0x5106: S->r[4] = v; break;
    case 0x5107: S->r[5] = v; break;
    case 0x5114: case 0x5115: case 0x5116: case 0x5117:
        S->r[6 + (a - 0x5114)] = v;
        mmc5_prg(n);
        break;
    case 0x5203: S->r[22] = v; break;
    case 0x5204: S->r[23] = v >> 7; if (S->r[23] && S->r[24]) irq_on(n); else irq_off(n); break;
    case 0x5205: S->r[25] = v; break;
    case 0x5206: S->r[26] = v; break;
    default:
        if (a >= 0x5120 && a <= 0x512B) {
            S->r[10 + (a - 0x5120)] = v;
            mmc5_chr(n, a >= 0x5128 && (S->ctrl & 0x20));
        }
        break;
    }
}

static int r_mmc5(Nes *n, uint16_t a, uint8_t *v)
{
    if (a >= 0x5C00 && a < 0x6000) {
        *v = (S->r[2] & 3) >= 2 ? S->exram[a - 0x5C00] : S->bus;
        return 1;
    }
    switch (a) {
    case 0x5204:
        *v = (uint8_t)((S->r[24] ? 0x80 : 0) | (S->in_frame ? 0x40 : 0));
        S->r[24] = 0;
        irq_off(n);
        return 1;
    case 0x5205: *v = (uint8_t)(S->r[25] * S->r[26]); return 1;
    case 0x5206: *v = (uint8_t)((S->r[25] * S->r[26]) >> 8); return 1;
    }
    return 0;
}

/* Счётчик строк MMC5: со строки 0 кадра считает строки, на совпадении — IRQ. */
static void l_mmc5(Nes *n)
{
    if (S->line >= 240 || !(S->mask & 0x18)) {
        S->in_frame = 0;
        return;
    }
    if (!S->in_frame) {
        S->in_frame = 1;
        S->sl_cnt = 0;
    } else if (++S->sl_cnt == S->r[22]) {
        S->r[24] = 1;
        if (S->r[23])
            irq_on(n);
    }
}

/* ---------- выбор маппера ---------- */

void nes_map_reset(Nes *n)
{
    NesState *s = S;

    memset(s->r, 0, sizeof(s->r));
    s->irq_cnt = s->irq_latch = s->irq_en = s->irq_reload = s->irq_mode = 0;
    s->wram_rom = -1;
    s->wram_off = 0;
    nes_prg32(n, 0);
    nes_prg16(n, 2, -1);
    nes_chr8(n, 0);
    switch (n->mapper) {
    case 1:
        s->r[2] = 0x0C;
        mmc1_update(n);
        break;
    case 4: case 206:
        s->r[14] = 0;
        s->r[15] = 1;
        mmc3_update(n);
        break;
    case 5:
        s->r[0] = 3;
        s->r[6] = s->r[7] = s->r[8] = s->r[9] = 0xFF;
        mmc5_prg(n);
        break;
    case 7: case 11: case 66: case 34: case 79: case 140: case 113:
        nes_prg32(n, 0);
        break;
    case 9:
        nes_prg8(n, 0, 0);
        nes_prg8(n, 1, -3);
        nes_prg8(n, 2, -2);
        nes_prg8(n, 3, -1);
        s->latch[0] = s->latch[1] = 0xFE;
        break;
    case 10:
        s->latch[0] = s->latch[1] = 0xFE;
        break;
    case 15:
        nes_prg32(n, 0);
        break;
    case 21: case 22: case 23: case 25:
        vrc4_update(n);
        break;
    case 24: case 26:
        nes_prg8(n, 2, -2);
        s->xsq[0].pos = s->xsq[1].pos = 15;
        break;
    case 69:
        nes_prg8(n, 3, -1);
        break;
    case 180:
        nes_prg16(n, 0, 0);
        nes_prg16(n, 2, 0);
        break;
    case 232:
        nes_prg16(n, 0, 0);
        nes_prg16(n, 2, 3);
        break;
    }
}

int nes_map_init(Nes *n, char *err, size_t errlen)
{
    switch (n->mapper) {
    case 0: break;
    case 1: n->m_write = w_mmc1; break;
    case 2: n->m_write = w_uxrom; break;
    case 3: n->m_write = w_cnrom; break;
    case 4: n->m_write = w_mmc3; n->m_ppu = p_mmc3; break;
    case 206: n->m_write = w_mmc3; break;
    case 5:
        n->m_write = w_mmc5;
        n->m_read = r_mmc5;
        n->m_nt = nt_mmc5;
        n->m_line = l_mmc5;
        n->m_spr = s_mmc5;
        break;
    case 7: n->m_write = w_axrom; break;
    case 9: case 10: n->m_write = w_mmc2; n->m_ppu = p_mmc2; break;
    case 11: n->m_write = w_color_dreams; break;
    case 15: n->m_write = w_m15; break;
    case 21: case 22: case 23: case 25: n->m_write = w_vrc4; n->m_cpu = c_vrc; break;
    case 24: case 26: n->m_write = w_vrc6; n->m_cpu = c_vrc6; n->m_audio = a_vrc6; break;
    case 34: n->m_write = w_bnrom; break;
    case 66: n->m_write = w_gxrom; break;
    case 69: n->m_write = w_fme7; n->m_cpu = c_fme7; break;
    case 71: n->m_write = w_camerica; break;
    case 79: n->m_write = w_nina03; break;
    case 87: n->m_write = w_m87; break;
    case 113: n->m_write = w_m113; break;
    case 140: n->m_write = w_m140; break;
    case 180: n->m_write = w_m180; break;
    case 232: n->m_write = w_m232; break;
    default:
        snprintf(err, errlen, "маппер %d пока не поддерживается", n->mapper);
        return -1;
    }
    return 0;
}
