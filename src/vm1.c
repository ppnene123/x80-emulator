
#define CPU_ID vm1

#define AF_MASK 0xFFFF
#define AF_ONES 0x0002
#define F_C 0x01
#define F_P 0x04
#define F_MF 0x08 /* vm1 memory bank bit */
#define F_H 0x10
#define F_V 0x20 /* vm1 overflow bit (called OF), replaces i85 K/UI/X5 */
#define F_Z 0x40
#define F_S 0x80
#define AF_KEEP F_MF

#define F_SUB_LIKE_ADD 1
#define F_SIMPLE_CPL 1

#define ADDRESS_MASK 0xFFFF
#define IOADDRESS_MASK 0xFFFF

#define get_address get_address
INLINE uint32_t get_address(x80_state_t * cpu, address_t address, int access)
{
	if((access & (X80_ACCESS_MODE_EXEC|X80_ACCESS_MODE_STACK|X80_ACCESS_MODE_PORT)))
	{
		return address & 0xFFFF;
	}
	else
	{
		return (cpu->af & F_MF) ? address ^ 0x10000 : address;
	}
}

PUBLIC void PREFIXED(reset)(x80_state_t * cpu)
{
	/* TODO */
	cpu->af = (cpu->af & AF_MASK) | AF_ONES;
	cpu->af &= ~F_MF; /* TODO: otherwise it might break */
	cpu->sp = 0x0000;
	cpu->pc = 0x0000;

	cpu->vm1.ie = 0;
}

PUBLIC void PREFIXED(exception_extint)(x80_state_t * cpu, int number, int count, void * data)
{
	if(cpu->vm1.ie == 1)
	{
		cpu->vm1.ie = 0;
		interpret_input(cpu, count, data);
	}
}

#define INT_DEFAULT 0

INLINE void disable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->vm1.ie = 0;
}

INLINE void enable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->vm1.ie = 1;
}

PUBLIC void PREFIXED(show_regs)(x80_state_t * cpu, bool only_system)
{
	if(!only_system)
	{
		printf("PSW = %04X\n", cpu->af);
		printf("C = %d, P = %d, MF = %d, AC = %d, OF = %d, Z = %d, S = %d\n",
			(cpu->af & F_C) != 0,
			(cpu->af & F_P) != 0,
			(cpu->af & F_MF) != 0,
			(cpu->af & F_H) != 0,
			(cpu->af & F_V) != 0,
			(cpu->af & F_Z) != 0,
			(cpu->af & F_S) != 0);
		printf("B   = %04X\n", cpu->bc);
		printf("D   = %04X\n", cpu->de);
		printf("H   = %04X\tH1   = %04X\n", cpu->hl, cpu->hl2);
	}

	printf("SP = %04X\n", cpu->sp);
	printf("PC = %04X\n", cpu->pc);
	printf("IE = %d\n", cpu->vm1.ie);
}

