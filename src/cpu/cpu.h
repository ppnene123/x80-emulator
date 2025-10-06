#ifndef _CPU_H
#define _CPU_H

// global definitions for the emulator

#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>

typedef uint32_t word_t;
typedef  int32_t sword_t;

// Z380 requires 32-bit
typedef uint32_t address_t;
// Z280 requires 32-bit
typedef uint32_t ioaddress_t;

typedef enum x80_cpu_type_t
{
	X80_CPU_NONE,

	// Intel 8008 (1972)
	X80_CPU_I8008,
	// Datapoint 2200 Version I (1970)
	X80_CPU_DP2200V1,
	// Datapoint 2200 Version II (1972)
	X80_CPU_DP2200V2,
	// Datapoint 5500 (1975)
	X80_CPU_DP5500, // TODO
	// Datapoint 6600 (1977)
	X80_CPU_DP6600, // TODO
	// Datapoint 1800 (1978)
	X80_CPU_DP1800,
	// Datapoint 3800 (1979)
	X80_CPU_DP3800,
	// TODO: other Datapoint architectures?

	// Intel 8080 (1974)
	X80_CPU_I80,
	// Intel 8085 (1976)
	X80_CPU_I85,

	// Sharp SM83 (1989) - Gameboy CPU, Sharp LR35902
	X80_CPU_SM83,

	// Zilog Z80 (1976)
	X80_CPU_Z80,
	// Hitachi HD64180, Zilog Z180 (1985)
	X80_CPU_Z180,
	// Zilog Z800 (1985)
	X80_CPU_Z800,
	// Zilog Z280 (1987)
	X80_CPU_Z280,
	// KR580VM1 (1989) - КР580ВМ1
	X80_CPU_VM1,
	// ASCII R800 (1990)
	X80_CPU_R800,
	// Zilog Z380 (1994)
	X80_CPU_Z380,

	// Rabbit 2000 (1999)
	X80_CPU_R2K, // TODO
	// Zilog eZ80 (2001)
	X80_CPU_EZ80,
	// Rabbit 3000 (2002)
	X80_CPU_R3K, // TODO
	// Rabbit 4000 (2006)
	X80_CPU_R4K, // TODO
	// Rabbit 5000 (2008)
	X80_CPU_R5K, // TODO
	// Rabbit 6000 (2010)
	X80_CPU_R6K, // TODO
	// SpecNext Ltd. Z80N (2017)
	X80_CPU_Z80N, // TODO

	// Intel 8086 (1978)
	X80_CPU_I86, /* will not be implemented */
	// Zilog Z8000 (1979)
	X80_CPU_Z8K, /* will not be implemented */
	// NEC V20 (1982)
	X80_CPU_V20, /* will not be implemented */
	// NEC µPD9002 (1987)
	X80_CPU_9002, /* will not be implemented */
} x80_cpu_type_t;

#define X80_ACCESS_MODE_READ    0x0001
#define X80_ACCESS_MODE_WRITE   0x0002
#define X80_ACCESS_MODE_EXEC    0x0004 // reads for execution purposes
#define X80_ACCESS_MODE_STACK   0x0008 // accessed as stack
#define X80_ACCESS_MODE_PORT    0x0010 // accessed via in/out instruction
#define X80_ACCESS_MODE_VIRTUAL 0x0020 // follows remapped memory space
#define X80_ACCESS_MODE_SHORT   X80_ACCESS_MODE_VIRTUAL
#define X80_ACCESS_MODE_DIRECT  0x0040 // ignores virtual memory
#define X80_ACCESS_MODE_LONG    X80_ACCESS_MODE_DIRECT
#define X80_ACCESS_MODE_DEBUG   0x4000 // access from the debugger/monitor

typedef enum x80_access_type_t
{
	X80_ACCESS_TYPE_READ   = X80_ACCESS_MODE_READ,
	X80_ACCESS_TYPE_WRITE  = X80_ACCESS_MODE_WRITE,
	X80_ACCESS_TYPE_FETCH  = X80_ACCESS_MODE_READ  | X80_ACCESS_MODE_EXEC,
	X80_ACCESS_TYPE_POP    = X80_ACCESS_MODE_READ  | X80_ACCESS_MODE_STACK,
	X80_ACCESS_TYPE_PUSH   = X80_ACCESS_MODE_WRITE | X80_ACCESS_MODE_STACK,
	X80_ACCESS_TYPE_INPUT  = X80_ACCESS_MODE_READ  | X80_ACCESS_MODE_PORT,
	X80_ACCESS_TYPE_OUTPUT = X80_ACCESS_MODE_WRITE | X80_ACCESS_MODE_PORT,
} x80_access_type_t;

typedef struct x80_state_t x80_state_t;

typedef uint8_t x80_read_byte_t    (x80_state_t *, address_t);
typedef void    x80_write_byte_t   (x80_state_t *, address_t,   uint8_t);
typedef bool    x80_fetch_byte_t   (x80_state_t *,              uint8_t *);
typedef uint8_t x80_input_byte_t   (x80_state_t *, ioaddress_t);
typedef void    x80_output_byte_t  (x80_state_t *, ioaddress_t, uint8_t);
// 8085 specific
typedef bool    x80_input_serial_t (x80_state_t *);
typedef void    x80_output_serial_t(x80_state_t *, bool);

struct x80_state_t
{
	x80_cpu_type_t cpu_type;

	void * user_data;
	x80_read_byte_t     * read_byte;
	x80_write_byte_t    * write_byte;
	x80_fetch_byte_t    * fetch_byte;
	x80_input_byte_t    * input_byte;
	x80_output_byte_t   * output_byte;
	x80_input_serial_t  * input_serial;
	x80_output_serial_t * output_serial;

	jmp_buf exc;
	// TODO: incorporate
	struct
	{
		uint16_t length;
		uint8_t * buffer;
		uint16_t pointer;
	} fetch_source;

	// most common registers
	union
	{
		uint16_t af;
		struct
		{
#if BYTE_ORDER == LITTLE_ENDIAN
			uint8_t f, a;
#elif BYTE_ORDER == BIG_ENDIAN
			uint8_t a, f;
#endif
		};
	};

	union
	{
		uint16_t af2;
		struct
		{
#if BYTE_ORDER == LITTLE_ENDIAN
			uint8_t f2, a2;
#elif BYTE_ORDER == BIG_ENDIAN
			uint8_t a2, f2;
#endif
		};
	};

#if BYTE_ORDER == LITTLE_ENDIAN
# define _DEFREG(__name, __low, __high, __suffix) \
	union \
	{ \
		address_t __name##__suffix; \
		struct \
		{ \
			uint8_t __low##__suffix, __high##__suffix; \
			address_t __name##u##__suffix : 16; \
		}; \
	}
#elif BYTE_ORDER == BIT_ENDIAN
# define _DEFREG(__name, __low, __high, __suffix) \
	union \
	{ \
		address_t __name##__suffix; \
		struct \
		{ \
			address_t __name##u##__suffix : 16; \
			uint8_t __high##__suffix, __low##__suffix; \
		}; \
	}
#endif

	_DEFREG(bc, b,   c,);
	_DEFREG(de, d,   e,);
	_DEFREG(hl, h,   l,);
	_DEFREG(ix, ixh, ixl,);
	_DEFREG(iy, iyh, iyl,);

	_DEFREG(bc, b,   c,   2);
	_DEFREG(de, d,   e,   2);
	_DEFREG(hl, h,   l,   2);
	_DEFREG(ix, ixh, ixl, 2);
	_DEFREG(iy, iyh, iyl, 2);

#undef _DEFREG

	address_t sp;
	address_t pc, old_pc;

	union
	{
		address_t ir; /* I and R registers */
		struct
		{
#if BYTE_ORDER == LITTLE_ENDIAN
			uint8_t r;
			address_t i : 24;
#elif BYTE_ORDER == BIT_ENDIAN
			address_t i : 24;
			uint8_t r;
#endif
		};
	};

	union
	{
		// structure used by Datapoint and Intel 8008
		struct
		{
			uint8_t stack[16];
			// DP2200 Version II and later
			uint8_t ie:1;
			// DP5500 and later (duplicates the A register)
			uint16_t xa, xa2;
		} dp;
		// structure used by Intel 8080, VM1 and SM83
		struct
		{
			uint8_t ie:1;
		} i80, sm83, vm1;
		struct
		{
			uint8_t imask; // also contains IE1
		} i85;
		struct
		{
			uint8_t ie1:1;
			uint8_t ie2:1;
			uint8_t im:2;
			uint8_t afp:1, alt:1; /* AF selector, main register selector */
			uint16_t wz; // undocumented internal register
		} z80;
		struct
		{
			uint8_t ie1:1;
			uint8_t ie2:1;
			uint8_t im:2;
			uint8_t afp:1, alt:1; /* AF selector, main register selector */
			uint8_t ior[64];
		} z180;
		struct
		{
			uint8_t afp:1, alt:1; /* AF selector, main register selector */
			uint8_t sir; // shadow interrupt register
			uint16_t ctl[256]; // TODO: uint8_t? also spsel
			uint16_t ior[256];
			uint16_t pdr[256];
			uint16_t ssp, usp; // system and user stack pointer, represents one of the sp
		} z280;
		struct
		{
			uint8_t ie2:1;
			uint32_t sr; // also contains IM, IE1, AFP, ALT
			uint32_t spc; // shadow PC
			uint8_t ior[256];

			uint16_t  af[8];
			address_t bc[8];
			address_t de[8];
			address_t hl[8];
			address_t ix[8];
			address_t iy[8];
		} z380;
		struct
		{
			uint8_t ie1:1;
			uint8_t ie2:1;
			uint8_t im:2;
			uint8_t afp:1, alt:1; /* AF selector, main register selector */
			uint8_t mbase;
			uint8_t ivect;
			uint8_t adl:1, madl:1;
			uint16_t sps;
			uint32_t spl; // short and long stack pointer, represents one of the sp

//			uint8_t ins_wordsize; // current wordsize as of instruction parsing
		} ez80;
	};
};

enum
{
	// common flags
	X80_F_C = 0x01,
	X80_F_N = 0x02, // only Z80 and derivatives
	X80_F_P = 0x04,
	X80_F_H = 0x10,
	X80_F_Z = 0x40,
	X80_F_S = 0x80,

	X80_Z180_TRAP_UNDEFOP = 0,
	X80_Z380_TRAP_UNDEFOP = 0,
	X80_EZ80_TRAP_UNDEFOP = 0,

	/* Datapoint 2200 interrupts */
	X80_DP22_INT = 0x0000,

	/* Datapoint 5500 interrupts */
	X80_DP55_INT_MEMORY_PARITY = 0xEF00,
	X80_DP55_INT_INPUT_PARITY = 0xEF06,
	X80_DP55_INT_OUTPUT_PARITY = 0xEF0C,
	X80_DP55_INT_WRITE_PROTECT = 0xEF12,
	X80_DP55_INT_ACCESS_PROTECT = 0xEF18,
	X80_DP55_INT_PRIVILEGED = 0xEF1E,
	X80_DP55_INT_MILLISECOND = 0xEF24,
	X80_DP55_INT_SYSTEM_CALL = 0xEF2A,
	X80_DP55_INT_BREAK_POINT = 0xEF30,

	/* 8085 interrupt mask bits */
	X80_I85_IM_M5_5 = 0x01,
	X80_I85_IM_M6_5 = 0x02,
	X80_I85_IM_M7_5 = 0x04,
	X80_I85_IM_IE   = 0x08,
	X80_I85_IM_I5_5 = 0x10,
	X80_I85_IM_I6_5 = 0x20,
	X80_I85_IM_I7_5 = 0x40,

	/* 8085 interrupt offsets */
	X80_I85_INT_5_5 = 0x2C,
	X80_I85_INT_6_5 = 0x34,
	X80_I85_INT_7_5 = 0x3C,

	/* SM83 interrupt offsets */
	X80_SM83_INT_VBLANK = 0x40,
	X80_SM83_INT_STAT = 0x48,
	X80_SM83_INT_TIMER = 0x50,
	X80_SM83_INT_SERIAL	 = 0x58,
	X80_SM83_INT_JOYPAD = 0x60,

	/* SM83 interrupt handling addresses */
	X80_SM83_IF = 0xFF0F,
	X80_SM83_IE = 0xFFFF,

	/* Z180 - offsets into table */
	X80_Z180_INT_INT1 = 0,
	X80_Z180_INT_INT2 = 2,
	X80_Z180_INT_PRT0 = 4,
	X80_Z180_INT_PRT1 = 6,
	X80_Z180_INT_DMAC0 = 8,
	X80_Z180_INT_DMAC1 = 10,
	X80_Z180_INT_CSIO = 12,
	X80_Z180_INT_ASCI0 = 14,
	X80_Z180_INT_ASCI1 = 16,

	/* Z800/Z280 traps, offsets into table */
	X80_Z280_TRAP_NMI = 0x0004,
	X80_Z280_TRAP_INTA = 0x0008,
	X80_Z280_TRAP_INTB = 0x000C,
	X80_Z280_TRAP_INTC = 0x0010,
	X80_Z280_TRAP_PRT0 = 0x0014,
	X80_Z280_TRAP_PRT1 = 0x0018,
	X80_Z280_TRAP_PRT2 = 0x001C, /* only Z800 */
	X80_Z280_TRAP_PRT3 = 0x0020, /* TRAP_PRT2 on Z280 */
	X80_Z280_TRAP_DMAC0 = 0x0024,
	X80_Z280_TRAP_DMAC1 = 0x0028,
	X80_Z280_TRAP_DMAC2 = 0x002C,
	X80_Z280_TRAP_DMAC3 = 0x0030,
	X80_Z280_TRAP_UARTREC = 0x0034,
	X80_Z280_TRAP_UARTTRA = 0x0038,
	X80_Z280_TRAP_SS = 0x003C, /* single step */
	X80_Z280_TRAP_BPOH = 0x0040, /* breakpoint on halt */
	X80_Z280_TRAP_DE = 0x0044, /* division exception */
	X80_Z280_TRAP_SSOW = 0x0048, /* system stack overflow warning */
	X80_Z280_TRAP_AV = 0x004C, /* access violation */
	X80_Z280_TRAP_SC = 0x0050, /* system call */
	X80_Z280_TRAP_PI = 0x0054, /* privileged instruction */
	X80_Z280_TRAP_EPUM = 0x0058,
	X80_Z280_TRAP_MEPU = 0x005C,
	X80_Z280_TRAP_EPUF = 0x0060,
	X80_Z280_TRAP_EPUI = 0x0064,
	X80_Z280_TRAP_NMIBASE = 0x0070,
	X80_Z280_TRAP_INTABASE = 0x0070,
	X80_Z280_TRAP_INTBBASE = 0x0170,
	X80_Z280_TRAP_INTCBASE = 0x0270,

	/* Z380 offsets into table */
	X80_Z380_INT_INT1 = 0x00,
	X80_Z380_INT_INT2 = 0x04,
	X80_Z380_INT_INT3 = 0x08,

	/* Z180 specific registers */
	X80_Z180_IOR_CNTLA0 = 0x00,
		X80_Z180_CNTLA0_MOD0 = 0x01,
		X80_Z180_CNTLA0_MOD1 = 0x02,
		X80_Z180_CNTLA0_MOD2 = 0x04,
		X80_Z180_CNTLA0_MPBR_EFR = 0x08,
		X80_Z180_CNTLA0_RTS0 = 0x10,
		X80_Z180_CNTLA0_TE = 0x20,
		X80_Z180_CNTLA0_RE = 0x40,
		X80_Z180_CNTLA0_MPE = 0x80,
	X80_Z180_IOR_CNTLA1 = 0x01,
		X80_Z180_CNTLA1_MOD0 = 0x01,
		X80_Z180_CNTLA1_MOD1 = 0x02,
		X80_Z180_CNTLA1_MOD2 = 0x04,
		X80_Z180_CNTLA1_MPBR_EFR = 0x08,
		X80_Z180_CNTLA1_CKA1D = 0x10,
		X80_Z180_CNTLA1_TE = 0x20,
		X80_Z180_CNTLA1_RE = 0x40,
		X80_Z180_CNTLA1_MPE = 0x80,
	X80_Z180_IOR_CNTLB0 = 0x02,
		X80_Z180_CNTLB0_SS0 = 0x01,
		X80_Z180_CNTLB0_SS1 = 0x02,
		X80_Z180_CNTLB0_SS2 = 0x04,
		X80_Z180_CNTLB0_DR = 0x08,
		X80_Z180_CNTLB0_PE0 = 0x10,
		X80_Z180_CNTLB0_CTS_PS = 0x20,
		X80_Z180_CNTLB0_MP = 0x40,
		X80_Z180_CNTLB0_MPBT = 0x80,
	X80_Z180_IOR_CNTLB1 = 0x03,
		X80_Z180_CNTLB1_SS0 = 0x01,
		X80_Z180_CNTLB1_SS1 = 0x02,
		X80_Z180_CNTLB1_SS2 = 0x04,
		X80_Z180_CNTLB1_DR = 0x08,
		X80_Z180_CNTLB1_PE0 = 0x10,
		X80_Z180_CNTLB1_CTS_PS = 0x20,
		X80_Z180_CNTLB1_MP = 0x40,
		X80_Z180_CNTLB1_MPBT = 0x80,
	X80_Z180_IOR_STAT0 = 0x04,
		X80_Z180_STAT0_TIE = 0x01,
		X80_Z180_STAT0_TDRE = 0x02,
		X80_Z180_STAT0_DCD0 = 0x04,
		X80_Z180_STAT0_RIE = 0x08,
		X80_Z180_STAT0_FE = 0x10,
		X80_Z180_STAT0_PE = 0x20,
		X80_Z180_STAT0_OVRN = 0x40,
		X80_Z180_STAT0_RDRF = 0x80,
	X80_Z180_IOR_STAT1 = 0x05,
		X80_Z180_STAT1_TIE = 0x01,
		X80_Z180_STAT1_TDRE = 0x02,
		X80_Z180_STAT1_CTS1E = 0x04,
		X80_Z180_STAT1_RIE = 0x08,
		X80_Z180_STAT1_FE = 0x10,
		X80_Z180_STAT1_PE = 0x20,
		X80_Z180_STAT1_OVRN = 0x40,
		X80_Z180_STAT1_RDRF = 0x80,
	X80_Z180_IOR_TDR0 = 0x06,
	X80_Z180_IOR_TDR1 = 0x07,
	X80_Z180_IOR_RDR0 = 0x08,
	X80_Z180_IOR_RDR1 = 0x09,
	X80_Z180_IOR_CNTR = 0x0A,
		X80_Z180_CNTR_SS0 = 0x01,
		X80_Z180_CNTR_SS1 = 0x02,
		X80_Z180_CNTR_SS2 = 0x04,
		X80_Z180_CNTR_TE = 0x10,
		X80_Z180_CNTR_RE = 0x20,
		X80_Z180_CNTR_EIE = 0x40,
		X80_Z180_CNTR_EF = 0x80,
	X80_Z180_IOR_TRD = 0x0B,
	X80_Z180_IOR_TMDR0L = 0x0C,
	X80_Z180_IOR_TMDR0H = 0x0D,
	X80_Z180_IOR_RLDR0L = 0x0E,
	X80_Z180_IOR_RLDR0H = 0x0F,
	X80_Z180_IOR_TCR = 0x10,
		X80_Z180_TCR_TDE0 = 0x01,
		X80_Z180_TCR_TDE1 = 0x02,
		X80_Z180_TCR_TOC0 = 0x04,
		X80_Z180_TCR_TOC1 = 0x08,
		X80_Z180_TCR_TIE0 = 0x10,
		X80_Z180_TCR_TIE1 = 0x20,
		X80_Z180_TCR_TIF0 = 0x40,
		X80_Z180_TCR_TIF1 = 0x80,
	X80_Z180_IOR_ASEXT0 = 0x12, /* Z8S180/Z8L180 only */
		X80_Z180_ASEXT0_SendBreak = 0x01,
		X80_Z180_ASEXT0_BreakDetect = 0x02,
		X80_Z180_ASEXT0_BreakFeatureEnable = 0x04,
		X80_Z180_ASEXT0_BRG0_Mode = 0x08,
		X80_Z180_ASEXT0_X1_Bit_Clk = 0x10,
		X80_Z180_ASEXT0_CTS0_Disable = 0x20,
		X80_Z180_ASEXT0_DCD0_Disable = 0x40,
		X80_Z180_ASEXT0_RDRF_Int_Inhibit = 0x80,
	X80_Z180_IOR_ASEXT1 = 0x13, /* Z8S180/Z8L180 only */
		X80_Z180_ASEXT1_SendBreak = 0x01,
		X80_Z180_ASEXT1_BreakDetect = 0x02,
		X80_Z180_ASEXT1_BreakFeatureEnable = 0x04,
		X80_Z180_ASEXT1_BRG1_Mode = 0x08,
		X80_Z180_ASEXT1_X1_Bit_Clk = 0x10,
		X80_Z180_ASEXT1_RDRF_Int_Inhibit = 0x80,
	X80_Z180_IOR_TMDR1L = 0x14,
	X80_Z180_IOR_TMDR1H = 0x15,
	X80_Z180_IOR_RLDR1L = 0x16,
	X80_Z180_IOR_RLDR1H = 0x17,
	X80_Z180_IOR_FRC = 0x18,
	X80_Z180_IOR_ASTC0L = 0x1A, /* Z8S180/Z8L180 only */
	X80_Z180_IOR_ASTC0H = 0x1B, /* Z8S180/Z8L180 only */
	X80_Z180_IOR_ASTC1L = 0x1C, /* Z8S180/Z8L180 only */
	X80_Z180_IOR_ASTC1H = 0x1D, /* Z8S180/Z8L180 only */
	X80_Z180_IOR_CMR = 0x1E, /* Z8S180/Z8L180 only */
		X80_Z180_CMR_X2 = 0x80,
	X80_Z180_IOR_CCR = 0x1F, /* Z8S180/Z8L180 only */
		X80_Z180_CCR_LNAD_DATA = 0x01,
		X80_Z180_CCR_LNCPUCTL = 0x02,
		X80_Z180_CCR_LNIO = 0x04,
		X80_Z180_CCR_STANDBY_IDLE_Enable = 0x08,
		X80_Z180_CCR_LNPHI = 0x10,
		X80_Z180_CCR_BREXT = 0x20,
		X80_Z180_CCR_STANDBY_IDLE_Mode = 0x40,
		X80_Z180_CCR_ClockDivide = 0x80,
	X80_Z180_IOR_SAR0L = 0x20,
	X80_Z180_IOR_SAR0H = 0x21,
	X80_Z180_IOR_SAR0B = 0x22,
	X80_Z180_IOR_DAR0L = 0x23,
	X80_Z180_IOR_DAR0H = 0x24,
	X80_Z180_IOR_DAR0B = 0x25,
	X80_Z180_IOR_BCR0L = 0x26,
	X80_Z180_IOR_BCR0H = 0x27,
	X80_Z180_IOR_MAR0L = 0x28,
	X80_Z180_IOR_MAR0H = 0x29,
	X80_Z180_IOR_MAR0B = 0x2A,
	X80_Z180_IOR_IAR1L = 0x2B,
	X80_Z180_IOR_IAR1H = 0x2C,
	X80_Z180_IOR_IAR1B = 0x2D, /* Z8S180/Z8L180 only */
	X80_Z180_IOR_BCR1L = 0x2E,
	X80_Z180_IOR_BCR1H = 0x2F,
	X80_Z180_IOR_DSTAT = 0x30,
		X80_Z180_DSTAT_DME = 0x01,
		X80_Z180_DSTAT_DIE0 = 0x04,
		X80_Z180_DSTAT_DIE1 = 0x08,
		X80_Z180_DSTAT_DWE0 = 0x10,
		X80_Z180_DSTAT_DWE1 = 0x20,
		X80_Z180_DSTAT_DE0 = 0x40,
		X80_Z180_DSTAT_DE1 = 0x80,
	X80_Z180_IOR_DMODE = 0x31,
		X80_Z180_DMODE_MMOD = 0x02,
		X80_Z180_DMODE_SM0 = 0x04,
		X80_Z180_DMODE_SM1 = 0x08,
		X80_Z180_DMODE_DM0 = 0x10,
		X80_Z180_DMODE_DM1 = 0x20,
	X80_Z180_IOR_DCNTL = 0x32,
		X80_Z180_DCNTL_DIM = 0x03,
		X80_Z180_DCNTL_DMS = 0x0C,
		X80_Z180_DCNTL_IWI = 0x30,
		X80_Z180_DCNTL_MWI = 0xC0,
	X80_Z180_IOR_IL = 0x33,
	X80_Z180_IOR_ITC = 0x34,
		X80_Z180_ITC_ITE0 = 0x01,
		X80_Z180_ITC_ITE1 = 0x02,
		X80_Z180_ITC_ITE2 = 0x04,
		X80_Z180_ITC_UFO = 0x40,
		X80_Z180_ITC_TRAP = 0x80,
	X80_Z180_IOR_RCR = 0x36,
		X80_Z180_RCR_REFW = 0x40,
		X80_Z180_RCR_REFE = 0x80,
	X80_Z180_IOR_CBR = 0x38,
	X80_Z180_IOR_BBR = 0x39,
	X80_Z180_IOR_CBAR = 0x3A,
	X80_Z180_IOR_OMCR = 0x3E,
	X80_Z180_IOR_ICR = 0x3F,

	/* Z280 specific registers */

	/* ctl */
	X80_Z280_CTL_MSR = 0x00,
		X80_Z280_MSR_INTA = 0x0001,
		X80_Z280_MSR_PRT0 = 0x0002,
		X80_Z280_MSR_DMAC0 = 0x0002,
		X80_Z280_MSR_INTB = 0x0004,
		X80_Z280_MSR_PRT1 = 0x0008,
		X80_Z280_MSR_DMAC1 = 0x0008,
		X80_Z280_MSR_UARTREC = 0x0008,
		X80_Z280_MSR_INTC = 0x0010,
		X80_Z280_MSR_DMAC2 = 0x0020,
		X80_Z280_MSR_UARTTRA = 0x0020,
		X80_Z280_MSR_PRT2 = 0x0040,
		X80_Z280_MSR_DMAC3 = 0x0040,
		X80_Z280_MSR_SS = 0x0100,
		X80_Z280_MSR_SSP = 0x0200,
		X80_Z280_MSR_BH = 0x1000,
		X80_Z280_MSR_US = 0x4000,
	X80_Z280_CTL_BTCR = 0x02,
	X80_Z280_CTL_SSLR = 0x04,
	X80_Z280_CTL_ITVTP = 0x06,
	X80_Z280_CTL_IOPR = 0x08,
	X80_Z280_CTL_TCR = 0x10,
		X80_Z280_TCR_S = 0x01,
		X80_Z280_TCR_E = 0x02,
		X80_Z280_TCR_I = 0x04,
	X80_Z280_CTL_CCR = 0x12,
	X80_Z280_CTL_LAR = 0x14,
	X80_Z280_CTL_ISR = 0x16,
		X80_Z280_ISR_IM_MASK = 0x0300,
		X80_Z280_ISR_IM_SHIFT = 8,
		X80_Z280_ISR_NMI = 0x1000,
		X80_Z280_ISR_INTA = 0x2000,
		X80_Z280_ISR_INTB = 0x4000,
		X80_Z280_ISR_INTC = 0x8000,
	X80_Z280_CTL_BTIR = 0xFF,

	/* ior */
	X80_Z280_IOR_MCR = 0xF0,
	X80_Z280_IOR_PDRP = 0xF1,
	X80_Z280_IOR_IP = 0xF2,
	X80_Z280_IOR_BMP = 0xF4,
	X80_Z280_IOR_DSP = 0xF5,

	/* z380 specific registers */
	/* SR */
	X80_Z380_SR_AFP = 0x00000001,
	X80_Z380_SR_LCK = 0x00000002,
	X80_Z380_SR_IM = 0x00000018,
	X80_Z380_SR_IM_SHIFT = 3,
	X80_Z380_SR_IEF1 = 0x00000020,
	X80_Z380_SR_LW = 0x00000040,
	X80_Z380_SR_XM = 0x00000080,
	X80_Z380_SR_ALT = 0x00000100,
	X80_Z380_SR_MAINBANK = 0x00000600,
	X80_Z380_SR_MAINBANK_SHIFT = 9,
	X80_Z380_SR_IXP = 0x00010000,
	X80_Z380_SR_IXBANK = 0x00060000,
	X80_Z380_SR_IXBANK_SHIFT = 17,
	X80_Z380_SR_IYP = 0x01000000,
	X80_Z380_SR_IYBANK = 0x06000000,
	X80_Z380_SR_IYBANK_SHIFT = 25,
	/* ior */
	X80_Z380_IOR_IER = 0x17,
		X80_Z380_IER_IE0 = 0x01,
		X80_Z380_IER_IE1 = 0x02,
		X80_Z380_IER_IE2 = 0x04,
		X80_Z380_IER_IE3 = 0x08,
	X80_Z380_IOR_AVBR = 0x18,
	X80_Z380_IOR_TRPBK = 0x19,
		X80_Z380_TRPBK_TV = 0x01,
		X80_Z380_TRPBK_TF = 0x02,
	X80_Z380_IOR_ID = 0xFF,

};

extern const char * const cpu_shortname[];
#define CPU_SHORTNAME(cpu) (cpu_shortname[(cpu)->cpu_type])
extern const char * const cpu_longname[];
#define CPU_LONGNAME(cpu) (cpu_longname[(cpu)->cpu_type])
extern const char * const cpu_fullname[];
#define CPU_FULLNAME(cpu) (cpu_fullname[(cpu)->cpu_type])
extern const int cpu_asm[];
#define CPU_ASM(cpu) (cpu_asm[(cpu)->cpu_type])
extern const int cpu_dasm[];
#define CPU_DASM(cpu) (cpu_dasm[(cpu)->cpu_type])

bool x80_step(x80_state_t * cpu, bool do_disasm);

#define GETHI(r) (((r) >> 8) & 0xFF)
#define GETLO(r) ((r) & 0xFF)
#define SETHI(r, v) ((r) = ((r) & ~0xFF00) | (((v) & 0xFF) << 8))
#define SETLO(r, v) ((r) = ((r) & ~0x00FF) | ((v) & 0xFF))

#endif /* _CPU_H */
