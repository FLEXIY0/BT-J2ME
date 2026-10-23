/* Ядро NES: процессор 6502, видеочип 2C02 и звук 2A03. Процессор шагает по тактам шины:
 * каждое чтение и запись — один такт, за который видеочип проходит 3 точки (PAL — 3,2),
 * поэтому прерывания, вспышка спрайта 0 и счётчики строк у мапперов срабатывают вовремя. */
#include "nes_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FC = 1, FZ = 2, FI = 4, FD = 8, FB = 16, FU = 32, FV = 64, FN = 128 };

static uint8_t rd(Nes *n, uint16_t a);
static void    wr(Nes *n, uint16_t a, uint8_t v);
static void    ppu_step(Nes *n);
static void    apu_step(Nes *n);

/* ---------- такт ---------- */

static void poll(NesState *s)
{
    s->nmi_need_prev = s->nmi_need;
    if (s->nmi_line && !s->nmi_prev)
        s->nmi_need = 1;
    s->nmi_prev = s->nmi_line;
    s->irq_run_prev = s->irq_run;
    s->irq_run = s->irq && !(s->p & FI);
}

static void tick(Nes *n)
{
    NesState *s = &n->st;
    int dots = 3;

    s->cycles++;
    if (n->region == NES_PAL && ++s->frac == 5) {   /* 16 точек на 5 тактов */
        s->frac = 0;
        dots = 4;
    }
    while (dots--)
        ppu_step(n);
    apu_step(n);
    if (n->m_cpu)
        n->m_cpu(n);
}

static void cyc(Nes *n)         /* такт без обращения к шине (DMA, ожидание) */
{
    tick(n);
    poll(&n->st);
}

/* ---------- палитра ---------- */

static const uint32_t base_pal[64] = {
    0x7C7C7C, 0x0000FC, 0x0000BC, 0x4428BC, 0x940084, 0xA80020, 0xA81000, 0x881400,
    0x503000, 0x007800, 0x006800, 0x005800, 0x004058, 0x000000, 0x000000, 0x000000,
    0xBCBCBC, 0x0078F8, 0x0058F8, 0x6844FC, 0xD800CC, 0xE40058, 0xF83800, 0xE45C10,
    0xAC7C00, 0x00B800, 0x00A800, 0x00A844, 0x008888, 0x000000, 0x000000, 0x000000,
    0xF8F8F8, 0x3CBCFC, 0x6888FC, 0x9878F8, 0xF878F8, 0xF85898, 0xF87858, 0xFCA044,
    0xF8B800, 0xB8F818, 0x58D854, 0x58F898, 0x00E8D8, 0x787878, 0x000000, 0x000000,
    0xFCFCFC, 0xA4E4FC, 0xB8B8F8, 0xD8B8F8, 0xF8B8F8, 0xF8A4C0, 0xF0D0B0, 0xFCE0A8,
    0xF8D878, 0xD8F878, 0xB8F8B8, 0xB8F8D8, 0x00FCFC, 0xF8D8F8, 0x000000, 0x000000
};

/* 8 вариантов «подсветки» (биты 5-7 PPUMASK): неподсвеченные каналы приглушаются. */
static void make_palette(Nes *n)
{
    int e, i;

    for (e = 0; e < 8; e++)
        for (i = 0; i < 64; i++) {
            uint32_t c = base_pal[i];
            int r = (int)(c >> 16 & 255), g = (int)(c >> 8 & 255), b = (int)(c & 255);
            int er = n->region == NES_NTSC ? e & 1 : e & 2, eg = n->region == NES_NTSC ? e & 2 : e & 1, eb = e & 4;
            if (e) {
                if (!er) r = r * 13 / 16;
                if (!eg) g = g * 13 / 16;
                if (!eb) b = b * 13 / 16;
            }
            n->palette[e * 64 + i] = 0xFF000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
        }
}

/* ---------- банки картриджа (для мапперов) ---------- */

void nes_prg8(Nes *n, int slot, int bank)
{
    int cnt = n->prg_size / 0x2000;

    if (bank < 0)
        bank += cnt;
    n->st.prg_off[slot & 3] = (bank % cnt) * 0x2000;
}

void nes_prg16(Nes *n, int slot, int bank)
{
    int cnt = n->prg_size / 0x4000;

    if (cnt < 1)
        cnt = 1;
    if (bank < 0)
        bank += cnt;
    bank %= cnt;
    n->st.prg_off[slot] = (bank * 0x4000) % n->prg_size;
    n->st.prg_off[slot + 1] = (bank * 0x4000 + 0x2000) % n->prg_size;
}

void nes_prg32(Nes *n, int bank)
{
    int i;

    for (i = 0; i < 4; i++)
        n->st.prg_off[i] = (bank * 0x8000 + i * 0x2000) % n->prg_size;
}

void nes_chr1(Nes *n, int slot, int bank)
{
    int cnt = n->chr_size / 0x400;

    if (bank < 0)
        bank += cnt;
    n->st.chr_off[slot & 7] = (bank % cnt) * 0x400;
}

void nes_chr2(Nes *n, int slot, int bank)
{
    nes_chr1(n, slot, bank * 2);
    nes_chr1(n, slot + 1, bank * 2 + 1);
}

void nes_chr4(Nes *n, int slot, int bank)
{
    int i;

    for (i = 0; i < 4; i++)
        nes_chr1(n, slot + i, bank * 4 + i);
}

void nes_chr8(Nes *n, int bank)
{
    int i;

    for (i = 0; i < 8; i++)
        nes_chr1(n, i, bank * 8 + i);
}

void nes_mirror(Nes *n, int m)
{
    static const uint8_t map[5][4] = { { 0, 0, 1, 1 }, { 0, 1, 0, 1 }, { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { 0, 1, 2, 3 } };

    n->st.mirror = (uint8_t)m;
    memcpy(n->st.nt, map[m], 4);
}

/* ---------- шина видеочипа ---------- */

static uint8_t nt_read(Nes *n, uint16_t a)
{
    int page = n->st.nt[(a >> 10) & 3];

    if (page < 4)
        return n->st.vram[page * 0x400 + (a & 0x3FF)];
    return n->m_nt ? n->m_nt(n, page, a & 0x3FF, 0, 0) : 0;
}

static uint8_t ppu_rd(Nes *n, uint16_t a)
{
    uint8_t v;

    a &= 0x3FFF;
    if (a < 0x2000)
        v = n->chr[n->st.chr_off[a >> 10] + (a & 0x3FF)];
    else
        v = nt_read(n, a);
    if (n->m_ppu)
        n->m_ppu(n, a);
    return v;
}

static uint8_t *pal_cell(NesState *s, uint16_t a)
{
    a &= 0x1F;
    if ((a & 0x13) == 0x10)
        a &= 0x0F;              /* $3F10/$14/$18/$1C — зеркала фона */
    return &s->pal[a];
}

static void ppu_wr(Nes *n, uint16_t a, uint8_t v)
{
    NesState *s = &n->st;

    a &= 0x3FFF;
    if (a < 0x2000) {
        if (n->chr_is_ram)
            s->chr_ram[(s->chr_off[a >> 10] + (a & 0x3FF)) % sizeof(s->chr_ram)] = v;
    } else if (a < 0x3F00) {
        int page = s->nt[(a >> 10) & 3];
        if (page < 4)
            s->vram[page * 0x400 + (a & 0x3FF)] = v;
        else if (n->m_nt)
            n->m_nt(n, page, a & 0x3FF, 1, v);
    } else {
        *pal_cell(s, a) = v & 0x3F;
    }
}

static void nmi_update(NesState *s)
{
    s->nmi_line = (s->ctrl & 0x80) && (s->status & 0x80);
}

/* ---------- видеочип ---------- */

static void inc_x(NesState *s)
{
    if ((s->v & 0x1F) == 31) {
        s->v &= ~0x1F;
        s->v ^= 0x400;
    } else {
        s->v++;
    }
}

static void inc_y(NesState *s)
{
    if ((s->v & 0x7000) != 0x7000) {
        s->v += 0x1000;
    } else {
        int y = (s->v & 0x3E0) >> 5;
        s->v &= ~0x7000;
        if (y == 29) {
            y = 0;
            s->v ^= 0x800;
        } else if (y == 31) {
            y = 0;
        } else {
            y++;
        }
        s->v = (uint16_t)((s->v & ~0x3E0) | (y << 5));
    }
}

static void load_bg(NesState *s)
{
    s->bg_lo = (s->bg_lo & 0xFF00) | s->lo_byte;
    s->bg_hi = (s->bg_hi & 0xFF00) | s->hi_byte;
    s->at_lo = (s->at_lo & 0xFF00) | ((s->at_byte & 1) ? 0xFF : 0);
    s->at_hi = (s->at_hi & 0xFF00) | ((s->at_byte & 2) ? 0xFF : 0);
}

static uint8_t flip8(uint8_t b)
{
    b = (uint8_t)((b & 0xF0) >> 4 | (b & 0x0F) << 4);
    b = (uint8_t)((b & 0xCC) >> 2 | (b & 0x33) << 2);
    return (uint8_t)((b & 0xAA) >> 1 | (b & 0x55) << 1);
}

/* Спрайты следующей строки: отбор по OAM и чтение их узоров (пустые места читают тайл $FF —
 * как железо, для счётчика строк MMC3 это важно). */
static void sprites(Nes *n, int eval)
{
    NesState *s = &n->st;
    int h = s->ctrl & 0x20 ? 16 : 8, cnt = 0, i;
    uint8_t sel[8] = { 0 };

    s->spr0_next = 0;
    for (i = 0; eval && i < 64; i++) {
        int row = s->line - s->oam[i * 4];
        if (row < 0 || row >= h)
            continue;
        if (cnt == 8) {
            s->status |= 0x20;  /* больше восьми на строке */
            break;
        }
        if (i == 0)
            s->spr0_next = 1;
        sel[cnt++] = (uint8_t)i;
    }
    n->spr_fetch = 1;
    if (n->m_spr)
        n->m_spr(n, 1);
    for (i = 0; i < 8; i++) {
        const uint8_t *o = &s->oam[sel[i] * 4];
        int tile = i < cnt ? o[1] : 0xFF, attr = i < cnt ? o[2] : 0, row = i < cnt ? s->line - o[0] : 0;
        uint16_t addr;
        uint8_t lo, hi;
        if (attr & 0x80)
            row = h - 1 - row;
        if (h == 16)
            addr = (uint16_t)(((tile & 1) << 12) | ((tile & 0xFE) << 4) | ((row & 8) << 1) | (row & 7));
        else
            addr = (uint16_t)(((s->ctrl & 0x08) << 9) | (tile << 4) | (row & 7));
        lo = ppu_rd(n, addr);
        hi = ppu_rd(n, (uint16_t)(addr + 8));
        if (i < cnt) {
            if (attr & 0x40) {
                lo = flip8(lo);
                hi = flip8(hi);
            }
            s->spr_x[i] = o[3];
            s->spr_a[i] = (uint8_t)attr;
            s->spr_lo[i] = lo;
            s->spr_hi[i] = hi;
        }
    }
    n->spr_fetch = 0;
    if (n->m_spr)
        n->m_spr(n, 0);
    s->spr_n = (uint8_t)cnt;
    s->spr0 = s->spr0_next;
}

static void pixel(Nes *n)
{
    NesState *s = &n->st;
    int x = s->dot - 1, i;
    uint8_t bp = 0, bpal = 0, sp = 0, spal = 0, front = 0, c;

    if (!(s->mask & 0x18)) {
        c = (s->v & 0x3F00) == 0x3F00 ? *pal_cell(s, s->v) : s->pal[0];
    } else {
        if ((s->mask & 0x08) && (x >= 8 || (s->mask & 0x02))) {
            uint16_t bit = (uint16_t)(0x8000 >> s->xfine);
            bp = (uint8_t)(((s->bg_lo & bit) ? 1 : 0) | ((s->bg_hi & bit) ? 2 : 0));
            bpal = (uint8_t)(((s->at_lo & bit) ? 1 : 0) | ((s->at_hi & bit) ? 2 : 0));
        }
        if ((s->mask & 0x10) && (x >= 8 || (s->mask & 0x04))) {
            for (i = 0; i < s->spr_n; i++) {
                int dx = x - s->spr_x[i];
                if (dx < 0 || dx > 7)
                    continue;
                sp = (uint8_t)(((s->spr_lo[i] >> (7 - dx)) & 1) | (((s->spr_hi[i] >> (7 - dx)) & 1) << 1));
                if (!sp)
                    continue;
                spal = (uint8_t)((s->spr_a[i] & 3) + 4);
                front = !(s->spr_a[i] & 0x20);
                if (i == 0 && s->spr0 && bp && x != 255)
                    s->status |= 0x40;      /* вспышка спрайта 0 */
                break;
            }
        }
        if (!bp && !sp)
            c = s->pal[0];
        else if (sp && (!bp || front))
            c = s->pal[spal * 4 + sp];
        else
            c = s->pal[bpal * 4 + bp];
    }
    c &= (s->mask & 1) ? 0x30 : 0x3F;
    n->fb[s->line * 256 + x] = n->palette[((s->mask >> 5) << 6) | c];
}

static void ppu_step(Nes *n)
{
    NesState *s = &n->st;
    int render = s->mask & 0x18, dot = s->dot, pre = s->line == n->pre_line;

    if (s->line < 240 || pre) {
        if (render) {
            if ((dot >= 2 && dot <= 257) || (dot >= 321 && dot <= 337)) {
                s->bg_lo <<= 1;
                s->bg_hi <<= 1;
                s->at_lo <<= 1;
                s->at_hi <<= 1;
                switch ((dot - 1) & 7) {
                case 0:
                    load_bg(s);
                    s->nt_byte = ppu_rd(n, (uint16_t)(0x2000 | (s->v & 0x0FFF)));
                    break;
                case 2: {
                    uint8_t at = ppu_rd(n, (uint16_t)(0x23C0 | (s->v & 0x0C00) | ((s->v >> 4) & 0x38) | ((s->v >> 2) & 7)));
                    if (s->v & 0x40)
                        at >>= 4;
                    if (s->v & 0x02)
                        at >>= 2;
                    s->at_byte = at & 3;
                    break;
                }
                case 4:
                    s->lo_byte = ppu_rd(n, (uint16_t)(((s->ctrl & 0x10) << 8) | (s->nt_byte << 4) | (s->v >> 12)));
                    break;
                case 6:
                    s->hi_byte = ppu_rd(n, (uint16_t)(((s->ctrl & 0x10) << 8) | (s->nt_byte << 4) | (s->v >> 12) | 8));
                    break;
                case 7:
                    inc_x(s);
                    break;
                }
            }
            if (dot == 256)
                inc_y(s);
            if (dot == 257) {
                load_bg(s);
                s->v = (uint16_t)((s->v & ~0x041F) | (s->t & 0x041F));
                sprites(n, !pre);
            }
            if (pre && dot >= 280 && dot <= 304)
                s->v = (uint16_t)((s->v & ~0x7BE0) | (s->t & 0x7BE0));
            if (dot == 338 || dot == 340)
                ppu_rd(n, (uint16_t)(0x2000 | (s->v & 0x0FFF)));
        } else if (dot == 257) {
            s->spr_n = 0;
        }
        if (pre && dot == 1) {
            s->status &= 0x1F;
            nmi_update(s);
        }
        if (dot == 1 && n->m_line)
            n->m_line(n);
        if (s->line < 240 && dot >= 1 && dot <= 256)
            pixel(n);
    } else if (s->line == n->vbl_line && dot == 1) {
        if (!s->vbl_skip)
            s->status |= 0x80;
        s->vbl_skip = 0;
        nmi_update(s);
        s->frame_done = 1;
    } else if (s->line == 240 && dot == 1 && n->m_line) {
        n->m_line(n);
    }

    s->dots++;
    if (++s->dot > 340 || (pre && s->dot == 340 && s->odd && render && n->region == NES_NTSC)) {
        s->dot = 0;
        if (++s->line > n->pre_line) {
            s->line = 0;
            s->odd ^= 1;
        }
    }
}

static uint8_t ppu_reg_read(Nes *n, int r)
{
    NesState *s = &n->st;
    uint8_t v = s->pbus;

    switch (r) {
    case 2:
        v = (uint8_t)((s->status & 0xE0) | (s->pbus & 0x1F));
        if (s->line == n->vbl_line && s->dot == 1) {
            s->vbl_skip = 1;    /* прочли за миг до установки — флага и NMI в этом кадре не будет */
            v &= 0x7F;
        }
        s->status &= 0x7F;
        s->w = 0;
        nmi_update(s);
        break;
    case 4:
        v = s->oam[s->oam_addr];
        break;
    case 7:
        if ((s->v & 0x3FFF) < 0x3F00) {
            v = s->rbuf;
            s->rbuf = ppu_rd(n, s->v);
        } else {
            v = (uint8_t)((*pal_cell(s, s->v) & ((s->mask & 1) ? 0x30 : 0x3F)) | (s->pbus & 0xC0));
            s->rbuf = ppu_rd(n, (uint16_t)(s->v - 0x1000));
        }
        s->v = (uint16_t)((s->v + ((s->ctrl & 4) ? 32 : 1)) & 0x7FFF);
        if (n->m_ppu)
            n->m_ppu(n, s->v & 0x3FFF);
        break;
    }
    s->pbus = v;
    return v;
}

static void ppu_reg_write(Nes *n, int r, uint8_t v)
{
    NesState *s = &n->st;

    s->pbus = v;
    switch (r) {
    case 0:
        s->ctrl = v;
        s->t = (uint16_t)((s->t & 0xF3FF) | ((v & 3) << 10));
        nmi_update(s);
        break;
    case 1:
        s->mask = v;
        break;
    case 3:
        s->oam_addr = v;
        break;
    case 4:
        if ((s->oam_addr & 3) == 2)
            v &= 0xE3;
        s->oam[s->oam_addr++] = v;
        break;
    case 5:
        if (!s->w) {
            s->t = (uint16_t)((s->t & ~0x1F) | (v >> 3));
            s->xfine = v & 7;
        } else {
            s->t = (uint16_t)((s->t & 0x8C1F) | ((v & 7) << 12) | ((v & 0xF8) << 2));
        }
        s->w ^= 1;
        break;
    case 6:
        if (!s->w) {
            s->t = (uint16_t)((s->t & 0x00FF) | ((v & 0x3F) << 8));
        } else {
            s->t = (uint16_t)((s->t & 0xFF00) | v);
            s->v = s->t;
            if (n->m_ppu)
                n->m_ppu(n, s->v & 0x3FFF);
        }
        s->w ^= 1;
        break;
    case 7:
        ppu_wr(n, s->v, v);
        s->v = (uint16_t)((s->v + ((s->ctrl & 4) ? 32 : 1)) & 0x7FFF);
        if (n->m_ppu)
            n->m_ppu(n, s->v & 0x3FFF);
        break;
    }
}

/* ---------- звук ---------- */

static const uint8_t len_table[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};
static const uint8_t duty_tab[4][8] = {
    { 0, 1, 0, 0, 0, 0, 0, 0 }, { 0, 1, 1, 0, 0, 0, 0, 0 }, { 0, 1, 1, 1, 1, 0, 0, 0 }, { 1, 0, 0, 1, 1, 1, 1, 1 }
};
static const uint8_t tri_seq[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};
static const uint16_t noise_ntsc[16] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
static const uint16_t noise_pal[16] = { 4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778 };
static const uint16_t dmc_ntsc[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54 };
static const uint16_t dmc_pal[16] = { 398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50 };
static float pulse_mix[31], tnd_mix[203];

static int sweep_target(const NesPulse *p)
{
    int c = p->period >> p->sw_shift;

    return p->sw_neg ? p->period - c - (p->first ? 1 : 0) : p->period + c;
}

static int pulse_out(const NesPulse *p)
{
    if (!p->len || p->period < 8 || sweep_target(p) > 0x7FF || !duty_tab[p->duty][p->pos])
        return 0;
    return p->cvol ? p->vol : p->env_vol;
}

static void pulse_write(NesPulse *p, int reg, uint8_t v)
{
    switch (reg) {
    case 0:
        p->duty = v >> 6;
        p->halt = (v >> 5) & 1;
        p->cvol = (v >> 4) & 1;
        p->vol = v & 15;
        break;
    case 1:
        p->sw_en = v >> 7;
        p->sw_per = (v >> 4) & 7;
        p->sw_neg = (v >> 3) & 1;
        p->sw_shift = v & 7;
        p->sw_reload = 1;
        break;
    case 2:
        p->period = (uint16_t)((p->period & 0x700) | v);
        break;
    case 3:
        p->period = (uint16_t)((p->period & 0xFF) | ((v & 7) << 8));
        if (p->on)
            p->len = len_table[v >> 3];
        p->pos = 0;
        p->env_start = 1;
        break;
    }
}

static void env_clock(uint8_t *start, uint8_t *div, uint8_t *vol, uint8_t period, uint8_t loop)
{
    if (*start) {
        *start = 0;
        *vol = 15;
        *div = period;
    } else if (*div) {
        (*div)--;
    } else {
        *div = period;
        if (*vol)
            (*vol)--;
        else if (loop)
            *vol = 15;
    }
}

static void quarter(NesState *s)
{
    int i;

    for (i = 0; i < 2; i++)
        env_clock(&s->sq[i].env_start, &s->sq[i].env_div, &s->sq[i].env_vol, s->sq[i].vol, s->sq[i].halt);
    env_clock(&s->noi_env_start, &s->noi_env_div, &s->noi_env_vol, s->noi_vol, s->noi_halt);
    if (s->tri_lin_reload)
        s->tri_lin = s->tri_lin_load;
    else if (s->tri_lin)
        s->tri_lin--;
    if (!s->tri_halt)
        s->tri_lin_reload = 0;
}

static void half(NesState *s)
{
    int i;

    for (i = 0; i < 2; i++) {
        NesPulse *p = &s->sq[i];
        int t = sweep_target(p);
        if (p->len && !p->halt)
            p->len--;
        if (!p->sw_div && p->sw_en && p->sw_shift && p->period >= 8 && t <= 0x7FF)
            p->period = (uint16_t)t;
        if (!p->sw_div || p->sw_reload) {
            p->sw_div = p->sw_per;
            p->sw_reload = 0;
        } else {
            p->sw_div--;
        }
    }
    if (s->tri_len && !s->tri_halt)
        s->tri_len--;
    if (s->noi_len && !s->noi_halt)
        s->noi_len--;
}

static void dmc_restart(NesState *s)
{
    s->dmc_addr = s->dmc_start;
    s->dmc_left = s->dmc_len;
}

/* DMA звука: процессор отдаёт шину на 4 такта, чтобы прочесть следующий байт сэмпла. */
static void dmc_fetch(Nes *n)
{
    NesState *s = &n->st;
    uint8_t v;

    s->dmc_need = 0;
    cyc(n);
    cyc(n);
    cyc(n);
    v = rd(n, s->dmc_addr);
    s->dmc_buf = v;
    s->dmc_buf_full = 1;
    s->dmc_addr = s->dmc_addr == 0xFFFF ? 0x8000 : (uint16_t)(s->dmc_addr + 1);
    if (--s->dmc_left == 0) {
        if (s->dmc_loop)
            dmc_restart(s);
        else if (s->dmc_irq_en)
            s->irq |= IRQ_DMC;
    }
}

static void apu_write(Nes *n, uint16_t a, uint8_t v)
{
    NesState *s = &n->st;

    switch (a) {
    case 0x4000: case 0x4001: case 0x4002: case 0x4003:
        pulse_write(&s->sq[0], a & 3, v);
        break;
    case 0x4004: case 0x4005: case 0x4006: case 0x4007:
        pulse_write(&s->sq[1], a & 3, v);
        break;
    case 0x4008:
        s->tri_halt = v >> 7;
        s->tri_lin_load = v & 0x7F;
        break;
    case 0x400A:
        s->tri_period = (uint16_t)((s->tri_period & 0x700) | v);
        break;
    case 0x400B:
        s->tri_period = (uint16_t)((s->tri_period & 0xFF) | ((v & 7) << 8));
        if (s->tri_on)
            s->tri_len = len_table[v >> 3];
        s->tri_lin_reload = 1;
        break;
    case 0x400C:
        s->noi_halt = (v >> 5) & 1;
        s->noi_cvol = (v >> 4) & 1;
        s->noi_vol = v & 15;
        break;
    case 0x400E:
        s->noi_mode = v >> 7;
        s->noi_period = (n->region == NES_PAL ? noise_pal : noise_ntsc)[v & 15];
        break;
    case 0x400F:
        if (s->noi_on)
            s->noi_len = len_table[v >> 3];
        s->noi_env_start = 1;
        break;
    case 0x4010:
        s->dmc_irq_en = v >> 7;
        s->dmc_loop = (v >> 6) & 1;
        s->dmc_rate = (n->region == NES_PAL ? dmc_pal : dmc_ntsc)[v & 15];
        if (!s->dmc_irq_en)
            s->irq &= ~IRQ_DMC;
        break;
    case 0x4011:
        s->dmc_level = v & 0x7F;
        break;
    case 0x4012:
        s->dmc_start = (uint16_t)(0xC000 + v * 64);
        break;
    case 0x4013:
        s->dmc_len = (uint16_t)(v * 16 + 1);
        break;
    case 0x4015:
        s->sq[0].on = v & 1;
        s->sq[1].on = (v >> 1) & 1;
        s->tri_on = (v >> 2) & 1;
        s->noi_on = (v >> 3) & 1;
        if (!s->sq[0].on) s->sq[0].len = 0;
        if (!s->sq[1].on) s->sq[1].len = 0;
        if (!s->tri_on) s->tri_len = 0;
        if (!s->noi_on) s->noi_len = 0;
        s->irq &= ~IRQ_DMC;
        if (!(v & 16))
            s->dmc_left = 0;
        else if (!s->dmc_left)
            dmc_restart(s);
        break;
    case 0x4017:
        s->fc_newval = v;
        s->fc_delay = (s->cycles & 1) ? 4 : 3;
        s->fc_inhibit = (v >> 6) & 1;
        if (s->fc_inhibit)
            s->irq &= ~IRQ_FRAME;
        break;
    }
}

static uint8_t apu_status(Nes *n)
{
    NesState *s = &n->st;
    uint8_t v = (uint8_t)((s->sq[0].len ? 1 : 0) | (s->sq[1].len ? 2 : 0) | (s->tri_len ? 4 : 0) | (s->noi_len ? 8 : 0) |
                          (s->dmc_left ? 16 : 0) | ((s->irq & IRQ_FRAME) ? 64 : 0) | ((s->irq & IRQ_DMC) ? 128 : 0) |
                          (s->bus & 0x20));
    s->irq &= ~IRQ_FRAME;
    return v;
}

static void frame_counter(Nes *n)
{
    static const int ntsc[6] = { 7457, 14913, 22371, 29828, 29829, 29830 };
    static const int pal[6] = { 8313, 16627, 24939, 33252, 33253, 33254 };
    NesState *s = &n->st;
    const int *t = n->region == NES_PAL ? pal : ntsc;
    int c;

    if (s->fc_delay && --s->fc_delay == 0) {
        s->fc_mode = s->fc_newval >> 7;
        s->fc_cycle = 0;
        if (s->fc_mode) {
            quarter(s);
            half(s);
        }
    }
    c = ++s->fc_cycle;
    if (c == t[0] || c == t[2]) {
        quarter(s);
    } else if (c == t[1]) {
        quarter(s);
        half(s);
    } else if (!s->fc_mode) {
        if (c == t[3] && !s->fc_inhibit)
            s->irq |= IRQ_FRAME;
        if (c == t[4]) {
            quarter(s);
            half(s);
            if (!s->fc_inhibit)
                s->irq |= IRQ_FRAME;
        }
        if (c >= t[5]) {
            if (!s->fc_inhibit)
                s->irq |= IRQ_FRAME;
            s->fc_cycle = 0;
        }
    } else {
        int five = n->region == NES_PAL ? 41565 : 37281;
        if (c == five) {
            quarter(s);
            half(s);
        }
        if (c >= five + 1)
            s->fc_cycle = 0;
    }
}

static void apu_step(Nes *n)
{
    NesState *s = &n->st;
    int i, p, t, ns, d, x = 0;
    double out;

    frame_counter(n);
    if (s->cycles & 1)          /* квадраты тикают раз в два такта */
        for (i = 0; i < 2; i++) {
            NesPulse *q = &s->sq[i];
            if (q->timer == 0) {
                q->timer = q->period;
                q->pos = (q->pos + 1) & 7;
            } else {
                q->timer--;
            }
        }
    if (s->tri_timer == 0) {
        s->tri_timer = s->tri_period;
        if (s->tri_len && s->tri_lin && s->tri_period >= 2)
            s->tri_pos = (s->tri_pos + 1) & 31;
    } else {
        s->tri_timer--;
    }
    if (s->noi_timer == 0) {
        int fb = (s->noi_lfsr & 1) ^ ((s->noi_lfsr >> (s->noi_mode ? 6 : 1)) & 1);
        s->noi_timer = s->noi_period;
        s->noi_lfsr = (uint16_t)((s->noi_lfsr >> 1) | (fb << 14));
    } else {
        s->noi_timer--;
    }
    if (s->dmc_timer == 0) {
        s->dmc_timer = s->dmc_rate;
        if (!s->dmc_silent) {
            if (s->dmc_shift & 1) {
                if (s->dmc_level <= 125)
                    s->dmc_level += 2;
            } else if (s->dmc_level >= 2) {
                s->dmc_level -= 2;
            }
        }
        s->dmc_shift >>= 1;
        if (s->dmc_bits)
            s->dmc_bits--;
        if (!s->dmc_bits) {
            s->dmc_bits = 8;
            if (s->dmc_buf_full) {
                s->dmc_silent = 0;
                s->dmc_shift = s->dmc_buf;
                s->dmc_buf_full = 0;
            } else {
                s->dmc_silent = 1;
            }
        }
    } else {
        s->dmc_timer--;
    }
    if (!s->dmc_buf_full && s->dmc_left)
        s->dmc_need = 1;

    /* смешивание по таблицам, как резисторы в приставке; копим среднее до сэмпла */
    p = pulse_out(&s->sq[0]) + pulse_out(&s->sq[1]);
    t = tri_seq[s->tri_pos];
    ns = (!(s->noi_lfsr & 1) && s->noi_len) ? (s->noi_cvol ? s->noi_vol : s->noi_env_vol) : 0;
    d = s->dmc_level;
    if (n->m_audio)
        x = n->m_audio(n);
    s->smp_acc += pulse_mix[p] + tnd_mix[3 * t + 2 * ns + d] + x / 32768.0;
    s->smp_cnt += 1;
    if (s->smp_cnt >= s->smp_step) {
        double y;
        out = s->smp_acc / s->smp_cnt;
        s->smp_acc = 0;
        s->smp_cnt -= s->smp_step;
        /* фильтры: срезаем постоянную составляющую и самый верх, как у приставки */
        y = 0.996 * (s->hp1 + out - s->hp1_prev);
        s->hp1_prev = out;
        s->hp1 = y;
        s->lp += (y - s->lp) * 0.6;
        if (n->snd_n < (int)(sizeof(n->snd) / sizeof(n->snd[0]))) {
            double v = s->lp * 1.6 * 32767.0;
            n->snd[n->snd_n++] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}

/* ---------- шина процессора ---------- */

static uint8_t pad_read(NesState *s, int port)
{
    uint8_t v;

    if (s->strobe)
        return s->pad[port] & 1;
    v = s->pad_shift[port] & 1;
    s->pad_shift[port] = (uint8_t)((s->pad_shift[port] >> 1) | 0x80);
    return v;
}

static uint8_t bus_read(Nes *n, uint16_t a)
{
    NesState *s = &n->st;
    uint8_t v;

    if (a < 0x2000)
        v = s->ram[a & 0x7FF];
    else if (a < 0x4000)
        v = ppu_reg_read(n, a & 7);
    else if (a < 0x4020) {
        if (a == 0x4015)
            return apu_status(n);           /* шину не трогает */
        if (a == 0x4016 || a == 0x4017)
            v = (uint8_t)(pad_read(s, a & 1) | (s->bus & 0xE0));
        else
            v = s->bus;
    } else if (n->m_read && n->m_read(n, a, &v)) {
        /* маппер ответил сам */
    } else if (a < 0x6000) {
        v = s->bus;
    } else if (a < 0x8000) {
        if (s->wram_rom >= 0)
            v = n->prg[(s->wram_rom + (a & 0x1FFF)) % n->prg_size];
        else
            v = s->wram_off ? s->bus : s->wram[a & 0x1FFF];
    } else {
        v = n->prg[s->prg_off[(a >> 13) & 3] + (a & 0x1FFF)];
    }
    s->bus = v;
    return v;
}

static void oam_dma(Nes *n, uint8_t page)
{
    int i;

    cyc(n);
    if (n->st.cycles & 1)
        cyc(n);
    for (i = 0; i < 256; i++) {
        uint8_t v = rd(n, (uint16_t)(page << 8 | i));
        cyc(n);
        ppu_reg_write(n, 4, v);
    }
}

static void bus_write(Nes *n, uint16_t a, uint8_t v)
{
    NesState *s = &n->st;

    s->bus = v;
    if (a < 0x2000) {
        s->ram[a & 0x7FF] = v;
    } else if (a < 0x4000) {
        ppu_reg_write(n, a & 7, v);
    } else if (a == 0x4014) {
        oam_dma(n, v);
    } else if (a == 0x4016) {
        s->strobe = v & 1;
        if (s->strobe) {
            s->pad_shift[0] = s->pad[0];
            s->pad_shift[1] = s->pad[1];
        }
    } else if (a < 0x4018) {
        apu_write(n, a, v);
    } else if (a >= 0x4020) {
        if (a >= 0x6000 && a < 0x8000 && s->wram_rom < 0 && !s->wram_off)
            s->wram[a & 0x1FFF] = v;
        if (n->m_write)
            n->m_write(n, a, v);
    }
}

static uint8_t rd(Nes *n, uint16_t a)
{
    uint8_t v;

    if (n->st.dmc_need)
        dmc_fetch(n);
    tick(n);
    v = bus_read(n, a);
    poll(&n->st);
    return v;
}

static void wr(Nes *n, uint16_t a, uint8_t v)
{
    tick(n);
    bus_write(n, a, v);
    poll(&n->st);
}

/* ---------- процессор 6502 ---------- */

enum { IMP, IMM, ZP, ZPX, ZPY, ABS, ABX, ABY, IZX, IZY, IND, REL };
enum { K_R, K_W, K_M };

/* Режим адресации каждой команды (включая недокументированные). */
static const uint8_t modes[256] = {
    IMP, IZX, IMP, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
    REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
    ABS, IZX, IMP, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
    REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
    IMP, IZX, IMP, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
    REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
    IMP, IZX, IMP, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, IND, ABS, ABS, ABS,
    REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
    IMM, IZX, IMM, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
    REL, IZY, IMP, IZY, ZPX, ZPX, ZPY, ZPY, IMP, ABY, IMP, ABY, ABX, ABX, ABY, ABY,
    IMM, IZX, IMM, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
    REL, IZY, IMP, IZY, ZPX, ZPX, ZPY, ZPY, IMP, ABY, IMP, ABY, ABX, ABX, ABY, ABY,
    IMM, IZX, IMM, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
    REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX,
    IMM, IZX, IMM, IZX, ZP,  ZP,  ZP,  ZP,  IMP, IMM, IMP, IMM, ABS, ABS, ABS, ABS,
    REL, IZY, IMP, IZY, ZPX, ZPX, ZPX, ZPX, IMP, ABY, IMP, ABY, ABX, ABX, ABX, ABX
};

static uint16_t base_addr;      /* для SHA/SHX/SHY/TAS: адрес до прибавления индекса */

static uint16_t ea(Nes *n, int mode, int kind)
{
    NesState *s = &n->st;
    uint16_t a, b;
    uint8_t z;

    switch (mode) {
    case IMM:
        return s->pc++;
    case ZP:
        return rd(n, s->pc++);
    case ZPX: case ZPY:
        z = rd(n, s->pc++);
        rd(n, z);
        return (uint8_t)(z + (mode == ZPX ? s->x : s->y));
    case ABS:
        a = rd(n, s->pc++);
        return (uint16_t)(a | rd(n, s->pc++) << 8);
    case ABX: case ABY:
        b = rd(n, s->pc++);
        b = (uint16_t)(b | rd(n, s->pc++) << 8);
        a = (uint16_t)(b + (mode == ABX ? s->x : s->y));
        if (kind != K_R || ((a ^ b) & 0xFF00))
            rd(n, (uint16_t)((b & 0xFF00) | (a & 0xFF)));
        base_addr = b;
        return a;
    case IZX:
        z = rd(n, s->pc++);
        rd(n, z);
        z = (uint8_t)(z + s->x);
        a = rd(n, z);
        return (uint16_t)(a | rd(n, (uint8_t)(z + 1)) << 8);
    case IZY:
        z = rd(n, s->pc++);
        b = rd(n, z);
        b = (uint16_t)(b | rd(n, (uint8_t)(z + 1)) << 8);
        a = (uint16_t)(b + s->y);
        if (kind != K_R || ((a ^ b) & 0xFF00))
            rd(n, (uint16_t)((b & 0xFF00) | (a & 0xFF)));
        base_addr = b;
        return a;
    default:
        return 0;
    }
}

#define NZ(v) (s->p = (uint8_t)((s->p & ~(FZ | FN)) | ((v) ? 0 : FZ) | ((v) & FN)))

static void push(Nes *n, uint8_t v)
{
    wr(n, (uint16_t)(0x100 | n->st.s), v);
    n->st.s--;
}

static uint8_t pull(Nes *n)
{
    n->st.s++;
    return rd(n, (uint16_t)(0x100 | n->st.s));
}

static void adc(NesState *s, uint8_t v)
{
    unsigned r = (unsigned)s->a + v + (s->p & FC);

    s->p = (uint8_t)((s->p & ~(FC | FV)) | (r > 0xFF ? FC : 0) | ((~(s->a ^ v) & (s->a ^ r) & 0x80) ? FV : 0));
    s->a = (uint8_t)r;
    NZ(s->a);
}

static void cmp(NesState *s, uint8_t reg, uint8_t v)
{
    uint8_t r = (uint8_t)(reg - v);

    s->p = (uint8_t)((s->p & ~FC) | (reg >= v ? FC : 0));
    NZ(r);
}

static uint8_t asl(NesState *s, uint8_t v)
{
    s->p = (uint8_t)((s->p & ~FC) | (v >> 7));
    v <<= 1;
    NZ(v);
    return v;
}

static uint8_t lsr(NesState *s, uint8_t v)
{
    s->p = (uint8_t)((s->p & ~FC) | (v & 1));
    v >>= 1;
    NZ(v);
    return v;
}

static uint8_t rol(NesState *s, uint8_t v)
{
    uint8_t c = s->p & FC;

    s->p = (uint8_t)((s->p & ~FC) | (v >> 7));
    v = (uint8_t)(v << 1 | c);
    NZ(v);
    return v;
}

static uint8_t ror(NesState *s, uint8_t v)
{
    uint8_t c = s->p & FC;

    s->p = (uint8_t)((s->p & ~FC) | (v & 1));
    v = (uint8_t)(v >> 1 | c << 7);
    NZ(v);
    return v;
}

static void branch(Nes *n, int cond)
{
    NesState *s = &n->st;
    int8_t off = (int8_t)rd(n, s->pc++);
    uint16_t np;

    if (!cond)
        return;
    rd(n, s->pc);
    np = (uint16_t)(s->pc + off);
    if ((np ^ s->pc) & 0xFF00)
        rd(n, (uint16_t)((s->pc & 0xFF00) | (np & 0xFF)));
    s->pc = np;
}

/* NMI или IRQ; brk — команда BRK (PC уже на байт дальше). */
static void interrupt(Nes *n, int brk)
{
    NesState *s = &n->st;
    uint16_t vec;

    if (!brk) {
        rd(n, s->pc);
        rd(n, s->pc);
    }
    push(n, (uint8_t)(s->pc >> 8));
    push(n, (uint8_t)s->pc);
    if (s->nmi_need) {
        s->nmi_need = 0;
        vec = 0xFFFA;
    } else {
        vec = 0xFFFE;
    }
    push(n, (uint8_t)(s->p | FU | (brk ? FB : 0)));
    s->p |= FI;
    s->pc = rd(n, vec);
    s->pc = (uint16_t)(s->pc | rd(n, (uint16_t)(vec + 1)) << 8);
    if (brk)
        s->nmi_need_prev = 0;
}

/* «Нестабильные» запись SHA/SHX/SHY/TAS: значение & (старший байт адреса + 1). */
static void store_and_high(Nes *n, uint16_t a, uint8_t val)
{
    uint8_t v = (uint8_t)(val & ((base_addr >> 8) + 1));

    if ((a ^ base_addr) & 0xFF00)
        a = (uint16_t)((a & 0xFF) | v << 8);
    wr(n, a, v);
}

static void cpu_step(Nes *n)
{
    NesState *s = &n->st;
    uint8_t op, v;
    uint16_t a = 0;
    int mode, kind;

    if (s->jam) {
        cyc(n);
        return;
    }
    if (s->nmi_need_prev || s->irq_run_prev) {
        interrupt(n, 0);
        return;
    }
    if (n->trace)
        n->trace(n);
    op = rd(n, s->pc++);
    mode = modes[op];
    switch (op) {   /* записи и чтение-изменение-запись по-разному делают пустые чтения */
    case 0x81: case 0x83: case 0x84: case 0x85: case 0x86: case 0x87: case 0x8C: case 0x8D: case 0x8E: case 0x8F:
    case 0x91: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: case 0x99: case 0x9B: case 0x9C: case 0x9D:
    case 0x9E: case 0x9F:
        kind = K_W;
        break;
    default:
        kind = (op & 7) == 6 || (op & 7) == 7 ? ((op & 0xE0) == 0x80 || (op & 0xE0) == 0xA0 ? K_R : K_M) : K_R;
        if ((op & 0x0F) == 0x0B && (op == 0x1B || op == 0x3B || op == 0x5B || op == 0x7B || op == 0xDB || op == 0xFB))
            kind = K_M;
        if ((op & 0x1F) == 0x16 || (op & 0x1F) == 0x1E)
            kind = (op & 0xE0) == 0x80 || (op & 0xE0) == 0xA0 ? kind : K_M;
        break;
    }
    if (mode != IMP && mode != REL && mode != IND && op != 0x20 && op != 0x4C)
        a = ea(n, mode, kind);

    switch (op) {
    /* загрузки и сохранения */
    case 0xA9: case 0xA5: case 0xB5: case 0xAD: case 0xBD: case 0xB9: case 0xA1: case 0xB1:
        s->a = rd(n, a); NZ(s->a); break;
    case 0xA2: case 0xA6: case 0xB6: case 0xAE: case 0xBE:
        s->x = rd(n, a); NZ(s->x); break;
    case 0xA0: case 0xA4: case 0xB4: case 0xAC: case 0xBC:
        s->y = rd(n, a); NZ(s->y); break;
    case 0x85: case 0x95: case 0x8D: case 0x9D: case 0x99: case 0x81: case 0x91:
        wr(n, a, s->a); break;
    case 0x86: case 0x96: case 0x8E:
        wr(n, a, s->x); break;
    case 0x84: case 0x94: case 0x8C:
        wr(n, a, s->y); break;
    /* арифметика и логика */
    case 0x69: case 0x65: case 0x75: case 0x6D: case 0x7D: case 0x79: case 0x61: case 0x71:
        adc(s, rd(n, a)); break;
    case 0xE9: case 0xEB: case 0xE5: case 0xF5: case 0xED: case 0xFD: case 0xF9: case 0xE1: case 0xF1:
        adc(s, (uint8_t)~rd(n, a)); break;
    case 0x29: case 0x25: case 0x35: case 0x2D: case 0x3D: case 0x39: case 0x21: case 0x31:
        s->a &= rd(n, a); NZ(s->a); break;
    case 0x09: case 0x05: case 0x15: case 0x0D: case 0x1D: case 0x19: case 0x01: case 0x11:
        s->a |= rd(n, a); NZ(s->a); break;
    case 0x49: case 0x45: case 0x55: case 0x4D: case 0x5D: case 0x59: case 0x41: case 0x51:
        s->a ^= rd(n, a); NZ(s->a); break;
    case 0xC9: case 0xC5: case 0xD5: case 0xCD: case 0xDD: case 0xD9: case 0xC1: case 0xD1:
        cmp(s, s->a, rd(n, a)); break;
    case 0xE0: case 0xE4: case 0xEC:
        cmp(s, s->x, rd(n, a)); break;
    case 0xC0: case 0xC4: case 0xCC:
        cmp(s, s->y, rd(n, a)); break;
    case 0x24: case 0x2C:
        v = rd(n, a);
        s->p = (uint8_t)((s->p & ~(FZ | FV | FN)) | (v & (FV | FN)) | ((v & s->a) ? 0 : FZ));
        break;
    /* сдвиги и инкременты: чтение, пустая запись старого, запись нового */
    case 0x0A: rd(n, s->pc); s->a = asl(s, s->a); break;
    case 0x4A: rd(n, s->pc); s->a = lsr(s, s->a); break;
    case 0x2A: rd(n, s->pc); s->a = rol(s, s->a); break;
    case 0x6A: rd(n, s->pc); s->a = ror(s, s->a); break;
    case 0x06: case 0x16: case 0x0E: case 0x1E:
        v = rd(n, a); wr(n, a, v); wr(n, a, asl(s, v)); break;
    case 0x46: case 0x56: case 0x4E: case 0x5E:
        v = rd(n, a); wr(n, a, v); wr(n, a, lsr(s, v)); break;
    case 0x26: case 0x36: case 0x2E: case 0x3E:
        v = rd(n, a); wr(n, a, v); wr(n, a, rol(s, v)); break;
    case 0x66: case 0x76: case 0x6E: case 0x7E:
        v = rd(n, a); wr(n, a, v); wr(n, a, ror(s, v)); break;
    case 0xE6: case 0xF6: case 0xEE: case 0xFE:
        v = rd(n, a); wr(n, a, v); v++; NZ(v); wr(n, a, v); break;
    case 0xC6: case 0xD6: case 0xCE: case 0xDE:
        v = rd(n, a); wr(n, a, v); v--; NZ(v); wr(n, a, v); break;
    case 0xE8: rd(n, s->pc); s->x++; NZ(s->x); break;
    case 0xCA: rd(n, s->pc); s->x--; NZ(s->x); break;
    case 0xC8: rd(n, s->pc); s->y++; NZ(s->y); break;
    case 0x88: rd(n, s->pc); s->y--; NZ(s->y); break;
    /* пересылки и флаги */
    case 0xAA: rd(n, s->pc); s->x = s->a; NZ(s->x); break;
    case 0x8A: rd(n, s->pc); s->a = s->x; NZ(s->a); break;
    case 0xA8: rd(n, s->pc); s->y = s->a; NZ(s->y); break;
    case 0x98: rd(n, s->pc); s->a = s->y; NZ(s->a); break;
    case 0xBA: rd(n, s->pc); s->x = s->s; NZ(s->x); break;
    case 0x9A: rd(n, s->pc); s->s = s->x; break;
    case 0x18: rd(n, s->pc); s->p &= ~FC; break;
    case 0x38: rd(n, s->pc); s->p |= FC; break;
    case 0x58: rd(n, s->pc); s->p &= ~FI; break;
    case 0x78: rd(n, s->pc); s->p |= FI; break;
    case 0xB8: rd(n, s->pc); s->p &= ~FV; break;
    case 0xD8: rd(n, s->pc); s->p &= ~FD; break;
    case 0xF8: rd(n, s->pc); s->p |= FD; break;
    /* стек */
    case 0x48: rd(n, s->pc); push(n, s->a); break;
    case 0x08: rd(n, s->pc); push(n, (uint8_t)(s->p | FB | FU)); break;
    case 0x68: rd(n, s->pc); rd(n, (uint16_t)(0x100 | s->s)); s->a = pull(n); NZ(s->a); break;
    case 0x28: rd(n, s->pc); rd(n, (uint16_t)(0x100 | s->s)); s->p = (uint8_t)((pull(n) & ~FB) | FU); break;
    /* переходы */
    case 0x4C:
        a = rd(n, s->pc++);
        s->pc = (uint16_t)(a | rd(n, s->pc) << 8);
        break;
    case 0x6C: {
        uint16_t p = rd(n, s->pc++);
        p = (uint16_t)(p | rd(n, s->pc++) << 8);
        a = rd(n, p);
        s->pc = (uint16_t)(a | rd(n, (uint16_t)((p & 0xFF00) | ((p + 1) & 0xFF))) << 8);   /* ошибка страницы */
        break;
    }
    case 0x20:
        a = rd(n, s->pc++);
        rd(n, (uint16_t)(0x100 | s->s));
        push(n, (uint8_t)(s->pc >> 8));
        push(n, (uint8_t)s->pc);
        s->pc = (uint16_t)(a | rd(n, s->pc) << 8);
        break;
    case 0x60:
        rd(n, s->pc);
        rd(n, (uint16_t)(0x100 | s->s));
        a = pull(n);
        s->pc = (uint16_t)(a | pull(n) << 8);
        rd(n, s->pc++);
        break;
    case 0x40:
        rd(n, s->pc);
        rd(n, (uint16_t)(0x100 | s->s));
        s->p = (uint8_t)((pull(n) & ~FB) | FU);
        a = pull(n);
        s->pc = (uint16_t)(a | pull(n) << 8);
        break;
    case 0x00:
        rd(n, s->pc++);
        interrupt(n, 1);
        break;
    case 0x10: branch(n, !(s->p & FN)); break;
    case 0x30: branch(n, s->p & FN); break;
    case 0x50: branch(n, !(s->p & FV)); break;
    case 0x70: branch(n, s->p & FV); break;
    case 0x90: branch(n, !(s->p & FC)); break;
    case 0xB0: branch(n, s->p & FC); break;
    case 0xD0: branch(n, !(s->p & FZ)); break;
    case 0xF0: branch(n, s->p & FZ); break;
    /* NOP разных видов (читают операнд) */
    case 0xEA: case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
        rd(n, s->pc); break;
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: case 0x04: case 0x44: case 0x64: case 0x14: case 0x34:
    case 0x54: case 0x74: case 0xD4: case 0xF4: case 0x0C: case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC:
    case 0xFC:
        rd(n, a); break;
    /* недокументированные, которыми пользуются игры */
    case 0xA7: case 0xB7: case 0xAF: case 0xBF: case 0xA3: case 0xB3:
        s->a = s->x = rd(n, a); NZ(s->a); break;
    case 0x87: case 0x97: case 0x8F: case 0x83:
        wr(n, a, s->a & s->x); break;
    case 0x07: case 0x17: case 0x0F: case 0x1F: case 0x1B: case 0x03: case 0x13:
        v = rd(n, a); wr(n, a, v); v = asl(s, v); wr(n, a, v); s->a |= v; NZ(s->a); break;
    case 0x27: case 0x37: case 0x2F: case 0x3F: case 0x3B: case 0x23: case 0x33:
        v = rd(n, a); wr(n, a, v); v = rol(s, v); wr(n, a, v); s->a &= v; NZ(s->a); break;
    case 0x47: case 0x57: case 0x4F: case 0x5F: case 0x5B: case 0x43: case 0x53:
        v = rd(n, a); wr(n, a, v); v = lsr(s, v); wr(n, a, v); s->a ^= v; NZ(s->a); break;
    case 0x67: case 0x77: case 0x6F: case 0x7F: case 0x7B: case 0x63: case 0x73:
        v = rd(n, a); wr(n, a, v); v = ror(s, v); wr(n, a, v); adc(s, v); break;
    case 0xC7: case 0xD7: case 0xCF: case 0xDF: case 0xDB: case 0xC3: case 0xD3:
        v = rd(n, a); wr(n, a, v); v--; wr(n, a, v); cmp(s, s->a, v); break;
    case 0xE7: case 0xF7: case 0xEF: case 0xFF: case 0xFB: case 0xE3: case 0xF3:
        v = rd(n, a); wr(n, a, v); v++; wr(n, a, v); adc(s, (uint8_t)~v); break;
    case 0x0B: case 0x2B:
        s->a &= rd(n, a); NZ(s->a); s->p = (uint8_t)((s->p & ~FC) | (s->a >> 7)); break;
    case 0x4B:
        s->a &= rd(n, a); s->a = lsr(s, s->a); break;
    case 0x6B:
        s->a &= rd(n, a);
        s->a = (uint8_t)(s->a >> 1 | (s->p & FC) << 7);
        NZ(s->a);
        s->p = (uint8_t)((s->p & ~(FC | FV)) | ((s->a >> 6) & 1) | (((s->a >> 6) ^ (s->a >> 5)) & 1 ? FV : 0));
        break;
    case 0xCB: {
        uint8_t ax = s->a & s->x;
        v = rd(n, a);
        s->p = (uint8_t)((s->p & ~FC) | (ax >= v ? FC : 0));
        s->x = (uint8_t)(ax - v);
        NZ(s->x);
        break;
    }
    case 0x8B: s->a = (uint8_t)((s->a | 0xEE) & s->x & rd(n, a)); NZ(s->a); break;
    case 0xAB: s->a = s->x = rd(n, a); NZ(s->a); break;
    case 0xBB: s->a = s->x = s->s = rd(n, a) & s->s; NZ(s->a); break;
    case 0x93: case 0x9F: store_and_high(n, a, s->a & s->x); break;
    case 0x9E: store_and_high(n, a, s->x); break;
    case 0x9C: store_and_high(n, a, s->y); break;
    case 0x9B: s->s = s->a & s->x; store_and_high(n, a, s->s); break;
    default:    /* JAM: процессор встал, приставка — нет */
        s->jam = 1;
        break;
    }
}

/* ---------- создание и кадр ---------- */

static void power(Nes *n)
{
    NesState *s = &n->st;
    int i;

    s->a = s->x = s->y = 0;
    s->s = 0;
    s->p = FI | FU;
    s->noi_lfsr = 1;
    s->dmc_bits = 8;
    s->dmc_rate = (n->region == NES_PAL ? dmc_pal : dmc_ntsc)[0];
    s->noi_period = 4;
    s->sq[0].first = 1;
    s->wram_rom = -1;
    for (i = 0; i < 0x20; i++)
        s->pal[i] = 0x0F;
    nes_map_reset(n);
    /* сброс: 7 тактов, из них три «пустые записи» в стек */
    rd(n, s->pc);
    rd(n, s->pc);
    for (i = 0; i < 3; i++)
        rd(n, (uint16_t)(0x100 | s->s--));
    s->pc = rd(n, 0xFFFC);
    s->pc = (uint16_t)(s->pc | rd(n, 0xFFFD) << 8);
}

Nes *nes_create(const uint8_t *rom, size_t len, int region, char *err, size_t errlen)
{
    Nes *n;
    size_t prg, chr, off = 16;
    int nes2, i;

    if (len < 16 || memcmp(rom, "NES\x1A", 4)) {
        snprintf(err, errlen, "это не образ NES (.nes)");
        return NULL;
    }
    nes2 = (rom[7] & 0x0C) == 0x08;
    prg = (size_t)rom[4] * 0x4000;
    chr = (size_t)rom[5] * 0x2000;
    if (nes2) {
        prg += (size_t)(rom[9] & 0x0F) << 8 << 14;
        chr += (size_t)(rom[9] >> 4) << 8 << 13;
    }
    if (rom[6] & 4)
        off += 512;             /* «трейнер» перед программой */
    if (!prg || off + prg + chr > len) {
        snprintf(err, errlen, "образ обрезан или повреждён");
        return NULL;
    }
    if (!(n = calloc(1, sizeof(*n))) || !(n->rom = malloc(prg + chr + 1))) {
        free(n);
        snprintf(err, errlen, "не хватает памяти");
        return NULL;
    }
    memcpy(n->rom, rom + off, prg + chr);
    n->prg = n->rom;
    n->prg_size = (int)prg;
    if (chr) {
        n->chr = n->rom + prg;
        n->chr_size = (int)chr;
    } else {
        n->chr = n->st.chr_ram;
        n->chr_size = sizeof(n->st.chr_ram);
        n->chr_is_ram = 1;
    }
    n->mapper = (rom[6] >> 4) | (rom[7] & 0xF0);
    if (!nes2 && (rom[12] || rom[13] || rom[14] || rom[15]))
        n->mapper = rom[6] >> 4;    /* старые образы с мусором в заголовке («DiskDude!») */
    if (nes2) {
        n->mapper |= (rom[8] & 0x0F) << 8;
        n->sub = rom[8] >> 4;
    }
    n->battery = (rom[6] >> 1) & 1;
    if (region < 0)
        region = nes2 ? ((rom[12] & 3) == 1 ? NES_PAL : (rom[12] & 3) == 3 ? NES_DENDY : NES_NTSC)
                      : (rom[9] & 1) ? NES_PAL : NES_NTSC;
    n->region = region;
    n->pre_line = region == NES_NTSC ? 261 : 311;
    n->vbl_line = region == NES_DENDY ? 291 : 241;
    n->cpu_hz = region == NES_PAL ? 1662607.0 : region == NES_DENDY ? 1773448.0 : 1789773.0;
    nes_mirror(n, (rom[6] & 8) ? MIR_4 : (rom[6] & 1) ? MIR_V : MIR_H);
    if (!pulse_mix[1]) {
        for (i = 1; i < 31; i++)
            pulse_mix[i] = (float)(95.52 / (8128.0 / i + 100));
        for (i = 1; i < 203; i++)
            tnd_mix[i] = (float)(163.67 / (24329.0 / i + 100));
    }
    make_palette(n);
    nes_set_rate(n, 44100);
    if (nes_map_init(n, err, errlen)) {
        nes_destroy(n);
        return NULL;
    }
    power(n);
    return n;
}

void nes_destroy(Nes *n)
{
    if (!n)
        return;
    free(n->rom);
    free(n);
}

void nes_reset(Nes *n)
{
    NesState *s = &n->st;

    s->ctrl = s->mask = 0;
    s->w = 0;
    apu_write(n, 0x4015, 0);
    s->irq = 0;
    s->jam = 0;
    s->p |= FI;
    s->s = (uint8_t)(s->s - 3);
    nes_map_reset(n);
    s->pc = rd(n, 0xFFFC);
    s->pc = (uint16_t)(s->pc | rd(n, 0xFFFD) << 8);
}

void nes_frame(Nes *n)
{
    int guard = 0;

    n->st.frame_done = 0;
    while (!n->st.frame_done && ++guard < 200000)
        cpu_step(n);
}

const uint32_t *nes_pixels(const Nes *n) { return n->fb; }

void nes_set_buttons(Nes *n, int port, int buttons)
{
    /* вверх+вниз или влево+вправо разом на крестовине не бывает — игры от этого ломаются */
    if ((buttons & (NES_UP | NES_DOWN)) == (NES_UP | NES_DOWN))
        buttons &= ~NES_DOWN;
    if ((buttons & (NES_LEFT | NES_RIGHT)) == (NES_LEFT | NES_RIGHT))
        buttons &= ~NES_RIGHT;
    n->st.pad[port & 1] = (uint8_t)buttons;
}

void nes_set_rate(Nes *n, double rate)
{
    n->rate = (int)rate;
    n->st.smp_step = n->cpu_hz / rate;
}

int nes_take_audio(Nes *n, int16_t *out, int max)
{
    int k = n->snd_n < max ? n->snd_n : max;

    memcpy(out, n->snd, (size_t)k * sizeof(int16_t));
    memmove(n->snd, n->snd + k, (size_t)(n->snd_n - k) * sizeof(int16_t));
    n->snd_n -= k;
    return k;
}

int nes_region(const Nes *n) { return n->region; }

double nes_fps(const Nes *n)
{
    return n->region == NES_NTSC ? 60.0988 : 50.0070;
}

uint8_t *nes_battery(Nes *n, size_t *len)
{
    if (!n->battery)
        return NULL;
    *len = sizeof(n->st.wram);
    return n->st.wram;
}

#define STATE_MAGIC 0x4E455331u   /* «NES1» */

size_t nes_state_size(const Nes *n)
{
    (void)n;
    return 8 + sizeof(NesState);
}

void nes_state_save(const Nes *n, uint8_t *out)
{
    uint32_t h[2] = { STATE_MAGIC, (uint32_t)n->mapper };

    memcpy(out, h, 8);
    memcpy(out + 8, &n->st, sizeof(NesState));
}

int nes_state_load(Nes *n, const uint8_t *in, size_t len)
{
    uint32_t h[2];

    if (len != nes_state_size(n))
        return -1;
    memcpy(h, in, 8);
    if (h[0] != STATE_MAGIC || h[1] != (uint32_t)n->mapper)
        return -1;
    memcpy(&n->st, in + 8, sizeof(NesState));
    n->snd_n = 0;
    return 0;
}

/* ---------- отладка ---------- */

static void (*trace_line)(const char *s);

static void trace_cb(Nes *n)
{
    char buf[96];
    NesState *s = &n->st;

    snprintf(buf, sizeof(buf), "%04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu", s->pc, s->a, s->x, s->y, s->p,
             s->s, (unsigned long long)s->cycles);
    trace_line(buf);
}

void nes_debug_trace(Nes *n, uint16_t start_pc, void (*line)(const char *s))
{
    trace_line = line;
    n->trace = line ? trace_cb : NULL;
    n->st.pc = start_pc;
}
