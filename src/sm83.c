
#define CPU_ID sm83

/* different from z80 */
#define AF_MASK 0xFFF0
#define AF_ONES 0x0000
#define F_C 0x10
#define F_H 0x20
#define F_N 0x40
#define F_Z 0x80

#define F_BITS_CLR (F_H | F_N | F_Z)
#define F_SCF_CLR (F_H | F_N)
#define F_CCF_CLR (F_H | F_N)

/* TODO: other bit manipulation instruction flag affection */

#define ADDRESS_MASK 0xFFFF

PUBLIC void PREFIXED(reset)(x80_state_t * cpu)
{
	cpu->af = (cpu->af & AF_MASK) | AF_ONES;
	cpu->sp = 0x0000;
	cpu->pc = 0x0100;

	cpu->sm83.ie = 0;
}

PUBLIC void PREFIXED(exception_extint)(x80_state_t * cpu, int number, int count, void * data)
{
	if(cpu->sm83.ie == 1)
	{
		int bit = (number - X80_SM83_INT_VBLANK) >> 3;
		if(((readbyte(cpu, X80_SM83_IE) >> bit) & 1) != 0)
		{
			cpu->sm83.ie = 0;
			writebyte(cpu, X80_SM83_IF, readbyte(cpu, X80_SM83_IF) & ~(1 << bit));
			do_call(cpu, number);
		}
		else
		{
			writebyte(cpu, X80_SM83_IF, readbyte(cpu, X80_SM83_IF) | (1 << bit));
		}
	}
}

#define INT_DEFAULT 0

INLINE void disable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->sm83.ie = 0;
}

INLINE void enable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->sm83.ie = 1;
}

PUBLIC void PREFIXED(show_regs)(x80_state_t * cpu, bool only_system)
{
	if(!only_system)
	{
		printf("AF = %04X\n", cpu->af);
		printf("C = %d, H = %d, N = %d, Z = %d\n",
			(cpu->af & F_C) != 0,
			(cpu->af & F_H) != 0,
			(cpu->af & F_N) != 0,
			(cpu->af & F_Z) != 0);
		printf("BC = %04X\n", cpu->bc);
		printf("DE = %04X\n", cpu->de);
		printf("HL = %04X\n", cpu->hl);
	}

	printf("SP = %04X\n", cpu->sp);
	printf("PC = %04X\n", cpu->pc);
	printf("IE = %d\n", cpu->sm83.ie);
}

