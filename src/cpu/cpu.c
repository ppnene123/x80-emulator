
// CPU/frontend specific routines

#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "common.h"

#define __PREFIXED(prefix, name) prefix##_##name
#define _PREFIXED(prefix, name) __PREFIXED(prefix, name)
#define PREFIXED(name) _PREFIXED(CPU_ID, name)

#include "cpu.h"

#if CPU_I80
# include "i80.c"
#elif CPU_I85
# include "i85.c"
#elif CPU_VM1
# include "vm1.c"
#elif CPU_SM83
# include "sm83.c"
#elif CPU_Z80
# include "z80.c"
#elif CPU_Z180
# include "z180.c"
#elif CPU_Z280
# include "z280.c"
#elif CPU_Z380
# include "z380.c"
#elif CPU_EZ80
# include "ez80.c"
#elif CPU_R800
# include "r800.c"
#elif CPU_DP
# include "dp.c"
#else
# error Unknown or undefined CPU emulation value
#endif

#include "common.c"

INLINE address_t extendsign(size_t bytes, address_t address)
{
	if(bytes < sizeof(address_t) && (address & (1 << ((bytes << 3) - 1))))
		return address | (-1 << (bytes << 3));
	else
		return address;
}

PUBLIC address_t PREFIXED(get_address)(x80_state_t * cpu, address_t address, x80_access_type_t access)
{
	return get_address(cpu, address, access);
}

INLINE uint8_t readbyte_access(x80_state_t * cpu, address_t address, int access)
{
	access &= X80_ACCESS_MODE_READ | X80_ACCESS_MODE_WRITE;
	return cpu->read_byte(cpu, get_address(cpu, address, X80_ACCESS_TYPE_READ | access));
}

INLINE uint8_t readbyte(x80_state_t * cpu, address_t address)
{
	return readbyte_access(cpu, address, 0);
}

INLINE uint8_t readbyte_exec(x80_state_t * cpu, address_t address)
{
	return readbyte_access(cpu, address, X80_ACCESS_MODE_EXEC);
}

INLINE uint8_t readbyte_stack(x80_state_t * cpu, address_t address)
{
	return readbyte_access(cpu, address, X80_ACCESS_MODE_STACK);
}

INLINE void writebyte_access(x80_state_t * cpu, address_t address, uint8_t value, int access)
{
	access &= X80_ACCESS_MODE_READ | X80_ACCESS_MODE_WRITE;
	cpu->write_byte(cpu, get_address(cpu, address, X80_ACCESS_TYPE_WRITE), value);
}

INLINE void writebyte(x80_state_t * cpu, address_t address, uint8_t value)
{
	writebyte_access(cpu, address, value, 0);
}

INLINE void writebyte_stack(x80_state_t * cpu, address_t address, uint8_t value)
{
	writebyte_access(cpu, address, value, X80_ACCESS_MODE_STACK);
}

INLINE address_t readword_access(x80_state_t * cpu, size_t bytes, address_t address, int access)
{
	size_t i;
	address_t value = 0;
	for(i = 0; i < bytes; i++)
		value |= readbyte_access(cpu, address + i, access) << (i << 3);
	return value;
}

INLINE address_t readword(x80_state_t * cpu, size_t bytes, address_t address)
{
	return readword_access(cpu, bytes, address, 0);
}

INLINE address_t readword_exec(x80_state_t * cpu, size_t bytes, address_t address)
{
	return readword_access(cpu, bytes, address, X80_ACCESS_MODE_EXEC);
}

INLINE address_t readword_stack(x80_state_t * cpu, size_t bytes, address_t address)
{
	return readword_access(cpu, bytes, address, X80_ACCESS_MODE_STACK);
}

INLINE void writeword_access(x80_state_t * cpu, size_t bytes, address_t address, address_t value, int access)
{
	size_t i;
	for(i = 0; i < bytes; i++)
		writebyte_access(cpu, address + i, value >> (i << 3), access);
}

INLINE void writeword(x80_state_t * cpu, size_t bytes, address_t address, address_t value)
{
	writeword_access(cpu, bytes, address, value, 0);
}

INLINE void writeword_stack(x80_state_t * cpu, size_t bytes, address_t address, address_t value)
{
	writeword_access(cpu, bytes, address, value, X80_ACCESS_MODE_STACK);
}

#if !CPU_SM83
PRIVATE uint8_t inputbyte(x80_state_t * cpu, address_t address)
{
# ifdef inputbyte_cpu
	int result;
	if(inputbyte_cpu(cpu, address, &result))
	{
		return result;
	}
	else
# endif
	if(cpu->input_byte)
	{
		return cpu->input_byte(cpu, get_address(cpu, address, X80_ACCESS_TYPE_INPUT));
	}
	else
	{
		return 0;
	}
}

PRIVATE void outputbyte(x80_state_t * cpu, address_t address, uint8_t value)
{
# ifdef outputbyte_cpu
	if(outputbyte_cpu(cpu, address, value))
	{
		return;
	}
	else
# endif
	if(cpu->output_byte)
	{
		cpu->output_byte(cpu, get_address(cpu, address, X80_ACCESS_TYPE_OUTPUT), value);
	}
}

#if CPU_Z280 || CPU_Z380
PRIVATE uint16_t inputword(x80_state_t * cpu, address_t address)
{
	uint16_t value;
# ifdef inputword_cpu
	if(inputword_cpu(cpu, address, &value))
	{
		return value;
	}
# endif
	value = inputbyte(cpu, address);
	value |= inputbyte(cpu, address + 1) << 8;
	return value;
}
#endif

#if CPU_Z280 || CPU_Z380
PRIVATE void outputword(x80_state_t * cpu, address_t address, uint16_t value)
{
# ifdef outputword_cpu
	if(outputword_cpu(cpu, address, value))
	{
		return;
	}
# endif
	outputbyte(cpu, address,     value);
	outputbyte(cpu, address + 1, value >> 8);
}
#endif
#endif // !CPU_SM83

PRIVATE uint8_t fetchbyte(x80_state_t * cpu)
{
	uint8_t value;
	if(cpu->fetch_byte)
	{
		if(cpu->fetch_byte(cpu, &value))
		{
			return value;
		}
		else
		{
			cpu->fetch_byte = NULL;
		}
	}
#if CPU_Z380
	if(!isextmode(cpu))
		cpu->pc &= 0xFFFF;
#endif
	value = readbyte_exec(cpu, cpu->pc);
	SETADDR(cpu->pc, cpu->pc + 1);
	return value;
}

PRIVATE address_t fetchword(x80_state_t * cpu, size_t bytes)
{
	if(cpu->fetch_byte)
	{
		address_t value = 0;
		size_t i;
		for(i = 1; i < bytes; i++)
		{
			value |= (address_t)fetchbyte(cpu) << (i << 3);
		}
		return value;
	}
#if CPU_Z380
	if(!isextmode(cpu))
		cpu->pc &= 0xFFFF;
#endif
	address_t value = readword_exec(cpu, bytes, cpu->pc);
	SETADDR(cpu->pc, cpu->pc + bytes);
	return value;
}

PRIVATE address_t popword(x80_state_t * cpu, size_t bytes)
{
#if CPU_DP2200V2
	address_t result = cpu->stack[cpu->sp];
	cpu->sp = (cpu->sp + 1) % 16;
	return result;
#else
	address_t value = readword_stack(cpu, bytes, cpu->sp);
	SETADDR(cpu->sp, cpu->sp + bytes);
	return value;
#endif
}

PRIVATE void pushword(x80_state_t * cpu, size_t bytes, address_t value)
{
#if CPU_DP2200V2
	cpu->sp = (cpu->sp + 16 - 1) % 16;
	cpu->stack[cpu->sp] = value;
#else
	address_t sp = GETADDR(cpu->sp - bytes);
# if CPU_Z280
	if(!isusermode(cpu) && (cpu->z280.ctl[X80_Z280_CTL_TCR] & X80_Z280_TCR_S))
	{
		if((sp & 0xFFF0) == (cpu->z280.ctl[X80_Z280_CTL_SSLR] & 0xFFF0))
		{
			exception_trap(cpu, X80_Z280_TRAP_SSOW);
		}
	}
# endif
	cpu->sp = sp;
	writeword_stack(cpu, bytes, sp, value);
#endif
}

/* absolute jump */
PRIVATE void do_jump(x80_state_t * cpu, address_t target)
{
	cpu->pc = GETADDR(target);
}

#if ZILOG || CPU_SM83
/* relative jump, differentiated for ez80 */
PRIVATE void do_jump_relative(x80_state_t * cpu, address_t target)
{
	cpu->pc = GETADDR(target);
}
#endif

PRIVATE void do_call(x80_state_t * cpu, address_t target)
{
#if CPU_DP2200 || CPU_I8008
	cpu->sp = (cpu->sp + PCCOUNT - 1) % PCCOUNT;
#else
	pushword(cpu, wordsize(cpu), cpu->pc);
	cpu->pc = GETADDR(target);
# if CPU_Z80
	cpu->z80.wz = cpu->pc;
# endif
#endif
}

PRIVATE void do_ret(x80_state_t * cpu)
{
#if CPU_DP2200 || CPU_I8008
	cpu->sp = (cpu->sp + 1) % PCCOUNT;
#else
	cpu->pc = GETADDR(popword(cpu, wordsize(cpu)));
# if CPU_Z80
	cpu->z80.wz = cpu->pc;
# endif
#endif
}

#if !CPU_EZ80 && !CPU_SM83
PUBLIC bool x80_step(x80_state_t * cpu, bool do_disasm);

PRIVATE bool internal_fetchbyte(x80_state_t * cpu, uint8_t * result)
{
	if(cpu->fetch_source.pointer >= cpu->fetch_source.length)
	{
		free(cpu->fetch_source.buffer);
		cpu->fetch_source.length = cpu->fetch_source.pointer = 0;
		return false;
	}
	
	*result = cpu->fetch_source.buffer[cpu->fetch_source.pointer++];
	return true;
}

PRIVATE void interpret_input(x80_state_t * cpu, int count, void * data)
{
	if(cpu->fetch_source.pointer != 0)
	{
		memmove(cpu->fetch_source.buffer, &cpu->fetch_source.buffer[cpu->fetch_source.pointer], cpu->fetch_source.length - cpu->fetch_source.pointer);
		cpu->fetch_source.length -= cpu->fetch_source.pointer;
		cpu->fetch_source.pointer = 0;
	}

	cpu->fetch_source.buffer =
		cpu->fetch_source.buffer != NULL ? realloc(cpu->fetch_source.buffer, cpu->fetch_source.length + count) : malloc(cpu->fetch_source.length + count);
	memcpy(&cpu->fetch_source.buffer[cpu->fetch_source.pointer], data, count);

	cpu->fetch_byte = internal_fetchbyte;
}
#endif

/* Flags */
/* I80: 0 and 1 */
/* Z80: undocumented, researched */
/* I85 & Z280: 0 */
/* Z180: 1 according to https://github.com/z88dk/z88dk/issues/759#issuecomment-405568422 */
/* Z380, EZ80: unknown */

#if F_P
static uint8_t paritybits[32] =
{
	0x69, 0x96, 0x96, 0x69, 0x96, 0x69, 0x69, 0x96,
	0x96, 0x69, 0x69, 0x96, 0x69, 0x96, 0x96, 0x69,
	0x96, 0x69, 0x69, 0x96, 0x69, 0x96, 0x96, 0x69,
	0x69, 0x96, 0x96, 0x69, 0x96, 0x69, 0x69, 0x96,
};

#define PARITY(value) ((paritybits[((value) >> 3) & 0x1F] >> ((value) & 7)) & 1)
#define WORDPARITY(value) (PARITY(value) == PARITY((value) >> 8))
#endif

#define CPYBIT(a, b, c) \
	if((c)) \
		(a) |= (b); \
	else \
		(a) &= ~(b);

/* based on z80 undocumented flags */
INLINE uint8_t addcbyte(x80_state_t * cpu, uint8_t a, uint8_t b, int cf)
{
	uint16_t c = a + b + (cf & 1);
	cpu->af = (cpu->af & (~0xFF | AF_KEEP)) | (c & (AF_COPY | F_S)) | AF_ONES;
	if((c & 0x0100))
	{
		cpu->af |= F_C;
	}
	if(((a ^ b ^ c) & 0x0010))
	{
		cpu->af |= F_H;
	}
#if F_P && F_P != F_V
	if(PARITY(c))
	{
		cpu->af |= F_P;
	}
#endif
#if F_V
	if((((a & b & ~c) | (~a & ~b & c)) & 0x0080))
	{
		cpu->af |= F_V;
	}
#endif
#if F_UI
	if(!(cpu->af & F_V) ^ !(cpu->af & F_S)) /* TODO: simplify */
	{
		cpu->af |= F_UI;
	}
#endif
	if((c & 0xFF) == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}

INLINE uint8_t addbyte(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	return addcbyte(cpu, a, b, 0);
}

/* based on z80 undocumented flags */
INLINE uint8_t subcbyte(x80_state_t * cpu, uint8_t a, uint8_t b, int cf)
{
	uint16_t c = a + (~b & 0xFF) + (~cf & 1);
	cpu->af = (cpu->af & (~0xFF | AF_KEEP)) | (c & (AF_COPY | F_S)) | (F_N & AF_MASK) | AF_ONES;
	if(!(c & 0x0100))
	{
		cpu->af |= F_C;
	}
#if CPU_I80 || CPU_I85 || CPU_VM1
	if(!((a ^ b ^ c) & 0x0010))
	{
		cpu->af |= F_H;
	}
#else
	if(((a ^ b ^ c) & 0x0010))
	{
		cpu->af |= F_H;
	}
#endif
#if F_P && F_P != F_V
	if(PARITY(c))
	{
		cpu->af |= F_P;
	}
#endif
#if F_V
	if((((a & ~b & ~c) | (~a & b & c)) & 0x0080))
	{
		cpu->af |= F_V;
	}
#endif
#if F_UI
	if(!(cpu->af & F_V) ^ !(cpu->af & F_S)) /* TODO: simplify */
	{
		cpu->af |= F_UI;
	}
#endif
	if((c & 0xFF) == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}

INLINE uint8_t subbyte(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	return subcbyte(cpu, a, b, 0);
}

#if AF_COPY
/* based on z80 undocumented flags */
INLINE void cmpbyte(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	uint16_t c = a + (~b & 0xFF) + 1;
	cpu->af = (cpu->af & (~0xFF | AF_KEEP)) | (c & F_S) | (b & AF_COPY) | F_N | AF_ONES;
	if(!(c & 0x0100))
	{
		cpu->af |= F_C;
	}
	if(((a ^ b ^ c) & 0x0010))
	{
		cpu->af |= F_H;
	}
#if F_V
	if((((a & ~b & ~c) | (~a & b & c)) & 0x0080))
	{
		cpu->af |= F_V;
	}
#endif
#if F_UI
	if(!(cpu->af & F_V) ^ !(cpu->af & F_S)) /* TODO: simplify */
	{
		cpu->af |= F_UI;
	}
#endif
	if((c & 0xFF) == 0)
	{
		cpu->af |= F_Z;
	}
}
#else
# define cmpbyte subbyte
#endif

/* based on z80 undocumented flags */
INLINE uint8_t incbyte(x80_state_t * cpu, uint8_t a)
{
	uint16_t c = a + 1;
	cpu->af = (cpu->af & (~0xFF | AF_KEEP | F_C)) | (c & (AF_COPY | F_S)) | AF_ONES;
	if(((a ^ c) & 0x0010))
	{
		cpu->af |= F_H;
	}
#if F_P && F_P != F_V
	if(PARITY(c))
	{
		cpu->af |= F_P;
	}
#endif
#if F_V
	if(((~a & c) & 0x0080))
	{
		cpu->af |= F_V;
	}
#endif
#if F_UI
	if(!(cpu->af & F_V) ^ !(cpu->af & F_S)) /* TODO: simplify */
	{
		cpu->af |= F_UI;
	}
#endif
	if((c & 0xFF) == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}

/* based on z80 undocumented flags */
INLINE uint8_t decbyte(x80_state_t * cpu, uint8_t a)
{
	uint16_t c = a - 1;
	cpu->af = (cpu->af & (~0xFF | AF_KEEP | F_C)) | (c & (AF_COPY | F_S)) | F_N | AF_ONES;
#if CPU_I80 || CPU_I85 || CPU_VM1
	if(!((a ^ c) & 0x0010))
	{
		cpu->af |= F_H;
	}
#else
	if(((a ^ c) & 0x0010))
	{
		cpu->af |= F_H;
	}
#endif
#if F_P && F_P != F_V
	if(PARITY(c))
	{
		cpu->af |= F_P;
	}
#endif
#if F_V
	if(((a & ~c) & 0x0080))
	{
		cpu->af |= F_V;
	}
#endif
#if F_UI
	if(!(cpu->af & F_V) ^ !(cpu->af & F_S)) /* TODO: simplify */
	{
		cpu->af |= F_UI;
	}
#endif
	if((c & 0xFF) == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}

/* based on z80 undocumented flags */
INLINE uint8_t andbyte(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	uint16_t c = a & b;
	cpu->af = (cpu->af & (~0xFF | AF_KEEP)) | (c & (AF_COPY | F_S)) | AF_ONES;
#if CPU_I80
	/* see Intel 8080/8085 Assembly Language Programming (1977) page 1-12 */
	if(((a | b) & 0x08))
	{
		cpu->af |= F_H;
	}
#elif !CPU_VM1
	cpu->af |= F_H;
#endif
#if F_P
	if(PARITY(c))
	{
		cpu->af |= F_P;
	}
#endif
#if F_UI
	if((cpu->af & F_S))
	{
		cpu->af |= F_UI;
	}
#endif
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}

#if ZILOG || CPU_SM83 /* TODO: define bitbyte instead */
/* z80 and beyond */
INLINE uint8_t bitbyte(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	uint16_t c = a & (1 << b);
	cpu->af = (cpu->af & (~0xFF | F_C)) | (a & AF_COPY) | (c & F_S) | F_H | AF_ONES;
	if(c == 0)
	{
		cpu->af |= F_Z | F_P;
	}
	return c;
}

# if CPU_Z80
INLINE uint8_t bitbyte_memory(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	uint16_t c = a & (1 << b);
	cpu->af = (cpu->af & (~0xFF | F_C)) | (c & F_S) | ((cpu->z80.wz >> 8) & AF_COPY) | F_H | AF_ONES;
	if(c == 0)
	{
		cpu->af |= F_Z | F_P;
	}
	return c;
}
# else
#  define bitbyte_memory bitbyte
# endif
#endif

/* based on z80 undocumented flags */
INLINE uint8_t orbyte(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	uint16_t c = a | b;
	cpu->af = (cpu->af & (~0xFF | AF_KEEP)) | (c & (AF_COPY | F_S)) | AF_ONES;
#if F_P
	if(PARITY(c))
	{
		cpu->af |= F_P;
	}
#endif
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
#if F_UI
	if((cpu->af & F_S))
	{
		cpu->af |= F_UI;
	}
#endif
	return c;
}

/* based on z80 undocumented flags */
INLINE uint8_t xorbyte(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	uint16_t c = a ^ b;
	cpu->af = (cpu->af & (~0xFF | AF_KEEP)) | (c & (AF_COPY | F_S)) | AF_ONES;
#if F_P
	if(PARITY(c))
	{
		cpu->af |= F_P;
	}
#endif
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
#if F_UI
	if((cpu->af & F_S))
	{
		cpu->af |= F_UI;
	}
#endif
	return c;
}

#if F_SIMPLE_CPL
# define cplbyte(a, b) (~(b))
#else
/* based on z80 undocumented flags */
INLINE uint8_t cplbyte(x80_state_t * cpu, uint8_t a)
{
	/* TODO: Nothing, but this will work for sm83 as well */
	cpu->af = (cpu->af & ~AF_COPY) | (~a & AF_COPY) | F_H | F_N | AF_ONES;
	return ~a;
}
#endif

/* based on z80 undocumented flags */
INLINE uint8_t daabyte(x80_state_t * cpu, uint8_t a)
{
	uint8_t d;
#if CPU_Z180
	/* TODO: z180 behaves differently, but it isn't documented */
	/* this is a guess based on https://github.com/z88dk/z88dk/issues/759#issuecomment-405568422 */
	if((cpu->af & F_N))
	{
		/* the guess is that only the H and C flags decide whether a subtraction takes place */
		d = 0;
		if((cpu->af & F_H))
		{
			d += 0x06;
		}
		if((cpu->af & F_C))
		{
			d += 0x60;
		}

		if((a & 0x0F) >= 0x06)
		{
			cpu->af &= ~F_H;
			if((a & 0xF0) >= 0x60)
			{
				cpu->af &= ~F_C;
			}
		}
		else if((cpu->af & F_H) ? (a & 0xF0) > 0x60 : (a & 0xF0) >= 0x60)
		{
			cpu->af &= ~F_C;
		}
	}
	else
#endif
	if((cpu->af & F_C))
	{
		if((a & 0x0F) < 0x0A && !(cpu->af & F_H))
		{
			d = 0x60;
		}
		else
		{
			d = 0x66;
		}
	}
	else
	{
		if((a & 0x0F) < 0x0A)
		{
			if(a < 0xA0)
			{
				if((cpu->af & F_H))
				{
					d = 0x06;
				}
				else
				{
					d = 0x00;
				}
			}
			else
			{
				if((cpu->af & F_H))
				{
					d = 0x66;
				}
				else
				{
					d = 0x60;
				}
			}
		}
		else
		{
			if(a < 0x90)
			{
				d = 0x06;
			}
			else
			{
				d = 0x66;
			}
		}
		if(a > 0x99)
		{
			cpu->af |= F_C;
		}
	}

#if F_N
	if((cpu->af & F_N))
	{
		d = a - d;
	}
	else
	{
		d = a + d;
	}
#else
	d = a + d;
#endif

	/* preserve H, N, C */
#if CPU_VM1
	cpu->af = (cpu->af & ~F_Z & ~F_C) | AF_ONES;
#else
	cpu->af = (cpu->af & (~0xFF | F_H | F_N | F_C | AF_KEEP)) | (d & (AF_COPY | F_S)) | AF_ONES;
#endif

#if CPU_I85
	if((a & 0xF0) == 0x70 && (d & 0xF0) == 0x80)
	{
		cpu->af |= F_V;
	}
#endif
#if F_P && !CPU_VM1
	if(PARITY(d))
	{
		cpu->af |= F_P;
	}
#endif
	if((d & 0xFF) == 0)
	{
		cpu->af |= F_Z;
	}

#if CPU_I80 || CPU_I85
	if((a & 0x0F) < 0x0A)
	{
		cpu->af &= ~F_H;
	}
	else
	{
		cpu->af |= F_H;
	}
#elif CPU_SM83
	/* TODO: express in a better way */
	cpu->af &= ~F_H;
#elif !CPU_VM1
	if((cpu->af & F_N))
	{
# if !CPU_Z180 /* the half carry was already set for this one */
		//if(!((cpu->af & F_H) && (a & 0x0F) < 0x06))
		if((a & 0x0F) > 0x05)
		{
			cpu->af &= ~F_H;
		}
# endif
	}
	else
	{
		if((a & 0x0F) < 0x0A)
		{
			cpu->af &= ~F_H;
		}
		else
		{
			cpu->af |= F_H;
		}
	}
#endif

#if F_UI
	if(!(cpu->af & F_V) ^ !(cpu->af & F_S))
	{
		cpu->af |= F_UI;
	}
#endif

	return d;
}

/* based on z80 undocumented flags */
INLINE void postrotatebyte(x80_state_t * cpu, uint8_t a)
{
	/* only used by Z80+ */
	cpu->af = (cpu->af & (~0xFF | AF_KEEP)) | (a & (AF_COPY | F_S)) | AF_ONES;
#if F_P
	if(PARITY(a))
	{
		cpu->af |= F_P;
	}
#endif
	if(a == 0)
	{
		cpu->af |= F_Z;
	}
}

/* for RLA/RLCA instructions (RAL, RLC for 8080) */
#if CPU_I80

INLINE void postrotateacc(x80_state_t * cpu)
{
	cpu->af = (cpu->af & ~F_C & AF_MASK) | AF_ONES;
}
#define postrotateaccleft(a, b, c) postrotateacc(a)
#define postrotateaccright(a, b, c) postrotateacc(a)

#elif CPU_I85

INLINE void postrotateaccleft(x80_state_t * cpu, uint8_t b)
{
	cpu->af = (cpu->af & ~F_C & ~F_V & AF_MASK) | AF_ONES;
}
#define postrotateaccleft(a, b, c) postrotateaccleft(a, c)

INLINE void postrotateaccright(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	cpu->af = (cpu->af & ~F_C & ~F_V & AF_MASK) | AF_ONES;
	if((a ^ b) & 0x80)
	{
		cpu->af |= F_V;
	}
}

#else

# ifndef F_BITS_CLR
#  define F_BITS_CLR 0
# endif

INLINE void postrotateacc(x80_state_t * cpu, uint8_t a)
{
	cpu->af = (cpu->af & ~(F_BITS_CLR | F_C)) | (a & AF_COPY) | AF_ONES;
}

#define postrotateaccleft(a, b, c) postrotateacc(a, c)
#define postrotateaccright(a, b, c) postrotateacc(a, c)

#endif

#if CPU_Z380
/* z380 */
INLINE void postrotateword(x80_state_t * cpu, uint16_t a)
{
	cpu->af = (cpu->af & ~0xFF) | ((a >> 8) & F_S);
	if(WORDPARITY(a))
	{
		cpu->af |= F_P;
	}
	if(a == 0)
	{
		cpu->af |= F_Z;
	}
}
#endif

#if ZILOG
/* based on z80 undocumented flags, execute after IN *, (C) */
INLINE void testinbyte(x80_state_t * cpu, uint8_t a)
{
	cpu->af = (cpu->af & (~0xFF | F_C)) | (a & (AF_COPY | F_S)) | AF_ONES;
	if(PARITY(a))
	{
		cpu->af |= F_P;
	}
	if(a == 0)
	{
		cpu->af |= F_Z;
	}
}
#define testrlx testinbyte
#endif

#ifndef F_SCF_CLR
# define F_SCF_CLR 0
#endif

INLINE void do_scf(x80_state_t * cpu)
{
	cpu->af = (cpu->af & ~(AF_COPY | F_SCF_CLR)) | ((cpu->af >> 8) & AF_COPY) | F_C | AF_ONES;
}

#ifdef ZILOG
/* based on z80 undocumented flags */
INLINE address_t addcword(x80_state_t * cpu, size_t bytes, address_t a, address_t b, int cf)
{
	address_t c = a + b + (cf & 1);
	/* TODO: CPU_Z280 */
	cpu->af = (cpu->af & ~0xFF) | ((c >> ((bytes - 1) << 3)) & (AF_COPY | F_S)) | AF_ONES;
	if((((a & b) | (a & ~c) | (b & ~c)) & (0x80 << ((bytes - 1) << 3))))
	{
		cpu->af |= F_C;
	}
//	if(((a ^ b ^ c) & (0x10 << ((bytes - 1) << 3))))
	if(((a ^ b ^ c) & 0x1000))
	{
		cpu->af |= F_H;
	}
	if((((a & b & ~c) | (~a & ~b & c)) & (0x80 << ((bytes - 1) << 3))))
	{
		cpu->af |= F_V;
	}
	if((c & (~0 >> ((sizeof(address_t) - bytes) << 3))) == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}
#endif

#if CPU_SM83
INLINE address_t addword(x80_state_t * cpu, size_t bytes, address_t a, address_t b)
{
	address_t c = a + b;
	cpu->af = (cpu->af & ~0xFF);
	if((((a & b) | (a & ~c) | (b & ~c)) & (0x80 << ((bytes - 1) << 3))))
	{
		cpu->af |= F_C;
	}
	if(((a ^ b ^ c) & 0x1000))
	{
		cpu->af |= F_H;
	}
	return c;
}
#elif CPU_Z280 || CPU_Z380
/* z280/z380 */
INLINE address_t addword(x80_state_t * cpu, size_t bytes, address_t a, address_t b)
{
	return addcword(cpu, bytes, a, b, 0);
}
#endif

/* special z380 instructions */
/* also DAD for i80/i85 */
INLINE address_t addaddress(x80_state_t * cpu, size_t bytes, address_t a, address_t b)
{
	address_t c = a + b;
#if ZILOG || CPU_SM83
	cpu->af = (cpu->af & ~(AF_COPY | F_C | F_N | F_H)) | ((c >> ((bytes - 1) << 3)) & AF_COPY) | AF_ONES;
#elif CPU_VM1
	cpu->af = (cpu->af & ~F_C);
#else
	cpu->af = (cpu->af & ~(F_UI | F_V | F_C)) | AF_ONES;
#endif
	if((((a & b) | (a & ~c) | (b & ~c)) & (0x80 << ((bytes - 1) << 3))))
	{
		cpu->af |= F_C;
	}
//	if(((a ^ b ^ c) & (0x10 << ((bytes - 1) << 3))))
#if ZILOG || CPU_SM83
	if(((a ^ b ^ c) & 0x1000))
	{
		cpu->af |= F_H;
	}
#endif
#if CPU_I85
	//if((((a & b & ~c) | (~a & ~b & c)) & (0x80 << ((bytes - 1) << 3))))
	if((((a & b & ~c) | (~a & ~b & c)) & 0x8000))
	{
		cpu->af |= F_V;
	}
#endif
	return c;
}

#if CPU_VM1
/* VM1 */
INLINE address_t addcaddress(x80_state_t * cpu, size_t bytes, address_t a, address_t b, int cf)
{
	address_t c = a + b + (cf & 1);
	cpu->af = (cpu->af & ~F_C);
	if((((a & b) | (a & ~c) | (b & ~c)) & (0x80 << ((bytes - 1) << 3))))
	{
		cpu->af |= F_C;
	}
	return c;
}
#endif

#if ZILOG || CPU_VM1
INLINE address_t subcword(x80_state_t * cpu, size_t bytes, address_t a, address_t b, int cf)
{
	address_t c = a + ~b + (~cf & 1);
	cpu->af = (cpu->af & ~0xFF) | ((c >> ((bytes - 1) << 3)) & (AF_COPY | F_S)) | F_N | AF_ONES;
	if(!(((a & ~b) | (a & ~c) | (~b & ~c)) & (0x80 << ((bytes - 1) << 3))))
	{
		cpu->af |= F_C;
	}
//	if(((a ^ b ^ c) & (0x10 << ((bytes - 1) << 3))))
# if F_H
	if(((a ^ b ^ c) & 0x1000))
	{
		cpu->af |= F_H;
	}
# endif
# if !VM1
	if((((a & ~b & ~c) | (~a & b & c)) & (0x80 << ((bytes - 1) << 3))))
	{
		cpu->af |= F_V;
	}
# endif
	if((c & (~0 >> ((sizeof(address_t) - bytes) << 3))) == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}
#endif

#if CPU_Z280 || CPU_Z380
/* z280/z380 */
INLINE address_t subword(x80_state_t * cpu, size_t bytes, address_t a, address_t b)
{
	return addcword(cpu, bytes, a, ~b, 0);
}
#elif CPU_I85 || CPU_VM1
INLINE address_t subword(x80_state_t * cpu, size_t bytes, address_t a, address_t b)
{
	address_t c = a - b;
	cpu->af = (cpu->af & ~0xFF) | ((c >> 8) & F_S) | F_N | AF_ONES;
	if(!(((a & ~b) | (a & ~c) | (~b & ~c)) & (0x80 << 8)))
	{
		cpu->af |= F_C;
	}
# if CPU_I85
	if(PARITY(a >> 8)) /* TODO: unsure */
	{
		cpu->af |= F_P;
	}
//	if(((a ^ b ^ c) & (0x10 << ((bytes - 1) << 3))))
	if(((a ^ b ^ c) & 0x1000)) /* TODO: unsure */
	{
		cpu->af |= F_H;
	}
	if((((a & ~b & ~c) | (~a & b & c)) & (0x80 << 8)))
	{
		cpu->af |= F_V;
	}
	if(!(cpu->af & F_V) ^ !(cpu->af & F_S))
	{
		cpu->af |= F_UI;
	}
# endif
	if((c & (~0 >> ((sizeof(address_t) - bytes) << 3))) == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}
#endif

#if CPU_Z380
/* special z380 instruction */
INLINE address_t subaddress(x80_state_t * cpu, size_t bytes, address_t a, address_t b)
{
	address_t c = a - b;
	cpu->af = (cpu->af & ~(AF_COPY | F_H | F_N | F_C)) | ((c >> ((bytes - 1) << 3)) & AF_COPY) | AF_ONES;
	if((((a & ~b) | (a & ~c) | (~b & ~c)) & (0x80 << ((bytes - 1) << 3))))
	{
		cpu->af |= F_C;
	}
//	if(((a ^ b ^ c) & (0x01 << ((bytes - 1) << 3))))
	if(((a ^ b ^ c) & 0x1000))
	{
		cpu->af |= F_H;
	}
	return c;
}
#endif

#if CPU_I85 || CPU_VM1
INLINE address_t incword(x80_state_t * cpu, address_t a, address_t b)
{
	uint32_t c = (uint32_t)a + (uint32_t)b;
	if((c & 0x10000)) /* TODO: rewrite without 32-bit */
	{
#if CPU_I85
		cpu->af |= F_UI;
#elif CPU_VM1
		cpu->af |= F_V;
#endif
	}
	else
	{
#if CPU_I85
		cpu->af &= ~F_UI;
#elif CPU_VM1
		cpu->af &= ~F_V;
#endif
	}
	return c;
}

INLINE address_t decword(x80_state_t * cpu, address_t a, address_t b)
{
	uint32_t c = (uint32_t)a + (uint32_t)(~b & 0xFFFF) + 1;
	if((c & 0x10000)) /* rewrite without 32-bit */
	{
#if CPU_I85
		cpu->af |= F_UI;
#elif CPU_VM1
		cpu->af |= F_V;
#endif
	}
	else
	{
#if CPU_I85
		cpu->af &= ~F_UI;
#elif CPU_VM1
		cpu->af &= ~F_V;
#endif
	}
	return c;
}
#else
#define incword(a, b, c) ((b) + (c))
#define decword(a, b, c) ((b) - (c))
#endif

#if CPU_Z380
/* z380 */
INLINE address_t andword(x80_state_t * cpu, address_t a, address_t b)
{
	uint16_t c = a & b;
	cpu->af = (cpu->af & ~0xFF) | (c & (AF_COPY | F_S)) | F_H | AF_ONES;
	if(WORDPARITY(c))
	{
		cpu->af |= F_P;
	}
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}

/* z380 */
INLINE address_t orword(x80_state_t * cpu, address_t a, address_t b)
{
	uint16_t c = a | b;
	cpu->af = (cpu->af & ~0xFF) | (c & (AF_COPY | F_S)) | AF_ONES;
	if(WORDPARITY(c))
	{
		cpu->af |= F_P;
	}
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}

/* z380 */
INLINE address_t xorword(x80_state_t * cpu, address_t a, address_t b)
{
	uint16_t c = a | b;
	cpu->af = (cpu->af & ~0xFF) | (c & (AF_COPY | F_S)) | AF_ONES;
	if(WORDPARITY(c))
	{
		cpu->af |= F_P;
	}
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}

/* z380 */
INLINE address_t cplword(x80_state_t * cpu, address_t a)
{
	/* copied from cplbyte */
	cpu->af = (cpu->af & ~(AF_COPY | F_H | F_N)) | (a & AF_COPY) | F_H | F_N | AF_ONES;
	return ~a;
}

/* z380 only */
INLINE uint16_t negword(x80_state_t * cpu, uint16_t a)
{
	uint16_t c = -a;
	cpu->af = (cpu->af & ~0xFF) | (c & (AF_COPY | F_S)) | F_N | AF_ONES;
	if(((~a & ~c) & 0x8000))
	{
		cpu->af |= F_C;
	}
	if(((a ^ c) & 0x0010)) /* this is different for z380 */
	{
		cpu->af |= F_H;
	}
	if(((a & c) & 0x8000))
	{
		cpu->af |= F_V;
	}
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
	return c;
}
#endif

/* TODO: Z280 and later */

#ifdef ZILOG
/* based on z80 undocumented flags */
INLINE void testcmpstring(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	uint16_t c = a - b;
	cpu->af = (cpu->af & (~0xFF | F_C)) | (c & F_S) | F_N | AF_ONES;
	if(((a ^ b ^ c) & 0x0010))
	{
		cpu->af |= F_H;
	}
	/* must assign F_V elsewhere */
	if((c & 0xFF) == 0)
	{
		cpu->af |= F_Z;
	}
# if AF_COPY
	c -= (cpu->af >> 4) & 1;
	cpu->af |= ((c & 0x08) | ((c << 4) & 0x20)) & AF_MASK;
# endif
}

/* based on z80 undocumented flags */
INLINE void testldstring(x80_state_t * cpu, uint8_t a)
{
	cpu->af = (cpu->af & ~(AF_COPY | F_H | F_V | F_N)) | AF_ONES;
	/* must assign F_V elsewhere */
# if AF_COPY
	cpu->af |= ((((a + (cpu->af >> 8)) << 4) & 0x20)
		| ((a + (cpu->af >> 8)) & 0x08)) & AF_COPY;
# endif
}

/* based on z80 undocumented flags: a is the counter, d is the data, v is an extra data value that depends on the instruction */
# if CPU_Z80
INLINE void testiostring(x80_state_t * cpu, address_t a, uint8_t d, uint8_t v)
# else
INLINE void testiostring(x80_state_t * cpu, address_t a, uint8_t d)
# endif
{
# if CPU_Z80
	cpu->af = (cpu->af & ~0xFF) | (a & (AF_COPY | F_S)) | AF_ONES;
# else
	cpu->af = (cpu->af & (~0xFF | F_S | F_H | F_P | F_C)) | AF_ONES;
# endif
	if((a == 0))
	{
		cpu->af |= F_Z;
	}

# if CPU_Z280 || CPU_Z380
	cpu->af |= F_N;
# else
	if((d & 0x80))
	{
		cpu->af |= F_N;
	}
# endif

# if CPU_Z80
	uint16_t tmp = d + v;
	if(tmp & 0x0100)
	{
		cpu->af |= F_H | F_C;
	}
	if(PARITY((tmp & 7) ^ GETHI(cpu->bc)))
	{
		cpu->af |= F_P;
	}
# endif
}

# if CPU_Z80
#  define testinistring(a, b, c) testiostring(a, b, c, (GETLO(cpu->bc) + 1) & 0xFF)
#  define testindstring(a, b, c) testiostring(a, b, c, (GETLO(cpu->bc) - 1) & 0xFF)
#  define testoutstring(a, b, c) testiostring(a, b, c, GETLO(cpu->hl))
# else
#  define testinstring testiostring
#  define testinistring testinstring
#  define testindstring testinstring
#  define testoutstring testiostring
# endif

/* based on z80 undocumented flags */
INLINE void testldair(x80_state_t * cpu, uint8_t a)
{
	cpu->af = (cpu->af & ~F_C) | (a & (AF_COPY | F_S)) | AF_ONES;
	if(a == 0)
	{
		cpu->af |= F_Z;
	}
# if CPU_Z80 || CPU_Z180 || CPU_EZ80
	if(cpu->z80.ie2)
	{
		cpu->af |= F_V;
	}
# elif CPU_Z280
	if((cpu->z280.ctl[X80_Z280_CTL_MSR] & X80_Z280_MSR_INTA))
	{
		cpu->af |= F_V;
	}
# elif CPU_Z380
	if((cpu->z380.sr & X80_Z380_SR_IEF1))
	{
		cpu->af |= F_V;
	}
# endif
}
#endif /* ZILOG */

#if CPU_Z280 || CPU_Z380
/* used on the z280 and z380, if F_V is set, the registers are unchanged */
INLINE uint32_t udivword(x80_state_t * cpu, uint32_t a, address_t b)
{
	uint32_t c;
	if(b == 0)
	{
		cpu->af |= F_V;
		cpu->af |= F_Z;
		cpu->af &= ~F_S;
		return 0;
	}
	c = a / b;
	if(c > 0xFFFF)
	{
		cpu->af |= F_V;
		cpu->af &= ~F_Z;
		cpu->af &= ~F_S;
		return 0;
	}
	cpu->af &= ~F_V;
	cpu->af &= ~F_S;
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
	else
	{
		cpu->af &= ~F_Z;
	}
	return ((a % b) << 16) | c;
}
#endif

#if CPU_Z280
/* z280 only */
INLINE int32_t sdivword(x80_state_t * cpu, int32_t a, int32_t b)
{
	int32_t c;
	if(b == 0)
	{
		cpu->af |= F_V;
		cpu->af |= F_Z;
		cpu->af &= ~F_S;
		return 0;
	}
	c = a / b;
	if(c > 0x7FFF || c < -0x8000)
	{
		cpu->af |= F_V;
		cpu->af &= ~F_Z;
		cpu->af &= ~F_S;
		return 0;
	}
	cpu->af &= ~F_V;
	if(c < 0)
	{
		cpu->af |= F_S;
	}
	else
	{
		cpu->af &= ~F_S;
	}
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
	else
	{
		cpu->af &= ~F_Z;
	}
	return ((a % b) << 16) | (c & 0xFFFF);
}
#endif

#if CPU_Z280
/* z280 only */
INLINE uint16_t udivbyte(x80_state_t * cpu, uint16_t a, uint8_t b)
{
	uint16_t c;
	if(b == 0)
	{
		cpu->af |= F_V;
		cpu->af |= F_Z;
		cpu->af &= ~F_S;
		return 0;
	}
	c = a / b;
	if(c > 0xFF)
	{
		cpu->af |= F_V;
		cpu->af &= ~F_Z;
		cpu->af &= ~F_S;
		return 0;
	}
	cpu->af &= ~F_V;
	cpu->af &= ~F_S;
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
	else
	{
		cpu->af &= ~F_Z;
	}
	return ((a % b) << 8) | c;
}
#endif

#if CPU_Z280
/* z280 only */
INLINE int16_t sdivbyte(x80_state_t * cpu, int16_t a, int8_t b)
{
	int16_t c;
	if(b == 0)
	{
		cpu->af |= F_V;
		cpu->af |= F_Z;
		cpu->af &= ~F_S;
		return 0;
	}
	c = a / b;
	if(c > 0x7F || c < -0x80)
	{
		cpu->af |= F_V;
		cpu->af &= ~F_Z;
		cpu->af &= ~F_S;
		return 0;
	}
	cpu->af &= ~F_V;
	if(c < 0)
	{
		cpu->af |= F_S;
	}
	else
	{
		cpu->af &= ~F_S;
	}
	if(c == 0)
	{
		cpu->af |= F_Z;
	}
	else
	{
		cpu->af &= ~F_Z;
	}
	return ((a % b) << 8) | (c & 0xFF);
}
#endif

#if CPU_Z280 || CPU_Z380 || CPU_R800
/* used on the z280 and z380, if F_V is set, the registers are unchanged */
INLINE uint32_t umulword(x80_state_t * cpu, uint16_t a, uint16_t b)
{
	uint32_t c = (uint32_t)a * b;
	cpu->af &= ~(F_S | F_V);
	if(c <= 0xFFFF)
		cpu->af |= F_C;
	else
		cpu->af &= ~F_C;
	if(c == 0)
		cpu->af |= F_Z;
	else
		cpu->af &= ~F_Z;
	return c;
}
#endif

#if CPU_Z280 || CPU_Z380
INLINE int32_t smulword(x80_state_t * cpu, int16_t a, int16_t b)
{
	int32_t c = (int32_t)a * b;
	cpu->af &= ~F_V;
	if(c < -0x8000 || c >= 0x8000)
		cpu->af |= F_C;
	else
		cpu->af &= ~F_C;
	if(c == 0)
		cpu->af |= F_Z;
	else
		cpu->af &= ~F_Z;
	if(c < 0)
		cpu->af |= F_S;
	else
		cpu->af &= ~F_S;
	return c;
}
#endif

#if CPU_Z280 || CPU_R800
INLINE uint16_t umulbyte(x80_state_t * cpu, uint8_t a, uint8_t b)
{
	uint16_t c = a * b;
	cpu->af &= ~(F_S | F_V);
	if(c <= 0xFF)
		cpu->af |= F_C;
	else
		cpu->af &= ~F_C;
	if(c == 0)
		cpu->af |= F_Z;
	else
		cpu->af &= ~F_Z;
	return c;
}
#endif

#if CPU_Z280
/* z280 only */
INLINE int16_t smulbyte(x80_state_t * cpu, int8_t a, int8_t b)
{
	int16_t c = a * b;
	cpu->af &= ~F_V;
	if(c < -0x80 || c >= 0x80)
		cpu->af |= F_C;
	else
		cpu->af &= ~F_C;
	if(c == 0)
		cpu->af |= F_Z;
	else
		cpu->af &= ~F_Z;
	if(c < 0)
		cpu->af |= F_S;
	else
		cpu->af &= ~F_S;
	return c;
}
#endif

#if CPU_Z380
/* z380 */
INLINE void testinword(x80_state_t * cpu, address_t a)
{
	/* imitating z80 flag effects */
	cpu->af = (cpu->af & (~0xFF | F_C)) | ((a >> 8) & (AF_COPY | F_S)) | AF_ONES;
	if(WORDPARITY(a))
	{
		cpu->af |= F_P;
	}
	if(a == 0)
	{
		cpu->af |= F_Z;
	}
}
#endif

#ifndef F_CCF_CLR
# define F_CCF_CLR 0
#endif

INLINE void do_ccf(x80_state_t * cpu)
{
#if ZILOG
	cpu->af = ((cpu->af ^ F_C) & ~(F_CCF_CLR | AF_COPY)) | ((cpu->af >> 8) & AF_COPY) | (cpu->af & F_C ? F_H : 0) | AF_ONES;
#else
	cpu->af = ((cpu->af ^ F_C) & ~(F_CCF_CLR | AF_COPY)) | ((cpu->af >> 8) & AF_COPY) | AF_ONES;
#endif
}

#if CPU_SM83
INLINE uint8_t swapnibbles(x80_state_t * cpu, uint8_t a)
{
	cpu->af = cpu->af & 0xFF00;
	a = (a >> 4) | (a << 4);
	if((a == 0))
	{
		cpu->af |= F_Z;
	}
	return a;
}
#endif

#if CPU_Z180 || CPU_Z380 || CPU_EZ80
# define UNDEFINED() exception_trap(cpu, TRAP_UNDEFOP)
#elif CPU_Z280 || CPU_SM83 || CPU_R800
# define UNDEFINED() /* TODO */
#endif

#if CPU_Z80 || CPU_Z180 || CPU_EZ80
# define REFRESH() (cpu->ir = (cpu->ir & ~0x7F) | ((cpu->ir + 1) & 0x7F))
#else
# define REFRESH() do; while(0)
#endif

//#define DEBUG(...) if(do_disasm) { fprintf(stderr, __VA_ARGS__); }
#define DEBUG(...) do; while(0)

/* word access instructions are separated into three types, according to how Z380 and eZ80 handle them
	W - word access
		z380:	always 16-bit
		ez80:	can be 16-bit or 24-bit
	X - address access
		z380:	16-bit in native mode, 32-bit in enhanced mode
		ez80:	can be 16-bit or 24-bit
	L - long access
		z380:	can be 16-bit or 32-bit
		ez80:	can be 16-bit or 24-bit
*/

#if CPU_Z380
# define DSPSIZE (1+SUPSIZE) /* depends on parse */
# define IMMSIZE (2+SUPSIZE) /* depends on parse */
# define WORDSIZE 2 /* usual arithmetic */
# define ADDRSIZE wordsize(cpu)
# define OPNDSIZE OPNDSIZE /* depends on parse */
#elif CPU_EZ80
# define DSPSIZE 1
# define IMMSIZE IMMSIZE /* depends on parse */
# define WORDSIZE WORDSIZE /* depends on parse */
# define ADDRSIZE WORDSIZE
# define OPNDSIZE WORDSIZE
#else
# define DSPSIZE 1
# define IMMSIZE 2
# define WORDSIZE 2 /* usual arithmetic */
# define ADDRSIZE 2 /* only used on Z280 */
# define OPNDSIZE 2
#endif

#if CPU_Z380
# define GETWORDW(w) (w)
# define SETWORDW(w, v) ((w) = (v))
# define GETWORDX(w) GETADDR(w)
# define SETWORDX(w, v) SETADDR((w), (v))
# define GETWORDL(w) (OPNDSIZE == 4 ? GETLONG(w) : GETSHORT(w))
# define SETWORDL(w, v) (OPNDSIZE == 4 ? SETLONG(w, v) : SETSHORT(w, v))
#elif CPU_EZ80
# define GETWORDW(w) (WORDSIZE == 3 ? GETLONG(w) : GETSHORT(w))
# define SETWORDW(w, v) (WORDSIZE == 3 ? SETLONG(w, v) : SETSHORT(w, v))
# define GETWORDX(w) (WORDSIZE == 3 ? GETLONG(w) : GETSHORT(w))
# define SETWORDX(w, v) (WORDSIZE == 3 ? SETLONG(w, v) : SETSHORT(w, v))
# define GETWORDL(w) (WORDSIZE == 3 ? GETLONG(w) : GETSHORT(w))
# define SETWORDL(w, v) (WORDSIZE == 3 ? SETLONG(w, v) : SETSHORT(w, v))
#else
# define GETWORDW(w) (w)
# define SETWORDW(w, v) ((w) = (v))
# define GETWORDX(w) (w)
# define SETWORDX(w, v) ((w) = (v))
# define GETWORDL(w) (w)
# define SETWORDL(w, v) ((w) = (v))
#endif

#if CPU_EZ80
# define do_call(c, t) do_call_size(c, SIZEPREF ? IMMSIZE : 1, t)
# define do_jump(c, t) do_jump_size(c, SIZEPREF ? IMMSIZE : 1, t)
# define do_ret(c)     (SIZEPREF ? do_ret_long(c) : do_ret(c)) /* TODO: must have IMMSIZE == 3 */
# define readbyte(a, b) (WORDSIZE == 3 ? readbyte_long(a, b) : readbyte_short(a, b))
# define writebyte(a, b, c) (WORDSIZE == 3 ? writebyte_long(a, b, c) : writebyte_short(a, b, c))
# define readword(a, b, c) (WORDSIZE == 3 ? readword_long(a, b, c) : readword_short(a, b, c))
# define writeword(a, b, c, d) (WORDSIZE == 3 ? writeword_long(a, b, c, d) : writeword_short(a, b, c, d))
#endif

#if CPU_Z380
# define GETIREG() GETWORDL(cpu->ir & ~0xFF)
# define SETIREG(v) SETWORDL(cpu->ir, ((v) & ~0xFF) | (cpu->ir & 0xFF))
#elif CPU_EZ80
# define GETIREG() GETWORDL(cpu->ir >> 8)
# define SETIREG(v) SETWORDL(cpu->ir, ((v) << 8) | (cpu->ir & 0xFF))
#endif

PUBLIC bool PREFIXED(step)(x80_state_t * cpu, bool do_disasm)
{
#if CPU_Z380
	/* DDIR: supplementary immediate size; operand size */
	int SUPSIZE = 0, OPNDSIZE = cpu->z380.sr & X80_Z380_SR_LW ? 4 : 2;

#elif CPU_EZ80

	int SIZEPREF = 0;
	int IMMSIZE = wordsize(cpu);
	int WORDSIZE = wordsize(cpu);

#endif

	address_t x, y, z, i, d;
	int op;
	(void) z;
	cpu->old_pc = cpu->pc;

	if(setjmp(cpu->exc))
		return false;

#if CPU_Z280
	if((cpu->z280.ctl[X80_Z280_CTL_MSR] & X80_Z280_MSR_SSP))
	{
		exception_trap(cpu, X80_Z280_TRAP_SS);
		return true;
	}
	else
	{
		/* TODO: when does this actually occur? */
		if((cpu->z280.ctl[X80_Z280_CTL_MSR] & X80_Z280_MSR_SS))
			cpu->z280.ctl[X80_Z280_CTL_MSR] |= X80_Z280_MSR_SSP;
		else
			cpu->z280.ctl[X80_Z280_CTL_MSR] &= ~X80_Z280_MSR_SSP;
	}
#endif

#if CPU_Z380 || CPU_EZ80
restart:
#endif
	op = fetchbyte(cpu);
	REFRESH();
	switch(op)
	{
#include "../../out/emulator.gen.c"
	}

	return true;
}

