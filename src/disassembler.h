#ifndef _DISASSEMBLER_H
#define _DISASSEMBLER_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARSE_BUFFER_SIZE 8

typedef struct parse_context_t
{
	int cpu;
	uint32_t offset;
	uint8_t buffer[PARSE_BUFFER_SIZE];
	uint8_t bstart, blength;
	uint32_t imm[2];
	int dspsize, immsize, defaultimmsize;
	const char * suffix;
	uint8_t (* fetch)(struct parse_context_t * context);
	void * data;
} parse_context_t;

uint8_t file_context_fetch(parse_context_t * context)
{
	return fgetc((FILE *)context->data);
}

uint8_t context_fetch(parse_context_t * context)
{
	int c = context->fetch(context);
	if(c == -1)
		return 0;
	context->buffer[(context->bstart + context->blength++) % PARSE_BUFFER_SIZE] = c;
	return c;
}

int get_cpu_address_width(int cpu);

void context_print_prefix(parse_context_t * context, FILE * out, size_t bytes)
{
	fprintf(out, "%0*X\t", 2 * get_cpu_address_width(context->cpu), context->offset);

	for(size_t i = 0; i < PARSE_BUFFER_SIZE; i++)
	{
		if(i)
			fprintf(out, " ");
		if(i < bytes)
			fprintf(out, "%02X", context->buffer[(context->bstart + i) % PARSE_BUFFER_SIZE]);
		else
			fprintf(out, "  ");
	}

	fprintf(out, "\t");
}

void context_format(parse_context_t * context, FILE * out, const char * format)
{
	int immcount = 0;
	int32_t imm;
	context_print_prefix(context, out, context->blength);
	for(size_t i = 0; format[i] != 0; i++)
	{
		if(format[i] == '%')
		{
			size_t size;
			switch(format[++i])
			{
			case '1':
				size = 2;
				break;
			case '2':
				size = 4;
				break;
			case '3':
				size = 6;
				break;
			case 'd':
				size = context->dspsize * 2;
				break;
			case 'i':
				size = context->immsize * 2;
				break;
			case '%':
				fputc('%', out);
				continue;
			default:
				fprintf(stderr, "INTERNAL ERROR: Invalid formatting character '%c'\n", format[i]);
				continue;
			}
			imm = context->imm[immcount];
			switch(format[++i])
			{
			case 'U':
				if(size < 8)
				{
					imm &= (1 << (size * 4)) - 1;
				}
				fprintf(out, "0x%0*X", (int)size, imm);
				break;
			case 'S':
				if(size < 8)
				{
					if((imm & (1 << (size * 4 - 1))))
						imm |= -1 << (size * 4);
					else
						imm &= (1 << (size * 4)) - 1;
				}
				fprintf(out, "%s0x%0*X", imm < 0 ? "-" : "", (int)size, imm < 0 ? -imm : imm);
				break;
			case 'D':
				if(size < 8)
				{
					if((imm & (1 << (size * 4 - 1))))
						imm |= -1 << (size * 4);
					else
						imm &= (1 << (size * 4)) - 1;
				}
				if(imm != 0)
					fprintf(out, "%s0x%0*X", imm < 0 ? "-" : "+", (int)size, imm < 0 ? -imm : imm);
				break;
			case 'R':
				if(size < 8)
				{
					if((imm & (1 << (size * 4 - 1))))
						imm |= -1 << (size * 4);
					else
						imm &= (1 << (size * 4)) - 1;
				}
				fprintf(out, "0x%0*X", 2 * get_cpu_address_width(context->cpu), imm + context->offset + context->blength);
				break;
			default:
				fprintf(stderr, "INTERNAL ERROR: Invalid formatting character '%c'\n", format[i]);
				continue;
			}
			immcount++;
		}
		else if(format[i] == '\t' && context->suffix)
		{
			fprintf(out, "%s\t", context->suffix);
			context->suffix = NULL;
		}
		else
		{
			fputc(format[i], out);
		}
	}
	if(context->suffix)
	{
		fprintf(out, "%s", context->suffix);
		context->suffix = NULL;
	}
	fprintf(out, "\n");
}

void context_spill(parse_context_t * context, FILE * out, size_t count)
{
	while(count > 0)
	{
		context_print_prefix(context, out, 1);
		fprintf(out, "db\t0x%02X", context->buffer[context->bstart]);
		fprintf(out, "\n");

		context->offset += 1;
		context->bstart = (context->bstart + 1) % PARSE_BUFFER_SIZE;
		context->blength -= 1;
		count -= 1;
	}
}

void context_empty(parse_context_t * context)
{
	context->offset += context->blength;
	context->bstart = context->blength = 0;
	context->imm[0] = context->imm[1] = 0;
}

uint32_t context_fetch_word(parse_context_t * context, size_t bytes)
{
	uint32_t value = 0;
	for(size_t i = 0; i < bytes; i++)
		value |= context_fetch(context) << (i * 8);
	return value;
}

bool is_ez80_prefix(parse_context_t * context)
{
	uint8_t op;
	return context->blength > 0 &&
		((op = context->buffer[context->bstart]) == 0x40 || op == 0x49 || op == 0x52 || op == 0x5B);
}

#include "../out/disassembler.gen.c"

void context_reset(parse_context_t * context)
{
	context->immsize = context->defaultimmsize;
	context->dspsize = 1;
}

void context_init(parse_context_t * context, int defaultimmsize)
{
	memset(context, 0, sizeof(parse_context_t));
	context->defaultimmsize = defaultimmsize;
	context_reset(context);
}

void context_init_with_file(parse_context_t * context, FILE * file, int cpu)
{
	context_init(context, get_default_immsize(cpu));
	context->fetch = file_context_fetch;
	context->data = file;
	context->cpu = cpu;
}

#endif /* _DISASSEMBLER_H */
