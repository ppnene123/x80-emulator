
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
#include <termios.h>
#include <poll.h>
#include <unistd.h>
#include "disassembler.h"
#include "cpu/cpu.h"

static inline address_t min(address_t a, address_t b)
{
	return a <= b ? a : b;
}

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

static inline address_t get_address_mask(x80_state_t * cpu)
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

static inline address_t x80_advance_pc(x80_state_t * cpu, size_t count)
{
	address_t pc = cpu->pc;
	address_t mask = -1;
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
address_t emulation_halt_pointer = -1;

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

//// terminal control

bool kbd_is_init = false;
struct termios kbd_old;

void kbd_reset(void)
{
	if(!kbd_is_init)
		return;
	tcsetattr(0, TCSAFLUSH, &kbd_old);
	kbd_is_init = false;
}

void kbd_init(void)
{
	struct termios kbd_new;
	if(kbd_is_init)
		return;
	tcgetattr(0, &kbd_old);
	kbd_is_init = true;
	atexit(kbd_reset);
	kbd_new = kbd_old;
	cfmakeraw(&kbd_new);
	kbd_new.c_oflag = kbd_old.c_oflag;
	tcsetattr(0, TCSAFLUSH, &kbd_new);
}

int kbd_readchar(int fd)
{
	unsigned char c;
	struct pollfd fds[1] = { { fd, POLLIN, 0 } };
	if(poll(fds, 1, 0) <= 0 || (fds[0].revents & POLLIN) == 0)
		return -1;
	if(read(fd, &c, 1) < 1)
		return -1;
	return c;
}

static int bdos_last_char = -1;

int bdos_getchar(void)
{
	if(bdos_last_char != -1)
	{
		int c = bdos_last_char;
		bdos_last_char = -1;
		return c;
	}
	else
	{
		return kbd_readchar(0);
	}
}

int bdos_peekchar(void)
{
	if(bdos_last_char == -1)
	{
		bdos_last_char = kbd_readchar(0);
	}
	return bdos_last_char;
}

int bdos_waitchar(void)
{
	if(bdos_last_char == -1)
	{
		return getchar();
	}
	else
	{
		int c = bdos_last_char;
		bdos_last_char = -1;
		return c;
	}
}

//// file access

uint8_t fread8(FILE * input_file)
{
	uint8_t value;
	fread(&value, 1, 1, input_file);
	return value;
}

uint16_t fread16le(FILE * input_file)
{
	uint16_t value;
	fread(&value, 2, 1, input_file);
	return le16toh(value);
}

enum
{
	X80_SYSTEM_NONE,
	X80_SYSTEM_CPM,
	X80_SYSTEM_UZI,
} system_type = X80_SYSTEM_CPM;

enum
{
	X80_MPM_FLAG = 0x01,

	// For most of these, the exact version number is reported when calling BDOS function call 6, except for the first three entries
	// CP/M 1975 source from the Computer History Museum, version 1.0 assigned for lack of better alternatives
	X80_CPM_10 = 0x10,
	// CP/M 1.3
	X80_CPM_13 = 0x13,
	// CP/M 1.4
	X80_CPM_14 = 0x14,
	// CP/M 2.0
	X80_CPM_20 = 0x20,
	// CP/M 2.2
	X80_CPM_22 = 0x22,
	// Personal CP/M
	X80_CPM_28 = 0x28,
	// MP/M 1
	X80_MPM_1  = 0x22,
	// MP/M II
	X80_MPM_2  = 0x30,
	// CP/M Plus
	X80_CPM_3  = 0x31,

	// MSX-DOS 1.00
	X80_MSXDOS_1 = 0x0100,
	// MSX-DOS 2.20
	X80_MSXDOS_2 = 0x0220,
};
static uint8_t cpm_version = X80_CPM_3;
static uint8_t cpm_version_flags = 0;
static uint8_t mpm_version = 0; // only for MP/M 2 and later
static uint16_t msx_dos_version = 0;

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

void load_prl_body(x80_state_t * cpu, FILE * input, long file_offset, address_t address, address_t relocation_address, uint16_t length)
{
	fseek(input, file_offset, SEEK_SET);
	for(uint16_t i = 0; i < length; i++)
	{
		int c = fgetc(input);
		if(c == -1)
			break;
		x80_writebyte(cpu, address + i, c);
	}
	if(relocation_address != 0x0100)
	{
		for(uint16_t i = 0; i < (length + 7) / 8; i++)
		{
			int c = fgetc(input);
			if(c == -1)
				break;
			for(int j = 7; j >= 0; j--)
			{
				if(((c >> j) & 1) != 0)
				{
					x80_writebyte(cpu, address + i * 8 + 7 - j,
						x80_readbyte(cpu, address + i * 8 + 7 - j) + ((relocation_address - 0x0100) >> 8));
				}
			}
		}
	}
}

// returns initial SP
address_t load_prl_file(x80_state_t * cpu, FILE * input_file, address_t zero_page)
{
	fseek(input_file, 1L, SEEK_SET);

	uint16_t image_size = fread16le(input_file);

	load_prl_body(cpu, input_file, 0x100L, zero_page + 0x100, zero_page + 0x100, image_size);

	return 0xFE00;
}

// returns initial SP
address_t load_com_file(x80_state_t * cpu, FILE * input_file)
{
	address_t address = 0x0100;
	fseek(input_file, 0L, SEEK_SET);
	while(true)
	{
		int byte = fgetc(input_file);
		if(byte == -1)
			break;
		x80_writebyte(cpu, address++, byte);
	}
	return 0xFE00;
}

// returns initial SP
address_t load_cpm3_file(x80_state_t * cpu, FILE * input_file)
{
	fseek(input_file, 1L, SEEK_SET);
	uint16_t image_size = fread16le(input_file);
	// TODO: pre-initialization code is ignored
	fseek(input_file, 0xFL, SEEK_SET);
	uint8_t rsx_count = fread8(input_file);
	rsx_count = min(15, rsx_count);

	address_t address = 0x100;

	fseek(input_file, 0x100L, SEEK_SET);
	for(uint16_t i = 0; i < image_size; i++)
	{
		int c = fgetc(input_file);
		if(c == -1)
			break;
		x80_writebyte(cpu, address + i, c);
	}

	struct rsx_record
	{
		uint16_t offset;
		uint16_t length;
	} rsx_records[15];

	for(uint8_t rsx_index = 0; rsx_index < rsx_count; rsx_index++)
	{
		fseek(input_file, 0x10L * (1 + rsx_index), SEEK_SET);
		rsx_records[rsx_index].offset = fread16le(input_file);
		rsx_records[rsx_index].length = fread16le(input_file);
	}

	uint16_t next_chain = 0xFE00;

	address = next_chain;
	for(uint8_t rsx_index = 0; rsx_index < rsx_count; rsx_index++)
	{
		address = (next_chain - rsx_records[rsx_index].length) & 0xFF00;
		load_prl_body(cpu, input_file, rsx_records[rsx_index].offset, address, address, rsx_records[rsx_index].length);

		printf("%X %X\n", address, next_chain);
		x80_writeword(cpu, address + 0x000A, 2, next_chain + 0x0006);
		if(next_chain != 0xFE00)
		{
			x80_writeword(cpu, next_chain + 0x000C, 2, address + 0x000B);
		}
		if(rsx_index == rsx_count - 1)
		{
			x80_writeword(cpu, address + 0x000C, 2, 0x0007);
		}
		next_chain = address;
		address -= 0x100;
	}

	x80_writeword(cpu, 0x0006, 2, next_chain + 0x0006);

	return address;
}

address_t load_cpm_file(x80_state_t * cpu, const char * filename, address_t load_address)
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
		address_t zero_page = load_address - 0x100;
		address_t sp = load_prl_file(cpu, fp, zero_page);
		fclose(fp);

		x80_writeword(cpu, zero_page + 0x06, 2, 0xFE06);

		sp -= 2;
		cpu->sp = sp;
		x80_writeword(cpu, sp, 2, 0);
		cpu->pc = zero_page + 0x0100;

		return zero_page;
	}
	else
	{
		if(load_address != 0x100)
			fprintf(stderr, "Warning: load address 0x%X specified, ignored\n", load_address);

		address_t sp;
		int byte = fgetc(fp);
		if(byte == 0xC9)
		{
			sp = load_cpm3_file(cpu, fp);
		}
		else
		{
			sp = load_com_file(cpu, fp);

			x80_writeword(cpu, 0x0006, 2, 0xFE06);
		}

		sp -= 2;
		cpu->sp = sp;
		x80_writeword(cpu, sp, 2, 0);
		cpu->pc = 0x0100;

		fclose(fp);
		return 0x0000;
	}
}

address_t load_uzi_file(x80_state_t * cpu, const char * filename, address_t load_address)
{
	FILE * input_file;

	input_file = fopen(filename, "rb");
	if(input_file == NULL)
	{
		fprintf(stderr, "Unable to open file `%s'\n", filename);
		exit(1);
	}

	if(load_address != 0x100)
		fprintf(stderr, "Warning: load address 0x%X specified, ignored\n", load_address);

	int byte = fgetc(input_file);
	if(byte != 0xC3)
	{
		fprintf(stderr, "Invalid binary\n");
		exit(1);
	}

	address_t address = 0x0100;
	fseek(input_file, 0L, SEEK_SET);
	while(true)
	{
		int byte = fgetc(input_file);
		if(byte == -1)
			break;
		x80_writebyte(cpu, address++, byte);
	}

	address_t sp = 0xFE00;

	sp -= 2;
	cpu->sp = sp;
	x80_writeword(cpu, sp, 2, 0);
	cpu->pc = 0x0100;

	fclose(input_file);
	return 0x0000;
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

	CMD_CPU_DISASM_STEP,
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
	case 'S':
		cmd = CMD_CPU_DISASM_STEP;
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
		switch(scan_sign_option())
		{
		case '+':
			emulation_halt_pointer = position_pointer + scan_hex();
			break;
		case '-':
			emulation_halt_pointer = position_pointer - scan_hex();
			break;
		case 0:
			if(scan_hex_option())
				emulation_halt_pointer = scan_hex();
			else
				emulation_halt_pointer = -1;
			break;
		}

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

	case CMD_CPU_DISASM_STEP:
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

		if(scan_hex_option())
			debugger_disasm(scan_hex());
		else
			debugger_disasm(1);

		debugger_step();

		end_line();
		break;
	}
}

struct msx_dos_environment_item
{
	char * name;
	char * value;
};

struct msx_dos_environment
{
	uint16_t item_count;
	struct msx_dos_environment_item * items;
} msx_dos_environment;

static inline void msx_dos_environment_put(const char * name, const char * value)
{
	msx_dos_environment.item_count ++;
	msx_dos_environment.items = msx_dos_environment.items ? realloc(msx_dos_environment.items, msx_dos_environment.item_count * sizeof(struct msx_dos_environment_item)) : malloc(msx_dos_environment.item_count * sizeof(struct msx_dos_environment_item));
	msx_dos_environment.items[msx_dos_environment.item_count - 1].name = strdup(name);
	msx_dos_environment.items[msx_dos_environment.item_count - 1].value = strdup(value);
}

static inline void msx_dos_environment_set(const char * name, const char * value)
{
	for(uint16_t index = 0; index < msx_dos_environment.item_count; index++)
	{
		if(strcmp(msx_dos_environment.items[index].name, name) == 0)
		{
			free(msx_dos_environment.items[index].value);
			msx_dos_environment.items[index].value = strdup(value);
			return;
		}
	}
	msx_dos_environment_put(name, value);
}

static inline char * msx_dos_environment_get(const char * name)
{
	for(uint16_t index = 0; index < msx_dos_environment.item_count; index++)
	{
		if(strcmp(msx_dos_environment.items[index].name, name) == 0)
		{
			return msx_dos_environment.items[index].value;
		}
	}
	return "";
}

int main(int argc, char * argv[])
{
	int argi;
	bool do_debug = false;
	bool do_disasm = false;
	emulator_state = STATE_RUNNING;
	int cpu_type = X80_CPU_Z80;
	address_t load_address = 0x100;
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
			case 'L':
				load_address = strtoll(&argv[argi][2], NULL, 0);
				break;
			case 'S':
				if(strcasecmp(&argv[argi][2], "cpm") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_CPM_3;
					cpm_version_flags = 0;
				}
				else if(strcasecmp(&argv[argi][2], "cpm10") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_CPM_10;
					cpm_version_flags = 0;
				}
				else if(strcasecmp(&argv[argi][2], "cpm13") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_CPM_13;
					cpm_version_flags = 0;
				}
				else if(strcasecmp(&argv[argi][2], "cpm14") == 0
					|| strcasecmp(&argv[argi][2], "cpm1") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_CPM_14;
					cpm_version_flags = 0;
				}
				else if(strcasecmp(&argv[argi][2], "cpm20") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_CPM_20;
					cpm_version_flags = 0;
				}
				else if(strcasecmp(&argv[argi][2], "cpm22") == 0
					|| strcasecmp(&argv[argi][2], "cpm2") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_CPM_22;
					cpm_version_flags = 0;
				}
				else if(strcasecmp(&argv[argi][2], "cpm28") == 0
					|| strcasecmp(&argv[argi][2], "pcpm") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_CPM_28;
					cpm_version_flags = 0;
				}
				else if(strcasecmp(&argv[argi][2], "cpm31") == 0
					|| strcasecmp(&argv[argi][2], "cpm3") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_CPM_3;
					cpm_version_flags = 0;
				}
				else if(strcasecmp(&argv[argi][2], "mpm1") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_MPM_1;
					cpm_version_flags = X80_MPM_FLAG;
				}
				else if(strcasecmp(&argv[argi][2], "mpm2") == 0
					|| strcasecmp(&argv[argi][2], "mpm20") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_MPM_2;
					mpm_version = 0x20;
					cpm_version_flags = X80_MPM_FLAG;
				}
				else if(strcasecmp(&argv[argi][2], "mpm21") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_MPM_2;
					mpm_version = 0x21;
					cpm_version_flags = X80_MPM_FLAG;
				}
				else if(strcasecmp(&argv[argi][2], "msxdos10") == 0
					|| strcasecmp(&argv[argi][2], "msxdos1") == 0
					|| strcasecmp(&argv[argi][2], "msx1") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_CPM_22;
					msx_dos_version = X80_MSXDOS_1;
				}
				else if(strcasecmp(&argv[argi][2], "msxdos20") == 0
					|| strcasecmp(&argv[argi][2], "msxdos2") == 0
					|| strcasecmp(&argv[argi][2], "msx2") == 0
					|| strcasecmp(&argv[argi][2], "msx") == 0)
				{
					system_type = X80_SYSTEM_CPM;
					cpm_version = X80_CPM_22;
					cpm_version_flags = 0;
					msx_dos_version = X80_MSXDOS_2;
				}
				else if(strcasecmp(&argv[argi][2], "uzi") == 0)
				{
					system_type = X80_SYSTEM_UZI;
					cpm_version_flags = 0;
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

	cpu = malloc(sizeof(x80_state_t));
	cpu->cpu_type = cpu_type;
	cpu->read_byte = memory_readbyte;
	cpu->write_byte = memory_writebyte;
	cpu_reset(cpu);

	if(argi >= argc)
	{
		fprintf(stderr, "Warning: no command given\n");
		emulator_state = STATE_WAITING;
		do_debug = true;
	}
	else
	{
		switch(system_type)
		{
		case X80_SYSTEM_NONE:
		default:
			fprintf(stderr, "Warning: no system type specified\n");
			// fall through
		case X80_SYSTEM_CPM:
			zero_page = load_cpm_file(cpu, argv[argi], load_address);
			break;
		case X80_SYSTEM_UZI:
			zero_page = load_uzi_file(cpu, argv[argi], load_address);
			break;
		}
	}

	address_t ccp_start = cpu->sp + 2; // TODO
	if(system_type == X80_SYSTEM_CPM)
	{
		if(emulator_state == STATE_RUNNING)
			kbd_init();

		// set up warm boot
		x80_writebyte(cpu, zero_page + 0x00,    0xC3); /* jmp */
		x80_writeword(cpu, zero_page + 0x01, 2, 0xFF03);

		// set up BDOS entry
		x80_writebyte(cpu, zero_page + 0x05,    0xC3); /* jmp */
//		x80_writeword(cpu, zero_page + 0x06, 2, 0xFE06);

		// BDOS entry point
		x80_writeword(cpu, 0xFE06,           2, 0x6464); /* special instruction */
		x80_writebyte(cpu, 0xFE08,              0); /* BDOS emulation */
		x80_writebyte(cpu, 0xFE09,              0xC9); /* ret */

		// BIOS entry points
		int bios_table_count;

		if(cpm_version < X80_CPM_20)
			bios_table_count = 15;
		else if(cpm_version < X80_CPM_3)
			bios_table_count = 17;
		else
			bios_table_count = 33;

		for(int i = 0; i < bios_table_count; i++)
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
				char c = argv[i][j];
				if(msx_dos_version == 0)
					c = toupper(c);
				x80_writebyte(cpu, pointer++, c);
				if(pointer >= 0x100)
					break;
			}
			if(pointer >= 0x100)
				break;
		}

		x80_writebyte(cpu, 0x80, pointer - 0x81);

		if(msx_dos_version >= 0x0200)
		{
			if(argi + 1 < argc)
			{
				size_t args_length = 0;
				for(int i = argi + 1; i < argc; i++)
				{
					args_length += 1 + strlen(argv[i]);
				}
				char * parameters = malloc(args_length);
				parameters[0] = '\0';
				for(int i = argi + 1; i < argc; i++)
				{
					strcat(parameters, " ");
					strcat(parameters, argv[i]);
				}
				msx_dos_environment_set("PARAMETERS", parameters);
				free(parameters);
			}

			// TODO: improve this format
			char * program = malloc(strlen(argv[argi]) + 4);
			strcpy(program, "A:\\");
			strcat(program, argv[argi]);
			msx_dos_environment_set("PROGRAM", program);
			free(program);
		}
	}
	else if(system_type == X80_SYSTEM_UZI)
	{
		// syscall entry point
		x80_writeword(cpu, 0x0030,           2, 0x6464); /* special instruction */
		x80_writebyte(cpu, 0x0032,              0x80); /* UZI emulation */
		x80_writebyte(cpu, 0x0033,              0xC9); /* ret */
	}

	while(true)
	{

#define DEBUG(...) do { if(do_debug) { fprintf(stderr, __VA_ARGS__); } } while(0)

		if(emulator_state == STATE_RUNNING && cpu->pc == emulation_halt_pointer)
		{
			emulation_halt_pointer = -1;
			emulator_state = STATE_WAITING;
		}
		else if(emulator_state == STATE_STEPPING)
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
			// this list is based on John Elliott's documents
			switch(code)
			{
			case 0:
				if(system_type == X80_SYSTEM_CPM)
				{
					/* BDOS */
					switch(creg)
					{
					case 0x00:
						DEBUG("%s()\n", msx_dos_version ? "TERM0" : "P_TERMCPM");
						exit(0);
						break;
					case 0x01:
					bdos_read_console:
						DEBUG("%s()\n", msx_dos_version ? "CONIN" : "C_READ");
						{
							int c = bdos_waitchar();
							// TODO: process character
							putchar(c);
							cpu->a = c;
							if(cpm_version >= X80_CPM_20)
								cpu->l = c;
						}
						break;
					case 0x02:
					bdos_write_console:
						DEBUG("%s(E=%02X)\n", msx_dos_version ? "CONOUT" : "C_WRITE", cpu->e);
						putchar(cpu->e);
						break;
					case 0x03:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							if(cpm_version < X80_MPM_2)
							{
								goto bdos_read_console;
							}
							DEBUG("raw console input()\n");
							int c = bdos_waitchar();
							cpu->a = cpu->l = c;
						}
						else
						{
							DEBUG("%s()\n", msx_dos_version ? "AUXIN" : "A_READ");
							// TODO
						}
						break;
					case 0x04:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							if(cpm_version < X80_MPM_2)
							{
								goto bdos_write_console;
							}
							DEBUG("raw console output(E=%02X)\n", cpu->e);
							putchar(cpu->e); // TODO
						}
						else
						{
							DEBUG("%s(E=%02X)\n", msx_dos_version ? "AUXOUT" : "A_WRITE", cpu->e);
							// TODO
						}
						break;
					case 0x05:
						DEBUG("%s(E=%02X)\n", msx_dos_version ? "LSTOUT" : "L_WRITE", cpu->e);
						// TODO
						break;
					case 0x06:
						if(cpm_version < X80_CPM_20)
						{
							DEBUG("raw memory size()\n");
							cpu->a = ccp_start;
							cpu->b = ccp_start >> 8;
						}
						else
						{
							DEBUG("%s(E=%02X)\n", msx_dos_version ? "DIRIO" : "C_RAWIO", cpu->e);
							switch(cpu->e)
							{
							case 0xFF:
								if((cpm_version < X80_MPM_2) && (cpm_version_flags & X80_MPM_FLAG))
								{
									cpu->a = cpu->l = bdos_waitchar();
								}
								else
								{
									int c = bdos_peekchar();
									cpu->a = cpu->l = c == -1 ? 0 : c;
								}
								break;
							case 0xFE:
								if(cpm_version >= X80_CPM_3)
								{
									cpu->a = cpu->l = bdos_last_char == -1 ? 0 : 0xFF;
									break;
								}
								// fallthrough
							case 0xFD:
								if(cpm_version >= X80_CPM_3)
								{
									cpu->a = cpu->l = bdos_waitchar();
									break;
								}
								// fallthrough
							default:
								putchar(cpu->e);
								break;
							}
						}
						break;
					case 0x07:
						if(msx_dos_version != 0)
						{
							DEBUG("DIRIN()\n");
							cpu->a = cpu->l = bdos_waitchar();
						}
						else if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("unsupported(C=07)\n");
							cpu->a = cpu->l = 0;
						}
						else if(cpm_version >= X80_CPM_3)
						{
							DEBUG("A_STATIN()\n");
							// TODO
						}
						else
						{
							DEBUG("get I/O byte()\n");
							cpu->a = x80_readbyte(cpu, zero_page + 0x0003);
							if(cpm_version >= X80_CPM_14)
							{
								cpu->l = cpu->a;
								cpu->h = cpu->b = 0;
							}
						}
						break;
					case 0x08:
						if(msx_dos_version != 0)
						{
							DEBUG("INNOE()\n");
							int c = bdos_waitchar();
							// TODO: process character
							cpu->a = cpu->l = c;
						}
						else if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("unsupported(C=07)\n");
							cpu->a = cpu->l = 0;
						}
						else if(cpm_version >= X80_CPM_3)
						{
							DEBUG("A_STATOUT()\n");
							// TODO
						}
						else
						{
							DEBUG("set I/O byte(E=%02X)\n", cpu->e);
							x80_writebyte(cpu, zero_page + 0x0003, cpu->e);
						}
						break;
					case 0x09:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "STROUT" : "C_WRITESTR", cpu->de);
						for(uint16_t i = 0; i < 0x10000; i++)
						{
							int c = x80_readbyte(cpu, cpu->de + i);
							if(c == '$')
								break;
							putchar(c);
						}
						break;
					case 0x0A:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "BUFIN" : "C_READSTR", cpu->de);
						// TODO
						break;
					case 0x0B:
						DEBUG("%s()\n", msx_dos_version ? "CONST" : "C_STAT");
						// TODO
						break;
					case 0x0C:
						DEBUG("%s()\n", msx_dos_version ? "CPMVER" : cpm_version < X80_CPM_20 ? "lift head" : "S_BDOSVER");
						{
							if(cpm_version >= X80_CPM_14)
							{
								cpu->a = cpu->l = cpm_version < X80_CPM_20 ? 0 : cpm_version;
								cpu->b = cpu->h = cpm_version_flags;
							}
							else
							{
								cpu->a = cpu->b = 0;
								cpu->hl = 0xFFFF; // TODO: FCBDSK address
							}
						}
						break;
					case 0x0D:
						DEBUG("%s()\n", msx_dos_version ? "DSKRST" : "DRV_ALLRESET");
						// TODO
						break;
					case 0x0E:
						DEBUG("%s(E=%02X)\n", msx_dos_version ? "SELDSK" : "DRV_SET", cpu->e);
						// TODO
						break;
					case 0x0F:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "FOPEN" : "F_OPEN", cpu->de);
						// TODO
						break;
					case 0x10:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "FCLOSE" : "F_CLOSE", cpu->de);
						// TODO
						break;
					case 0x11:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "SFIRST" : "F_SFIRST", cpu->de);
						// TODO
						break;
					case 0x12:
						DEBUG("%s()\n", msx_dos_version ? "SNEXT" : "F_SNEXT");
						// TODO
						break;
					case 0x13:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "FDEL" : "F_DELETE", cpu->de);
						// TODO
						break;
					case 0x14:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "RDSEQ" : "F_READ", cpu->de);
						// TODO
						break;
					case 0x15:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "WRSEQ" : "F_WRITE", cpu->de);
						// TODO
						break;
					case 0x16:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "FMAKE" : "F_MAKE", cpu->de);
						// TODO
						break;
					case 0x17:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "FREN" : "F_RENAME", cpu->de);
						// TODO
						break;
					case 0x18:
						DEBUG("%s()\n", msx_dos_version ? "LOGIN" : "DRV_LOGINVEC");
						// TODO
						break;
					case 0x19:
						DEBUG("%s()\n", msx_dos_version ? "CURDRV" : "DRV_GET");
						// TODO
						break;
					case 0x1A:
						DEBUG("%s(DE=%04X)\n", msx_dos_version ? "SETDTA" : "F_DMAOFF", cpu->de);
						// TODO
						break;
					case 0x1B:
						if(msx_dos_version != 0)
						{
							DEBUG("ALLOC(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							DEBUG("DRV_ALLOCVEC()\n");
							// TODO
						}
						break;
					case 0x1C:
						if(msx_dos_version != 0)
						{
					msx_dos_unused_bdos_call:
							DEBUG("unused(C=%02X)\n", cpu->c);
							cpu->hl = 0;
							cpu->a = cpu->b = 0;
						}
						else
						{
							DEBUG("DRV_SETRO()\n");
							// TODO
						}
						break;
					case 0x1D:
						if(msx_dos_version != 0)
						{
							goto msx_dos_unused_bdos_call;
						}
						else
						{
							DEBUG("DRV_ROVEC()\n");
							// TODO
						}
						break;
					case 0x1E:
						if(msx_dos_version != 0)
						{
							goto msx_dos_unused_bdos_call;
						}
						else if(cpm_version == X80_CPM_10)
						{
							DEBUG("set echo mode(E=%02X)\n", cpu->e);
							// TODO
						}
						else if(cpm_version == X80_CPM_14)
						{
							DEBUG("set directory buffer(DE=%04X)\n", cpu->de);
							// TODO
						}
						else if(cpm_version >= X80_CPM_20)
						{
							DEBUG("F_ATTRIB(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x1F:
						if(msx_dos_version != 0)
						{
							goto msx_dos_unused_bdos_call;
						}
						else if(cpm_version >= X80_CPM_20)
						{
							DEBUG("DRV_DPB()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x20:
						if(msx_dos_version != 0)
						{
							goto msx_dos_unused_bdos_call;
						}
						else if(cpm_version >= X80_CPM_20)
						{
							DEBUG("F_USERNUM(E=%02X)\n", cpu->e);
							if(cpu->e == 0xFF)
							{
								cpu->a = cpu->l = x80_readbyte(cpu, zero_page + 0x0004) >> 4;
							}
							else
							{
								x80_writebyte(cpu, zero_page + 0x0004, (cpu->e << 4) | (x80_readbyte(cpu, zero_page + 0x0004) & 0x0F));
							}
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x21:
						if(cpm_version >= X80_CPM_20)
						{
							DEBUG("%s(DE=%04X)\n", msx_dos_version ? "RDRND" : "F_READRAND", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x22:
						if(cpm_version >= X80_CPM_20)
						{
							DEBUG("%s(DE=%04X)\n", msx_dos_version ? "WRRND" : "F_WRITERAND", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x23:
						if(cpm_version >= X80_CPM_20)
						{
							DEBUG("%s(DE=%04X)\n", msx_dos_version ? "FSIZE" : "F_SIZE", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x24:
						if(cpm_version >= X80_CPM_20)
						{
							DEBUG("%s(DE=%04X)\n", msx_dos_version ? "SETRND" : "F_RANDREC", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x25:
						if(msx_dos_version != 0)
						{
							goto msx_dos_unused_bdos_call;
						}
						else if(cpm_version >= X80_CPM_22)
						{
							DEBUG("DRV_RESET(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x26:
						if(msx_dos_version != 0)
						{
							DEBUG("WRBLK(DE=%04X,HL=%04X)\n", cpu->de, cpu->hl);
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("DRV_ACCESS(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x27:
						if(msx_dos_version != 0)
						{
							DEBUG("WRBLK(DE=%04X,HL=%04X)\n", cpu->de, cpu->hl);
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("DRV_FREE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x28:
						if(cpm_version >= X80_CPM_22)
						{
							DEBUG("%s(DE=%04X)\n", msx_dos_version ? "WRZER" : "F_WRITEZF", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x29:
						if(msx_dos_version != 0)
						{
							goto msx_dos_unused_bdos_call;
						}
						else if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("test and write record(DE=%04X)\n", cpu->de);
							if(cpm_version >= X80_CPM_3)
							{
								cpu->a = cpu->l = 0xFF;
							}
							else
							{
								// TODO
							}
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x2A:
						if(msx_dos_version != 0)
						{
							DEBUG("GDATE()\n");
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("F_LOCK(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x2B:
						if(msx_dos_version != 0)
						{
							DEBUG("SDATE(DE=%04X,HL=%04X)\n", cpu->de, cpu->hl);
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("F_UNLOCK(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x2C:
						if(msx_dos_version != 0)
						{
							DEBUG("GTIME()\n");
							// TODO
						}
						else if(cpm_version >= 0x0030)
						{
							DEBUG("MULTISEC(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x2D:
						if(msx_dos_version != 0)
						{
							DEBUG("STIME(DE=%04X,HL=%04X)\n", cpu->de, cpu->hl);
							// TODO
						}
						else if(cpm_version >= X80_CPM_28)
						{
							DEBUG("F_ERRMODE(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x2E:
						if(msx_dos_version != 0)
						{
							DEBUG("VERIFY(E=%02X)\n", cpu->e);
							// TODO
						}
						else if(cpm_version >= 0x0030)
						{
							DEBUG("DRV_SPACE(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x2F:
						if(msx_dos_version != 0)
						{
							DEBUG("RDABS(DE=%04X,HL=%04X)\n", cpu->de, cpu->hl);
							// TODO
						}
						else if(cpm_version >= 0x0030)
						{
							DEBUG("P_CHAIN(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x30:
						if(msx_dos_version != 0)
						{
							DEBUG("WRABS(DE=%04X,HL=%04X)\n", cpu->de, cpu->hl);
							// TODO
						}
						else if(cpm_version >= X80_CPM_28)
						{
							DEBUG("DRV_FLUSH(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x31:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("DPARM(DE=%04X,L=%02X)\n", cpu->de, cpu->l);
							// TODO
						}
						else if(cpm_version >= X80_CPM_3)
						{
							DEBUG("access system control block(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x32:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							goto msx_dos_unused_bdos_call;
						}
						else if(cpm_version >= X80_CPM_3)
						{
							DEBUG("S_BIOS(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x33:
					case 0x34:
					case 0x35:
					case 0x36:
					case 0x37:
					case 0x38:
					case 0x39:
					case 0x3A:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							goto msx_dos_unused_bdos_call;
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x3B:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							goto msx_dos_unused_bdos_call;
						}
						else if(cpm_version >= X80_CPM_3)
						{
							DEBUG("P_LOAD(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x3C:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							goto msx_dos_unused_bdos_call;
						}
						else if(cpm_version >= X80_CPM_3)
						{
							DEBUG("call to RSX(DE=%04X)\n", cpu->de);
							/* default behavior */
							cpu->hl = 0x00FF;
							cpu->a = cpu->l;
							cpu->b = cpu->h;
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x3D:
					case 0x3E:
					case 0x3F:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							goto msx_dos_unused_bdos_call;
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x40:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("FFIRST(DE=%04X,IX=%04X)\n", cpu->de, cpu->ix);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x41:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("FNEXT(IX=%04X)\n", cpu->ix);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x42:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("FNEW(B=%02X,DE=%04X,HL=%04X,IX=%04X)\n", cpu->b, cpu->de, cpu->hl, cpu->ix);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x43:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("FOPEN(A=%02X,DE=%04X)\n", cpu->a, cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x44:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("CREATE(A=%02X,B=%02X,DE=%04X)\n", cpu->a, cpu->b, cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x45:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("CLOSE(B=%02X)\n", cpu->b);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x46:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("ENSURE(B=%02X)\n", cpu->b);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x47:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("DUP(B=%02X)\n", cpu->b);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x48:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("READ(B=%02X,DE=%04X,HL=%04X)\n", cpu->b, cpu->de, cpu->hl);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x49:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("WRITE(B=%02X,DE=%04X,HL=%04X)\n", cpu->b, cpu->de, cpu->hl);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x4A:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("SEEK(A=%02X,B=%02X,DE=%04X,HL=%04X)\n", cpu->a, cpu->b, cpu->de, cpu->hl);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x4B:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("IOCTL(A=%02X,B=%02X,DE=%04X)\n", cpu->a, cpu->b, cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x4C:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("HTEST(B=%02X,DE=%04X)\n", cpu->b, cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x4D:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("DELETE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x4E:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("RENAME(DE=%04X,HL=%04X)\n", cpu->de, cpu->hl);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x4F:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("MOVE(DE=%04X,HL=%04X)\n", cpu->de, cpu->hl);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x50:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("ATTR(A=%02X,DE=%04X,L=%02X)\n", cpu->a, cpu->de, cpu->l);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x51:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("FTIME(A=%02X,DE=%04X,HL=%04X,IX=%04X)\n", cpu->a, cpu->de, cpu->hl, cpu->ix);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x52:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("HDELETE(B=%02X)\n", cpu->b);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x53:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("HRENAME(B=%02X,HL=%04X)\n", cpu->b, cpu->hl);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x54:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("HMOVE(B=%02X,HL=%04X)\n", cpu->b, cpu->hl);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x55:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("HATTR(A=%02X,B=%02X,L=%02X)\n", cpu->a, cpu->b, cpu->l);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x56:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("HFTIME(A=%02X,B=%02X,HL=%04X,IX=%04X)\n", cpu->a, cpu->b, cpu->hl, cpu->ix);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x57:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("GETDTA()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x58:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("GETVFY()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x59:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("GETCD(B=%02X,DE=%04X)\n", cpu->b, cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x5A:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("CHDIR(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x5B:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("PARSE(B=%02X,DE=%04X)\n", cpu->b, cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x5C:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("PFILE(DE=%04X,HL=%04X)\n", cpu->de, cpu->hl);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x5D:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("CHKCHR(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x5E:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("WPATH(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x5F:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("FLUSH(B=%02X,D=%02X)\n", cpu->b, cpu->d);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x60:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("FORK()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x61:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("JOIN(B=%02X)\n", cpu->b);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x62:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("TERM(B=%02X)\n", cpu->b);
							exit(cpu->b);
						}
						else if(cpm_version >= X80_CPM_3)
						{
							DEBUG("clean up disk()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x63:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("DEFAB(DE=%04X)\n", cpu->de);
							// TODO
						}
						else if(cpm_version >= X80_CPM_3)
						{
							DEBUG("F_TRUNCATE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x64:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("DEFER(DE=%04X)\n", cpu->de);
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("DRV_SETLABEL(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x65:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("ERROR()\n");
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("DRV_GETLABEL(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x66:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("EXPLAIN(B=%02X,DE=%04X)\n", cpu->b, cpu->de);
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("F_TIMEDATE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x67:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("FORMAT(A=%02X,B=%02X,DE=%04X,HL=%04X)\n", cpu->a, cpu->b, cpu->de, cpu->hl);
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("F_WRITEXFCB(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x68:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("RAMD(B=%02X)\n", cpu->b);
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("T_SET(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x69:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("BUFFER(B=%02X)\n", cpu->b);
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("T_GET(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x6A:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("ASSIGN(B=%02X,D=%02X)\n", cpu->b, cpu->d);
							// TODO
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("F_PASSWD(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x6B:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("GENV(B=%02X,DE=%04X,HL=%04X)\n", cpu->b, cpu->de, cpu->hl);
							char buffer[255];
							for(uint8_t i = 0; ; i++)
							{
								buffer[i] = x80_readbyte(cpu, cpu->hl + i);
								if(buffer[i] == '\0')
									break;
								if(i == 254)
								{
									cpu->a = 0xC0 /* .IENV */;
									goto end_6B;
								}
							}
							char * value = msx_dos_environment_get(buffer);
							for(uint8_t i = 0; i < cpu->b; i++)
							{
								x80_writebyte(cpu, cpu->de + i, value[i]);
								if(value[i] == 0)
									break;
							}
							cpu->a = cpu->b < strlen(value) + 1 ? 0xBF /* .ELONG */ : 0;
						}
						else if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("S_SERIAL(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					end_6B:
						break;
					case 0x6C:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("SENV(B=%02X,DE=%04X,HL=%04X)\n", cpu->b, cpu->de, cpu->hl);
							// TODO
						}
						else if(cpm_version >= X80_CPM_3)
						{
							DEBUG("P_CODE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x6D:
						if(msx_dos_version != 0)
						{
							DEBUG("FENV(B=%02X,DE=%04X,HL=%04X)\n", cpu->b, cpu->de, cpu->hl);
							char * name;
							if(cpu->de == 0 || cpu->de > msx_dos_environment.item_count)
							{
								name = "";
							}
							else
							{
								name = msx_dos_environment.items[cpu->de - 1].name;
							}
							for(uint8_t i = 0; i < cpu->b; i++)
							{
								x80_writebyte(cpu, cpu->hl + i, name[i]);
								if(name[i] == 0)
									break;
							}
							cpu->a = cpu->b < strlen(name) + 1 ? 0xBF /* .ELONG */ : 0;
						}
						else if(!(cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_CPM_28)
						{
							DEBUG("C_MODE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x6E:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("DSKCHK(A=%02X,B=%02X)\n", cpu->a, cpu->b);
							// TODO
						}
						else if(!(cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_CPM_28)
						{
							DEBUG("C_DELIMIT(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x6F:
						if(msx_dos_version != 0)
						{
							DEBUG("DOSVER()\n");
							cpu->a = 0;
							if(msx_dos_version < 0x0200)
							{
								cpu->b = 0;
							}
							else
							{
								cpu->bc = msx_dos_version;
								cpu->de = msx_dos_version;
							}
						}
						else if(!(cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_CPM_28)
						{
							DEBUG("C_WRITEBLK(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x70:
						if(msx_dos_version >= X80_MSXDOS_2)
						{
							DEBUG("REDIR(A=%02X,B=%02X)\n", cpu->a, cpu->b);
							// TODO
						}
						else if(!(cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_CPM_28)
						{
							DEBUG("L_WRITEBLK(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x71:
						if(cpm_version == X80_CPM_28)
						{
							DEBUG("direct screen function(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x7C:
						if(cpm_version == X80_CPM_28)
						{
							DEBUG("byte block copy()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x7D:
						if(cpm_version == X80_CPM_28)
						{
							DEBUG("byte block alter()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x80:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("M_ALLOC absolute(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x81:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("M_ALLOC relocatable(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x82:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("M_FREE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x83:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("DEV_POLL(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x84:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("DEV_WAITFLAG(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x85:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("DEV_SETFLAG(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x86:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("Q_MAKE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x87:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("Q_OPEN(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x88:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("Q_DELETE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x89:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("Q_READ(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x8A:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("Q_CREAD(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x8B:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("Q_WRITE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x8C:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("Q_CWRITE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x8D:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("P_DELAY(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x8E:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("P_DISPATCH()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x8F:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("P_TERM(E=%02X)\n", cpu->e);
							exit(0);
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x90:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("P_CREATE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x91:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("P_PRIORITY(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x92:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("C_ATTACH()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x93:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("C_DETACH()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x94:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("C_SET(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x95:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("C_ASSIGN(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x96:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("P_CLI(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x97:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("P_RPL(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x98:
						if((cpm_version_flags & X80_MPM_FLAG) || cpm_version >= X80_CPM_3)
						{
							DEBUG("F_PARSE(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x99:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("C_GET()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x9A:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("S_SYSDAT()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x9B:
						if((cpm_version_flags & X80_MPM_FLAG))
						{
							DEBUG("T_SECONDS(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x9C:
						if((cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_MPM_2)
						{
							DEBUG("P_PDADR()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x9D:
						if((cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_MPM_2)
						{
							DEBUG("P_ABORT(DE=%04X)\n", cpu->de);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x9E:
						if((cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_MPM_2)
						{
							DEBUG("L_ATTACH()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0x9F:
						if((cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_MPM_2)
						{
							DEBUG("L_DETACH()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0xA0:
						if((cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_MPM_2)
						{
							DEBUG("L_SET(E=%02X)\n", cpu->e);
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0xA1:
						if((cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_MPM_2)
						{
							DEBUG("L_CATTACH()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0xA2:
						if((cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_MPM_2)
						{
							DEBUG("C_CATTACH()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0xA3:
						if((cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_MPM_2)
						{
							DEBUG("S_OSVER()\n");
							cpu->a = cpu->l = mpm_version;
							cpu->b = cpu->h = cpm_version_flags;
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					case 0xA4:
						if((cpm_version_flags & X80_MPM_FLAG) && cpm_version >= X80_MPM_2)
						{
							DEBUG("L_GET()\n");
							// TODO
						}
						else
						{
							goto unimplemented_bdos_call;
						}
						break;
					default:
					unimplemented_bdos_call:
						fprintf(stderr, "Unimplemented (BDOS %02X)\n", creg);
						if(msx_dos_version >= 0x0200)
						{
							cpu->a = 0xDC /* .IBDOS */;
							cpu->hl = 0;
						}
						else if((cpm_version_flags & X80_MPM_FLAG))
						{
							cpu->a = cpu->b = 0xFF;
							cpu->hl = 0xFFFF;
						}
						else
						{
							// versions 1.x seem to crash on invalid calls
							cpu->hl = 0;
							cpu->a = cpu->l;
							cpu->b = cpu->h;
						}
						break;
					}
				}
				break;
			case 0x80:
				if(system_type == X80_SYSTEM_UZI)
				{
					// UZI system call
					uint16_t syscallnum = x80_readword(cpu, cpu->sp + 2, 2);
					switch(syscallnum)
					{
					case 0x00:
						exit(x80_readword(cpu, cpu->sp + 6, 2));
						break;
					case 0x08:
						{
							uint16_t fd = x80_readword(cpu, cpu->sp + 10, 2);
							uint16_t buf = x80_readword(cpu, cpu->sp + 8, 2);
							uint16_t count = x80_readword(cpu, cpu->sp + 6, 2);

							char * buffer = malloc(count);
							for(uint16_t offset = 0; offset < count; offset++)
							{
								buffer[offset] = x80_readbyte(cpu, buf + offset);
							}
							cpu->hl = write(fd, buffer, count);
							free(buffer);
						}
						break;
					default:
						fprintf(stderr, "Unimplemented UZI system call %04X\n", syscallnum);
						exit(0);
					}
					break;
				}
				// fall through
			default:
				if(system_type == X80_SYSTEM_CPM)
				{
					switch(code)
					{
					case 1:
						DEBUG("BOOT()\n");
						exit(0);
						break;
					case 2:
						DEBUG("WBOOT()\n");
						exit(0);
						break;
					case 3:
						DEBUG("CONST()\n");
						{
							int c = bdos_peekchar();
							cpu->a = c == -1 ? 0xFF : c;
						}
						break;
					case 4:
						DEBUG("CONIN()\n");
						cpu->a = bdos_waitchar();
						break;
					case 5:
						DEBUG("CONOUT(C=%02X)\n", cpu->c);
						putchar(cpu->c);
						break;
					case 6:
						DEBUG("LIST(C=%02X)\n", cpu->c);
						// TODO
						break;
					case 7:
						DEBUG("%s(C=%02X)\n", cpm_version < X80_CPM_3 ? "PUNCH" : "AUXOUT", cpu->c);
						// TODO
						break;
					case 8:
						DEBUG("%s()\n", cpm_version < X80_CPM_3 ? "READER" : "AUXIN");
						// TODO
						break;
					case 9:
						if(msx_dos_version != 0)
							break;
						DEBUG("HOME()\n");
						// TODO
						break;
					case 10:
						if(msx_dos_version != 0)
							break;
						DEBUG("SELDSK(C=%02X,E=%02X)\n", cpu->c, cpu->e);
						// TODO
						break;
					case 11:
						{
							if(msx_dos_version != 0)
								break;

							uint16_t param;
							if(cpm_version < X80_CPM_20)
							{
								DEBUG("SETTRK(C=%02X)\n", cpu->c);
								param = cpu->c;
							}
							else
							{
								DEBUG("SETTRK(BC=%04X)\n", cpu->bc);
								param = cpu->bc;
							}
							// TODO
							(void) param;
						}
						break;
					case 12:
						{
							if(msx_dos_version != 0)
								break;

							uint16_t param;
							if(cpm_version < X80_CPM_20)
							{
								DEBUG("SETSEC(C=%02X)\n", cpu->c);
								param = cpu->c;
							}
							else
							{
								DEBUG("SETSEC(BC=%04X)\n", cpu->bc);
								param = cpu->bc;
							}
							// TODO
							(void) param;
						}
						break;
					case 13:
						if(msx_dos_version != 0)
							break;
						DEBUG("SETDMA(BC=%04X)\n", cpu->bc);
						// TODO
						break;
					case 14:
						if(msx_dos_version != 0)
							break;
						DEBUG("READ()\n");
						// TODO
						break;
					case 15:
						if(msx_dos_version != 0)
							break;
						DEBUG("WRITE(C=%02X)\n", cpu->c);
						// TODO
						break;
					case 16:
						if(cpm_version < X80_CPM_20)
							goto unimplemented_bios_call;
						DEBUG("LISTST()\n");
						// TODO
						break;
					case 17:
						if(cpm_version < X80_CPM_20)
							goto unimplemented_bios_call;
						if(msx_dos_version != 0)
							break;
						DEBUG("SECTRAN(BC=%04X,DE=%04X)\n", cpu->bc, cpu->de);
						// TODO
						break;
					case 18:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("CONOST()\n");
						// TODO
						break;
					case 19:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("AUXIST()\n");
						// TODO
						break;
					case 20:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("AUXOST()\n");
						// TODO
						break;
					case 21:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("DEVTBL()\n");
						// TODO
						break;
					case 22:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("DEVINI(C=%02X)\n", cpu->c);
						// TODO
						break;
					case 23:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("DRVTBL()\n");
						// TODO
						break;
					case 24:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("MULTIO(C=%02X)\n", cpu->c);
						// TODO
						break;
					case 25:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("FLUSH()\n");
						// TODO
						break;
					case 26:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("MOVE(BC=%04X,DE=%04X,HL=%04X)\n", cpu->bc, cpu->de, cpu->hl);
						// TODO
						break;
					case 27:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("TIME(C=%02X)\n", cpu->c);
						// TODO
						break;
					case 28:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("SELMEM(A=%02X)\n", cpu->a);
						// TODO
						break;
					case 29:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("SETBNK(A=%02X)\n", cpu->a);
						// TODO
						break;
					case 30:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("XMOVE(B=%02X,C=%02X)\n", cpu->b, cpu->c);
						// TODO
						break;
					case 31:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("USERF()\n");
						// TODO
						break;
					case 32:
					case 33:
						if(cpm_version < X80_CPM_3)
							goto unimplemented_bios_call;
						DEBUG("RESERV%d()\n", code - 31);
						cpu->pc = 0;
						break;
					default:
					unimplemented_bios_call:
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

