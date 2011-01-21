#define MERAM_REG_BASE 0xE8000000
#define MERAM_REG_SIZE 0x200000
#define MERAM_ICB0BASE 0x400
#define MERAM_ICB28BASE 0x780
#define MExxCTL 0x0
#define MExxSIZE 0x4
#define MExxMNCF 0x8
#define MExxSARA 0x10
#define MExxSARB 0x14
#define MExxBSIZE 0x18
#define MSAR_OFF 0x3C0

#define MEACTS 0x10
#define MEQSEL1 0x40
#define MEQSEL2 0x44

#define MERAM_START(ind, ab) (0xC0000000 | ((ab & 0x1) << 23) | \
        ((ind & 0x1F) << 24))

