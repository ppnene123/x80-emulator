
#ifndef CPU_ID // also used by R800, so we must not set it unless it has not been set before
# define CPU_ID z80
#endif

#define ZILOG 1

/* z80 rest of bits: undocumented, researched */
#define AF_MASK 0xFFFF
#define AF_ONES 0x0000
#define F_C 0x01
#define F_N 0x02
#define F_P 0x04
#define F_V 0x04
#define F_H 0x10
#define F_Z 0x40
#define F_S 0x80
#define AF_COPY 0x28 /* bits copied to the result */

#define F_BITS_CLR (F_H | F_N)
#define F_SCF_CLR (F_H | F_N)
#define F_CCF_CLR (F_H | F_N)

#define ADDRESS_MASK 0xFFFF
#define IOADDRESS_MASK 0xFFFF

/* accessing the interrupt mode bits */
INLINE uint8_t getim(x80_state_t * cpu)
{
	return cpu->z80.im;
}

INLINE void setim(x80_state_t * cpu, uint8_t value)
{
	cpu->z80.im = value;
}

/* switching AF registers */
INLINE bool getafp(x80_state_t * cpu)
{
	return cpu->z80.afp;
}

INLINE void flipafp(x80_state_t * cpu)
{
	uint16_t af;

	af = cpu->af;
	cpu->af = cpu->af2;
	cpu->af2 = af;

	cpu->z80.afp ^= 1;
}

/* switching BC/DE/HL register banks */
INLINE bool getalt(x80_state_t * cpu)
{
	return cpu->z80.alt;
}

INLINE void flipalt(x80_state_t * cpu)
{
	uint16_t reg;

	reg = cpu->bc;
	cpu->bc = cpu->bc2;
	cpu->bc2 = reg;

	reg = cpu->de;
	cpu->de = cpu->de2;
	cpu->de2 = reg;

	reg = cpu->hl;
	cpu->hl = cpu->hl2;
	cpu->hl2 = reg;

	cpu->z80.alt ^= 1;
}

PUBLIC void PREFIXED(reset)(x80_state_t * cpu)
{
	cpu->af = (cpu->af & AF_MASK) | AF_ONES;
	cpu->sp = 0x0000;
	cpu->pc = 0x0000;
	cpu->ir = 0x0000;

	cpu->z80.ie1 = cpu->z80.ie2 = 0;
}

PUBLIC void PREFIXED(exception_nmi)(x80_state_t * cpu, int data)
{
	cpu->z80.ie1 = 0;

	do_call(cpu, 0x0066);
}

PUBLIC void PREFIXED(exception_extint)(x80_state_t * cpu, int number, int count, void * data)
{
	if(cpu->z80.ie1 == 1)
	{
		cpu->z80.ie1 = cpu->z80.ie2 = 0;
		switch(getim(cpu))
		{
		case 0:
			interpret_input(cpu, count, data);
			break;
		case 1:
			pushword(cpu, 2, cpu->pc);
			cpu->pc = 0x0038;
			break;
		case 2:
			pushword(cpu, 2, cpu->pc);
			cpu->pc = readword(cpu, 2, (cpu->ir & 0xFF00) | (((uint8_t *)data)[0] & 0x00FF));
			break;
		}
	}
}

#define INT_DEFAULT 0

INLINE void disable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->z80.ie1 = cpu->z80.ie2 = 0;
}

INLINE void enable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->z80.ie1 = cpu->z80.ie2 = 1;
}

INLINE void do_before_retn(x80_state_t * cpu)
{
	cpu->z80.ie1 = cpu->z80.ie2;
}

PUBLIC void PREFIXED(show_regs)(x80_state_t * cpu, bool only_system)
{
	if(!only_system)
	{
		printf("AF = %04X\tAF' = %04X\n", (uint16_t)cpu->af, (uint16_t)cpu->af2);
		printf("C = %d, N = %d, P/V = %d, H = %d, Z = %d, S = %d\n",
			(cpu->af & F_C) != 0,
			(cpu->af & F_N) != 0,
			(cpu->af & F_P) != 0,
			(cpu->af & F_H) != 0,
			(cpu->af & F_Z) != 0,
			(cpu->af & F_S) != 0);
		printf("BC = %04X\tBC' = %04X\n", (uint16_t)cpu->bc, (uint16_t)cpu->bc2);
		printf("DE = %04X\tDE' = %04X\n", (uint16_t)cpu->de, (uint16_t)cpu->de2);
		printf("HL = %04X\tHL' = %04X\n", (uint16_t)cpu->hl, (uint16_t)cpu->hl2);
		printf("IX = %04X\n", (uint16_t)cpu->ix);
		printf("IY = %04X\n", (uint16_t)cpu->iy);
		printf("WZ = %04X\n", cpu->z80.wz);
	}

	printf("SP = %04X\n", (uint16_t)cpu->sp);
	printf("PC = %04X\n", (uint16_t)cpu->pc);
	printf("IFF1 = %d, IFF2 = %d\n", cpu->z80.ie1, cpu->z80.ie2);

	if(!only_system)
	{
		printf("I  = %02X\tR   = %02X\n", (uint16_t)cpu->ir >> 8, (uint16_t)cpu->ir & 0xFF);
		printf("IM = %d\n", cpu->z80.im);
	}

#if USE_Z80EX
	printf("SP = %04X\n", z80ex_get_reg(Z80CPU, regSP));
	printf("PC = %04X\n", z80ex_get_reg(Z80CPU, regPC));
	printf("IFF1 = %d, IFF2 = %d\n", z80ex_get_reg(Z80CPU, regIFF1), z80ex_get_reg(Z80CPU, regIFF2));

	if(!only_system)
	{
		printf("AF = %04X\tAF' = %04X\n", z80ex_get_reg(Z80CPU, regAF), z80ex_get_reg(Z80CPU, regAF_));
//		printf("C = %d, N = %d, P/V = %d, H = %d, Z = %d, S = %d\n", cpu->AF & 1, (cpu->AF >> 1) & 1, (cpu->AF >> 2) & 1, (cpu->AF >> 4) & 1, (cpu->AF >> 6) & 1, (cpu->AF >> 7) & 1);
		printf("BC = %04X\tBC' = %04X\n", z80ex_get_reg(Z80CPU, regBC), z80ex_get_reg(Z80CPU, regBC_));
		printf("DE = %04X\tDE' = %04X\n", z80ex_get_reg(Z80CPU, regDE), z80ex_get_reg(Z80CPU, regDE_));
		printf("HL = %04X\tHL' = %04X\n", z80ex_get_reg(Z80CPU, regHL), z80ex_get_reg(Z80CPU, regHL_));
		printf("IX = %04X\n", z80ex_get_reg(Z80CPU, regIX));
		printf("IY = %04X\n", z80ex_get_reg(Z80CPU, regIY));
//		printf("WZ = %04X\n", cpu->wz);
		printf("I  = %02X\tR   = %02X\n", z80ex_get_reg(Z80CPU, regI), z80ex_get_reg(Z80CPU, regR) | (z80ex_get_reg(Z80CPU, regR7) << 7));

		printf("IM = %d\n", z80ex_get_reg(Z80CPU, regIM));
	}
#endif
}

