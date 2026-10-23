/* Внутренности эмулятора NES/Денди: общее для ядра (nes.c) и мапперов (nes_map.c).
 * Всё изменяемое состояние лежит в NesState одним куском — так быстрое сохранение
 * игры это просто копия структуры, без указателей внутри. */
#ifndef BT_NES_INTERNAL_H
#define BT_NES_INTERNAL_H

#include "nes.h"

enum { MIR_H, MIR_V, MIR_A, MIR_B, MIR_4 };     /* горизонтальное, вертикальное, один экран A/B, четыре */
enum { IRQ_FRAME = 1, IRQ_DMC = 2, IRQ_MAP = 4 };

/* Генератор квадратной волны (APU и VRC6). */
typedef struct {
    uint8_t  duty, pos, len, halt, cvol, vol, env_start, env_div, env_vol;
    uint8_t  sw_en, sw_per, sw_neg, sw_shift, sw_div, sw_reload, on, first;
    uint16_t period, timer;
} NesPulse;

typedef struct {
    /* процессор */
    uint16_t pc;
    uint8_t  a, x, y, s, p, jam;
    uint8_t  nmi_line, nmi_prev, nmi_need, nmi_need_prev, irq_run, irq_run_prev;
    uint8_t  irq;                       /* источники IRQ: IRQ_* */
    uint64_t cycles;
    uint8_t  ram[2048];
    uint8_t  bus;                       /* «открытая шина»: последнее, что было на линии данных */
    uint8_t  pad[2], pad_shift[2], strobe;

    /* видеочип */
    uint8_t  ctrl, mask, status, oam_addr, xfine, w, rbuf, pbus, vbl_skip, odd;
    uint16_t v, t;
    int      line, dot;
    uint64_t dots;
    uint8_t  oam[256], pal[32];
    uint8_t  vram[4096];                /* 2 КБ своих + 2 КБ для четырёх экранов */
    uint8_t  nt_byte, at_byte, lo_byte, hi_byte;
    uint16_t bg_lo, bg_hi, at_lo, at_hi;
    uint8_t  spr_n, spr_x[8], spr_a[8], spr_lo[8], spr_hi[8], spr0, spr0_next;
    uint8_t  sec_n, sec[32], sec0;
    uint8_t  frame_done;
    int      frac;                      /* PAL: 3,2 точки на такт */

    /* звук */
    NesPulse sq[2];
    uint8_t  tri_len, tri_halt, tri_lin, tri_lin_load, tri_lin_reload, tri_pos, tri_on;
    uint16_t tri_period, tri_timer;
    uint8_t  noi_len, noi_halt, noi_cvol, noi_vol, noi_env_start, noi_env_div, noi_env_vol, noi_mode, noi_on;
    uint16_t noi_period, noi_timer, noi_lfsr;
    uint8_t  dmc_irq_en, dmc_loop, dmc_level, dmc_buf, dmc_buf_full, dmc_shift, dmc_bits, dmc_silent, dmc_need;
    uint16_t dmc_rate, dmc_timer, dmc_start, dmc_addr, dmc_len, dmc_left;
    uint8_t  fc_mode, fc_inhibit, fc_delay, fc_newval;
    int      fc_cycle;
    double   smp_acc, smp_cnt, smp_step, hp1, hp1_prev, hp2, hp2_prev, lp;

    /* картридж */
    int      prg_off[4];                /* 8 КБ окна $8000-$FFFF — смещения в PRG */
    int      chr_off[8];                /* 1 КБ окна $0000-$1FFF — смещения в CHR */
    uint8_t  nt[4];                     /* какая 1 КБ страница видна в каждом из 4 экранов */
    uint8_t  mirror;
    uint8_t  wram[8192];
    uint8_t  chr_ram[8192];
    int      wram_rom;                  /* >=0 — по $6000 видна страница PRG (FME-7, MMC5) */
    uint8_t  wram_off;                  /* память $6000 выключена */

    /* мапперы: общие регистры и счётчики */
    uint8_t  r[40];
    int      irq_cnt, irq_latch, irq_en, irq_reload, irq_pre, irq_mode;
    uint8_t  a12;
    uint64_t a12_low;
    uint64_t last_write;
    uint8_t  latch[2];
    uint8_t  exram[1024];
    NesPulse xsq[2];                    /* VRC6 / MMC5: дополнительные квадраты */
    uint8_t  saw_acc, saw_rate, saw_step, saw_on, xsq_vol;
    uint16_t saw_period, saw_timer;
    uint8_t  mmc5_pcm;
    int      in_frame, sl_cnt;
} NesState;

typedef struct NesMapper NesMapper;
struct Nes {
    NesState   st;
    const uint8_t *prg, *chr;
    uint8_t   *rom;                     /* копия образа */
    int        prg_size, chr_size, chr_is_ram;
    int        mapper, sub, battery, region;
    int        pre_line, vbl_line;      /* предрендерная строка и начало кадрового гашения */
    double     cpu_hz;
    uint32_t   fb[256 * 240];
    uint32_t   palette[512];
    int16_t    snd[4096];
    int        snd_n;
    int        rate;
    /* обработчики маппера (в сохранение не входят — восстанавливаются по номеру) */
    void     (*m_write)(Nes *n, uint16_t a, uint8_t v);
    int      (*m_read)(Nes *n, uint16_t a, uint8_t *v);  /* 1 — прочитал сам */
    void     (*m_cpu)(Nes *n);
    void     (*m_ppu)(Nes *n, uint16_t a);
    uint8_t  (*m_nt)(Nes *n, int page, int off, int write, uint8_t v);
    void     (*m_line)(Nes *n);
    int      (*m_audio)(Nes *n);
    void     (*m_spr)(Nes *n, int on);  /* видеочип начал (1) или закончил (0) читать спрайты */
    int        spr_fetch;               /* видеочип сейчас читает спрайты (MMC5) */
    void     (*trace)(Nes *n);          /* отладка: перед каждой командой */
};

/* nes_map.c */
int  nes_map_init(Nes *n, char *err, size_t errlen);
void nes_map_reset(Nes *n);

/* помощники для мапперов (nes.c) */
void nes_prg8(Nes *n, int slot, int bank);
void nes_prg16(Nes *n, int slot, int bank);
void nes_prg32(Nes *n, int bank);
void nes_chr1(Nes *n, int slot, int bank);
void nes_chr2(Nes *n, int slot, int bank);
void nes_chr4(Nes *n, int slot, int bank);
void nes_chr8(Nes *n, int bank);
void nes_mirror(Nes *n, int m);

#endif
