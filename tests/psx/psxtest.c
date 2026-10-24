/* Проверочная программа для PlayStation: квадрат 32×32 на синем фоне, крестовина его двигает,
 * × красит в красный, ○ — в зелёный, △ записывает на карту памяти файл BTTEST (через BIOS —
 * так проверяется сохранение и с HLE-BIOS ядра, и со встроенным OpenBIOS). Собирается tests/psx/build.sh (нужен mipsel-linux-gnu-gcc),
 * кнопки читает BIOS (InitPad/StartPad) — так работает и со встроенным HLE-BIOS ядра. */
#define GP0  (*(volatile unsigned *)0x1F801810)
#define GP1  (*(volatile unsigned *)0x1F801814)
#define STAT (*(volatile unsigned *)0x1F801814)

int InitPad(unsigned char *a, int na, unsigned char *b, int nb);
int StartPad(void);
int InitCARD(int pad_enable);
int StartCARD(void);
void bu_init(void);
int bopen(const char *name, int mode);
int bwrite(int fd, const void *buf, int len);
int bclose(int fd);

static unsigned char pad1[34], pad2[34];

static void gp0(unsigned v)
{
    while (!(STAT & (1u << 26)))
        ;
    GP0 = v;
}

static void fill(int x, int y, int w, int h, unsigned bgr)
{
    gp0(0x02000000u | bgr);
    gp0(((unsigned)y << 16) | (unsigned)x);
    gp0(((unsigned)h << 16) | (unsigned)w);
}

void main(void)
{
    int x = 144, y = 104, frame = 0;
    unsigned color = 0xFFFFFF;
    int saved = 0;

    GP1 = 0x00000000;           /* сброс */
    GP1 = 0x08000001;           /* 320×240, NTSC, 15 бит */
    GP1 = 0x05000000;           /* показывать VRAM с 0,0 */
    GP1 = 0x06C60260;
    GP1 = 0x07040010;
    gp0(0xE1000400);            /* рисовать в показываемую область можно */
    gp0(0xE3000000);
    gp0(0xE4000000u | (239u << 10) | 319u);
    gp0(0xE5000000);
    GP1 = 0x03000000;           /* экран включён */
    InitPad(pad1, 34, pad2, 34);
    InitCARD(1);
    StartPad();
    StartCARD();
    bu_init();
    for (;;) {
        unsigned b = ~(unsigned)(pad1[2] | (pad1[3] << 8)) & 0xFFFF;   /* 0 — нажата */
        volatile int d;
        if (pad1[0] != 0 || pad1[1] != 0x41)
            b = 0;              /* пульта нет или BIOS его ещё не опросил */
        if (b & 0x0010) y -= 2;     /* вверх */
        if (b & 0x0040) y += 2;     /* вниз */
        if (b & 0x0080) x -= 2;     /* влево */
        if (b & 0x0020) x += 2;     /* вправо */
        if (b & 0x4000) color = 0x0000FF;  /* × — красный (BGR) */
        if (b & 0x2000) color = 0x00FF00;  /* ○ — зелёный */
        if ((b & 0x1000) && !saved) {      /* △ — сохранение на карту: один блок 8 КБ */
            static unsigned char block[8192];
            int fd, i;
            for (i = 0; i < 8192; i++)
                block[i] = (unsigned char)i;
            fd = bopen("bu00:BTTEST", 0x200 | (1 << 16));   /* создать */
            if (fd >= 0)
                bclose(fd);
            fd = bopen("bu00:BTTEST", 2);                    /* запись */
            if (fd >= 0) {
                if (bwrite(fd, block, 8192) == 8192)
                    color = 0xFF0000;          /* записали — квадрат синий */
                bclose(fd);
            }
            saved = 1;
        }
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x > 288) x = 288;
        if (y > 208) y = 208;
        fill(0, 0, 320, 240, 0x803000);    /* тёмно-синий фон */
        fill(x, y, 32, 32, color);
        fill(0, 232, (frame++ & 255) + 1, 8, 0x00FFFF);    /* полоска-счётчик кадров: жёлтая */
        for (d = 0; d < 60000; d++)
            ;
    }
}
