
#define CPU_ID i80

#define AF_MASK 0xFFD7
#define AF_ONES 0x0002
#define F_C 0x01
#define F_P 0x04
#define F_H 0x10
#define F_Z 0x40
#define F_S 0x80

#define ADDRESS_MASK 0xFFFF
#define IOADDRESS_MASK 0xFFFF

#define INT_DEFAULT 0

/* Undefined flags */
/* I80: 0 and 1 */

#define F_SUB_LIKE_ADD 1
#define F_SIMPLE_CPL 1

PUBLIC void PREFIXED(reset)(x80_state_t * cpu)
{
	cpu->af = (cpu->af & AF_MASK) | AF_ONES;
	cpu->sp = 0x0000;
	cpu->pc = 0x0000;

	cpu->i80.ie = 0;
}

PUBLIC void PREFIXED(exception_extint)(x80_state_t * cpu, int number, int count, void * data)
{
	if(cpu->i80.ie == 1)
	{
		cpu->i80.ie = 0;
		interpret_input(cpu, count, data);
	}
}

INLINE void disable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->i80.ie = 0;
}

INLINE void enable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->i80.ie = 1;
}

PUBLIC void PREFIXED(show_regs)(x80_state_t * cpu, bool only_system)
{
	if(!only_system)
	{
		printf("PSW = %04X\n", cpu->af);
		printf("C = %d, P = %d, AC = %d, Z = %d, S = %d\n",
			(cpu->af & F_C) != 0,
			(cpu->af & F_P) != 0,
			(cpu->af & F_H) != 0,
			(cpu->af & F_Z) != 0,
			(cpu->af & F_S) != 0);
		printf("B   = %04X\n", cpu->bc);
		printf("D   = %04X\n", cpu->de);
		printf("H   = %04X\n", cpu->hl);
	}
	printf("SP = %04X\n", cpu->sp);
	printf("PC = %04X\n", cpu->pc);
	printf("IE = %d\n", cpu->i80.ie);
}

