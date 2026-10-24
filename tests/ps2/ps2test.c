/* Проверочная программа для PlayStation 2: включает вывод NTSC 640×448 и рисует через GIF (DMA
 * канал 2) красный фон и синий квадрат 64×64 по центру. Игр PS2 в тестах нет — так проверяется
 * загрузка диска (SYSTEM.CNF с BOOT2 → ELF), BIOS и картинка ядра. Сборка — tests/ps2/build.sh. */
typedef unsigned long long u64;
typedef unsigned int u32;

#define REG32(a) (*(volatile u32 *)(a))
#define GS_PMODE    0x12000000
#define GS_DISPFB1  0x12000070
#define GS_DISPLAY1 0x12000080
#define D2_CHCR     0x1000A000
#define D2_MADR     0x1000A010
#define D2_QWC      0x1000A020

static u64 packet[2 * 16] __attribute__((aligned(16)));

static void gs_priv(u32 addr, u64 v)
{
    REG32(addr) = (u32)v;
    REG32(addr + 4) = (u32)(v >> 32);
}

static void setgscrt(int interlace, int mode, int field)
{
    register int a0 __asm__("$4") = interlace, a1 __asm__("$5") = mode, a2 __asm__("$6") = field;
    register int v1 __asm__("$3") = 2;
    __asm__ volatile("syscall\n\tnop" : "+r"(v1) : "r"(a0), "r"(a1), "r"(a2) : "memory");
}

static int n;
static void ad(u64 data, u64 reg)
{
    volatile u64 *p = (volatile u64 *)((u32)packet | 0x20000000);    /* некэшируемое окно */
    p[2 + n * 2] = data;
    p[3 + n * 2] = reg;
    n++;
}

static void sprite(int x0, int y0, int x1, int y1, u32 rgb)
{
    ad(6, 0x00);                                                    /* PRIM = спрайт */
    ad((u64)rgb | (0x80ull << 24), 0x01);                           /* RGBAQ */
    ad(((u64)(x0 << 4)) | ((u64)(y0 << 4) << 16), 0x05);            /* XYZ2 */
    ad(((u64)(x1 << 4)) | ((u64)(y1 << 4) << 16), 0x05);
}

void _start(void)
{
    volatile u64 *p = (volatile u64 *)((u32)packet | 0x20000000);

    setgscrt(1, 2, 1);                                              /* NTSC, чересстрочно, кадр */
    gs_priv(GS_PMODE, 0x0001 | (1 << 2) | (0x80 << 8));             /* EN1, CRTMD=1, ALP=0x80 */
    gs_priv(GS_DISPFB1, 10ull << 9);                                /* FBP=0, FBW=640/64, CT32 */
    gs_priv(GS_DISPLAY1, 656 | (36 << 12) | (3 << 23) | (2559ull << 32) | (447ull << 44));

    n = 0;
    ad(10ull << 16, 0x4C);                                          /* FRAME_1: FBW=10 */
    ad(639ull << 16 | 447ull << 48, 0x40);                          /* SCISSOR_1 */
    ad(0, 0x18);                                                    /* XYOFFSET_1 */
    ad(1ull << 16 | 1ull << 17, 0x47);                              /* TEST_1: Z всегда */
    ad(1ull << 32, 0x4E);                                           /* ZBUF_1: без записи Z */
    sprite(0, 0, 640, 448, 0x0000FF);                               /* фон — красный (ABGR) */
    sprite(288, 192, 352, 256, 0xFF0000);                           /* квадрат — синий */
    p[0] = (u64)n | (1ull << 15) | (1ull << 60);                    /* GIFtag: NLOOP, EOP, NREG=1, PACKED */
    p[1] = 0xE;                                                     /* A+D */

    REG32(D2_MADR) = (u32)packet;
    REG32(D2_QWC) = (u32)(n + 1);
    REG32(D2_CHCR) = 0x101;                                         /* из памяти, старт */
    while (REG32(D2_CHCR) & 0x100)
        ;
    for (;;)
        ;
}
