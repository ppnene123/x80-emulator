
#define CPU_ID dp

/* These correspond approximately to the result of the Datapoint 5500 CCS instruction */
#define AF_MASK 0xFFC7
#define AF_ONES 0x0000
#define F_C 0x80
#define F_P 0x01
#define F_Z 0x02
#define F_S 0x40

/* Undefined flags */
/* I80: 0 and 1 */

#define F_SUB_LIKE_ADD 1
#define F_SIMPLE_CPL 1

INLINE uint16_t memory_size_mask(x80_state_t * cpu)
{
	if(cpu->cpu_type < X80_CPU_DP5500)
		return 0x3FFF;
	else
		return 0xFFFF;
}

#define GETADDR(a) ((a) & memory_size_mask(cpu))
#define SETADDR(a, b) ((a) = ((b) & memory_size_mask(cpu)))
#define ADDADDR(a, d) (((a) + (d)) & memory_size_mask(cpu))

INLINE void sync_registers(x80_state_t * cpu)
{
	cpu->dp.xa = (cpu->dp.xa & ~0xFF) | (cpu->af >> 8);
	cpu->dp.xa2 = (cpu->dp.xa2 & ~0xFF) | (cpu->af2 >> 8);

	switch(cpu->cpu_type)
	{
	case X80_CPU_I8008:
		cpu->dp.stack[cpu->sp & 0x7] = cpu->pc;
		break;
	case X80_CPU_DP2200V1:
		cpu->dp.stack[cpu->sp & 0xF] = cpu->pc;
		break;
	default:
		break;
	}
}

INLINE void reload_registers(x80_state_t * cpu)
{
	cpu->af = (cpu->af & 0xFF) | ((cpu->dp.xa & 0xFF) << 8);
	cpu->af2 = (cpu->af2 & 0xFF) | ((cpu->dp.xa2 & 0xFF) << 8);

	switch(cpu->cpu_type)
	{
	case X80_CPU_I8008:
		cpu->pc = cpu->dp.stack[cpu->sp & 0x7];
		break;
	case X80_CPU_DP2200V1:
		cpu->pc = cpu->dp.stack[cpu->sp & 0xF];
		break;
	default:
		break;
	}
}

PUBLIC void PREFIXED(reset)(x80_state_t * cpu)
{
	sync_registers(cpu);

	cpu->af = cpu->af & AF_MASK;
	cpu->sp = 0;
	cpu->pc = 0x0000;

#if CPU == CPU_DP2200V2
	cpu->dp.ie = 0;
#endif
}

PUBLIC void PREFIXED(exception_extint)(x80_state_t * cpu, int number, int count, void * data)
{
	if(cpu->cpu_type == X80_CPU_I8008 || cpu->dp.ie == 1)
	{
		cpu->dp.ie = 0;
		do_call(cpu, number);
	}
}

INLINE void disable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	if(cpu->cpu_type != X80_CPU_I8008)
		cpu->dp.ie = 0;
}

INLINE void enable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	if(cpu->cpu_type != X80_CPU_I8008)
		cpu->dp.ie = 1;
}

/* Undefined flags */
/* I80: 0 and 1 */

#define F_SUB_LIKE_ADD 1
#define F_SIMPLE_CPL 1

PUBLIC void PREFIXED(show_regs)(x80_state_t * cpu, bool only_system)
{
	sync_registers(cpu);

	if(!only_system)
	{
		if(cpu->cpu_type < X80_CPU_DP5500)
			printf("A = %02X\n", cpu->af >> 8);
		else
			printf("XA = %04X\n", cpu->dp.xa);
		printf("C = %d, P = %d, Z = %d, S = %d\n",
			(cpu->af & F_C) != 0,
			(cpu->af & F_P) != 0,
			(cpu->af & F_Z) != 0,
			(cpu->af & F_S) != 0);
		printf("BC = %04X\n", cpu->bc);
		printf("DE = %04X\n", cpu->de);
		printf("HL = %04X\n", cpu->hl);

		if(cpu->cpu_type < X80_CPU_DP5500)
			printf("A' = %02X\n", cpu->af2 >> 8);
		else
			printf("XA' = %04X\n", cpu->dp.xa2);
		printf("C'= %d, P'= %d, Z'= %d,'S = %d\n",
			(cpu->af2 & F_C) != 0,
			(cpu->af2 & F_P) != 0,
			(cpu->af2 & F_Z) != 0,
			(cpu->af2 & F_S) != 0);
		printf("BC'= %04X\n", cpu->bc2);
		printf("DE'= %04X\n", cpu->de2);
		printf("HL'= %04X\n", cpu->hl2);
	}

	switch(cpu->cpu_type)
	{
	case X80_CPU_I8008:
		for(int i = 0; i < 8; i++)
		{
			printf("PC%d = %04X%c", i, cpu->dp.stack[(cpu->sp + i) & 0x7], i == 7 - 1 ? '\n' : '\t');
		}
		break;
	case X80_CPU_DP2200V1:
		printf("PC = %04X\n", cpu->pc);
		for(int i = 0; i < 16; i++)
		{
			printf("PC%d = %04X%c", i, cpu->dp.stack[(cpu->sp + i) & 0xF], i == 16 - 1 ? '\n' : '\t');
		}
		break;
	default:
		printf("PC = %04X\n", cpu->pc);
		for(int i = 0; i < 16; i++)
		{
			printf("PC%d = %04X%c", i, cpu->dp.stack[(cpu->sp + i) & 0xF], i == 16 - 1 ? '\n' : '\t');
		}
		printf("IE = %d\n", cpu->dp.ie);
		break;
	}
}

