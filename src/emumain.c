
// TODO: separate into frontend and emulator

#include <assert.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "disassembler.h"
#include "cpu/cpu.h"

#define __PREFIXED(prefix, name) prefix##name
#define _PREFIXED(prefix, name) __PREFIXED(prefix, name)
#define PREFIXED(name) _PREFIXED(CPU_ID, name)

x80_state_t * cpu;

typedef uint8_t page_t[0x10000];

page_t * memory[0x10000];

static inline page_t * fetch_page(address_t address)
{
	address >>= 16;
	if(memory[address] == NULL)
	{
		memory[address] = malloc(sizeof(page_t));
		memset(memory[address], 0, sizeof(page_t));
	}
	return memory[address];
}

static inline uint32_t get_address_mask(x80_state_t * cpu)
{
	switch(cpu->cpu_type)
	{
	case X80_CPU_I8008:
	case X80_CPU_DP2200V1:
	case X80_CPU_DP2200V2:
		return 0x00003FFF;
	default:
		return 0xFFFF;
	case X80_CPU_VM1:
		return 0x0001FFFF;
	case X80_CPU_Z180:
		return 0x000FFFFF;
	case X80_CPU_Z800:
	case X80_CPU_Z280:
	case X80_CPU_EZ80:
		return 0x00FFFFFF;
	case X80_CPU_Z380:
		return 0xFFFFFFFF;
	}
}

static uint8_t memory_readbyte(x80_state_t * cpu, address_t address)
{
	address &= get_address_mask(cpu);
	return (*fetch_page(address))[address];
}

static void memory_writebyte(x80_state_t * cpu, address_t address, uint8_t value)
{
	address &= get_address_mask(cpu);
	(*fetch_page(address))[address] = value;
}

static inline uint32_t x80_advance_pc(x80_state_t * cpu, size_t count)
{
	uint32_t pc = cpu->pc;
	uint32_t mask = -1;
	switch(cpu->cpu_type)
	{
	case X80_CPU_I8008:
	case X80_CPU_DP2200V1:
	case X80_CPU_DP2200V2:
		mask = 0x3FFF;
		break;

	default:
		mask = 0xFFFF;
		break;

	case X80_CPU_EZ80:
		if(cpu->ez80.adl)
		{
			mask = 0xFFFFFF;
		}
		else
		{
			mask = 0x00FFFF;
		}
		break;

	case X80_CPU_Z380:
		if(!(cpu->z380.sr & X80_Z380_SR_XM))
		{
			// the high 16 bits of PC get cleared as well
			mask = 0xFFFF;
		}
		break;
	}

	pc &= mask;
	cpu->pc = (cpu->pc + count) & mask;
	return pc;
}

enum state
{
	STATE_WAITING,
	STATE_RUNNING,
	STATE_STEPPING,
} emulator_state;
unsigned long position_pointer;

extern address_t i80_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access);
extern address_t i85_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access);
extern address_t vm1_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access);
extern address_t sm83_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access);
extern address_t z80_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access);
extern address_t z180_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access);
extern address_t z280_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access);
extern address_t z380_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access);
extern address_t ez80_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access);
extern address_t r800_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access);

address_t x80_get_address(x80_state_t * cpu, address_t address, x80_access_type_t access)
{
	switch(cpu->cpu_type)
	{
	case X80_CPU_I80:
		return i80_get_address(cpu, address, access | X80_ACCESS_MODE_DEBUG);
	case X80_CPU_I85:
		return i85_get_address(cpu, address, access | X80_ACCESS_MODE_DEBUG);
	case X80_CPU_VM1:
		return vm1_get_address(cpu, address, access | X80_ACCESS_MODE_DEBUG);
	case X80_CPU_SM83:
		return sm83_get_address(cpu, address, access | X80_ACCESS_MODE_DEBUG);
	case X80_CPU_Z80:
		return z80_get_address(cpu, address, access | X80_ACCESS_MODE_DEBUG);
	case X80_CPU_Z180:
		return z180_get_address(cpu, address, access | X80_ACCESS_MODE_DEBUG);
	case X80_CPU_Z280:
	case X80_CPU_Z800:
		return z280_get_address(cpu, address, access | X80_ACCESS_MODE_DEBUG);
	case X80_CPU_Z380:
		return z380_get_address(cpu, address, access | X80_ACCESS_MODE_DEBUG);
	case X80_CPU_EZ80:
		return ez80_get_address(cpu, address, access | X80_ACCESS_MODE_DEBUG);
	case X80_CPU_R800:
		return r800_get_address(cpu, address, access | X80_ACCESS_MODE_DEBUG);
	default:
		assert(false);
	}
	assert(false);
}

uint8_t x80_readbyte(x80_state_t * cpu, address_t address)
{
	return cpu->read_byte(cpu, x80_get_address(cpu, address, X80_ACCESS_TYPE_READ));
}

address_t x80_readword(x80_state_t * cpu, address_t address, size_t bytes)
{
	address_t value = 0;
	for(size_t i = 0; i < bytes; i++)
	{
		value |= x80_readbyte(cpu, address + i) << (i << 3);
	}
	return value;
}

uint8_t x80_readbyte_exec(x80_state_t * cpu, address_t address)
{
	return cpu->read_byte(cpu, x80_get_address(cpu, address, X80_ACCESS_TYPE_FETCH));
}

address_t x80_readword_exec(x80_state_t * cpu, address_t address, size_t bytes)
{
	address_t value = 0;
	for(size_t i = 0; i < bytes; i++)
	{
		value |= x80_readbyte_exec(cpu, address + i) << (i << 3);
	}
	return value;
}

void x80_writebyte(x80_state_t * cpu, address_t address, uint8_t value)
{
	cpu->write_byte(cpu, x80_get_address(cpu, address, X80_ACCESS_TYPE_WRITE), value);
}

void x80_writeword(x80_state_t * cpu, address_t address, size_t bytes, address_t value)
{
	for(size_t i = 0; i < bytes; i++)
	{
		x80_writebyte(cpu, address + i, value >> (i << 3));
	}
}

#define USEASLIB
#define putbyte(data) x80_writebyte(cpu, position_pointer++, (data))
#ifndef CPU_ID
# define CPU_ID
#endif
#include "assembler.c"

extern void i80_reset(x80_state_t * cpu);
extern void i85_reset(x80_state_t * cpu);
extern void vm1_reset(x80_state_t * cpu);
extern void sm83_reset(x80_state_t * cpu);
extern void z80_reset(x80_state_t * cpu);
extern void z180_reset(x80_state_t * cpu);
extern void z280_reset(x80_state_t * cpu);
extern void z380_reset(x80_state_t * cpu);
extern void ez80_reset(x80_state_t * cpu);
extern void r800_reset(x80_state_t * cpu);

void cpu_reset(x80_state_t * cpu)
{
	switch(cpu->cpu_type)
	{
	case X80_CPU_I80:
		i80_reset(cpu);
		break;
	case X80_CPU_I85:
		i85_reset(cpu);
		break;
	case X80_CPU_VM1:
		vm1_reset(cpu);
		break;
	case X80_CPU_SM83:
		sm83_reset(cpu);
		break;
	case X80_CPU_Z80:
		z80_reset(cpu);
		break;
	case X80_CPU_Z180:
		z180_reset(cpu);
		break;
	case X80_CPU_Z280:
	case X80_CPU_Z800:
		z280_reset(cpu);
		break;
	case X80_CPU_Z380:
		z380_reset(cpu);
		break;
	case X80_CPU_EZ80:
		ez80_reset(cpu);
		break;
	case X80_CPU_R800:
		r800_reset(cpu);
		break;
	default:
		break;
	}
}

extern void i80_show_regs(x80_state_t * cpu, bool only_system);
extern void i85_show_regs(x80_state_t * cpu, bool only_system);
extern void vm1_show_regs(x80_state_t * cpu, bool only_system);
extern void sm83_show_regs(x80_state_t * cpu, bool only_system);
extern void z80_show_regs(x80_state_t * cpu, bool only_system);
extern void z180_show_regs(x80_state_t * cpu, bool only_system);
extern void z280_show_regs(x80_state_t * cpu, bool only_system);
extern void z380_show_regs(x80_state_t * cpu, bool only_system);
extern void ez80_show_regs(x80_state_t * cpu, bool only_system);
extern void r800_show_regs(x80_state_t * cpu, bool only_system);

void show_regs(x80_state_t * cpu, bool only_system)
{
	switch(cpu->cpu_type)
	{
	case X80_CPU_I80:
		i80_show_regs(cpu, only_system);
		break;
	case X80_CPU_I85:
		i85_show_regs(cpu, only_system);
		break;
	case X80_CPU_VM1:
		vm1_show_regs(cpu, only_system);
		break;
	case X80_CPU_SM83:
		sm83_show_regs(cpu, only_system);
		break;
	case X80_CPU_Z80:
		z80_show_regs(cpu, only_system);
		break;
	case X80_CPU_Z180:
		z180_show_regs(cpu, only_system);
		break;
	case X80_CPU_Z280:
	case X80_CPU_Z800:
		z280_show_regs(cpu, only_system);
		break;
	case X80_CPU_Z380:
		z380_show_regs(cpu, only_system);
		break;
	case X80_CPU_EZ80:
		ez80_show_regs(cpu, only_system);
		break;
	case X80_CPU_R800:
		r800_show_regs(cpu, only_system);
		break;
	default:
		break;
	}
}

extern bool i80_step(x80_state_t * cpu, bool do_disasm);
extern bool i85_step(x80_state_t * cpu, bool do_disasm);
extern bool vm1_step(x80_state_t * cpu, bool do_disasm);
extern bool sm83_step(x80_state_t * cpu, bool do_disasm);
extern bool z80_step(x80_state_t * cpu, bool do_disasm);
extern bool z180_step(x80_state_t * cpu, bool do_disasm);
extern bool z280_step(x80_state_t * cpu, bool do_disasm);
extern bool z380_step(x80_state_t * cpu, bool do_disasm);
extern bool ez80_step(x80_state_t * cpu, bool do_disasm);
extern bool r800_step(x80_state_t * cpu, bool do_disasm);

bool step(x80_state_t * cpu, bool do_disasm)
{
	switch(cpu->cpu_type)
	{
	case X80_CPU_I80:
		return i80_step(cpu, do_disasm);
	case X80_CPU_I85:
		return i85_step(cpu, do_disasm);
	case X80_CPU_VM1:
		return vm1_step(cpu, do_disasm);
	case X80_CPU_SM83:
		return sm83_step(cpu, do_disasm);
	case X80_CPU_Z80:
		return z80_step(cpu, do_disasm);
	case X80_CPU_Z180:
		return z180_step(cpu, do_disasm);
	case X80_CPU_Z280:
	case X80_CPU_Z800:
		return z280_step(cpu, do_disasm);
	case X80_CPU_Z380:
		return z380_step(cpu, do_disasm);
	case X80_CPU_EZ80:
		return ez80_step(cpu, do_disasm);
	case X80_CPU_R800:
		return r800_step(cpu, do_disasm);
	default:
		return false;
	}
}

typedef int x80_register_t;

x80_register_t parse_register_name(const char * buff)
{
	// TODO
	return 0;
}

const char * const cpu_shortname[] =
{
	[X80_CPU_I80] = "i80",
	[X80_CPU_I85] = "i85",
	[X80_CPU_VM1] = "vm1",
	[X80_CPU_Z80] = "z80",
	[X80_CPU_Z180] = "z180",
	[X80_CPU_Z280] = "z280",
	[X80_CPU_Z380] = "z380",
	[X80_CPU_EZ80] = "ez80",
	[X80_CPU_R800] = "r800",
	[X80_CPU_SM83] = "sm83",
};

const char * const cpu_longname[] =
{
	[X80_CPU_I80] = "8080",
	[X80_CPU_I85] = "8085",
	[X80_CPU_VM1] = "KR580VM1",
	[X80_CPU_Z80] = "Z80",
	[X80_CPU_Z180] = "Z180",
	[X80_CPU_Z280] = "Z280",
	[X80_CPU_Z380] = "Z380",
	[X80_CPU_EZ80] = "eZ80",
	[X80_CPU_R800] = "R800",
	[X80_CPU_SM83] = "SM83",
};

const char * const cpu_fullname[] =
{
	[X80_CPU_I80] = "Intel 8080",
	[X80_CPU_I85] = "Intel 8085",
	[X80_CPU_VM1] = "KR580VM1",
	[X80_CPU_Z80] = "Zilog Z80",
	[X80_CPU_Z180] = "Zilog Z180",
	[X80_CPU_Z280] = "Zilog Z280",
	[X80_CPU_Z380] = "Zilog Z380",
	[X80_CPU_EZ80] = "Zilog eZ80",
	[X80_CPU_R800] = "ASCII R800",
	[X80_CPU_SM83] = "Sharp SM83",
};

const int cpu_asm[] =
{
	[X80_CPU_I80] = ASM_I80,
	[X80_CPU_I85] = ASM_I85,
	[X80_CPU_VM1] = ASM_VM1,
	[X80_CPU_Z80] = ASM_Z80,
	[X80_CPU_Z180] = ASM_Z180,
	[X80_CPU_Z280] = ASM_Z280,
	[X80_CPU_Z380] = ASM_Z380,
	[X80_CPU_EZ80] = ASM_EZ80,
	[X80_CPU_R800] = ASM_R800,
	[X80_CPU_SM83] = ASM_SM83,
};

const int cpu_dasm[] =
{
	[X80_CPU_I80] = DASM_I80,
	[X80_CPU_I85] = DASM_I85,
	[X80_CPU_VM1] = DASM_VM1,
	[X80_CPU_Z80] = DASM_Z80,
	[X80_CPU_Z180] = DASM_Z180,
	[X80_CPU_Z280] = DASM_Z280,
	[X80_CPU_Z380] = DASM_Z380,
	[X80_CPU_EZ80] = DASM_EZ80,
	[X80_CPU_R800] = DASM_R800,
	[X80_CPU_SM83] = DASM_SM83,
};

#if 0
#if CPU_Z280
#define INT0 "INTA"
#else
#define INT0 "INT0"
#endif

int main()
{
	if(setjmp(exc) != 0)
		;

	cpu->AF = 0x1201;
	cpu->BC = 0x3D00;
	writebyte(0, 0210);
	step(0);
	printf("%04X %04X\n", cpu->AF, cpu->BC);

#if 0
	printf("Testing NMI\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);

	exception_nmi(0);
	show_regs(1);

	printf("Testing " INT0 ", mode 1\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(1);

	exception_extint(0, 0);
	show_regs(1);

	printf("Testing " INT0 ", mode 2\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(2);
	cpu->IR = 0xA000;
	writeword(2, (cpu->IR & 0xFF00) | 0x0A, 0x1234);

	exception_extint(0, 0x0A);
	show_regs(1);

#if CPU_Z180
	printf("Testing INT1\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	cpu->IR = 0xA000;
	cpu->ior[IOR_ITC] |= ITC_ITE1;
	cpu->ior[IOR_IL] = 0xE0;
	writeword(2, (cpu->IR & 0xFF00) | 0xE0 | INT_INT1, 0x1234);

	exception_extint(1, 0);
	show_regs(1);

	printf("Testing INT2\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	cpu->IR = 0xA000;
	cpu->ior[IOR_ITC] |= ITC_ITE2;
	cpu->ior[IOR_IL] = 0xE0;
	writeword(2, (cpu->IR & 0xFF00) | 0xE0 | INT_INT2, 0x1234);

	exception_extint(2, 0);
	show_regs(1);

	printf("Testing internal interrupt\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	cpu->IR = 0xA000;
	cpu->ior[IOR_IL] = 0xE0;
	writeword(2, (cpu->IR & 0xFF00) | 0xE0 | INT_PRT0, 0x1234);

	exception_intint(INT_PRT0);
	show_regs(1);

	printf("Testing undefined operation trap\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	cpu->old_pc = (cpu->PC - 2) & 0xFFFF;

	exception_trap(TRAP_UNDEFOP);
	show_regs(1);
#endif

#if CPU_Z280
	printf("Testing NMI, mode 3, vectored\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(3);
	cpu->ctl[CTL_ITVTP] = 0xA000;
	cpu->ctl[CTL_ISR] |= ISR_NMI;
	cpu->USP = 0x8000;
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_NMI,            0x0001);
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_NMI + 2,        0);
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_NMIBASE + 0x0A, 0x1234);

	exception_nmi(0x0A);
	show_regs(1);

	printf("Testing INTA, mode 3, non-vectored\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(3);
	cpu->ctl[CTL_ITVTP] = 0xA000;
	cpu->USP = 0x8000;
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_INTA,     0x0001);
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_INTA + 2, 0x1234);

	exception_extint(0, 0);
	show_regs(1);

	printf("Testing INTA, mode 3, vectored\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(3);
	cpu->ctl[CTL_ITVTP] = 0xA000;
	cpu->ctl[CTL_ISR] |= ISR_INTA;
	cpu->USP = 0x8000;
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_INTA,            0x0001);
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_INTA + 2,        0);
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_INTABASE + 0x0A, 0x1234);

	exception_extint(0, 0x0A);
	show_regs(1);

	printf("Testing INTB, mode 1\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(1);

	exception_extint(1, 0);
	show_regs(1);

	printf("Testing INTB, mode 3, non-vectored\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(3);
	cpu->ctl[CTL_ITVTP] = 0xA000;
	cpu->USP = 0x8000;
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_INTB,     0x0001);
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_INTB + 2, 0x1234);

	exception_extint(1, 0);
	show_regs(1);

	printf("Testing privileged instruction trap\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	cpu->old_pc = (cpu->PC - 1) & 0xFFFF;
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_PI,     0x0001);
	writeword_direct(2, (cpu->ctl[CTL_ITVTP] << 8) + TRAP_PI + 2, 0x1234);

	exception_trap(TRAP_PI);
	show_regs(1);
#endif

#if CPU_Z380
	printf("Testing " INT0 ", mode 3\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(3);
	cpu->IR = 0xA0000000;
	writeword(4, (cpu->IR & 0xFFFF0000) | 0x0A00, 0x12345678);

	exception_extint(0, 0x0A00);
	show_regs(1);

	printf("Testing " INT0 ", mode 3, extended mode\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(3);
	cpu->IR = 0xA0000000;
	cpu->sr |= SR_XM;
	writeword(4, (cpu->IR & 0xFFFF0000) | 0x0A00, 0x12345678);

	exception_extint(0, 0x0A00);
	show_regs(1);

	printf("Testing INT1\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	cpu->IR = 0xA0000000;
	cpu->ior[IOR_IER] |= IER_IE1;
	cpu->ior[IOR_AVBR] = 0xFE;
	writeword(2, (cpu->IR & 0xFFFF0000) | 0xFE00 | INT_INT1, 0x1234);

	exception_extint(1, 0);
	show_regs(1);

	printf("Testing undefined operation trap\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	cpu->old_pc = (cpu->PC - 2) & 0xFFFF;

	exception_trap(TRAP_UNDEFOP);
	show_regs(1);
#endif

#if CPU_EZ80
	printf("Testing " INT0 ", mode 2, ADL\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(2);
	cpu->IR = 0xA000;
	cpu->adl = 1;
	writeword(2, (cpu->IR & 0xFF00) | 0x0A, 0x1234);

	exception_extint(0, 0x0A);
	show_regs(1);
	
	printf("Testing " INT0 ", mode 2, mixed Z80\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(2);
	cpu->IR = 0xA000;
	cpu->madl = 1;
	writeword(2, (cpu->IR & 0xFF00) | 0x0A, 0x1234);

	exception_extint(0, 0x0A);
	show_regs(1);
	
	printf("Testing " INT0 ", mode 2, mixed ADL\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	setim(2);
	cpu->IR = 0xA000;
	cpu->adl = cpu->madl = 1;
	writeword(2, (cpu->IR & 0xFF00) | 0x0A, 0x1234);

	exception_extint(0, 0x0A);
	show_regs(1);

	printf("Testing internal interrupt\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	cpu->IR = 0xA00000;
	cpu->ivect = 0xE0;
	writeword(2, (cpu->IR & 0xFF0000) | 0xE000 | 0x0A, 0x1234);

	exception_intint(0x0A);
	show_regs(1);

	printf("Testing undefined operation trap\n");
	cpu_reset();
	enable_interrupts(INT_DEFAULT);
	cpu->old_pc = (cpu->PC - 2) & 0xFFFF;

	exception_trap(TRAP_UNDEFOP);
	show_regs(1);
#endif
#endif

	return 0;
}
#endif

enum
{
	X80_SYSTEM_NONE,
	X80_SYSTEM_CPM,
	X80_SYSTEM_UZI,
} system_type = X80_SYSTEM_CPM;

static uint16_t cpm_version = 0x0031;

FILE * open_com_file(const char * filename)
{
	FILE * fp;
	char buffer[strlen(filename) + 5];
	memcpy(buffer, filename, sizeof buffer - 5);
	memcpy(buffer + sizeof buffer - 5, ".com", 5);
	if((fp = fopen(buffer, "rb")) != NULL)
		return fp;
	memcpy(buffer + sizeof buffer - 5, ".COM", 5);
	if((fp = fopen(buffer, "rb")) != NULL)
		return fp;
	memcpy(buffer + sizeof buffer - 5, ".cpm", 5);
	if((fp = fopen(buffer, "rb")) != NULL)
		return fp;
	memcpy(buffer + sizeof buffer - 5, ".CPM", 5);
	fp = fopen(buffer, "rb");
	return fp;
}

FILE * open_prl_file(const char * filename)
{
	FILE * fp;
	char buffer[strlen(filename) + 5];
	memcpy(buffer, filename, sizeof buffer - 5);
	memcpy(buffer + sizeof buffer - 5, ".prl", 5);
	if((fp = fopen(buffer, "rb")) != NULL)
		return fp;
	memcpy(buffer + sizeof buffer - 5, ".PRL", 5);
	fp = fopen(buffer, "rb");
	return fp;
}

void load_prl_file(x80_state_t * cpu, FILE * input_file)
{
	/* TODO */
	fprintf(stderr, "TODO: PRL executables not yet supported\n");
}

void load_com_file(x80_state_t * cpu, FILE * input_file)
{
	uint32_t address = 0x0100;
	fseek(input_file, 0L, SEEK_SET);
	while(true)
	{
		int byte = fgetc(input_file);
		if(byte == -1)
			break;
		x80_writebyte(cpu, address++, byte);
	}
}

void load_cpm3_file(x80_state_t * cpu, FILE * input_file)
{
	/* TODO */
	fprintf(stderr, "TODO: CP/M Plus executables not yet supported\n");
}

address_t load_cpm_file(x80_state_t * cpu, const char * filename)
{
	FILE * fp;
	int isprl = 0;

	fp = fopen(filename, "rb");
	if(fp == NULL)
	{
		fp = open_com_file(filename);
		if(fp == NULL)
		{
			fp = open_prl_file(filename);
			if(fp == NULL)
			{
				fprintf(stderr, "Unable to open file `%s'\n", filename);
				exit(1);
			}
			isprl = 1;
		}
	}
	else
	{
		size_t length = strlen(filename);
		if(length >= 4 && (strncasecmp(filename + length - 4, ".prl", 4) == 0))
		{
			isprl = 1;
		}
	}

	if(isprl)
	{
		load_prl_file(cpu, fp);
		fclose(fp);
		return 0;
	}
	else
	{
		int byte = fgetc(fp);
		if(byte == 0xC9)
		{
			load_cpm3_file(cpu, fp);
		}
		else
		{
			load_com_file(cpu, fp);
		}
		fclose(fp);
		return 0x0000;
	}
}

enum
{
	CMD_NONE,
	CMD_ASM,
	CMD_BREAK,
	CMD_CPU,
	CMD_DISASM,
	CMD_GOTO,
	CMD_HELP,
	CMD_JUMP,
	CMD_PRINT,
	CMD_QUIT,
	CMD_RUN,
	CMD_STEP,
	CMD_WRITE,
	CMD_ERROR,
};

static const char * cmdnames[] =
{
	[CMD_NONE] = "<none>",
	[CMD_ERROR] = "<error>",
	[CMD_ASM] = "asm",
	[CMD_BREAK] = "break",
	[CMD_CPU] = "cpu",
	[CMD_DISASM] = "disasm",
	[CMD_GOTO] = "goto",
	[CMD_HELP] = "help",
	[CMD_JUMP] = "jump",
	[CMD_PRINT] = "print",
	[CMD_QUIT] = "quit",
	[CMD_RUN] = "run",
	[CMD_STEP] = "step",
	[CMD_WRITE] = "write",
};

int scan_command(void)
{
	int c, cmd, i;
	do
	{
		c = getchar();
	} while(c == ' ' || c == '\t');
	switch(c)
	{
	case 'a':
		cmd = CMD_ASM;
		break;
	case 'b':
		cmd = CMD_BREAK;
		break;
	case 'c':
		cmd = CMD_CPU;
		break;
	case 'd':
		cmd = CMD_DISASM;
		break;
	case 'g':
		cmd = CMD_GOTO;
		break;
	case 'h':
		cmd = CMD_HELP;
		break;
	case 'j':
		cmd = CMD_JUMP;
		break;
	case 'p':
		cmd = CMD_PRINT;
		break;
	case 'q':
		cmd = CMD_QUIT;
		break;
	case 'r':
		cmd = CMD_RUN;
		break;
	case 's':
		cmd = CMD_STEP;
		break;
	case 'w':
		cmd = CMD_WRITE;
		break;
	case '\n':
		return CMD_NONE;
	default:
		return CMD_ERROR;
	}
	c = getchar();
	if(c != ' ' && c != '\t' && c != '\n')
	{
		for(i = 1; cmdnames[cmd][i]; i++)
		{
			if(c != cmdnames[cmd][i])
				return CMD_ERROR;
			c = getchar();
		}
		if(c != ' ' && c != '\t' && c != '\n')
			return CMD_ERROR;
	}
	if(c == '\n')
		ungetc(c, stdin);
	return cmd;
}

unsigned long scan_dec(void)
{
	unsigned long i;
	scanf("%lu", &i);
	return i;
}

int scan_dec_option(void)
{
	int c;
	while((c = getchar()) == ' ' || c == '\t')
		;
	ungetc(c, stdin);
	return '0' <= c && c <= '9';
}

unsigned long scan_hex(void)
{
	unsigned long i;
	scanf("%lX", &i);
	return i;
}

int scan_hex_option(void)
{
	int c;
	while((c = getchar()) == ' ' || c == '\t')
		;
	ungetc(c, stdin);
	return ('0' <= c && c <= '9') || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
}

int scan_sign_option(void)
{
	int c;
	while((c = getchar()) == ' ' || c == '\t')
		;
	switch(c)
	{
	case '+':
	case '-':
		return c;
	default:
		ungetc(c, stdin);
		return 0;
	}
}

int scan_string_option(void)
{
	int c;
	while((c = getchar()) == ' ' || c == '\t')
		;
	ungetc(c, stdin);
	return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || c == '.' || c == '_' || c == '\'';
}

void scan_string(char * buff, int buffsize)
{
	int i, c;
	i = 0;
	while((c = getchar(), ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') || c == '.' || c == '_' || c == '\'' || ('0' <= c && c <= '9')))
	{
		if(i + 1 < buffsize)
			buff[i++] = c;
	}
	ungetc(c, stdin);
	buff[i] = '\0';
}

void scan_line(char * buff, int buffsize)
{
	int i, c;
	i = 0;
	while((c = getchar()) != '\n' && c != -1)
	{
		if(i + 2 < buffsize && c != -1)
			buff[i++] = c;
	}
	if(c != -1)
		ungetc(c, stdin);
	buff[i++] = '\n';
	buff[i] = '\0';
}

void end_line(void)
{
	int c;
	while((c = getchar()) != '\n' && c != -1)
	{
		if(c != ' ' && c != '\t')
			fprintf(stderr, "Ignoring character '%c'\n", c);
	}
}

void end_error_line(void)
{
	int c;
	while((c = getchar()) != '\n' && c != -1)
		;
}

void debugger_help(void)
{
	/* TODO */
}

void debugger_break(unsigned long address)
{
	/* TODO */
}

void debugger_run(void)
{
	emulator_state = STATE_RUNNING;
}

void debugger_step(void)
{
	emulator_state = STATE_STEPPING;
}

void debugger_jump(unsigned long address)
{
//	set_register(cpu, X80_REG_PC, address); // TODO
}

#undef READBYTE
#undef WRITEBYTE
#define READBYTE(a) 0
#define WRITEBYTE(a, v) do;while(0)

void debugger_print(unsigned long count)
{
	unsigned long i;
	printf("%08lX", position_pointer);
	for(i = 0; i < count; i++)
		printf("%c%02X", i ? ' ' : '\t', x80_readbyte(cpu, position_pointer++));
	printf("\n");
}

void debugger_write(unsigned long data)
{
	x80_writebyte(cpu, position_pointer++, data);
}

static uint8_t debugger_fetch(parse_context_t * context)
{
	return x80_readbyte_exec(cpu, context->offset + context->blength);
}

extern void scanner_initialize(int cpu);

struct yy_buffer_state * yy_scan_string(const char * yy_str);

void debugger_asm(char * data)
{
	instruction_stream_clear();
	scanner_initialize(CPU_ASM(cpu));
	yy_scan_string(data);
	int result = yyparse();
	if(result != 0)
		return;
	instruction_stream_append(instruction_create(DIR_EOF, 0));

	result = calculate_lengths(GEN_IGNORE_SYMBOLS);
	if(result == -1)
		return;
	while((result = calculate_lengths(GEN_CALCULATE_OFFSETS)))
		;
	if(result == -1)
		return;
	calculate_lengths(GEN_GENERATE_CODE);
}

void debugger_disasm(unsigned long limit)
{
	parse_context_t Context;
#if CPU_Z380
	context_init(&Context, 2); // TODO
#elif CPU_EZ80
	context_init(&Context, get_register(cpu, X80_REG_ADL) ? 3 : 2);
#else
	context_init(&Context, 2);
#endif
	Context.cpu = CPU_DASM(cpu);
	Context.offset = position_pointer;
	Context.fetch = debugger_fetch;
	while(Context.offset < position_pointer + limit)
	{
		context_parse(&Context);
	}
	position_pointer = Context.offset;
}

void debugger_cpu_all(void)
{
	show_regs(cpu, 0);
}

void debugger_cpu_get(const char * name)
{
	x80_register_t reg = parse_register_name(name);
	if(reg == -1)
	{
		fprintf(stderr, "Undefined register '%s'\n", name);
	}
	else
	{
		//unsigned long value = get_register(cpu, reg); // TODO
		//printf("%s\t=\t%0*lX\n", name, (int)(register_size_table[value] + 3) >> 2, value); // TODO
	}
}

void debugger_cpu_set(const char * name, unsigned long value)
{
	x80_register_t reg = parse_register_name(name);
	if(reg == -1)
	{
		fprintf(stderr, "Undefined register '%s'\n", name);
	}
	else
	{
		//set_register(cpu, reg, value); // TODO
	}
}

void process_command(int cmd)
{
	switch(cmd)
	{
	case CMD_NONE:
		break;
	case CMD_ERROR:
		fprintf(stderr, "Invalid command\n");
		end_error_line();
		break;
	case CMD_ASM:
		{
			char buff[64];
			scan_line(buff, sizeof buff);
			debugger_asm(buff);
		}
		end_line();
		break;
	case CMD_BREAK:
		debugger_break(scan_hex());
		end_line();
		break;
	case CMD_RUN:
		debugger_run();
		end_line();
		break;
	case CMD_STEP:
		debugger_step();
		end_line();
		break;
	case CMD_GOTO:
		switch(scan_sign_option())
		{
		case '+':
			position_pointer += scan_hex();
			break;
		case '-':
			position_pointer -= scan_hex();
			break;
		case 0:
			if(scan_hex_option())
				position_pointer = scan_hex();
			else
				position_pointer = cpu->pc;
			break;
		}
		end_line();
		break;
	case CMD_JUMP:
		if(scan_hex_option())
			debugger_jump(scan_hex());
		else
			debugger_jump(position_pointer);
		end_line();
		break;
	case CMD_PRINT:
		if(scan_hex_option())
			debugger_print(scan_hex());
		else
			debugger_print(1);
		end_line();
		break;
	case CMD_WRITE:
		while(scan_hex_option())
			debugger_write(scan_hex());
		end_line();
		break;
	case CMD_DISASM:
		if(scan_hex_option())
			debugger_disasm(scan_hex());
		else
			debugger_disasm(1);
		end_line();
		break;
	case CMD_HELP:
		debugger_help();
		end_line();
		break;
	case CMD_CPU:
		if(!scan_string_option())
			debugger_cpu_all();
		else
		{
			char buff[8];
			scan_string(buff, sizeof buff);
			if(scan_hex_option())
				debugger_cpu_set(buff, scan_hex());
			else
				debugger_cpu_get(buff);
		}
		end_line();
		break;
	case CMD_QUIT:
		exit(0);
	}
}

int main(int argc, char * argv[])
{
	int argi;
	bool do_debug = false;
	bool do_disasm = false;
	emulator_state = STATE_RUNNING;
	int cpu_type = X80_CPU_Z80;
	for(argi = 1; argi < argc; argi++)
	{
		if(argv[argi][0] == '-')
		{
			switch(argv[argi][1])
			{
			case 'D':
				do_disasm = true; /* TODO: remove */
				/* fallthru */
			case 'd':
				emulator_state = STATE_WAITING;
				do_debug = true;
				break;
			case 'S':
				if(strcasecmp(&argv[argi][2], "cpm") == 0)
				{
					system_type = X80_SYSTEM_CPM;
				}
				else if(strcasecmp(&argv[argi][2], "cpm10") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = 0x0010;
				}
				else if(strcasecmp(&argv[argi][2], "cpm13") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = 0x0013;
				}
				else if(strcasecmp(&argv[argi][2], "cpm14") == 0
					|| strcasecmp(&argv[argi][2], "cpm1") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = 0x0014;
				}
				else if(strcasecmp(&argv[argi][2], "cpm20") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = 0x0020;
				}
				else if(strcasecmp(&argv[argi][2], "cpm22") == 0
					|| strcasecmp(&argv[argi][2], "cpm2") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = 0x0022;
				}
				else if(strcasecmp(&argv[argi][2], "cpm31") == 0
					|| strcasecmp(&argv[argi][2], "cpm3") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = 0x0031;
				}
				else if(strcasecmp(&argv[argi][2], "mpm1") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = 0x0122;
				}
				else if(strcasecmp(&argv[argi][2], "mpm2") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = 0x0130;
				}
				break;
			case 'c':
				if(strcasecmp(&argv[argi][2], "i8080") == 0
				|| strcasecmp(&argv[argi][2], "8080") == 0
				|| strcasecmp(&argv[argi][2], "i80") == 0
				|| strcasecmp(&argv[argi][2], "80") == 0)
				{
					cpu_type = X80_CPU_I80;
				}
				else if(strcasecmp(&argv[argi][2], "i8085") == 0
				|| strcasecmp(&argv[argi][2], "8085") == 0
				|| strcasecmp(&argv[argi][2], "i85") == 0
				|| strcasecmp(&argv[argi][2], "85") == 0)
				{
					cpu_type = X80_CPU_I85;
				}
				else if(strcasecmp(&argv[argi][2], "vm1") == 0)
				{
					cpu_type = X80_CPU_VM1;
				}
				else if(strcasecmp(&argv[argi][2], "z80") == 0)
				{
					cpu_type = X80_CPU_Z80;
				}
				else if(strcasecmp(&argv[argi][2], "z180") == 0)
				{
					cpu_type = X80_CPU_Z180;
				}
				else if(strcasecmp(&argv[argi][2], "z800") == 0)
				{
					cpu_type = X80_CPU_Z800;
				}
				else if(strcasecmp(&argv[argi][2], "z280") == 0)
				{
					cpu_type = X80_CPU_Z280;
				}
				else if(strcasecmp(&argv[argi][2], "z380") == 0)
				{
					cpu_type = X80_CPU_Z380;
				}
				else if(strcasecmp(&argv[argi][2], "ez80") == 0)
				{
					cpu_type = X80_CPU_EZ80;
				}
				else if(strcasecmp(&argv[argi][2], "sm83") == 0
				|| strcasecmp(&argv[argi][2], "gbz80") == 0)
				{
					cpu_type = X80_CPU_SM83;
				}
				else if(strcasecmp(&argv[argi][2], "r800") == 0)
				{
					cpu_type = X80_CPU_R800;
				}
				else
				{
					fprintf(stderr, "Error: unknown cpu `%s'\n", &argv[argi][2]);
				}
				break;
			default:
				fprintf(stderr, "Error: unknown flag `%s'\n", argv[argi]);
				exit(1);
			}
		}
		else
		{
			/* found the command */
			break;
		}
	}

	address_t zero_page = 0;
	bool loaded_file;

	cpu = malloc(sizeof(x80_state_t));
	cpu->cpu_type = cpu_type;
	cpu->read_byte = memory_readbyte;
	cpu->write_byte = memory_writebyte;
	cpu_reset(cpu);

	if(argi >= argc)
	{
		fprintf(stderr, "Warning: no command given\n");
		loaded_file = false;
		emulator_state = STATE_WAITING;
		do_debug = true;
	}
	else
	{
		zero_page = load_cpm_file(cpu, argv[argi]);
		loaded_file = true;
	}

	if(system_type == X80_SYSTEM_CPM)
	{
		// set up warm boot
		x80_writebyte(cpu, zero_page + 0x00,    0xC3); /* jmp */
		x80_writeword(cpu, zero_page + 0x01, 2, 0xFF03);
		// set up BDOS entry
		x80_writebyte(cpu, zero_page + 0x05,    0xC3); /* jmp */
		x80_writeword(cpu, zero_page + 0x06, 2, 0xFE06);

		// BDOS entry point
		x80_writeword(cpu, 0xFE06,           2, 0x6464); /* special instruction */
		x80_writebyte(cpu, 0xFE08,              0); /* BDOS emulation */
		x80_writebyte(cpu, 0xFE09,              0xC9); /* ret */

		// BIOS entry points
		for(int i = 0; i < 16; i++)
		{
			// jump table entry
			x80_writebyte(cpu, 0xFF00 + 3 * i,        0xC3); /* jmp */
			x80_writeword(cpu, 0xFF00 + 3 * i + 1, 2, 0xFF80 + 4 * i);
			x80_writeword(cpu, 0xFF80 + 4 * i,     2, 0x6464); /* special */
			x80_writebyte(cpu, 0xFF80 + 4 * i + 2,    1 + i); /* BIOS function #i emulation */
			x80_writebyte(cpu, 0xFF80 + 4 * i + 3,    0xC9); /* ret */
		}

		// command line
		uint8_t pointer = 0x81;
		for(int i = argi + 1; i < argc; i++)
		{
			x80_writebyte(cpu, pointer++, ' ');
			if(pointer >= 0x100)
				break;
			for(int j = 0; argv[i][j] != 0; j++)
			{
				x80_writebyte(cpu, pointer++, toupper(argv[i][j]));
				if(pointer >= 0x100)
					break;
			}
			if(pointer >= 0x100)
				break;
		}

		x80_writebyte(cpu, 0x80, pointer - 0x81);
	}

	if(loaded_file)
	{
		uint16_t sp = 0xFE00 - 2;
		cpu->sp = sp;
		x80_writeword(cpu, sp, 2, 0);
		cpu->pc = zero_page + 0x0100;
	}

	while(true)
	{
		if(emulator_state == STATE_STEPPING)
		{
			emulator_state = STATE_WAITING;
		}

		position_pointer = cpu->pc;
		while(emulator_state == STATE_WAITING)
		{
			switch(cpu->cpu_type)
			{
			default:
				fprintf(stderr, "%s:%04lX>", CPU_LONGNAME(cpu), position_pointer);
				break;
			case X80_CPU_EZ80:
				fprintf(stderr, "%s:%06lX>", CPU_LONGNAME(cpu), position_pointer);
				break;
			case X80_CPU_Z380:
				fprintf(stderr, "%s:%08lX>", CPU_LONGNAME(cpu), position_pointer);
				break;
			}
			fflush(stderr);
			process_command(scan_command());
		}

		if(setjmp(cpu->exc) == 0)
			step(cpu, do_disasm);

		if(x80_readword_exec(cpu, cpu->pc, 2) == 0x6464)
		{
			x80_advance_pc(cpu, 2);
			int code = x80_readbyte_exec(cpu, x80_advance_pc(cpu, 1));
			uint8_t creg = cpu->bc & 0xFF;
			switch(code)
			{
			case 0:
				if(system_type == X80_SYSTEM_CPM)
				{
					/* BDOS */
					if(do_debug)
					{
						fprintf(stderr, "BDOS(%02X)\n", creg);
					}
					switch(creg)
					{
					case 0x00:
						exit(0);
						break;
					case 0x01:
						{
							int c = getchar();
							cpu->a = c;
							if(cpm_version >= 0x0020)
								cpu->l = c;
						}
						break;
					case 0x02:
						putchar(cpu->de & 0xFF);
						break;
					case 0x09:
						for(uint16_t i = 0; i < 0x10000; i++)
						{
							int c = x80_readbyte(cpu, cpu->de + i);
							if(c == '$')
								break;
							putchar(c);
						}
						break;
					case 0x0C:
						{
							uint16_t version = cpm_version < 0x0020 ? 0 : cpm_version;
							if(cpm_version >= 0x0014)
							{
								cpu->hl = version;
								cpu->a = cpu->l;
								cpu->b = cpu->h;
							}
							else
							{
								cpu->a = cpu->b = 0;
								cpu->hl = 1; // TODO: FCBDSK address
							}
						}
						break;
					default:
						fprintf(stderr, "Unimplemented (BDOS %02X)\n", creg);
						exit(1);
					}
				}
				break;
			default:
				if(system_type == X80_SYSTEM_CPM)
				{
					if(do_debug)
					{
						fprintf(stderr, "BIOS(%d)\n", code - 1);
					}
					switch(code)
					{
					case 1:
						/* BOOT */
						exit(0);
						break;
					case 2:
						/* WBOOT */
						exit(0);
						break;
					default:
						fprintf(stderr, "Unimplemented (BIOS %d)\n", code - 1);
						exit(1);
					}
				}
				break;
			}
		}
	}
	return 0;
}

