#ifndef INCLUDEFILE_H
#define INCLUDEFILE_H

#include <stddef.h>

#define IS_ZILOG(cpu) ((cpu) == ASM_Z80 || (cpu) == ASM_Z180 || (cpu) == ASM_Z280 || (cpu) == ASM_Z380 || (cpu) == ASM_Z380 || (cpu) == ASM_EZ80 || (cpu) == ASM_EZ80_ADL || (cpu) == ASM_SM83 || (cpu) == ASM_Z80N)

struct expression
{
	int type;
	union
	{
		struct expression * args[2];
		char * name;
		int value;
	};
};

struct operand
{
	int type;
	struct expression * displacement;
};

struct instruction
{
	int arch;
	int mnem;
	int attr;
	size_t count;
	struct operand ** args;
	struct instruction * next;

/* to be filled in later */
	size_t length;
	size_t offset, new_offset;
	int * opsizes;
};

struct definition
{
	char * name;
	int islabel;
	union
	{
		struct expression * value;
		struct instruction ** location;
	};
	struct definition * next;
};

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX(a, b) ((a) >= (b) ? (a) : (b))

extern int yylex(void);
void yyerror(const char * s);

struct expression * expression_identifier(char * name);
struct expression * expression_string(char * value);
struct expression * expression_integer(int value);
struct expression * expression_unary(int type, struct expression * arg0);
struct expression * expression_binary(int type, struct expression * arg0, struct expression * arg1);
void expression_delete(struct expression * exp);

struct operand * operand_make(int type, struct expression * displacement);

void instruction_stream_clear(void);
struct instruction * instruction_create(int mnem, int attr);
void instruction_add_operand(struct instruction * ins, struct operand * opd);

void instruction_stream_append(struct instruction * ins);

void define_constant(char * name, struct expression * value);
void define_label(char * name);

extern int current_cpu;
extern struct instruction * instruction_stream;
extern struct instruction ** instruction_pointer;
extern struct definition * global_definition;
extern int enable_adl;
extern int last_ddir;

#endif
