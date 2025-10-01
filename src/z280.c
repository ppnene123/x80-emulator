
#define CPU_ID z280

#define ZILOG 1

#define AF_MASK 0xFFD7 /* z280: documented */
#define AF_ONES 0x0000
#define F_C 0x01
#define F_N 0x02
#define F_P 0x04
#define F_V 0x04
#define F_H 0x10
#define F_Z 0x40
#define F_S 0x80
/* other Zilog CPUs need a separate command because of the undocumented flags */
//#define F_VAL_MASK 0x28 /* bits copied to the result */

#define F_BITS_CLR (F_H | F_N)
#define F_SCF_CLR (F_H | F_N)
#define F_CCF_CLR (F_H | F_N)

#define ADDRESS_MASK 0xFFFF
#define IOADDRESS_MASK 0xFFFF

/* traps and interrupts */

INLINE address_t readword_direct(x80_state_t * cpu, size_t bytes, address_t address)
{
	return readword_access(cpu, bytes, address, X80_ACCESS_MODE_DIRECT);
}

INLINE void writeword_direct(x80_state_t * cpu, size_t bytes, address_t address, address_t value)
{
	writeword_access(cpu, bytes, address, value, X80_ACCESS_MODE_DIRECT);
}

/* selecting between user/system mode */

PRIVATE bool isusermode(x80_state_t * cpu)
{
	return cpu->z280.ctl[X80_Z280_CTL_MSR] & X80_Z280_MSR_US;
}

/* these must be called when changing MSR.US or accessing USP/SSP directly */

INLINE void z280_sync_registers(x80_state_t * cpu)
{
	if(isusermode(cpu))
		cpu->z280.usp = cpu->sp;
	else
		cpu->z280.ssp = cpu->sp;
}

INLINE void z280_reload_registers(x80_state_t * cpu)
{
	if(isusermode(cpu))
		cpu->sp = cpu->z280.usp;
	else
		cpu->sp = cpu->z280.ssp;
}

/* accessing the interrupt mode bits */
INLINE int getim(x80_state_t * cpu)
{
	return (cpu->z280.ctl[X80_Z280_CTL_ISR] & X80_Z280_ISR_IM_MASK) >> X80_Z280_ISR_IM_SHIFT;
}

INLINE void setim(x80_state_t * cpu, int value)
{
	cpu->z280.ctl[X80_Z280_CTL_ISR] = (cpu->z280.ctl[X80_Z280_CTL_ISR] & ~X80_Z280_ISR_IM_MASK) | ((value << X80_Z280_ISR_IM_SHIFT) & X80_Z280_ISR_IM_MASK);
}

/* switching AF registers */
INLINE bool getafp(x80_state_t * cpu)
{
	return cpu->z280.afp;
}

INLINE void flipafp(x80_state_t * cpu)
{
	uint16_t af;

	af = cpu->af;
	cpu->af = cpu->af2;
	cpu->af2 = af;

	cpu->z280.afp ^= 1;
}

/* switching BC/DE/HL register banks */
INLINE bool getalt(x80_state_t * cpu)
{
	return cpu->z280.alt;
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

	cpu->z280.alt ^= 1;
}

/* memory address mapping */
PRIVATE uint32_t getpage(x80_state_t * cpu, unsigned page, int access)
{
	if(!(access & X80_ACCESS_MODE_DEBUG)) // in debug mode, avoid access checks
	{
		if(!(cpu->z280.pdr[page] & 8))
		{
			/* invalid */
			exception_trap(cpu, X80_Z280_TRAP_AV, page);
		}
		if((access & (X80_ACCESS_MODE_READ|X80_ACCESS_MODE_WRITE)) == X80_ACCESS_MODE_WRITE)
		{
			if((cpu->z280.pdr[page] & 4))
			{
				/* write protected */
				exception_trap(cpu, X80_Z280_TRAP_AV, page);
			}
			cpu->z280.pdr[page] |= 1; /* modified */
		}
	}
	return (uint32_t)(cpu->z280.pdr[page] & 0xFFF0) << 12;
}

/* the Z280 separates program and data space */
PRIVATE uint32_t getuseraddress(x80_state_t * cpu, address_t address, int access)
{
	if((cpu->z280.ior[X80_Z280_IOR_MCR] & 0x8000))
	{
		if(!(cpu->z280.ior[X80_Z280_IOR_MCR] & 0x4000))
		{
			/* no program/data separation */
			return getpage(cpu, (address >> 12) + 16, access) + (address & 0x0FFF);
		}
		else if((access & X80_ACCESS_MODE_EXEC))
		{
			/* program memory */
			return getpage(cpu, (address >> 13) + 24, access) + (address & 0x0FFF);
		}
		else
		{
			/* data memory */
			return getpage(cpu, (address >> 13) + 16, access) + (address & 0x0FFF);
		}
	}
	else
	{
		return address;
	}
}

#define get_address get_address
PRIVATE uint32_t get_address(x80_state_t * cpu, address_t address, int access)
{
	if((access & X80_ACCESS_MODE_PORT))
	{
		return ((ioaddress_t)cpu->z280.ctl[X80_Z280_CTL_IOPR] << 16) | address;
	}
	else if(isusermode(cpu))
	{
		return getuseraddress(cpu, address, access);
	}
	else
	{
		if((cpu->z280.ior[X80_Z280_IOR_MCR] & 0x0800))
		{
			if(!(cpu->z280.ior[X80_Z280_IOR_MCR] & 0x0400))
			{
				/* no program/data separation */
				return getpage(cpu, (address >> 12), access) + (address & 0x0FFF);
			}
			else if((access & X80_ACCESS_MODE_EXEC))
			{
				/* program memory */
				return getpage(cpu, (address >> 13) + 8, access) + (address & 0x0FFF);
			}
			else
			{
				/* data memory */
				return getpage(cpu, (address >> 13), access) + (address & 0x0FFF);
			}
		}
		else
		{
			return address;
		}
	}
}

INLINE uint8_t readbyte_direct(x80_state_t * cpu, uint32_t address)
{
	return cpu->read_byte(cpu, address & 0xFFFFFF);
}

INLINE void writebyte_direct(x80_state_t * cpu, uint32_t address, uint8_t value)
{
	cpu->write_byte(cpu, address & 0xFFFFFF, value);
}

/* the Z280 has on-chip I/O ports */
#define outputbyte_cpu outputbyte_cpu
INLINE bool outputbyte_cpu(x80_state_t * cpu, address_t address, uint8_t value)
{
	int i;
	if(cpu->z280.ctl[X80_Z280_CTL_IOPR] == 0xFF)
	{
		switch((address & 0xFF))
		{
		case X80_Z280_IOR_IP:
			for(i = 0; i < 32; i++)
			{
				if((value & (1 << (i >> 3))))
				{
					cpu->z280.pdr[i] &= ~8; /* invalidated */
				}
			}
			return true;
		/* TODO: verify if accessing word I/O ports as byte I/O ports matters */
		default:
			return false;
		}
	}
	else
	{
		return false;
	}
}

#define inputword_cpu inputword_cpu
INLINE bool inputword_cpu(x80_state_t * cpu, address_t address, uint16_t * result)
{
	if(cpu->z280.ctl[X80_Z280_CTL_IOPR] == 0xFF)
	{
		switch((address & 0xFF))
		{
		default:
			*result = cpu->z280.ior[address & 0xFF];
			return true;
		case X80_Z280_IOR_DSP:
			*result = cpu->z280.pdr[cpu->z280.ior[X80_Z280_IOR_DSP] & 0x1F];
			return true;
		case X80_Z280_IOR_BMP:
			*result = cpu->z280.pdr[cpu->z280.ior[X80_Z280_IOR_DSP] & 0x1F];
			cpu->z280.ior[X80_Z280_IOR_DSP] = (cpu->z280.ior[X80_Z280_IOR_DSP] + 1) & 0x1F;
			return true;
		/* TODO: verify accessing byte ports */
		}
	}
	else
	{
		return false;
	}
}

#define outputword_cpu outputword_cpu
INLINE bool outputword_cpu(x80_state_t * cpu, address_t address, uint16_t value)
{
	if(cpu->z280.ctl[X80_Z280_CTL_IOPR] == 0xFF)
	{
		switch((address & 0xFF))
		{
		default:
			cpu->z280.ior[address & 0xFF] = value;
			return true;
		case X80_Z280_IOR_DSP:
			cpu->z280.pdr[cpu->z280.ior[X80_Z280_IOR_DSP] & 0x1F] = value;
			return true;
		case X80_Z280_IOR_BMP:
			value = cpu->z280.pdr[cpu->z280.ior[X80_Z280_IOR_DSP] & 0x1F];
			cpu->z280.ior[X80_Z280_IOR_DSP] = (cpu->z280.ior[X80_Z280_IOR_DSP] + 1) & 0x1F;
			return true;
		/* TODO: verify accessing byte ports */
		}
	}
	else
	{
		return false;
	}
}

PUBLIC void PREFIXED(reset)(x80_state_t * cpu)
{
	cpu->af = (cpu->af & AF_MASK) | AF_ONES;

	cpu->z280.ssp = 0x0000;
	cpu->pc = 0x0000;
	cpu->ir = 0x0000;

	cpu->z280.ctl[X80_Z280_CTL_MSR] = 0x0000;
	cpu->z280.ctl[X80_Z280_CTL_BTCR] = 0x30;
	cpu->z280.ctl[X80_Z280_CTL_BTIR] = 0x80;
	cpu->z280.ctl[X80_Z280_CTL_IOPR] = 0x00;
	cpu->z280.ctl[X80_Z280_CTL_CCR] = 0x20;
	cpu->z280.ctl[X80_Z280_CTL_TCR] = 0x00;
	cpu->z280.ctl[X80_Z280_CTL_SSLR] = 0x0000;
	cpu->z280.ctl[X80_Z280_CTL_LAR] = 0x0000;
	cpu->z280.ctl[X80_Z280_CTL_ISR] &= 0x007F;

	cpu->z280.ior[X80_Z280_IOR_MCR] = 0x0000;

	/* TODO: others */

	z280_reload_registers(cpu);
}

INLINE void exception_load_vector(x80_state_t * cpu, int msroff, int pcoff)
{
	cpu->z280.ctl[X80_Z280_CTL_MSR] = readword_direct(cpu, 2, ((cpu->z280.ctl[X80_Z280_CTL_ITVTP] & 0xFFF0) << 8) + msroff);
	cpu->pc = readword_direct(cpu, 2, ((cpu->z280.ctl[X80_Z280_CTL_ITVTP] & 0xFFF0) << 8) + pcoff);
}

INLINE int exception_enter_system(x80_state_t * cpu)
{
	z280_sync_registers(cpu);
	int msr = cpu->z280.ctl[X80_Z280_CTL_MSR];
	cpu->z280.ctl[X80_Z280_CTL_MSR] &= ~X80_Z280_MSR_US;
	z280_reload_registers(cpu);
	return msr;
}

INLINE void exception_extended(x80_state_t * cpu, uint16_t reason, int msroff, int pcoff)
{
	int msr = exception_enter_system(cpu); /* make sure we are in system mode */
	pushword(cpu, 2, cpu->pc);
	pushword(cpu, 2, msr);
	pushword(cpu, 2, reason);
	exception_load_vector(cpu, msroff, pcoff);
}

INLINE void do_retil(x80_state_t * cpu)
{
	int msr = popword(cpu, 2);
	cpu->pc = popword(cpu, 2);
	if((cpu->z280.ctl[X80_Z280_CTL_MSR] & X80_Z280_MSR_SSP))
		msr |= X80_Z280_MSR_SSP;
	cpu->z280.ctl[X80_Z280_CTL_MSR] = msr;
}

INLINE void exception_fatal(x80_state_t * cpu)
{
	cpu->hl = cpu->pc;
	cpu->de = cpu->z280.ctl[X80_Z280_CTL_MSR];
	cpu->z280.ctl[X80_Z280_CTL_MSR] &= 0xFF80;
	/* TODO: halt state */
}

PUBLIC void PREFIXED(exception_nmi)(x80_state_t * cpu, int data)
{
	if(getim(cpu) != 3)
	{
		z280_sync_registers(cpu);
		cpu->z280.sir = cpu->z280.ctl[X80_Z280_CTL_MSR] & 0x1F;
		cpu->z280.ctl[X80_Z280_CTL_MSR] &= ~(X80_Z280_MSR_US | X80_Z280_MSR_SS | 0x1F);
		z280_reload_registers(cpu);

		do_call(cpu, 0x0066);
	}
	else
	{
		exception_extended(cpu, data, X80_Z280_TRAP_NMI,
			cpu->z280.ctl[X80_Z280_CTL_ISR] & X80_Z280_ISR_NMI ? X80_Z280_TRAP_NMIBASE + (data & 0xFF) : X80_Z280_TRAP_NMI + 2);
	}
}

PUBLIC void PREFIXED(exception_extint)(x80_state_t * cpu, int number, int count, void * data)
{
	uint8_t reason;
	switch(number)
	{
	case 0:
		if(!(cpu->z280.ctl[X80_Z280_CTL_MSR] & X80_Z280_MSR_INTA))
			return;
		break;
	case 1:
		if(!(cpu->z280.ctl[X80_Z280_CTL_MSR] & X80_Z280_MSR_INTB))
			return;
		break;
	case 2:
		if(!(cpu->z280.ctl[X80_Z280_CTL_MSR] & X80_Z280_MSR_INTC))
			return;
		break;
	}
	switch(getim(cpu))
	{
	case 0:
		z280_sync_registers(cpu);
		cpu->z280.ctl[X80_Z280_CTL_MSR] &= ~(X80_Z280_MSR_US | X80_Z280_MSR_SS | 0x1F);
		z280_reload_registers(cpu);

		interpret_input(cpu, count, data);
		break;
	case 1:
		z280_sync_registers(cpu);
		cpu->z280.ctl[X80_Z280_CTL_MSR] &= ~(X80_Z280_MSR_US | X80_Z280_MSR_SS | 0x1F);
		z280_reload_registers(cpu);

		pushword(cpu, 2, cpu->pc);
		cpu->pc = 0x0038;
		break;
	case 2:
		z280_sync_registers(cpu);
		cpu->z280.ctl[X80_Z280_CTL_MSR] &= ~(X80_Z280_MSR_US | X80_Z280_MSR_SS | 0x1F);
		z280_reload_registers(cpu);
		pushword(cpu, 2, cpu->pc);
		cpu->pc = readword(cpu, 2, (cpu->ir & 0xFF00) | (((uint8_t *)data)[0] & 0x00FF));
		break;
	case 3:
		reason = ((uint8_t *)data)[0];
		switch(number)
		{
		case 0:
			exception_extended(cpu, reason, X80_Z280_TRAP_INTA,
				cpu->z280.ctl[X80_Z280_CTL_ISR] & X80_Z280_ISR_INTA ? X80_Z280_TRAP_INTABASE + (reason & 0xFF) : X80_Z280_TRAP_INTA + 2);
			break;
		case 1:
			exception_extended(cpu, reason, X80_Z280_TRAP_INTB,
				cpu->z280.ctl[X80_Z280_CTL_ISR] & X80_Z280_ISR_INTB ? X80_Z280_TRAP_INTBBASE + (reason & 0xFF) : X80_Z280_TRAP_INTB + 2);
			break;
		case 2:
			exception_extended(cpu, reason, X80_Z280_TRAP_INTC,
				cpu->z280.ctl[X80_Z280_CTL_ISR] & X80_Z280_ISR_INTC ? X80_Z280_TRAP_INTCBASE + (reason & 0xFF) : X80_Z280_TRAP_INTC + 2);
			break;
		}
		break;
	}
}

PUBLIC void PREFIXED(exception_intint)(x80_state_t * cpu, int number)
{
	exception_extended(cpu, number, number, number + 2);
}

INLINE void exception_trap(x80_state_t * cpu, int number, ...)
{
	int msr = exception_enter_system(cpu);
	va_list list;
	switch(number)
	{
	case X80_Z280_TRAP_EPUM:
	case X80_Z280_TRAP_MEPU:
	case X80_Z280_TRAP_EPUF:
	case X80_Z280_TRAP_EPUI:
		pushword(cpu, 2, cpu->pc);
		pushword(cpu, 2, msr);
		va_start(list, number);
		if(number == X80_Z280_TRAP_EPUM || number == X80_Z280_TRAP_MEPU)
		{
			/* memory operand */
			pushword(cpu, 2, va_arg(list, int));
		}
		/* template address */
		pushword(cpu, 2, va_arg(list, int));
		va_end(list);
		break;
	case X80_Z280_TRAP_PI:
		/* privileged instruction */
		msr &= ~X80_Z280_MSR_SSP;
		pushword(cpu, 2, cpu->old_pc);
		pushword(cpu, 2, msr);
		break;
	case X80_Z280_TRAP_SC:
		/* system call */
		pushword(cpu, 2, cpu->pc);
		pushword(cpu, 2, msr);
		va_start(list, number);
		/* system call operand */
		pushword(cpu, 2, va_arg(list, int));
		va_end(list);
		break;
	case X80_Z280_TRAP_AV:
		/* access violation */
		msr &= ~X80_Z280_MSR_SSP;
		pushword(cpu, 2, cpu->old_pc);
		pushword(cpu, 2, msr);
		va_start(list, number);
		/* faulting address */
		cpu->z280.ior[X80_Z280_IOR_MCR] = (cpu->z280.ior[X80_Z280_IOR_MCR] & 0xFFE0) | (va_arg(list, int) & 0x001F);
		va_end(list);
		break;
	case X80_Z280_TRAP_SSOW:
		/* system stack overflow */
		cpu->z280.ctl[X80_Z280_CTL_TCR] &= ~X80_Z280_TCR_S;
		pushword(cpu, 2, cpu->pc);
		pushword(cpu, 2, msr);
		break;
	case X80_Z280_TRAP_DE:
		/* division exception */
		msr &= ~X80_Z280_MSR_SSP;
		pushword(cpu, 2, cpu->old_pc);
		pushword(cpu, 2, msr);
		break;
	case X80_Z280_TRAP_SS:
		/* single step */
		msr &= ~X80_Z280_MSR_SSP;
		pushword(cpu, 2, cpu->old_pc);
		pushword(cpu, 2, msr);
		break;
	case X80_Z280_TRAP_BPOH:
		/* breakpoint on halt */
		msr &= ~X80_Z280_MSR_SSP;
		pushword(cpu, 2, cpu->old_pc);
		pushword(cpu, 2, msr);
		break;
	}
	exception_load_vector(cpu, number, number + 2);
	/* terminate processing of instructions */
	longjmp(cpu->exc, 1);
}

#define INT_DEFAULT -1

INLINE void disable_interrupts(x80_state_t * cpu, int bits)
{
	cpu->z280.ctl[X80_Z280_CTL_MSR] &= ~(bits & 0x1F);
}

INLINE void enable_interrupts(x80_state_t * cpu, int bits)
{
	cpu->z280.ctl[X80_Z280_CTL_MSR] &= 0xFF80;
	cpu->z280.ctl[X80_Z280_CTL_MSR] |= bits & 0x1F;
}

INLINE void do_before_retn(x80_state_t * cpu)
{
	cpu->z280.ctl[X80_Z280_CTL_MSR] = (cpu->z280.ctl[X80_Z280_CTL_MSR] & 0xFF80) | (cpu->z280.sir & 0x1F);
}

PUBLIC void PREFIXED(show_regs)(x80_state_t * cpu, bool only_system)
{
	z280_sync_registers(cpu);

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
		printf("BC = %04X\tBC' = %04X\n", cpu->bc, cpu->bc2);
		printf("DE = %04X\tDE' = %04X\n", cpu->de, cpu->de2);
		printf("HL = %04X\tHL' = %04X\n", cpu->hl, cpu->hl2);
		printf("IX = %04X\n", cpu->ix);
		printf("IY = %04X\n", cpu->iy);
	}

	printf("SSP%c= %04X\tUSP%c= %04X\n", isusermode(cpu) == 0 ? '*' : ' ', cpu->z280.ssp, isusermode(cpu) == 1 ? '*' : ' ', cpu->z280.usp);
	printf("PC = %04X\n", cpu->pc);
	printf("MSR = %04X, SIR = %02X\n", cpu->z280.ctl[X80_Z280_CTL_MSR], cpu->z280.sir);

	if(!only_system)
	{
		printf("I  = %02X\tR   = %02X\n", cpu->ir >> 8, cpu->ir & 0xFF);
		/* TODO: CTLs, IORs, PDRs */
	}
}

