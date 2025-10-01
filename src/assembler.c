
#include "../out/parser.tab.h"
#include "../out/parser.gen.h"
#include "../src/assembler.h"

int enable_adl;
int last_ddir;
int current_cpu = ASM_Z80;

enum generation_level
{
	GEN_IGNORE_SYMBOLS,
	GEN_CALCULATE_OFFSETS,
	GEN_GENERATE_CODE,
};

struct expression * expression_make(int type)
{
	struct expression * e = malloc(sizeof(struct expression));
	memset(e, 0, sizeof(struct expression));
	e->type = type;
	return e;
}

void expression_delete(struct expression * exp)
{
	if(exp->type == IDENTIFIER || exp->type == STRING)
	{
		free(exp->name);
	}
	else if(exp->type == '_' || exp->type == '~')
	{
		expression_delete(exp->args[0]);
	}
	else if(exp->type != INTEGER)
	{
		expression_delete(exp->args[0]);
		expression_delete(exp->args[1]);
	}
	free(exp);
}

struct expression * expression_identifier(char * name)
{
	struct expression * exp = expression_make(IDENTIFIER);
	exp->name = name;
	return exp;
}

struct expression * expression_string(char * value)
{
	struct expression * exp = expression_make(STRING);
	exp->name = value;
	return exp;
}

struct expression * expression_integer(int value)
{
	struct expression * exp = expression_make(INTEGER);
	exp->value = value;
	return exp;
}

struct expression * expression_unary(int type, struct expression * arg0)
{
	struct expression * exp = expression_make(type);
	exp->args[0] = arg0;
	exp->args[1] = NULL;
	return exp;
}

struct expression * expression_binary(int type, struct expression * arg0, struct expression * arg1)
{
	struct expression * exp = expression_make(type);
	exp->args[0] = arg0;
	exp->args[1] = arg1;
	return exp;
}

struct operand * operand_make(int type, struct expression * displacement)
{
	struct operand * o = malloc(sizeof(struct operand));
	o->type = type;
	o->displacement = displacement;
	return o;
}

struct instruction * instruction_create(int mnem, int attr)
{
	struct instruction * i = malloc(sizeof(struct instruction));
	memset(i, 0, sizeof(struct instruction));
	i->arch = current_cpu;
	if(current_cpu == ASM_EZ80)
	{
		if(enable_adl)
			i->arch = ASM_EZ80_ADL;
		if(attr)
		{
			if(enable_adl)
			{
				if(!(attr & (SFX_S | SFX_L)))
					attr |= SFX_L;
				if(!(attr & (SFX_IS | SFX_IL)))
					attr |= SFX_IL;
			}
			else
			{
				if(!(attr & (SFX_S | SFX_L)))
					attr |= SFX_S;
				if(!(attr & (SFX_IS | SFX_IL)))
					attr |= SFX_IS;
			}
		}
	}
	i->mnem = mnem;
	i->attr = attr;
	return i;
}

void instruction_add_operand(struct instruction * ins, struct operand * opd)
{
	if(ins->count == 0)
	{
		ins->args = malloc(sizeof(struct operand *));
	}
	else
	{
		ins->args = realloc(ins->args, sizeof(struct operand *) * (ins->count + 1));
	}
	ins->args[ins->count] = opd;
	ins->count ++;
}

struct instruction * instruction_stream = NULL;
struct instruction ** instruction_pointer = &instruction_stream;

static void delete_instruction(struct instruction * ins)
{
	// TODO: delete members as well
	free(ins);
}

void instruction_stream_clear(void)
{
	struct instruction * current = instruction_stream;
	while(current)
	{
		struct instruction * next = current->next;
		delete_instruction(current);
		current = next;
	}
	instruction_stream = NULL;
	instruction_pointer = &instruction_stream;
}

void instruction_stream_append(struct instruction * ins)
{
	*instruction_pointer = ins;
	instruction_pointer = &ins->next;
	if(ins->count)
	{
		ins->opsizes = malloc(sizeof(int) * ins->count);
		memset(ins->opsizes, 0, sizeof(int) * ins->count);
	}
	if(current_cpu == ASM_Z380)
	{
		ins->attr = last_ddir;
		if(ins->mnem == MNEM_DDIR)
		{
			int i;
			last_ddir = SFX_SPECIFIED;
			for(i = 0; i < ins->count; i++)
			{
				switch(ins->args[i]->type)
				{
				case OP_IB:
					last_ddir |= SFX_IB;
					break;
				case OP_IW:
					last_ddir |= SFX_IW;
					break;
				}
			}
		}
		else
		{
			last_ddir = 0;
		}
	}
}

struct definition * global_definition = NULL;

void definition_free(struct definition * d)
{
	free(d->name);
	if(!d->islabel)
		expression_delete(d->value);
	free(d);
}

void definition_chain(struct definition * d)
{
	struct definition ** current = &global_definition;

	while(*current)
	{
		int cmp = strcmp((*current)->name, d->name);
		if(cmp == 0)
		{
			struct definition * tmp = *current;
			fprintf(stderr, "Warning: name %s redefined\n", d->name);
			d->next = tmp->next;
			*current = d;
			definition_free(*current);
			return;
		}
		else if(cmp < 0)
		{
			current = &(*current)->next;
		}
		else
		{
			d->next = *current;
			*current = d;
			return;
		}
	}

	/* reached end */
	d->next = NULL; // just to be safe
	*current = d;
}

void definition_create(char * name, int islabel, void * value)
{
	struct definition * d = malloc(sizeof(struct definition));
	d->name = name;
	d->islabel = islabel;
	d->value = value;
	d->next = NULL;

	definition_chain(d);
}

void define_constant(char * name, struct expression * value)
{
	definition_create(name, 0, value);
}

void define_label(char * name)
{
	definition_create(name, 1, instruction_pointer);
}

struct definition * definition_lookup(const char * name)
{
	struct definition ** current = &global_definition;

	while(*current)
	{
		int cmp = strcmp((*current)->name, name);
		if(cmp == 0)
		{
			return *current;
		}
		else if(cmp < 0)
		{
			current = &(*current)->next;
		}
		else
		{
			break;
		}
	}
	fprintf(stderr, "Undefined name: %s\n", name);
	exit(1); /* TODO */
	return NULL;
}

enum
{
	CON_EQU, /* default */
	CON_FIT, /* fits specified type */
	CON_REL, /* relative address of size specified */
	CON_HIGH, /* high byte must be equal */

	CON_ANY = -1,

	FIT_U8 = 1, /* unsigned byte */
	FIT_S8, /* unsigned byte */
//	FIT_U16, /* unsigned word */
	FIT_S16, /* unsigned word */
//	FIT_U24, /* unsigned 24-bit (z380) */
	FIT_S24, /* unsigned 24-bit (z380) */
	FIT_S8PLUS, /* unsigned byte or more */
	FIT_U16PLUS,

	LEN_PLUS = 0x80,
};

enum
{
	_ACT_START = -0x100,
	ACT_PUT8, /* inserts a byte */
	ACT_REL8, /* inserts a byte minus PC */
	ACT_PUT16, /* inserts a 16-bit value */
	ACT_REL16, /* inserts a 16-bit value minus PC */
//	ACT_PUT24, /* inserts a 24-bit value */
	ACT_REL24, /* inserts a 24-bit value */
	ACT_PUT32, /* inserts a 32-bit value */
	ACT_PUT8PLUS, /* inserts a value at least 8 bits wide (IB/IW) */
	ACT_PUT16PLUS, /* inserts a value at least 16 bits wide (IB/IW) */
};

struct operand_pattern
{
	int type;
	int constraint;
	int value;
};

struct pattern
{
	struct operand_pattern args[3];
	int length;
	int acts[10];
};

struct patternset
{
	size_t count;
	const struct pattern * patterns;
};

#include "../out/parser.gen.c"

static size_t current_instruction_location;

int expression_evaluate(struct expression * exp)
{
	if(exp == NULL)
		return 0;
	switch(exp->type)
	{
	case IDENTIFIER:
		if(strcmp("$", exp->name) == 0)
			return current_instruction_location;
	{
		struct definition * d = definition_lookup(exp->name);
		if(d->islabel)
			return (*d->location)->offset;
		else
			return expression_evaluate(d->value);
	}
	case STRING:
	{
		int i, value = 0;
		for(i = 0; i < 4 && exp->name[i]; i++)
			value |= exp->name[i] << (i << 3);
		return value;
	}
	case INTEGER:
		return exp->value;
	case '_':
		return -expression_evaluate(exp->args[0]);
	case '~':
		return -expression_evaluate(exp->args[0]);
	case '+':
		return expression_evaluate(exp->args[0]) + expression_evaluate(exp->args[1]);
	case '-':
		return expression_evaluate(exp->args[0]) - expression_evaluate(exp->args[1]);
	case LL:
		return expression_evaluate(exp->args[0]) << expression_evaluate(exp->args[1]);
	case GG:
		return expression_evaluate(exp->args[0]) >> expression_evaluate(exp->args[1]);
	case GGG:
		return (unsigned)expression_evaluate(exp->args[0]) >> expression_evaluate(exp->args[1]);
	case '*':
		return expression_evaluate(exp->args[0]) * expression_evaluate(exp->args[1]);
	case '/':
		return expression_evaluate(exp->args[0]) / expression_evaluate(exp->args[1]);
	case DD:
		return (unsigned)expression_evaluate(exp->args[0]) / expression_evaluate(exp->args[1]);
	case '%':
		return expression_evaluate(exp->args[0]) % expression_evaluate(exp->args[1]);
	case MM:
		return (unsigned)expression_evaluate(exp->args[0]) % expression_evaluate(exp->args[1]);
	case '&':
		return expression_evaluate(exp->args[0]) & expression_evaluate(exp->args[1]);
	case '^':
		return expression_evaluate(exp->args[0]) ^ expression_evaluate(exp->args[1]);
	case '|':
		return expression_evaluate(exp->args[0]) | expression_evaluate(exp->args[1]);
	default:
		fprintf(stderr, "Internal error: undefined node type %d\n", exp->type);
		return 0;
	}
}

int ez80_has_extra(struct instruction * ins)
{
	switch(ins->arch)
	{
	default:
		return 0;
	case ASM_EZ80_ADL:
		return !(ins->attr & SFX_IS);
	case ASM_EZ80:
		return !!(ins->attr & SFX_IL);
	}
}

size_t fetch_length(struct instruction * ins, const struct pattern * pat)
{
	size_t length = pat->length;
	if((length & LEN_PLUS))
	{
		length &= ~LEN_PLUS;
		switch(ins->arch)
		{
		case ASM_Z380:
			if((ins->attr & SFX_IB))
				length += 1;
			else if((ins->attr & SFX_IW))
				length += 2;
			if(ins->attr != 0 && !(ins->attr & SFX_SPECIFIED))
				length += 2;
			break;
		case ASM_EZ80_ADL:
		case ASM_EZ80:
			length += ez80_has_extra(ins);
			break;
		}
	}
	if(ins->attr && (ins->arch == ASM_EZ80 || ins->arch == ASM_EZ80_ADL))
	{
		/* prefix */
		length += 1;
	}
	return length;
}

int check_unsigned_size(unsigned int value)
{
	if(value <= 0xFF)
	{
		return 1;
	}
	else if(value <= 0xFFFF)
	{
		return 2;
	}
	else if(value <= 0xFFFFFF)
	{
		return 3;
	}
	else
	{
		return 4;
	}
}

int check_signed_size(int value)
{
	if(-0x80 <= value && value <= 0x7F)
	{
		return 1;
	}
	else if(-0x8000 <= value && value <= 0x7FFF)
	{
		return 2;
	}
	else if(-0x800000 <= value && value <= 0x7FFFFF)
	{
		return 3;
	}
	else
	{
		return 4;
	}
}

const struct pattern * match_pattern(const struct patternset * set, struct instruction * ins, int * opvals, enum generation_level generate)
{
	size_t index, i;
	int * opsizes = malloc(sizeof(int) * ins->count);
	size_t fits_ranges;
	int * almost_best_opsizes = NULL;
	const struct pattern * best_pattern = NULL;
	int current_ddir = ins->attr;
	if(generate != GEN_IGNORE_SYMBOLS)
	{
		for(i = 0; i < ins->count; i++)
		{
			opvals[i] = expression_evaluate(ins->args[i]->displacement);
		}
	}
	for(index = 0; index < set->count; index++)
	{
		const struct pattern * pattern = &set->patterns[index];
		//if(pattern->count != ins->count)
		//	continue;
		size_t length = fetch_length(ins, pattern);
		fits_ranges = 1;
		for(i = 0; i < ins->count; i++)
		{
			if(pattern->args[i].type != ins->args[i]->type)
				goto advance;
			switch(pattern->args[i].constraint)
			{
			case CON_EQU:
				if(generate == GEN_IGNORE_SYMBOLS)
					break;
				if(pattern->args[i].value != opvals[i])
					goto advance;
				break;
			case CON_FIT:
				switch(pattern->args[i].value)
				{
				case FIT_S8:
					if(generate != GEN_IGNORE_SYMBOLS && (ins->opsizes[i] > 1 || opvals[i] < -0x80 || opvals[i] >= 0x80))
					{
						if(generate == GEN_GENERATE_CODE)
							goto advance;
						fits_ranges = 0;
					}
					opsizes[i] = 1;
					break;
				case FIT_S8PLUS:
					if(generate == GEN_IGNORE_SYMBOLS)
					{
						opsizes[i] = 1;
					}
					else
					{
						int size = check_signed_size(opvals[i]);
						if(!(ins->attr & SFX_SPECIFIED))
						{
							if(ins->attr && size < 2)
							{
								size = 2;
							}
							if(size > 3)
							{
								if(generate == GEN_GENERATE_CODE)
									goto advance;
								fits_ranges = 0;
							}
						}
						else switch(ins->attr & ~SFX_SPECIFIED)
						{
						case 0:
							if(size > 1)
							{
								if(generate == GEN_GENERATE_CODE)
									goto advance;
								fits_ranges = 0;
							}
							size = 1;
							break;
						case SFX_IB:
							if(size > 2)
							{
								if(generate == GEN_GENERATE_CODE)
									goto advance;
								fits_ranges = 0;
							}
							size = 2;
							break;
						case SFX_IW:
							if(size > 3)
							{
								if(generate == GEN_GENERATE_CODE)
									goto advance;
								fits_ranges = 0;
							}
							size = 3;
							break;
						}
						if(size < ins->opsizes[i])
						{
							size = ins->opsizes[i];
						}
						opsizes[i] = size;
						switch(opsizes[i])
						{
						case 1:
							current_ddir = 0;
							break;
						case 2:
							current_ddir = SFX_IB;
							break;
						case 3:
							current_ddir = SFX_IW;
							break;
						}
					}
					break;
				case FIT_U16PLUS:
					if(generate == GEN_IGNORE_SYMBOLS)
					{
						opsizes[i] = 2;
					}
					else
					{
						int size = check_unsigned_size(opvals[i]);
						if(size < 2)
						{
							size = 2;
						}
						if(!(ins->attr & SFX_SPECIFIED))
						{
							if(ins->attr && size < 3)
							{
								size = 3;
							}
						}
						else switch(ins->attr & ~SFX_SPECIFIED)
						{
						case SFX_IB:
							if(size > 3)
							{
								if(generate == GEN_GENERATE_CODE)
									goto advance;
								fits_ranges = 0;
							}
							size = 3;
							break;
						case SFX_IW:
							size = 4;
							break;
						}
						if(size < ins->opsizes[i])
						{
							size = ins->opsizes[i];
						}
						opsizes[i] = size;
						switch(opsizes[i])
						{
						case 2:
							current_ddir = 0;
							break;
						case 3:
							current_ddir = SFX_IB;
							break;
						case 4:
							current_ddir = SFX_IW;
							break;
						}
					}
					break;
				}
				break;
			case CON_REL:
			{
				int tmp = opvals[i] - current_instruction_location - length;
				switch(pattern->args[i].value)
				{
				case FIT_S8:
					if(generate != GEN_IGNORE_SYMBOLS && (ins->opsizes[i] > 1 || tmp < -0x80 || tmp >= 0x80))
					{
						if(generate == GEN_GENERATE_CODE)
							goto advance;
						fits_ranges = 0;
					}
					opsizes[i] = 1;
					break;
				case FIT_S16:
					if(generate != GEN_IGNORE_SYMBOLS && (ins->opsizes[i] > 2 || tmp < -0x8000 || tmp >= 0x8000))
					{
						if(generate == GEN_GENERATE_CODE)
							goto advance;
						fits_ranges = 0;
					}
					opsizes[i] = 2;
					break;
				case FIT_S24:
					if(generate != GEN_IGNORE_SYMBOLS && (ins->opsizes[i] > 3 || tmp < -0x800000 || tmp >= 0x800000))
					{
						if(generate == GEN_GENERATE_CODE)
							goto advance;
						fits_ranges = 0;
					}
					opsizes[i] = 3;
					break;
				}
				break;
			}
			case CON_HIGH:
				if(generate != GEN_IGNORE_SYMBOLS && ((opvals[i] >> 8) != pattern->args[i].value))
				{
					if(generate == GEN_GENERATE_CODE)
						goto advance;
					fits_ranges = 0;
				}
				opsizes[i] = 1;
				break;
			}
		}
		if(!fits_ranges)
		{
			best_pattern = pattern;
			almost_best_opsizes = malloc(sizeof(int) * ins->count);
			memcpy(almost_best_opsizes, opsizes, sizeof(int) * ins->count);
			continue;
		}
		best_pattern = pattern;
		goto found_match;
	advance:
		;
	}
	free(opsizes);
	if(almost_best_opsizes != NULL)
	{
		opsizes = almost_best_opsizes;
		goto found_match;
	}
	return NULL;
found_match:
	memcpy(ins->opsizes, opsizes, sizeof(int) * ins->count);
	if(!(ins->attr & SFX_SPECIFIED))
		ins->attr = current_ddir;
	free(opsizes);
	return best_pattern;
}

#ifndef putbyte
# define putbyte putchar
#endif

void putdata(int size, int value)
{
	switch(size)
	{
	case 1:
#if DEBUG
		printf("%02X ", value & 0xFFu);
#else
		putbyte(value);
#endif
		break;
	case 2:
#if DEBUG
		printf("%04X ", value & 0xFFFFu);
#else
		putbyte(value);
		putbyte(value >> 8);
#endif
		break;
	case 3:
#if DEBUG
		printf("%06X ", value & 0xFFFFFFu);
#else
		putbyte(value);
		putbyte(value >> 8);
		putbyte(value >> 16);
#endif
		break;
	case 4:
#if DEBUG
		printf("%08X ", value & 0xFFFFFFFFu);
#else
		putbyte(value);
		putbyte(value >> 8);
		putbyte(value >> 16);
		putbyte(value >> 24);
#endif
		break;
	}
}

void putstring(const char * value)
{
#if DEBUG
	printf("\"%s\" ", value);
#else
	int i;
	for(i = 0; value[i]; i++)
	{
		putdata(1, value[i]);
	}
#endif
}

void putzeroes(int count)
{
	int i;
	for(i = 0; i < count; i++)
	{
		putdata(1, 0);
	}
}

void produce_pattern(struct instruction * ins, const struct pattern * pat, int * opvals)
{
	size_t length = fetch_length(ins, pat);
	size_t i, b = 0, tmp;
	if(ins->attr)
	{
		if((ins->arch == ASM_EZ80 || ins->arch == ASM_EZ80_ADL))
		{
			tmp = 0x40;
			if((ins->attr & SFX_L))
				tmp += 0x09;
			if((ins->attr & SFX_IL))
				tmp += 0x12;
			putdata(1, tmp);
			b ++;
		}
		else if(ins->arch == ASM_Z380 && !(ins->attr & SFX_SPECIFIED))
		{
			/* z380 usually has a separate instruction preceding it, syntactically */
			if((ins->attr & SFX_IB))
				putdata(2, 0xC3DD);
			else if((ins->attr & SFX_IW))
				putdata(2, 0xC3FD);
			b += 2;
		}
	}
	for(i = 0; b < length; i++, b++)
	{
		if(pat->acts[i] >= 0)
			putdata(1, pat->acts[i]);
		else switch(pat->acts[i])
		{
		case ACT_PUT8:
			putdata(1, opvals[pat->acts[++i]]);
			break;
		case ACT_PUT8PLUS:
			switch(ins->attr)
			{
			default:
				putdata(1, opvals[pat->acts[++i]]);
				break;
			case SFX_IB:
				putdata(2, opvals[pat->acts[++i]]);
				b ++;
				break;
			case SFX_IW:
				putdata(3, opvals[pat->acts[++i]]);
				b += 2;
				break;
			}
			break;
		case ACT_REL8:
			putdata(1, opvals[pat->acts[++i]] - current_instruction_location - length);
			break;

		case ACT_PUT16:
			tmp = ez80_has_extra(ins);
			putdata(2 + tmp, opvals[pat->acts[++i]]);
			b += tmp + 1;
			break;
		case ACT_PUT16PLUS:
			switch(ins->attr)
			{
			default:
				putdata(2, opvals[pat->acts[++i]]);
				b ++;
				break;
			case SFX_IB:
				putdata(3, opvals[pat->acts[++i]]);
				b += 2;
				break;
			case SFX_IW:
				putdata(4, opvals[pat->acts[++i]]);
				b += 3;
				break;
			}
			break;
		case ACT_REL16:
			tmp = ez80_has_extra(ins);
			putdata(2 + tmp, opvals[pat->acts[++i]] - current_instruction_location - length);
			b += tmp + 1;
			break;

		case ACT_REL24:
			putdata(3, opvals[pat->acts[++i]] - current_instruction_location - length);
			b += 2;
			break;

		case ACT_PUT32:
			putdata(4, opvals[pat->acts[++i]]);
			b += 3;
			break;
		}
	}
}

int calculate_lengths(enum generation_level generate)
{
	struct instruction * current = instruction_stream;
	size_t offset = 0L;
	int changed = 0;
	int i;

	while(current)
	{
		current->offset = current->new_offset;
		current = current->next;
	}

	current = instruction_stream;

	while(current)
	{
		if(current->mnem != DIR_ORG && current->offset != offset)
		{
			current->new_offset = offset;
			changed = 1;
		}

		current_instruction_location = current->offset; /* needed to handle $ pseudo-variable */
		if(current->mnem >= 0)
		{
			int * opvals = malloc(sizeof(int) * current->count);
			const struct pattern * pat = match_pattern(&patterns[current->arch][current->mnem][current->count], current, opvals, generate);
			if(!pat)
			{
				fprintf(stderr, "Invalid instruction\n");
				return -1;
			}
			size_t length = fetch_length(current, pat);
			if(length > current->length)
			{
				current->length = length;
				changed = 1;
			}
			offset += current->length;
			if(generate == GEN_GENERATE_CODE)
			{
				produce_pattern(current, pat, opvals);
			}
			free(opvals);
		}
		else switch(current->mnem)
		{
		case DIR_ORG:
			offset = expression_evaluate(current->args[0]->displacement);
			break;
		case DIR_D8:
		case DIR_D16:
		case DIR_D24:
		case DIR_D32:
			current->length = 0;
			for(i = 0; i < current->count; i++)
			{
				if(current->args[i]->displacement->type == STRING)
				{
					current->length += strlen(current->args[i]->displacement->name);
					current->length &= current->mnem;
				}
				else
				{
					current->length += -current->mnem;
				}
			}
			offset += current->length;
			if(generate == GEN_GENERATE_CODE)
			{
				for(i = 0; i < current->count; i++)
				{
					if(current->args[i]->displacement->type == STRING)
					{
						putstring(current->args[i]->displacement->name);
						putzeroes(-strlen(current->args[i]->displacement->name) & ~current->mnem);
					}
					else
					{
						putdata(-current->mnem, expression_evaluate(current->args[i]->displacement));
					}
				}
			}
			break;
		}

		current = current->next;
	}

	return changed;
}

void usage(char * argv0)
{
	fprintf(stderr, "Usage: %s [options] [input files]\n", argv0);
}

#define MULTIPLE_INPUT_FILES ((void *)1) /* special value that can't actually be a string */

#ifndef USEASLIB
int main(int argc, char * argv[])
{
	int i;
	char * ofn = NULL;
	int result;
	char * ifn = NULL;

	for(i = 1; i < argc; i++)
	{
		if(argv[i][0] == '-')
		{
			switch(argv[i][1])
			{
			case 'o':
				if(argv[i][2])
					ofn = &argv[i][2];
				else
					ofn = argv[++i];
				break;
			default:
				fprintf(stderr, "Error: unknown flag: `%s'\n", argv[i]);
				usage(argv[0]);
				exit(1);
				break;
			case 'h':
				usage(argv[0]);
				exit(0);
				break;
			}
		}
		else
		{
			if(freopen(argv[i], "r", stdin) == NULL)
			{
				fprintf(stderr, "Error: unable to open input file `%s'\n", argv[i]);
				exit(1);
			}
			result = yyparse();
			fclose(stdin);
			if(result)
				return result;
			if(ifn == NULL)
				ifn = argv[i]; /* make output file the same without .asm */
			else if(ifn != MULTIPLE_INPUT_FILES)
				ifn = MULTIPLE_INPUT_FILES; /* default output file */
		}
	}

	if(ifn == NULL)
	{
		/* no input files, use stdin */
		result = yyparse();
		fclose(stdin);
		if(result)
			return result;
	}

	/* permits proper functioning of final labels */
	instruction_stream_append(instruction_create(DIR_EOF, 0));

	result = calculate_lengths(GEN_IGNORE_SYMBOLS);
	if(result == -1)
		exit(1);
	while((result = calculate_lengths(GEN_CALCULATE_OFFSETS)))
		;
	if(result == -1)
		exit(1);

	if(ofn == NULL)
	{
		if(ifn == NULL || ifn == MULTIPLE_INPUT_FILES)
		{
			ofn = "a.out";
		}
		else
		{
			char * dot = strrchr(ifn, '.');
			long fnend = dot ? dot - ifn : strlen(ifn);
			ofn = malloc(fnend + 5);
			memcpy(ofn, ifn, fnend);
			memcpy(ofn + fnend, ".out", 5);
		}
	}

	if(freopen(ofn, "wb", stdout) == NULL)
	{
		fprintf(stderr, "Error: Unable to open output file `%s'\n", ofn);
	}

	result = calculate_lengths(GEN_GENERATE_CODE);
	if(result == -1)
		exit(1);

	fclose(stdout);

	return 0;
}
#endif /* USEASLIB */

