
#define CPU_ID ez80

#define ZILOG 1

/* TODO: ez80 flags, unknown */
#define AF_MASK 0xFFFF
#define AF_ONES 0x0000
#define F_C 0x01
#define F_N 0x02
#define F_P 0x04
#define F_V 0x04
#define F_H 0x10
#define F_Z 0x40
#define F_S 0x80
#define AF_COPY 0x28 /* TODO: what happens to the undefined bits */

#define F_BITS_CLR (F_H | F_N)
#define F_SCF_CLR (F_H | F_N)
#define F_CCF_CLR (F_H | F_N)

#define ADDRESS_MASK 0x00FFFFFF
#define IOADDRESS_MASK 0x0000FFFF

#define GETSHORT(a) ((a)&0xFFFF)
#define GETLONG(a) ((a)&0xFFFFFF)
#define SETSHORT(a, b) ((a) = GETSHORT(b)) /* high 8 bits are undefined, we will zero it out */
#define SETLONG(a, b) ((a) = GETLONG(b))
#define ADDSHORT(a, d) GETSHORT((a) + (d))
#define ADDLONG(a, d) ((a) + (d))
#define GETADDR(a) (cpu->ez80.adl?GETLONG(a):GETSHORT(a))
#define SETADDR(a, b) (cpu->ez80.adl?SETLONG((a),(b)):SETSHORT((a),(b)))
#define ADDADDR(a, d) (cpu->ez80.adl?(a)+(d):ADDSHORT((a),(d)))

#define TRAP_UNDEFOP X80_EZ80_TRAP_UNDEFOP

#define wordsize wordsize
INLINE size_t wordsize(x80_state_t * cpu)
{
	return cpu->ez80.adl ? 3 : 2;
}

INLINE address_t readword_short(x80_state_t * cpu, size_t bytes, address_t address)
{
	return readword_access(cpu, bytes, address, X80_ACCESS_MODE_SHORT);
}

INLINE address_t readword_long(x80_state_t * cpu, size_t bytes, address_t address)
{
	return readword_access(cpu, bytes, address, X80_ACCESS_MODE_LONG);
}

INLINE void writeword_short(x80_state_t * cpu, size_t bytes, address_t address, address_t value)
{
	writeword_access(cpu, bytes, address, value, X80_ACCESS_MODE_SHORT);
}

INLINE void writeword_long(x80_state_t * cpu, size_t bytes, address_t address, address_t value)
{
	writeword_access(cpu, bytes, address, value, X80_ACCESS_MODE_LONG);
}

/* accessing the interrupt mode bits */
INLINE uint8_t getim(x80_state_t * cpu)
{
	return cpu->ez80.im;
}

INLINE void setim(x80_state_t * cpu, uint8_t value)
{
	cpu->ez80.im = value;
}

/* switching AF registers */
INLINE bool getafp(x80_state_t * cpu)
{
	return cpu->ez80.afp;
}

INLINE void flipafp(x80_state_t * cpu)
{
	uint16_t af;

	af = cpu->af;
	cpu->af = cpu->af2;
	cpu->af2 = af;

	cpu->ez80.afp ^= 1;
}

/* switching BC/DE/HL register banks */
INLINE bool getalt(x80_state_t * cpu)
{
	return cpu->ez80.alt;
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

	cpu->ez80.alt ^= 1;
}

INLINE uint32_t get_short_address(x80_state_t * cpu, address_t address, int access)
{
	return (address & 0xFFFF) | (cpu->ez80.mbase << 16);
}

INLINE uint32_t get_long_address(x80_state_t * cpu, address_t address, int access)
{
	return address & 0xFFFFFF;
}

#define get_address get_address
INLINE uint32_t get_address(x80_state_t * cpu, address_t address, int access)
{
	if((access & (X80_ACCESS_MODE_PORT)))
	{
		return address & 0xFFFF;
	}
	else if((access & (X80_ACCESS_MODE_EXEC | X80_ACCESS_MODE_STACK)))
	{
		if(cpu->ez80.adl)
		{
			return get_long_address(cpu, address, access);
		}
		else
		{
			return get_short_address(cpu, address, access);
		}
	}
	else if((access & X80_ACCESS_MODE_DIRECT))
	{
		return get_long_address(cpu, address, access);
	}
	else
	{
		return get_short_address(cpu, address, access);
	}
}

/* these must be called when changing MSR.US or accessing USP/SSP directly */

INLINE void ez80_sync_registers(x80_state_t * cpu)
{
	if(cpu->ez80.adl)
		cpu->ez80.spl = cpu->sp;
	else
		cpu->ez80.sps = cpu->sp;
}

INLINE void ez80_reload_registers(x80_state_t * cpu)
{
	if(cpu->ez80.adl)
		cpu->sp = cpu->ez80.spl;
	else
		cpu->sp = cpu->ez80.sps;
}

INLINE uint8_t readbyte_long(x80_state_t * cpu, uint32_t address)
{
	return cpu->read_byte(cpu, get_long_address(cpu, address, X80_ACCESS_TYPE_READ));
}

INLINE void writebyte_long(x80_state_t * cpu, uint32_t address, uint8_t value)
{
	cpu->write_byte(cpu, get_long_address(cpu, address, X80_ACCESS_TYPE_WRITE), value);
}

INLINE uint8_t readbyte_short(x80_state_t * cpu, uint32_t address)
{
	return cpu->read_byte(cpu, get_short_address(cpu, address, X80_ACCESS_TYPE_READ));
}

INLINE void writebyte_short(x80_state_t * cpu, uint32_t address, uint8_t value)
{
	cpu->write_byte(cpu, get_short_address(cpu, address, X80_ACCESS_TYPE_WRITE), value);
}

INLINE address_t popword_short(x80_state_t * cpu, size_t bytes)
{
	ez80_sync_registers(cpu);
	address_t value = readword_short(cpu, bytes, cpu->ez80.sps);
	cpu->ez80.sps = (cpu->ez80.sps + bytes) & 0xFFFF;
	ez80_reload_registers(cpu);
	return value;
}

INLINE void pushword_short(x80_state_t * cpu, size_t bytes, address_t value)
{
	ez80_sync_registers(cpu);
	cpu->ez80.sps = (cpu->ez80.sps - bytes) & 0xFFFF;
	writeword_short(cpu, bytes, cpu->ez80.sps, value);
	ez80_reload_registers(cpu);
}

INLINE address_t popword_long(x80_state_t * cpu, size_t bytes)
{
	ez80_sync_registers(cpu);
	address_t value = readword_long(cpu, bytes, cpu->ez80.spl);
	cpu->ez80.spl = (cpu->ez80.spl + bytes) & 0xFFFFFF;
	ez80_reload_registers(cpu);
	return value;
}

INLINE void pushword_long(x80_state_t * cpu, size_t bytes, address_t value)
{
	ez80_sync_registers(cpu);
	cpu->ez80.spl = (cpu->ez80.spl - bytes) & 0xFFFFFF;
	writeword_long(cpu, bytes, cpu->ez80.spl, value);
	ez80_reload_registers(cpu);
}

PUBLIC void PREFIXED(reset)(x80_state_t * cpu)
{
	cpu->af = (cpu->af & AF_MASK) | AF_ONES;

	cpu->ez80.sps = 0x0000;
	cpu->ez80.spl = 0x000000;
	cpu->pc = 0x000000;
	cpu->ir = 0x000000;
	cpu->ix = 0x000000;
	cpu->iy = 0x000000;
	cpu->ez80.mbase = 0x000000;
	cpu->ez80.adl = cpu->ez80.madl = 0;

	cpu->ez80.ie1 = cpu->ez80.ie2 = 0;
	/* TODO */

	ez80_reload_registers(cpu);
}

PRIVATE void do_jump_size(x80_state_t * cpu, int size, address_t target)
{
	switch(size)
	{
	case 1:
		do_jump(cpu, target);
		break;
	case 2:
		/* JUMP.SIS */
		cpu->pc = target & 0xFFFF;
		cpu->ez80.adl = 0;
		break;
	case 3:
		/* JUMP.LIL */
		cpu->pc = target & 0xFFFFFF;
		cpu->ez80.adl = 1;
		break;
	}
}

INLINE void do_call_short(x80_state_t * cpu, address_t target)
{
	/* CALL.SIS, CALL.LIS */
	pushword_short(cpu, 2, cpu->pc);
	if(cpu->ez80.adl)
	{
		pushword_long(cpu, 1, cpu->pc >> 16);
		pushword_long(cpu, 1, 0x03);
	}
	else
	{
		pushword_long(cpu, 1, 0x02);
	}
	cpu->pc = target & 0xFFFF;
	cpu->ez80.adl = 0;
}

INLINE void do_call_long(x80_state_t * cpu, address_t target, int byte)
{
	/* CALL.SIL, CALL.LIL */
	pushword_long(cpu, wordsize(cpu), cpu->pc);
	if(cpu->ez80.adl)
	{
		pushword_long(cpu, 1, byte | 1);
	}
	else
	{
		pushword_long(cpu, 1, byte & ~1);
	}
	cpu->pc = target & 0xFFFFFF;
	cpu->ez80.adl = 1;
}

PRIVATE void do_call_size(x80_state_t * cpu, int size, address_t target)
{
	switch(size)
	{
	case 1:
		do_call(cpu, target);
		break;
	case 2:
		do_call_short(cpu, target);
		break;
	case 3:
		do_call_long(cpu, target, 0x02);
		break;
	}
}

PRIVATE void do_ret_long(x80_state_t * cpu)
{
	/* RET.LIS, RET.LIL */
	if((popword_long(cpu, 1) & 1) == 0)
	{
		cpu->pc = popword(cpu, 2);
		cpu->ez80.adl = 0;
	}
	else
	{
		if(cpu->ez80.adl == 1)
		{
			cpu->pc = popword(cpu, 3);
		}
		else
		{
			cpu->pc = popword_long(cpu, 1) << 16;
			cpu->pc |= popword_short(cpu, 2);
			cpu->ez80.adl = 0;
		}
	}
	/* TODO: default? */
}

PUBLIC void PREFIXED(exception_nmi)(x80_state_t * cpu, int data)
{
	cpu->ez80.ie2 = cpu->ez80.ie1;
	cpu->ez80.ie1 = 0;

	if(cpu->ez80.madl)
	{
		do_call_long(cpu, 0x000066, 0x02);
	}
	else
	{
		do_call(cpu, 0x0066);
	}
}

PUBLIC void PREFIXED(exception_extint)(x80_state_t * cpu, int number, int data, ...)
{
	va_list list;
	if(cpu->ez80.ie1 == 1)
	{
		switch(getim(cpu))
		{
		case 0:
			switch(data)
			{
			case 0xC7:
			case 0xCF:
			case 0xD7:
			case 0xDF:
			case 0xE7:
			case 0xEF:
			case 0xF7:
			case 0xFF:
				data &= 0x3F;
				break;
			case 0xCD:
				data &= 0xFF;
				va_start(list, data);
				data |= (va_arg(list, int) & 0xFF) << 8;
				if(cpu->ez80.adl || cpu->ez80.madl)
				{
					data |= (va_arg(list, int) & 0xFF) << 16;
				}
				va_end(list);
				break;
			default:
				/* treated as NOP */
				return;
			}
			break;
		case 1:
			data = 0x000038;
			break;
		case 2:
			if(cpu->ez80.adl || cpu->ez80.madl)
			{
				data = readword_long(cpu, 3, (cpu->ir & 0xFFFF00) | (data & 0x0000FF));
			}
			else
			{
				data = readword_short(cpu, 2, (cpu->ir & 0xFF00) | (data & 0x00FF));
			}
			break;
		}
		cpu->ez80.ie1 = cpu->ez80.ie2 = 0;
		if(cpu->ez80.madl)
		{
			do_call_long(cpu, data, getim(cpu) == 2 ? 0x00 : 0x02);
		}
		else
		{
			do_call(cpu, data);
		}
	}
}

PUBLIC void PREFIXED(exception_intint)(x80_state_t * cpu, int number)
{
	int data;

	if(cpu->ez80.ie1 == 1)
	{
		if(cpu->ez80.adl || cpu->ez80.madl)
		{
			data = readword_long(cpu, 3, (cpu->ir & 0xFFFF00) | cpu->ez80.ivect);
		}
		else
		{
			data = readword_short(cpu, 2, (cpu->ir & 0xFF00) | cpu->ez80.ivect);
		}
		cpu->ez80.ie1 = cpu->ez80.ie2 = 0;
		if(cpu->ez80.madl)
		{
			do_call_long(cpu, data, getim(cpu) == 2 ? 0x00 : 0x02);
		}
		else
		{
			do_call(cpu, data);
		}
	}
}

PRIVATE void exception_trap(x80_state_t * cpu, int number, ...)
{
	switch(number)
	{
	case X80_EZ80_TRAP_UNDEFOP:
		cpu->ez80.ie1 = cpu->ez80.ie2 = 0;
		if(cpu->ez80.madl)
		{
			do_call_long(cpu, 0x000000, getim(cpu) == 2 ? 0x00 : 0x02);
		}
		else
		{
			do_call(cpu, 0x000000);
		}
		break;
	}
	/* terminate processing of instructions */
	longjmp(cpu->exc, 1);
}

#define INT_DEFAULT 0

INLINE void disable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->ez80.ie1 = cpu->ez80.ie2 = 0;
}

INLINE void enable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->ez80.ie1 = cpu->ez80.ie2 = 1;
}

INLINE void do_before_retn(x80_state_t * cpu)
{
	cpu->ez80.ie1 = cpu->ez80.ie2;
}

PUBLIC void PREFIXED(show_regs)(x80_state_t * cpu, bool only_system)
{
	ez80_sync_registers(cpu);

	if(!only_system)
	{
		printf("AF = %04X\tAF' = %04X\n", cpu->af, cpu->af2);
		printf("C = %d, N = %d, P/V = %d, H = %d, Z = %d, S = %d\n",
			(cpu->af & F_C) != 0,
			(cpu->af & F_N) != 0,
			(cpu->af & F_P) != 0,
			(cpu->af & F_H) != 0,
			(cpu->af & F_Z) != 0,
			(cpu->af & F_S) != 0);
		printf("BC = %06X\tBC' = %06X\n", cpu->bc, cpu->bc2);
		printf("DE = %06X\tDE' = %06X\n", cpu->de, cpu->de2);
		printf("HL = %06X\tHL' = %06X\n", cpu->hl, cpu->hl2);
		printf("IX = %06X\n", cpu->ix);
		printf("IY = %06X\n", cpu->iy);
	}

	printf("SPS%c= %04X\tSPL%c= %06X\n", cpu->ez80.adl == 0 ? '*' : ' ', cpu->ez80.sps, cpu->ez80.adl == 1 ? '*' : ' ', cpu->ez80.spl);
	printf("PC = %06X\n", cpu->pc);

	if(!only_system)
	{
		printf("I  = %04X\tR   = %02X\n", cpu->ir >> 8, cpu->ir & 0xFF);

		printf("IM = %d\tMBASE = %02X\n", cpu->ez80.im, cpu->ez80.mbase);
	}

	printf("IEF1 = %d, IEF2 = %d, ADL = %d, MADL = %d\n", cpu->ez80.ie1, cpu->ez80.ie2, cpu->ez80.adl, cpu->ez80.madl);

	/* TODO: ivect */
}

