
#define CPU_ID z180

#define ZILOG 1

#define AF_MASK 0xFFFF
#define AF_ONES 0x0028 /* z180: based on discussion in https://github.com/z88dk/z88dk/issues/759 */
#define F_C 0x01
#define F_N 0x02
#define F_P 0x04
#define F_V 0x04
#define F_H 0x10
#define F_Z 0x40
#define F_S 0x80
/* other Zilog CPUs need a separate command because of the undocumented flags */
//#define AF_COPY 0x28 /* bits copied to the result */

#define F_BITS_CLR (F_H | F_N)
#define F_SCF_CLR (F_H | F_N)
#define F_CCF_CLR (F_H | F_N)

#define ADDRESS_MASK 0xFFFF
#define IOADDRESS_MASK 0xFFFF

#define TRAP_UNDEFOP X80_Z180_TRAP_UNDEFOP

/* accessing the interrupt mode bits */
INLINE uint8_t getim(x80_state_t * cpu)
{
	return cpu->z180.im;
}

INLINE void setim(x80_state_t * cpu, uint8_t value)
{
	cpu->z180.im = value;
}

/* switching AF registers */
INLINE bool getafp(x80_state_t * cpu)
{
	return cpu->z180.afp;
}

INLINE void flipafp(x80_state_t * cpu)
{
	uint16_t af;

	af = cpu->af;
	cpu->af = cpu->af2;
	cpu->af2 = af;

	cpu->z180.afp ^= 1;
}

/* switching BC/DE/HL register banks */
INLINE bool getalt(x80_state_t * cpu)
{
	return cpu->z180.alt;
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

	cpu->z180.alt ^= 1;
}

/* translation of memory addresses via memory banking */
#define get_address get_address
INLINE uint32_t get_address(x80_state_t * cpu, address_t address, int access)
{
	if((access & X80_ACCESS_MODE_PORT))
	{
		return address & 0xFFFF;
	}
	else if((address >> 12) < (cpu->z180.ior[X80_Z180_IOR_CBAR] & 0xF))
	{
		/* Common Area 0 */
		return address;
	}
	else if((address >> 12) < (cpu->z180.ior[X80_Z180_IOR_CBAR] >> 4))
	{
		/* Bank Area */
		return (address + (cpu->z180.ior[X80_Z180_IOR_BBR] << 12)) & 0xFFFFF;
	}
	else
	{
		/* Common Area 1 */
		return (address + (cpu->z180.ior[X80_Z180_IOR_CBR] << 12)) & 0xFFFFF;
	}
}

/* on-chip I/O ports */

#define inputbyte_cpu inputbyte_cpu
INLINE bool inputbyte_cpu(x80_state_t * cpu, address_t address, int * result)
{
	if((address & 0xFFC0) == (cpu->z180.ior[X80_Z180_IOR_ICR] & 0xC0))
	{
		*result = cpu->z180.ior[address & 0x3F];
		return true;
	}
	else
	{
		return false;
	}
}

#define outputbyte_cpu outputbyte_cpu
INLINE bool outputbyte_cpu(x80_state_t * cpu, address_t address, uint8_t value)
{
	if((address & 0xFFC0) == (cpu->z180.ior[X80_Z180_IOR_ICR] & 0xC0))
	{
		/* TODO: some bits should not be modified */
		cpu->z180.ior[address & 0x3F] = value;
		return true;
	}
	else
	{
		return false;
	}
}

PUBLIC void PREFIXED(reset)(x80_state_t * cpu)
{
	cpu->af = (cpu->af & AF_MASK) | AF_ONES;
	cpu->sp = 0x0000;
	cpu->pc = 0x0000;
	cpu->ir = 0x0000;

	cpu->z180.ie1 = cpu->z180.ie2 = 0;

	cpu->z180.ior[X80_Z180_IOR_CNTLA0] = 0x10;
	cpu->z180.ior[X80_Z180_IOR_CNTLA1] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_CNTLB0] = 0x07;
	cpu->z180.ior[X80_Z180_IOR_CNTLB1] = 0x07;
	cpu->z180.ior[X80_Z180_IOR_STAT0] = 0x04;
	cpu->z180.ior[X80_Z180_IOR_STAT1] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_TDR0] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_TDR1] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_RDR0] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_RDR1] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_CNTR] = 0x07;
	cpu->z180.ior[X80_Z180_IOR_TRD] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_TMDR0L] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_TMDR0H] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_RLDR0L] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_RLDR0H] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_TCR] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_ASEXT0] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_ASEXT1] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_TMDR1L] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_TMDR1H] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_RLDR1L] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_RLDR1H] = 0x00;
	//cpu->z180.ior[X80_Z180_IOR_FRC];
	cpu->z180.ior[X80_Z180_IOR_ASTC0L] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_ASTC0H] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_ASTC1L] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_ASTC1H] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_CMR] = 0x7F;
	cpu->z180.ior[X80_Z180_IOR_CCR] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_IAR1B] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_DSTAT] = (cpu->z180.ior[X80_Z180_IOR_DSTAT] & 0x01) | 0x30;
	cpu->z180.ior[X80_Z180_IOR_DMODE] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_DCNTL] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_IL] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_ITC] = 0x01; /* ITC_ITE0 on */
	cpu->z180.ior[X80_Z180_IOR_RCR] = 0xC0;
	cpu->z180.ior[X80_Z180_IOR_CBR] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_BBR] = 0x00;
	cpu->z180.ior[X80_Z180_IOR_CBAR] = 0xF0;
	cpu->z180.ior[X80_Z180_IOR_ICR] = 0;
	/* TODO: do we need to initialize the following registers?
		IOR_SAR0L, IOR_SAR0H, IOR_SAR0B, IOR_DAR0L, IOR_DAR0H, IOR_DAR0B, IOR_BCR0L, IOR_BCR0H, IOR_MAR0L, IOR_MAR0H, IOR_MAR0B, IOR_IAR0L, IOR_IAR0H, IOR_BCR0L, IOR_BCR0H, IOR_OMCR */
}

INLINE void exception_vectored(x80_state_t * cpu, int code)
{
	pushword(cpu, 2, cpu->pc);
	cpu->pc = readword(cpu, 2, (cpu->ir & 0xFF00) | (cpu->z180.ior[X80_Z180_IOR_IL] & 0x00E0) | (code & 0x1F));
}

PUBLIC void PREFIXED(exception_nmi)(x80_state_t * cpu, int data)
{
	cpu->z180.ie2 = cpu->z180.ie1;
	cpu->z180.ie1 = 0;

	cpu->z180.ior[X80_Z180_IOR_DSTAT] &= X80_Z180_DSTAT_DME;
	do_call(cpu, 0x0066);
}

PUBLIC void PREFIXED(exception_extint)(x80_state_t * cpu, int number, int count, void * data)
{
	switch(number)
	{
	case 0:
		if(cpu->z180.ie1 == 1 && (cpu->z180.ior[X80_Z180_IOR_ITC] & X80_Z180_ITC_ITE0))
		{
			cpu->z180.ie1 = cpu->z180.ie2 = 0;
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
		break;
	case 1:
		if(cpu->z180.ie1 == 1 && (cpu->z180.ior[X80_Z180_IOR_ITC] & X80_Z180_ITC_ITE1))
		{
			cpu->z180.ie1 = cpu->z180.ie2 = 0;
			exception_vectored(cpu, X80_Z180_INT_INT1);
		}
		break;
	case 2:
		if(cpu->z180.ie1 == 1 && (cpu->z180.ior[X80_Z180_IOR_ITC] & X80_Z180_ITC_ITE2))
		{
			cpu->z180.ie1 = cpu->z180.ie2 = 0;
			exception_vectored(cpu, X80_Z180_INT_INT2);
		}
		break;
	}
}

PUBLIC void PREFIXED(exception_intint)(x80_state_t * cpu, int number)
{
	cpu->z180.ie1 = cpu->z180.ie2 = 0;

	exception_vectored(cpu, number);
}

PRIVATE void exception_trap(x80_state_t * cpu, int number, ...)
{
	switch(number)
	{
	case TRAP_UNDEFOP:
		cpu->z180.ior[X80_Z180_IOR_ITC] |= X80_Z180_ITC_TRAP;
		if(cpu->pc == ((cpu->old_pc + 1) & 0xFF))
			cpu->z180.ior[X80_Z180_IOR_ITC] &= ~X80_Z180_ITC_UFO;
		else
			cpu->z180.ior[X80_Z180_IOR_ITC] |= X80_Z180_ITC_UFO;
		pushword(cpu, 2, cpu->pc);
		cpu->pc = 0x0000;
		break;
	}
	/* terminate processing of instructions */
	longjmp(cpu->exc, 1);
}

#define INT_DEFAULT 0

INLINE void disable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->z180.ie1 = cpu->z180.ie2 = 0;
}

INLINE void enable_interrupts(x80_state_t * cpu, uint8_t bits)
{
	cpu->z180.ie1 = cpu->z180.ie2 = 1;
}

INLINE void do_before_retn(x80_state_t * cpu)
{
	cpu->z180.ie1 = cpu->z180.ie2;
}

PUBLIC void PREFIXED(show_regs)(x80_state_t * cpu, bool only_system)
{
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

	printf("SP = %04X\n", cpu->sp);
	printf("PC = %04X\n", cpu->pc);
	printf("IEF1 = %d, IEF2 = %d, ITC = 0x%02X\n", cpu->z180.ie1, cpu->z180.ie2, cpu->z180.ior[X80_Z180_IOR_ITC]);

	if(!only_system)
	{
		printf("I  = %02X\tR   = %02X\n", cpu->ir >> 8, cpu->ir & 0xFF);

		printf("IM = %d\n", cpu->z180.im);
		/* TODO: IORs */
	}
}

