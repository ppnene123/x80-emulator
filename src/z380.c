
#define CPU_ID z380

#define ZILOG 1

/* TODO: z380 flags, unknown */
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

#define ADDRESS_MASK 0xFFFFFFFF
#define IOADDRESS_MASK 0x0000FFFF

/* high 16 bits are left intact */
#define GETSHORT(a) ((a) & 0xFFFF)
#define GETLONG(a) (a)
#define SETSHORT(a, b) ((a) = ((a) & ~0xFFFF) | GETSHORT(b))
#define SETLONG(a, b) ((a) = (b))
#define ADDSHORT(a, d) (((a) & ~0xFFFF) | GETSHORT((a) + (d)))
#define ADDLONG(a, d) ((a) + (d))
#define GETADDR(a) (isextmode(cpu)?GETLONG(a):GETSHORT(a))
#define SETADDR(a, b) (isextmode(cpu)?SETLONG((a),(b)):SETSHORT((a),(b)))
#define ADDADDR(a, d) (isextmode(cpu)?ADDLONG((a),(d)):ADDSHORT((a),(d)))

/* traps and interrupts */

#define TRAP_UNDEFOP X80_Z380_TRAP_UNDEFOP

/* the word size depends on the current execution mode */
INLINE bool isextmode(x80_state_t * cpu)
{
	return cpu->z380.sr & X80_Z380_SR_XM;
}

#define wordsize wordsize
INLINE size_t wordsize(x80_state_t * cpu)
{
	return isextmode(cpu) ? 4 : 2;
}

/* switching AF registers */
INLINE int getafp(x80_state_t * cpu)
{
	return (cpu->z380.sr & X80_Z380_SR_AFP) != 0;
}

INLINE void flipafp(x80_state_t * cpu)
{
	uint16_t af;

	af = cpu->af;
	cpu->af = cpu->af2;
	cpu->af2 = af;

	cpu->z380.sr ^= X80_Z380_SR_AFP;
}

/* switching BC/DE/HL register banks */
INLINE int getalt(x80_state_t * cpu)
{
	return (cpu->z380.sr & X80_Z380_SR_ALT) != 0;
}

INLINE void flipalt(x80_state_t * cpu)
{
	uint32_t reg;

	reg = cpu->bc;
	cpu->bc = cpu->bc2;
	cpu->bc2 = reg;

	reg = cpu->de;
	cpu->de = cpu->de2;
	cpu->de2 = reg;

	reg = cpu->hl;
	cpu->hl = cpu->hl2;
	cpu->hl2 = reg;

	cpu->z380.sr ^= X80_Z380_SR_ALT;
}

/* the Z380 offers 4 banks, each with two banks of AF, BC/DE/HL, for a total of 8 for each */
INLINE int getmainbank(x80_state_t * cpu)
{
	return (cpu->z380.sr & X80_Z380_SR_MAINBANK) >> X80_Z380_SR_MAINBANK_SHIFT;
}

/* the Z380 also offers 4*2 banks for the IX and IY registers */
INLINE int getixp(x80_state_t * cpu)
{
	return (cpu->z380.sr & X80_Z380_SR_IXP) != 0;
}

INLINE int getixbank(x80_state_t * cpu)
{
	return (cpu->z380.sr & X80_Z380_SR_IXBANK) >> X80_Z380_SR_IXBANK_SHIFT;
}

INLINE void flipixp(x80_state_t * cpu)
{
	uint32_t reg;

	reg = cpu->ix;
	cpu->ix = cpu->ix2;
	cpu->ix2 = reg;

	cpu->z380.sr ^= X80_Z380_SR_IXP;
}

INLINE int getiyp(x80_state_t * cpu)
{
	return (cpu->z380.sr & X80_Z380_SR_IYP) != 0;
}

INLINE int getiybank(x80_state_t * cpu)
{
	return (cpu->z380.sr & X80_Z380_SR_IYBANK) >> X80_Z380_SR_IYBANK_SHIFT;
}

INLINE void flipiyp(x80_state_t * cpu)
{
	uint32_t reg;

	reg = cpu->iy;
	cpu->iy = cpu->iy2;
	cpu->iy2 = reg;

	cpu->z380.sr ^= X80_Z380_SR_IYP;
}

/* these must be called when changing SR.XM or changing banks */
INLINE void z380_sync_registers(x80_state_t * cpu)
{
	uint8_t af_field = ((cpu->z380.sr & X80_Z380_SR_MAINBANK)                  >> (X80_Z380_SR_MAINBANK_SHIFT - 1)) | (cpu->z380.sr & X80_Z380_SR_AFP);
	uint8_t hl_field = (cpu->z380.sr & (X80_Z380_SR_MAINBANK|X80_Z380_SR_ALT)) >> (X80_Z380_SR_MAINBANK_SHIFT - 1);
	uint8_t ix_field = (cpu->z380.sr & (X80_Z380_SR_IXBANK  |X80_Z380_SR_IXP)) >> (X80_Z380_SR_IXBANK_SHIFT   - 1);
	uint8_t iy_field = (cpu->z380.sr & (X80_Z380_SR_IYBANK  |X80_Z380_SR_IYP)) >> (X80_Z380_SR_IYBANK_SHIFT   - 1);
	cpu->z380.af[af_field]     = cpu->af;
	cpu->z380.af[af_field ^ 1] = cpu->af2;
	cpu->z380.bc[hl_field]     = cpu->bc;
	cpu->z380.bc[hl_field ^ 1] = cpu->bc2;
	cpu->z380.de[hl_field]     = cpu->de;
	cpu->z380.de[hl_field ^ 1] = cpu->de2;
	cpu->z380.hl[hl_field]     = cpu->hl;
	cpu->z380.hl[hl_field ^ 1] = cpu->hl2;
	cpu->z380.ix[ix_field]     = cpu->ix;
	cpu->z380.ix[ix_field ^ 1] = cpu->ix2;
	cpu->z380.iy[iy_field]     = cpu->iy;
	cpu->z380.iy[iy_field ^ 1] = cpu->iy2;
}

INLINE void z380_reload_registers(x80_state_t * cpu)
{
	uint8_t af_field = ((cpu->z380.sr & X80_Z380_SR_MAINBANK)                  >> (X80_Z380_SR_MAINBANK_SHIFT - 1)) | (cpu->z380.sr & X80_Z380_SR_AFP);
	uint8_t hl_field = (cpu->z380.sr & (X80_Z380_SR_MAINBANK|X80_Z380_SR_ALT)) >> (X80_Z380_SR_MAINBANK_SHIFT - 1);
	uint8_t ix_field = (cpu->z380.sr & (X80_Z380_SR_IXBANK  |X80_Z380_SR_IXP)) >> (X80_Z380_SR_IXBANK_SHIFT   - 1);
	uint8_t iy_field = (cpu->z380.sr & (X80_Z380_SR_IYBANK  |X80_Z380_SR_IYP)) >> (X80_Z380_SR_IYBANK_SHIFT   - 1);
	cpu->af  = cpu->z380.af[af_field];
	cpu->af2 = cpu->z380.af[af_field ^ 1];
	cpu->bc  = cpu->z380.bc[hl_field];
	cpu->bc2 = cpu->z380.bc[hl_field ^ 1];
	cpu->de  = cpu->z380.de[hl_field];
	cpu->de2 = cpu->z380.de[hl_field ^ 1];
	cpu->hl  = cpu->z380.hl[hl_field];
	cpu->hl2 = cpu->z380.hl[hl_field ^ 1];
	cpu->ix  = cpu->z380.ix[ix_field];
	cpu->ix2 = cpu->z380.ix[ix_field ^ 1];
	cpu->iy  = cpu->z380.iy[iy_field];
	cpu->iy2 = cpu->z380.iy[iy_field ^ 1];
}

INLINE int getim(x80_state_t * cpu)
{
	return (cpu->z380.sr & X80_Z380_SR_IM) >> X80_Z380_SR_IM_SHIFT;
}

INLINE void setim(x80_state_t * cpu, int value)
{
	cpu->z380.sr = (cpu->z380.sr & ~X80_Z380_SR_IM) | ((value << X80_Z380_SR_IM_SHIFT) & X80_Z380_SR_IM);
}

INLINE uint8_t inputbyte0(x80_state_t * cpu, uint8_t address)
{
	/* TODO */
	return cpu->z380.ior[address & 0x3F];
}

INLINE void outputbyte0(x80_state_t * cpu, uint8_t address, uint8_t value)
{
	/* TODO: some bits should not be modified */
	cpu->z380.ior[address & 0x3F] = value;
}

PUBLIC void PREFIXED(reset)(x80_state_t * cpu)
{
	z380_sync_registers(cpu);

	cpu->af = (cpu->af & AF_MASK) | AF_ONES;

	cpu->sp = 0x00000000;
	cpu->pc = 0x00000000;
	cpu->ir = 0x00000000;
	cpu->z380.sr = 0x00000000;

	cpu->z380.ie2 = 0;

	cpu->z380.ior[X80_Z380_IOR_IER] = 0x01;
	cpu->z380.ior[X80_Z380_IOR_AVBR] = 0x00;
	cpu->z380.ior[X80_Z380_IOR_TRPBK] = 0x00;

	/* TODO: bank 0 BC, DE, HL, IY? */

	/* TODO: I/O Bus Control Register 0 */

	z380_reload_registers(cpu);
}

INLINE void do_retb(x80_state_t * cpu)
{
	cpu->pc = cpu->z380.spc;
}

PUBLIC void PREFIXED(exception_nmi)(x80_state_t * cpu, int data)
{
	cpu->z380.ie2 = cpu->z380.sr & X80_Z380_SR_IEF1 ? 1 : 0;
	cpu->z380.sr &= ~X80_Z380_SR_IEF1;

	do_call(cpu, 0x00000066);
}

PUBLIC void PREFIXED(exception_extint)(x80_state_t * cpu, int number, int count, void * data)
{
	switch(number)
	{
	case 0:
		if((cpu->z380.sr & X80_Z380_SR_IEF1) && (cpu->z380.ior[X80_Z380_IOR_IER] & X80_Z380_IER_IE0))
		{
			cpu->z380.sr &= ~X80_Z380_SR_IEF1;
			cpu->z380.ie2 = 0;
			switch(getim(cpu))
			{
			case 0:
				interpret_input(cpu, count, data);
				break;
			case 1:
				pushword(cpu, wordsize(cpu), cpu->pc);
				cpu->pc = 0x00000038;
				break;
			case 2:
				pushword(cpu, wordsize(cpu), cpu->pc);
				cpu->pc = readword(cpu, wordsize(cpu), (cpu->ir & 0xFFFFFF00) | (((uint8_t *)data)[0] & 0x00FF));
				break;
			case 3:
				pushword(cpu, wordsize(cpu), cpu->pc);
				cpu->pc = readword(cpu, wordsize(cpu), (cpu->ir & 0xFFFF0000) | (((uint8_t *)data)[0] & 0xFFFF));
				break;
			}
		}
		break;
	case 1:
		if((cpu->z380.sr & X80_Z380_SR_IEF1) && (cpu->z380.ior[X80_Z380_IOR_IER] & X80_Z380_IER_IE1))
		{
			cpu->z380.sr &= ~X80_Z380_SR_IEF1;
			cpu->z380.ie2 = 0;
			pushword(cpu, wordsize(cpu), cpu->pc);
			cpu->pc = readword(cpu, wordsize(cpu), (cpu->ir & 0xFFFF0000) | ((cpu->z380.ior[X80_Z380_IOR_AVBR] & 0xFE) << 8) | X80_Z380_INT_INT1);
		}
		break;
	case 2:
		if((cpu->z380.sr & X80_Z380_SR_IEF1) && (cpu->z380.ior[X80_Z380_IOR_IER] & X80_Z380_IER_IE2))
		{
			cpu->z380.sr &= ~X80_Z380_SR_IEF1;
			cpu->z380.ie2 = 0;
			pushword(cpu, wordsize(cpu), cpu->pc);
			cpu->pc = readword(cpu, wordsize(cpu), (cpu->ir & 0xFFFF0000) | ((cpu->z380.ior[X80_Z380_IOR_AVBR] & 0xFE) << 8) | X80_Z380_INT_INT2);
		}
		break;
	case 3:
		if((cpu->z380.sr & X80_Z380_SR_IEF1) && (cpu->z380.ior[X80_Z380_IOR_IER] & X80_Z380_IER_IE3))
		{
			cpu->z380.sr &= ~X80_Z380_SR_IEF1;
			cpu->z380.ie2 = 0;
			pushword(cpu, wordsize(cpu), cpu->pc);
			cpu->pc = readword(cpu, wordsize(cpu), (cpu->ir & 0xFFFF0000) | ((cpu->z380.ior[X80_Z380_IOR_AVBR] & 0xFE) << 8) | X80_Z380_INT_INT3);
		}
		break;
	}
}

PRIVATE void exception_trap(x80_state_t * cpu, int number, ...)
{
	switch(number)
	{
	case TRAP_UNDEFOP:
		cpu->z380.sr &= ~X80_Z380_SR_IEF1;
		cpu->z380.ie2 = 0;
		if(cpu->pc == cpu->old_pc)
			cpu->z380.ior[X80_Z380_IOR_TRPBK] |= X80_Z380_TRPBK_TV; /* undefined during mode 0 interrupt acknowledge */
		else
			cpu->z380.ior[X80_Z380_IOR_TRPBK] |= X80_Z380_TRPBK_TF; /* undefined during fetch */
		pushword(cpu, wordsize(cpu), cpu->old_pc);
		cpu->pc = 0x00000000;
		break;
	}
	/* terminate processing of instructions */
	longjmp(cpu->exc, 1);
}

#define INT_DEFAULT 1

INLINE void disable_interrupts(x80_state_t * cpu, int bits)
{
	if((bits & 1))
		cpu->z380.sr &= ~X80_Z380_SR_IEF1;
	cpu->z380.ior[X80_Z380_IOR_IER] &= ~((bits >> 1) & 0xF);
}

INLINE void enable_interrupts(x80_state_t * cpu, int bits)
{
	if((bits & 1))
		cpu->z380.sr |= X80_Z380_SR_IEF1;
	cpu->z380.ior[X80_Z380_IOR_IER] |= (bits >> 1) & 0xF;
}

INLINE void do_before_retn(x80_state_t * cpu)
{
	if(cpu->z380.ie2)
		cpu->z380.sr |= X80_Z380_SR_IEF1;
	else
		cpu->z380.sr &= ~X80_Z380_SR_IEF1;
}

PUBLIC void PREFIXED(show_regs)(x80_state_t * cpu, bool only_system)
{
	z380_sync_registers(cpu);

	if(!only_system)
	{
		int i;
		for(i = 0; i < 4; i++)
		{
			printf("AF%d%c= %04X\tAF%d' = %04X\n", i, getmainbank(cpu) == i ? '*' : ' ', cpu->z380.af[(i<<1)|getafp(cpu)], i, cpu->z380.af[(i<<1)|(getafp(cpu) ^ 1)]);
			printf("BC%d%c= %08X\tBC%d' = %08X\n", i, getmainbank(cpu) == i ? '*' : ' ', cpu->z380.bc[(i<<1)|getalt(cpu)], i, cpu->z380.bc[(i<<1)|(getalt(cpu) ^ 1)]);
			printf("DE%d%c= %08X\tDE%d' = %08X\n", i, getmainbank(cpu) == i ? '*' : ' ', cpu->z380.de[(i<<1)|getalt(cpu)], i, cpu->z380.de[(i<<1)|(getalt(cpu) ^ 1)]);
			printf("HL%d%c= %08X\tHL%d' = %08X\n", i, getmainbank(cpu) == i ? '*' : ' ', cpu->z380.hl[(i<<1)|getalt(cpu)], i, cpu->z380.hl[(i<<1)|(getalt(cpu) ^ 1)]);
			printf("IX%d%c= %08X\tIX%d' = %08X\n", i, getixbank(cpu)   == i ? '*' : ' ', cpu->z380.ix[(i<<1)|getixp(cpu)], i, cpu->z380.ix[(i<<1)|(getixp(cpu) ^ 1)]);
			printf("IY%d%c= %08X\tIY%d' = %08X\n", i, getiybank(cpu)   == i ? '*' : ' ', cpu->z380.iy[(i<<1)|getiyp(cpu)], i, cpu->z380.iy[(i<<1)|(getiyp(cpu) ^ 1)]);
		}
		printf("C = %d, N = %d, P/V = %d, H = %d, Z = %d, S = %d\n",
			(cpu->af & F_C) != 0,
			(cpu->af & F_N) != 0,
			(cpu->af & F_P) != 0,
			(cpu->af & F_H) != 0,
			(cpu->af & F_Z) != 0,
			(cpu->af & F_S) != 0);
	}

	printf("SP = %08X\n", cpu->sp);
	printf("PC = %08X\n", cpu->pc);
	printf("SR = %08X, IEF2 = %d\n", cpu->z380.sr, cpu->z380.ie2);

	if(!only_system)
	{
		printf("I  = %06X\tR   = %02X\n", cpu->ir >> 8, cpu->ir & 0xFF);
		/* TODO: IORs, SPC */
	}
}

