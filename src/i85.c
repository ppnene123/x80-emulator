
#define CPU_ID i85

#define AF_MASK 0xFFF7
#define AF_ONES 0x0000
#define F_C 0x01
#define F_V 0x02 /* i85 undocumented, researched */
#define F_P 0x04
#define F_H 0x10
#define F_UI 0x20 /* i85 undocumented, researched (UI/K/X5 flag) */
#define F_Z 0x40
#define F_S 0x80

#define F_SUB_LIKE_ADD 1
#define F_SIMPLE_CPL 1

#define ADDRESS_MASK 0xFFFF
#define IOADDRESS_MASK 0xFFFF

PUBLIC void PREFIXED(reset)(x80_state_t * cpu)
{
	cpu->af = (cpu->af & AF_MASK) | AF_ONES;
	cpu->sp = 0x0000;
	cpu->pc = 0x0000;

	cpu->i85.imask = X80_I85_IM_M5_5 | X80_I85_IM_M6_5 | X80_I85_IM_M7_5;
}

PUBLIC void PREFIXED(exception_nmi)(x80_state_t * cpu, int data)
{
	cpu->i85.imask &= ~X80_I85_IM_IE; // TODO: probably not needed

	do_call(cpu, 0x0024);
}

PUBLIC void PREFIXED(exception_extint)(x80_state_t * cpu, int number, int count, void * data)
{
	switch(number)
	{
	case 0:
		if((cpu->i85.imask & X80_I85_IM_IE))
		{
			cpu->i85.imask &= ~X80_I85_IM_IE;
			interpret_input(cpu, count, data);
		}
		break;
	case X80_I85_INT_5_5:
		if(!(cpu->i85.imask & X80_I85_IM_M5_5))
		{
			cpu->i85.imask |= X80_I85_IM_M5_5;
			pushword(cpu, 2, cpu->pc);
			cpu->pc = X80_I85_INT_5_5;
		}
		break;
	case X80_I85_INT_6_5:
		if(!(cpu->i85.imask & X80_I85_IM_M6_5))
		{
			cpu->i85.imask |= X80_I85_IM_M6_5;
			pushword(cpu, 2, cpu->pc);
			cpu->pc = X80_I85_INT_6_5;
		}
		break;
	case X80_I85_INT_7_5:
		if(!(cpu->i85.imask & X80_I85_IM_M7_5))
		{
			/*cpu->i85.imask |= X80_I85_IM_M7_5;*/ /* does not happen automatically */
			pushword(cpu, 2, cpu->pc);
			cpu->pc = X80_I85_INT_7_5;
		}
		break;
	}
}

/* TODO: the documentation is unclear what happens to the interrupt pending masks */

INLINE uint8_t read_interrupt_mask(x80_state_t * cpu)
{
	/* RIM */
	return (cpu->i85.imask & 0x7F & ~((cpu->i85.imask & 0x03) << 4)) | (cpu->input_serial ? cpu->input_serial(cpu) << 7 : 0);
}

INLINE void write_interrupt_mask(x80_state_t * cpu, uint8_t value)
{
	/* SIM */
	if((value & 0x08))
	{
		cpu->i85.imask = (cpu->i85.imask & ~(X80_I85_IM_M5_5 | X80_I85_IM_M6_5 | X80_I85_IM_M7_5)) | (value & (X80_I85_IM_M5_5 | X80_I85_IM_M6_5 | X80_I85_IM_M7_5));
	}
	if((value & 0x10))
	{
		cpu->i85.imask &= ~X80_I85_IM_I7_5;
	}
	if((value & 0x40) && cpu->output_serial)
	{
		cpu->output_serial(cpu, value >> 7);
	}
}

#define INT_DEFAULT 0

INLINE void disable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->i85.imask &= ~X80_I85_IM_IE;
}

INLINE void enable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->i85.imask |= X80_I85_IM_IE;
}

PUBLIC void PREFIXED(show_regs)(x80_state_t * cpu, bool only_system)
{
	if(!only_system)
	{
		printf("PSW = %04X\n", cpu->af);
		printf("C = %d, V = %d, P = %d, AC = %d, UI = %d, Z = %d, S = %d\n",
			(cpu->af & F_C) != 0,
			(cpu->af & F_V) != 0,
			(cpu->af & F_P) != 0,
			(cpu->af & F_H) != 0,
			(cpu->af & F_UI) != 0,
			(cpu->af & F_Z) != 0,
			(cpu->af & F_S) != 0);
		printf("B   = %04X\n", cpu->bc);
		printf("D   = %04X\n", cpu->de);
		printf("H   = %04X\n", cpu->hl);
	}

	printf("SP = %04X\n", cpu->sp);
	printf("PC = %04X\n", cpu->pc);
	printf("IM = %02X\n", cpu->i85.imask);
	printf("IE = %d\n", (cpu->i85.imask & X80_I85_IM_IE) != 0);
}

