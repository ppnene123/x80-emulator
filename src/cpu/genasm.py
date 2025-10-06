#! /usr/bin/python3

import os
import sys
from database import *

PATTERNS = []

def generate_parser():
	global PATTERNS
	cpus = ['i80', 'i85', 'vm1', 'z80', 'z180', 'z280', 'z380', 'ez80', 'sm83', 'z80n', 'r800']
	nocpus = set(T.keys())
	nocpus.difference_update(cpus)
	for key in nocpus:
		del T[key]
	prefixes = set()
	for cpu in cpus:
		prefixes.update(T[cpu].keys())

	# the ordering is important, because ## will match more than # will
	PATTERN_ORDER = [
		# immediates
		'#',
		'##', '##~',
		'####',
		# byte registers
		'A', '.A',
		'A\'',
		'B', '.B',
		'B\'',
		'C', '.C',
		'C\'',
		'D', '.D',
		'D\'',
		'E', '.E',
		'E\'',
		'F', '.F',
		'H', '.H',
		'H\'', 'H1',
		'IXH', 'IXU', '.IXH',
		'IXL', '.IXL',
		'IYH', 'IYU', '.IYH',
		'IYL', '.IYL',
		'L', '.L',
		'L\'', 'L1',
		# word registers
		'PSW', 'AF', '.AF',
		'AF\'', '.AF\'',
		'BC', '.BC',
		'BC\'',
		'DE', '.DE',
		'DE\'',
		'HL', '.HL',
		'HL\'',
		'IX', '.IX',
		'IX\'',
		'IY', '.IY',
		'IY\'',
		'SP', '.SP',
		# double registers
		'DEHL',
		# special registers
		'I', '.I',
		'R', '.R',
		'DSR',
		'MB',
		'SR',
		'USP',
		'XSR',
		'YSR',
		# register-immediate (ez80)
		'IX+#',
		'IY+#',
		'SP+#',
		'PC+#',
		'PC+##',
		'PC+###',
		# indirect
		'(#)', '[#]',
		'(0FF00h+#)',
		'(##)', '(##~)', '[##]',
		'(C)', '[.C]', '(BC)', '[.BC]', # sometimes the same (ez80), sometimes different (z80)
		'(DE)', '[.DE]',
		'(HL)', '[.HL]', 'M',
		'M1',
		'(IX)', '[.IX]',
		'(IY)', '[.IY]',
		'(SP)', '[.SP]',
		# complex indirect
		'(0FF00h+C)',
		'(HL+##)',
		'(IX+#)', '(IX+#~)', '[.IX+#]',
		'(IX+##)',
		'(IY+#)', '(IY+#~)', '[.IY+#]',
		'(IY+##)',
		'(PC+##)', '<##>',
		'(SP+#~)',
		'(SP+##)',
		'(HL+IX)',
		'(HL+IY)',
		'(IX+IY)',
		# auto-alter indirect
		'[.DE--]',
		'[.DE++]',
		'(HLD)', '(HL-)', '[.HL--]',
		'(HLI)', '(HL+)', '[.HL++]',
		# special
		'NC',
		'NZ',
		'PE',
		'PO',
		'P',
		'Z',
		'IB',
		'IW',
		'LCK',
		'LW',
		'W',
		'XM',
		'?',
	]

	PATTERNS = {
		'#': "{ OP_IMM, CON_ANY }",
		'##': "{ OP_IMM, CON_ANY }",
		'(#)': "{ OP_MEM, CON_FIT, FIT_U8 }",
		'(##)': "{ OP_MEM, CON_ANY }",
		'(BC)': "{ OP_MEM_BC }",
		'(DE)': "{ OP_MEM_DE }",
		'(HL)': "{ OP_MEM_HL }",
		'(IX)': "{ OP_MEM_IX }",
		'(IX+#)': "{ OP_MEM_IX, CON_FIT, FIT_S8 }",
		'(IY)': "{ OP_MEM_IY }",
		'(IY+#)': "{ OP_MEM_IY, CON_FIT, FIT_S8 }",
		'(SP)': "{ OP_MEM_SP }",
		'(C)': "{ OP_MEM_C }",
		'A': "{ OP_A }",
		'B': "{ OP_B }",
		'C': "{ OP_C }",
		'D': "{ OP_D }",
		'E': "{ OP_E }",
		'F': "{ OP_F }",
		'H': "{ OP_H }",
		'I': "{ OP_I }",
		'L': "{ OP_L }",
		'R': "{ OP_R }",
		'IXH': "{ OP_IXH }",
		'IXL': "{ OP_IXL }",
		'IYH': "{ OP_IYH }",
		'IYL': "{ OP_IYL }",
		'AF': "{ OP_AF }",
		'BC': "{ OP_BC }",
		'DE': "{ OP_DE }",
		'HL': "{ OP_HL }",
		'SP': "{ OP_SP }",
		'IX': "{ OP_IX }",
		'IY': "{ OP_IY }",
		'AF\'': "{ OP_AF2 }",
		'?': "{ OP_QUESTION }",

		'M': "{ OP_M }",
		'NC': "{ OP_NC }",
		'NZ': "{ OP_NZ }",
		'P': "{ OP_P }",
		'PE': "{ OP_PE }",
		'PO': "{ OP_PO }",
		'Z': "{ OP_Z }",

	# z280
		'####': "{ OP_IMM, CON_ANY }",
		'(HL+##)': "{ OP_MEM_HL, CON_ANY }",
		'(HL+IX)': "{ OP_MEM_HL_IX, CON_ANY }",
		'(HL+IY)': "{ OP_MEM_HL_IY, CON_ANY }",
		'(IX+IY)': "{ OP_MEM_IX_IY, CON_ANY }",
		'(IX+##)': "{ OP_MEM_IX, CON_ANY }",
		'(IY+##)': "{ OP_MEM_IY, CON_ANY }",
		'(SP+##)': "{ OP_MEM_SP, CON_ANY }",
		'(PC+##)': "{ OP_MEM_PC, CON_ANY }",
		'<##>': "{ OP_MEM_REL, CON_ANY }",
		'DEHL': "{ OP_DEHL }",
		'USP': "{ OP_USP }",

	# z380
		'##~': "{ OP_IMM, CON_FIT, FIT_U16PLUS }",
		'(##~)': "{ OP_MEM, CON_FIT, FIT_U16PLUS }",
		'(IX+#~)': "{ OP_MEM_IX, CON_FIT, FIT_S8PLUS }",
		'(IY+#~)': "{ OP_MEM_IY, CON_FIT, FIT_S8PLUS }",
		'(SP+#~)': "{ OP_MEM_SP, CON_FIT, FIT_S8PLUS }",

		'DSR': "{ OP_DSR }",
		'IXU': "{ OP_IXH }",
		'IYU': "{ OP_IYH }",
		'SR': "{ OP_SR }",
		'XSR': "{ OP_XSR }",
		'YSR': "{ OP_YSR }",

		'A\'': "{ OP_A2 }",
		'B\'': "{ OP_B2 }",
		'C\'': "{ OP_C2 }",
		'D\'': "{ OP_D2 }",
		'E\'': "{ OP_E2 }",
		'H\'': "{ OP_H2 }",
		'L\'': "{ OP_L2 }",

		'BC\'': "{ OP_BC2 }",
		'DE\'': "{ OP_DE2 }",
		'HL\'': "{ OP_HL2 }",
		'IX\'': "{ OP_IX2 }",
		'IY\'': "{ OP_IY2 }",

		'IB': "{ OP_IB }",
		'IW': "{ OP_IW }",
		'W': "{ OP_W }",
		'LW': "{ OP_LW }",

		'LCK': "{ OP_LCK }",
		'XM': "{ OP_XM }",

	# ez80
		'MB': "{ OP_MB }",
		'IX+#': "{ OP_IX, CON_FIT, FIT_S8 }",
		'IY+#': "{ OP_IY, CON_FIT, FIT_S8 }",

	# sm83
		'(0FF00h+#)': "{ OP_MEM, CON_HIGH, 0xFF }",
		'(0FF00h+C)': "{ OP_MEM_C, CON_EQU, 0xFF00 }",
		'(HLI)': "{ OP_HLI }",
		'(HL+)': "{ OP_HLI }",
		'(HLD)': "{ OP_HLD }",
		'(HL-)': "{ OP_HLD }",
		'SP+#': "{ OP_SP, CON_FIT, FIT_S8 }",

	# r800
		'[#]': "{ OP_MEM, CON_FIT, FIT_U8 }",
		'[##]': "{ OP_MEM, CON_ANY }",
		'[.BC]': "{ OP_MEM_BC }",
		'[.DE]': "{ OP_MEM_DE }",
		'[.HL]': "{ OP_MEM_HL }",
		'[.IX]': "{ OP_MEM_IX }",
		'[.IX+#]': "{ OP_MEM_IX, CON_FIT, FIT_S8 }",
		'[.IY]': "{ OP_MEM_IY }",
		'[.IY+#]': "{ OP_MEM_IY, CON_FIT, FIT_S8 }",
		'[.SP]': "{ OP_MEM_SP }",
		'[.C]': "{ OP_MEM_C }",
		'[.DE++]': "{ OP_DEI }",
		'[.HL++]': "{ OP_HLI }",
		'[.DE--]': "{ OP_DED }",
		'[.HL--]': "{ OP_HLD }",
		'.A': "{ OP_A }",
		'.B': "{ OP_B }",
		'.C': "{ OP_REG_C }",
		'.D': "{ OP_D }",
		'.E': "{ OP_E }",
		'.F': "{ OP_F }",
		'.H': "{ OP_H }",
		'.I': "{ OP_I }",
		'.L': "{ OP_L }",
		'.R': "{ OP_R }",
		'.IXH': "{ OP_IXH }",
		'.IXL': "{ OP_IXL }",
		'.IYH': "{ OP_IYH }",
		'.IYL': "{ OP_IYL }",
		'.AF': "{ OP_AF }",
		'.BC': "{ OP_BC }",
		'.DE': "{ OP_DE }",
		'.HL': "{ OP_HL }",
		'.SP': "{ OP_SP }",
		'.IX': "{ OP_IX }",
		'.IY': "{ OP_IY }",
		'.AF\'': "{ OP_AF2 }",

	# i80
		'PSW': "{ OP_PSW }",
	# vm1
		'H1': "{ OP_H2 }",
		'L1': "{ OP_L2 }",
		'M1': "{ OP_M2 }",
	}

	P_RELATIVE = {
		'PC+#': "{ OP_IMM, CON_REL, FIT_S8 }",

	# z380
		'PC+##': "{ OP_IMM, CON_REL, FIT_S16 }",
		'PC+###': "{ OP_IMM, CON_REL, FIT_S24 }",
	}

	ACTIONS = {
		'#': "ACT_PUT8",
		'##': "ACT_PUT16",
		'(#)': "ACT_PUT8",
		'(##)': "ACT_PUT16",
		'(IX+#)': "ACT_PUT8",
		'(IY+#)': "ACT_PUT8",

	# z280
		'####': "ACT_PUT32",
		'(HL+##)': "ACT_PUT16",
		'(IX+##)': "ACT_PUT16",
		'(IY+##)': "ACT_PUT16",
		'(SP+##)': "ACT_PUT16",
		'(PC+##)': "ACT_PUT16",
		'<##>': "ACT_REL16",

	# z380
		'##~': "ACT_PUT16PLUS",
		'(##~)': "ACT_PUT16PLUS",
		'(IX+#~)': "ACT_PUT8PLUS",
		'(IY+#~)': "ACT_PUT8PLUS",
		'(SP+#~)': "ACT_PUT8PLUS",

	# ez80
		'IX+#': "ACT_PUT8",
		'IY+#': "ACT_PUT8",

	# sm83
		'(0FF00h+#)': "ACT_PUT8",
		'SP+#': "ACT_PUT8",

	# r800
		'[#]': "ACT_PUT8",
		'[##]': "ACT_PUT16",
		'[.IX+#]': "ACT_PUT8",
		'[.IY+#]': "ACT_PUT8",
	}

	A_RELATIVE = {
		'PC+#': "ACT_REL8",
	# z380
		'PC+##': "ACT_REL16",
		'PC+###': "ACT_REL24",
	}

	for prefix in T['z380']:
		for opcode in range(256):
			if type(T['z380'][prefix][opcode]) is not Instruction:
				continue
			if T['z380'][prefix][opcode].forms[0].mnem != 'DDIR':
				continue
			T['z380'][prefix][opcode].forms[0].args[:] = (arg.strip() for arg in T['z380'][prefix][opcode].forms[0].args[0].split(','))

	def getpattern(arg, mnem):
		global PATTERNS
		if arg[0].isdigit():
			if arg.endswith('H'):
				arg = int(arg[:-1], 16)
			else:
				arg = int(arg)
			return f"{{ OP_IMM, CON_EQU, 0x{arg:02X} }}"
		elif mnem in {'DJNZ', 'CALR', 'JR', 'JAF', 'JAR', 'DBNZ', 'SHORT BR', 'SHORT BC', 'SHORT BNC', 'SHORT BZ', 'SHORT BNZ'} and arg.startswith('PC'):
			return P_RELATIVE[arg]
		else:
			return PATTERNS[arg]

	def getaction(arg, mnem):
		global PATTERNS
		if arg[0].isdigit():
			return None
		elif mnem in {'DJNZ', 'CALR', 'JR', 'JAF', 'JAR', 'DBNZ', 'SHORT BR', 'SHORT BC', 'SHORT BZ', 'SHORT BNC', 'SHORT BNZ'} and arg.startswith('PC'):
			return A_RELATIVE[arg]
		else:
			return ACTIONS.get(arg)

	def argorder(arg):
		if arg in PATTERN_ORDER:
			return (1, PATTERN_ORDER.index(arg))
		elif arg.endswith('H'):
			return (0, int(arg[:-1], 16))
		else:
			return (0, int(arg, 10))

	def createcase(args, cpu, prefix, opcode, file):
		print("\t{ { " + ", ".join(getpattern(arg, mnem) for arg in args) + " }, ", end = '', file = file)
		length = str((len(prefix) + 1) // 3 + 1 + sum(arg.count('#') for arg in args))
		if cpu == 'z380' and any('~' in arg for arg in args) or cpu == 'ez80' and any('##' in arg for arg in args):
			length += '|LEN_PLUS'
		print(length, end = '', file = file)
		encode = "".join("0x" + pref + ", " for pref in prefix.split(' ') if pref != '')
		ignore = None
		if prefix in {'DD CB', 'FD CB'}:
			for op in ['(IX+#)', '(IY+#)', '(IX+#~)', '(IY+#~)', '(SP+#~)', '[.IX+#]', '[.IY+#]']:
				if op in args:
					ignore = args.index(op)
					encode += getaction(op, mnem) + ", " + str(ignore) + ", "
					break
			else:
				assert False
		encode += f"0x{opcode:02X}"
		for index, arg in enumerate(args):
			if index == ignore:
				continue
			act = getaction(arg, mnem)
			if act is not None:
				encode += ", " + act + ", " + str(index)
		print(", { " + encode + " } },", file = file)
		if '(PC+##)' in args:
			assert args.count('(PC+##)') == 1
			createcase([arg if arg != '(PC+##)' else '<##>' for arg in args], cpu, prefix, opcode, file)

	with open(sys.argv[3] + '.gen.c', 'w') as file:
		print("/* this file is automatically generated */", file = file)
		for cpu in cpus:
			M = {}
			for prefix in T[cpu]:
				for opcode in range(256):
					if type(T[cpu][prefix][opcode]) is not Instruction:
						continue
					if T[cpu][prefix][opcode].style == 'duplicate':
						continue
					for ins in T[cpu][prefix][opcode].forms:
						if ins.mnem not in M:
							M[ins.mnem] = {}
						if len(ins.args) not in M[ins.mnem]:
							M[ins.mnem][len(ins.args)] = []
						M[ins.mnem][len(ins.args)].append((prefix, opcode, ins.args))
			for mnem in sorted(M.keys()):
				if mnem in {'SL1', 'SLI'} or mnem.startswith('.'):
					continue
				for count in sorted(M[mnem].keys()):
					print(f"static const struct pattern pattern_{cpu}_{mnem.replace(' ', '_').lower()}_{count}[] = {{", file = file)
					M[mnem][count] = sorted(M[mnem][count], key = lambda triplet: tuple(map(argorder, triplet[2])))
					for prefix, opcode, args in M[mnem][count]:
						createcase(args, cpu, prefix, opcode, file)
					print("};", file = file)
			print(f"static const struct patternset pattern_{cpu}[][4] = {{", file = file)
			for mnem in sorted(M.keys()):
				if mnem in {'SL1', 'SLI'} or mnem.startswith('.'):
					continue
				print(f"\t[MNEM_{mnem.replace(' ', ' + MNEM_')}] = {{", file = file)
				for count in sorted(M[mnem].keys()):
					print(f"\t\t[{count}] = {{ sizeof pattern_{cpu}_{mnem.replace(' ', '_').lower()}_{count} / sizeof pattern_{cpu}_{mnem.replace(' ', '_').lower()}_{count}[0], pattern_{cpu}_{mnem.replace(' ', '_').lower()}_{count} }},", file = file)
				print("\t},", file = file)
			print("};", file = file)

		print("const struct patternset (* patterns[])[4] =\n{", file = file)
		for cpu in cpus:
			print(f"\t[ASM_{cpu.replace(' ', '_').upper()}] = pattern_{cpu.replace(' ', '_')},", file = file)
			if cpu == 'ez80':
				print(f"\t[ASM_EZ80_ADL] = pattern_ez80,", file = file)
		print("};", file = file)

	opds = {}
	mnems = {}
	for cpu in cpus:
		for prefix in T[cpu]:
			for opcode in range(256):
				if type(T[cpu][prefix][opcode]) is not Instruction:
					continue
				for form in T[cpu][prefix][opcode].forms:
					if form.mnem[0] != '.' and ' ' not in form.mnem:
						if form.mnem not in mnems:
							mnems[form.mnem] = [cpu]
						elif cpu not in mnems[form.mnem]:
							mnems[form.mnem].append(cpu)
					for arg in form.args:
						if arg[0].isdigit():
							continue
						arg = arg.replace('#~', '#').replace('####', '#').replace('###', '#').replace('##', '#')
						if arg not in opds:
							opds[arg] = [cpu]
						elif cpu not in opds[arg]:
							opds[arg].append(cpu)

	if 'vm1' in cpus:
		mnems['SMF0'] = ['vm1']
		mnems['SMF1'] = ['vm1']

	with open(sys.argv[3] + '.gen.h', 'w') as file:
		print(r"""/* this file is automatically generated */

enum
{""", file = file)
		for mnem in sorted(mnems.keys()):
			if mnem in {'SL1', 'SLI'}:
				continue
			print('\tMNEM_' + mnem.replace(' ', '_') + ',', file = file)

		print("\tMNEM_SHORT,", file = file)
		print("\tMNEM_RS,", file = file)
		print("\tMNEM_MB = MNEM_RS * 2,", file = file)
		print("\tMNEM_CS = MNEM_RS * 3,", file = file)

		started = True
		for cpu in cpus:
			print('\tASM_' + cpu.upper() + (' = 0' if started else '') + ',', file = file)
			if cpu == 'ez80':
				print('\tASM_EZ80_ADL,', file = file)
			started = False

		print("""

/* org and data are negative values */
	DIR_ORG = -0xFF,
	DIR_EOF,

	DIR_D8 = -1,
	DIR_D16 = -2,
	DIR_D24 = -3,
	DIR_D32 = -4,

/* eZ80 */
	SFX_IS = 0x01,
	SFX_IL = 0x02,
	SFX_S  = 0x04,
	SFX_L  = 0x08,
	SFX_SIS = SFX_S | SFX_IS,
	SFX_SIL = SFX_S | SFX_IL,
	SFX_LIS = SFX_L | SFX_IS,
	SFX_LIL = SFX_L | SFX_IL,
/* Z380 */
	SFX_IB = 0x10,
	SFX_IW = 0x20,
	SFX_SPECIFIED = 0x80, /* when the ddir command is explicit */
};
""", file = file)

	with open(sys.argv[3] + '.lex', 'w') as file:
		print(r"""/* this file is automatically generated */
%{
#include "../out/parser.tab.h"
#include "../out/parser.gen.h"

#define YY_USER_ACTION \
	yylloc.first_line = yylloc.last_line; \
	yylloc.first_column = yylloc.last_column; \
	yylloc.last_line = yylineno; \
	if(strchr(yytext, '\n')) \
		yylloc.last_column = strlen(yytext) - (strrchr(yytext, '\n') - yytext); \
	else \
		yylloc.last_column += strlen(yytext);
%}

%option noyywrap
%option nodefault
%option yylineno
""", file = file)

		print("%s " + ' '.join(cpu.upper() for cpu in cpus), file = file)
		print(r"""%%

[ \t]+	;
;[^\n]*\n	;
""", file = file)

		for opd in sorted(opds.keys()):
			if len(opd) > 1 and not opd.isalnum() and not (opd[-1] == '\'' and opd[:-1].isalnum()) and not (opd[0] == '.' and opd[1:].isalnum()) and opd.upper() != ".AF'" or opd == '#':
				continue
			if opds[opd] == cpus:
				support = ''
			else:
				support = '<' + ','.join(cpu.upper() for cpu in opds[opd]) + '>'
			# replace .C by OP_REG_C so C can be distinguished from it
			print(support + '"' + opd.lower() + '"\treturn OP_' + opd.replace('IXU', 'IXH').replace('IYU', 'IYH').replace('.C', 'REG_C').replace('.', '').replace('\'', '2').replace('1', '2').replace('?', 'QUESTION') + ';', file = file)

		print(r"""

<SM83>"hld"	return OP_HLD;
<SM83>"hli"	return OP_HLI;

<EZ80>".l"	yylval.i = SFX_L; return SFX;
<EZ80>".s"	yylval.i = SFX_S; return SFX;
<EZ80>".il"	yylval.i = SFX_IL; return SFX;
<EZ80>".is"	yylval.i = SFX_IS; return SFX;
<EZ80>".lil"	yylval.i = SFX_LIL; return SFX;
<EZ80>".lis"	yylval.i = SFX_LIS; return SFX;
<EZ80>".sil"	yylval.i = SFX_SIL; return SFX;
<EZ80>".sis"	yylval.i = SFX_SIS; return SFX;

<R800>"short"	yylval.i = MNEM_SHORT; return PREF;
""", file = file)

		for mnem in sorted(mnems.keys()):
			if mnems[mnem] == cpus:
				support = ''
			else:
				support = '<' + ','.join(cpu.upper() for cpu in mnems[mnem]) + '>'
			# TODO: short bz
			print(support + '"' + mnem.lower() + '"\tyylval.i = MNEM_' + mnem.replace('SL1', 'SLL').replace('SLI', 'SLL').replace(' ', '_') + '; return MNEM;', file = file)

		print("<VM1>\"mb\"\tyylval.i = MNEM_MB;\t return PREF;", file = file)
		print("<VM1>\"cs\"\tyylval.i = MNEM_CS;\t return PREF;", file = file)
		print("<VM1>\"rs\"\tyylval.i = MNEM_RS;\t return PREF;", file = file)

		print(file = file)

		for cpu in cpus:
			print('".' + cpu + '"\tyylval.i = ASM_' + cpu.upper() + '; BEGIN(' + cpu.upper() + '); return MODE;', file = file)

		print(r"""
<EZ80>".adl"	return ADL;
".org"	return ORG;

"db"	yylval.i = 1; return DATA;
"dw"	yylval.i = 2; return DATA;
"dt"	yylval.i = 3; return DATA;
"dl"|"dd"	yylval.i = 4; return DATA;
"d8"	yylval.i = 1; return DATA;
"d16"	yylval.i = 2; return DATA;
"d24"	yylval.i = 3; return DATA;
"d32"	yylval.i = 4; return DATA;

[.A-Za-z_][A-Za-z_0-9']*	yylval.s = strdup(yytext); return IDENTIFIER;
0|[1-9][0-9]*	yylval.i = strtol(yytext, NULL, 10); return INTEGER;
0[Xx][0-9A-Fa-f]+	yylval.i = strtol(yytext + 2, NULL, 16); return INTEGER;
\$[0-9A-Fa-f]+	yylval.i = strtol(yytext + 1, NULL, 16); return INTEGER;
[0-9][0-9A-Fa-f]*[Hh]	yylval.i = strtol(yytext, NULL, 16); return INTEGER;
\"[^\"]*\"	yytext[strlen(yytext) - 1] = '\0'; yylval.s = strdup(yytext + 1); return STRING;
\'[^\']*\'	yytext[strlen(yytext) - 1] = '\0'; yylval.s = strdup(yytext + 1); return STRING;

"<<"	return LL;
">>"	return GG;
">>>"	return GGG;
"//"	return DD;
"%%"	return MM;

<R800>"++"	return INC;
<R800>"--"	return DEC;

.|\n	return yytext[0]; (void) input; (void) yyunput;

%%

void scanner_initialize(int cpu)
{
	YY_FLUSH_BUFFER;
	switch(cpu)
	{""", file = file)

		for cpu in cpus:
			print(f"\tcase ASM_{cpu.upper()}:\n\t\tBEGIN({cpu.upper()});\n\t\tbreak;", file = file)

		print(r"""	}
}
""", file = file)

	with open(sys.argv[3] + '.y', 'w') as file:
		print(r"""/* this file is automatically generated */

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../out/parser.gen.h"
#include "../src/assembler.h"
%}

%define parse.error verbose
%define parse.lac full
%locations

%union
{
	char * s;
	int i;
	struct expression * e;
	struct operand * o;
	struct instruction * n;
}
""", file = file)

		for opd in sorted(opds.keys()):
			if len(opd) > 1 and not opd.isalnum() and not (opd[-1] == '\'' and opd[:-1].isalnum()) or opd == '#':
				continue
			if opd in {'IXU', 'IYU'}:
				continue
			print('%token OP_' + opd.replace('.C', 'REG_C').replace('.', '').replace('\'', '2').replace('?', 'QUESTION'), file = file)

		print(r"""
%token OP_REG_C
%token OP_HLD
%token OP_HLI
%token OP_DED
%token OP_DEI
%token OP_M2
%token OP_PC

/* these are only generated by the parser */

%token OP_IMM
%token OP_MEM
%token OP_MEM_BC
%token OP_MEM_C
%token OP_MEM_DE
%token OP_MEM_HL
%token OP_MEM_IX
%token OP_MEM_IY
%token OP_MEM_HL_IX
%token OP_MEM_HL_IY
%token OP_MEM_IX_IY
%token OP_MEM_SP
%token OP_MEM_PC
%token OP_MEM_REL

%token <i> SFX
%token <i> MNEM
%token <i> PREF

%token <i> DATA MODE
%token ADL ORG SHORT

%token <s> IDENTIFIER STRING
%token <i> INTEGER

%token DD "//" MM "%%" LL "<<" GG ">>" GGG ">>>" INC "++" DEC "--"

%type <e> primary primary_noparen
%type <e> unary unary_noparen
%type <e> factor factor_noparen
%type <e> term term_noparen
%type <e> displacement displacement_opt sum sum_noparen
%type <e> conjunction conjunction_noparen
%type <e> expression expression_noparen

%type <o> operand

%type <n> mnemonic mnemonic_operands instruction data

%%

program
	:
	| program line
	;

line
	: '\n'
	| instruction '\n'
		{
			instruction_stream_append($1);
		}
	| IDENTIFIER ':' '\n'
		{
			define_label($1);
		}
	| IDENTIFIER ':'
		{
			define_label($1);
		}
		instruction '\n'
		{
			instruction_stream_append($4);
		}
	| MODE '\n'
		{
			current_cpu = $1;
			last_ddir = 0;
			enable_adl = 0;
		}
	| data '\n'
		{
			instruction_stream_append($1);
		}
	| IDENTIFIER '=' expression '\n'
		{
			define_constant($1, $3);
		}
	| ADL '=' INTEGER '\n'
		{
			enable_adl = $3;
		}
	| ORG expression '\n'
		{
			struct instruction * tmp = instruction_create(DIR_ORG, 0);
			instruction_add_operand(tmp, operand_make(OP_IMM, $2));
			instruction_stream_append(tmp);
		}
	;

data
	: DATA expression
		{
			$$ = instruction_create(-$1, 0);
			instruction_add_operand($$, operand_make(OP_IMM, $2));
		}
	| data ',' expression
		{
			$$ = $1;
			instruction_add_operand($$, operand_make(OP_IMM, $3));
		}
	;

instruction
	: mnemonic
	| mnemonic_operands
	;

mnemonic
	: MNEM
		{
			$$ = instruction_create($1, 0);
		}
	| MNEM SFX
		{
			$$ = instruction_create($1, $2);
		}
	| PREF mnemonic
		{
			$$ = $2;
			$$->mnem += $1;
			/* TODO: make sure doesn't result in invalid mnemonic */
		}
	;

mnemonic_operands
	: mnemonic operand
		{
			$$ = $1;
			instruction_add_operand($$, $2);
		}
	| mnemonic_operands ',' operand
		{
			$$ = $1;
			instruction_add_operand($$, $3);
		}
	;

operand
	: expression_noparen
		{
			$$ = operand_make(OP_IMM, $1);
		}
	| '(' expression ')'
		{
			if(IS_ZILOG(current_cpu))
			{
				$$ = operand_make(OP_MEM, $2);
			}
			else
			{
				$$ = operand_make(OP_IMM, $2);
			}
		}
	| '(' OP_BC ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (bc)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_BC, NULL);
		}
	| '(' OP_DE ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (de)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_DE, NULL);
		}
	| '(' OP_C displacement_opt ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (c)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_C, $3);
		}
	| '(' OP_HL ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (hl)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_HL, NULL);
		}
	| '(' OP_HLI ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (hli)");
				YYERROR;
			}
			$$ = operand_make(OP_HLI, NULL);
		}
	| '(' OP_HL '+' ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (hl+)");
				YYERROR;
			}
			$$ = operand_make(OP_HLI, NULL);
		}
	| '(' OP_HLD ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (hld)");
				YYERROR;
			}
			$$ = operand_make(OP_HLD, NULL);
		}
	| '(' OP_HL '-' ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (hl-)");
				YYERROR;
			}
			$$ = operand_make(OP_HLD, NULL);
		}
	| '(' OP_HL displacement ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (hl+...)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_HL, $3);
		}
	| '(' OP_IX displacement_opt ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (ix+...)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_IX, $3);
		}
	| '(' OP_IY displacement_opt ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (iy+...)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_IY, $3);
		}
	| '(' OP_SP displacement_opt ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (sp+...)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_SP, $3);
		}
	| '(' OP_PC displacement_opt ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (pc+...)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_PC, $3);
		}
	| '(' OP_HL '+' OP_IX ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (hl+ix)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_HL_IX, NULL);
		}
	| '(' OP_HL '+' OP_IY ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (hl+iy)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_HL_IY, NULL);
		}
	| '(' OP_IX '+' OP_IY ')'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: (ix+iy)");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_IX_IY, NULL);
		}
	| '<' expression '>'
		{
			if(!IS_ZILOG(current_cpu))
			{
				yyerror("Unsupported operand: <...>");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_REL, $2);
		}
	| '[' expression ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [...]");
				YYERROR;
			}
			$$ = operand_make(OP_MEM, $2);
		}
	| '[' OP_BC ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [bc]");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_BC, NULL);
		}
	| '[' OP_DE ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [de]");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_DE, NULL);
		}
	| '[' OP_REG_C displacement_opt ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [c+...]");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_C, $3);
		}
	| '[' OP_HL ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [hl]");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_HL, NULL);
		}
	| '[' OP_DE "++" ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [de++]");
				YYERROR;
			}
			$$ = operand_make(OP_DEI, NULL);
		}
	| '[' OP_DE "--" ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [de--]");
				YYERROR;
			}
			$$ = operand_make(OP_DED, NULL);
		}
	| '[' OP_HL "++" ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [hl++]");
				YYERROR;
			}
			$$ = operand_make(OP_HLI, NULL);
		}
	| '[' OP_HL "--" ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [hl--]");
				YYERROR;
			}
			$$ = operand_make(OP_HLD, NULL);
		}
	| '[' OP_IX displacement_opt ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [ix+...])");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_IX, $3);
		}
	| '[' OP_IY displacement_opt ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [iy+...]");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_IY, $3);
		}
	| '[' OP_SP ']'
		{
			if(current_cpu != ASM_R800)
			{
				yyerror("Unsupported operand: [sp]");
				YYERROR;
			}
			$$ = operand_make(OP_MEM_SP, NULL);
		}
	| OP_A
		{
			$$ = operand_make(OP_A, NULL);
		}
	| OP_A2
		{
			$$ = operand_make(OP_A2, NULL);
		}
	| OP_AF
		{
			$$ = operand_make(OP_AF, NULL);
		}
	| OP_AF2
		{
			$$ = operand_make(OP_AF2, NULL);
		}
	| OP_B
		{
			$$ = operand_make(OP_B, NULL);
		}
	| OP_B2
		{
			$$ = operand_make(OP_B2, NULL);
		}
	| OP_BC
		{
			$$ = operand_make(OP_BC, NULL);
		}
	| OP_BC2
		{
			$$ = operand_make(OP_BC2, NULL);
		}
	| OP_C
		{
			$$ = operand_make(OP_C, NULL);
		}
	| OP_REG_C
		{
			$$ = operand_make(OP_REG_C, NULL);
		}
	| OP_C2
		{
			$$ = operand_make(OP_C2, NULL);
		}
	| OP_D
		{
			$$ = operand_make(OP_D, NULL);
		}
	| OP_D2
		{
			$$ = operand_make(OP_D2, NULL);
		}
	| OP_DE
		{
			$$ = operand_make(OP_DE, NULL);
		}
	| OP_DE2
		{
			$$ = operand_make(OP_DE2, NULL);
		}
	| OP_DEHL
		{
			$$ = operand_make(OP_DEHL, NULL);
		}
	| OP_DSR
		{
			$$ = operand_make(OP_DSR, NULL);
		}
	| OP_E
		{
			$$ = operand_make(OP_E, NULL);
		}
	| OP_E2
		{
			$$ = operand_make(OP_E2, NULL);
		}
	| OP_F
		{
			$$ = operand_make(OP_F, NULL);
		}
	| OP_H
		{
			$$ = operand_make(OP_H, NULL);
		}
	| OP_H2
		{
			$$ = operand_make(OP_H2, NULL);
		}
	| OP_HL
		{
			$$ = operand_make(OP_HL, NULL);
		}
	| OP_HL2
		{
			$$ = operand_make(OP_HL2, NULL);
		}
	| OP_I
		{
			$$ = operand_make(OP_I, NULL);
		}
	| OP_IB
		{
			$$ = operand_make(OP_IB, NULL);
		}
	| OP_IW
		{
			$$ = operand_make(OP_IW, NULL);
		}
	| OP_IX displacement_opt
		{
			$$ = operand_make(OP_IX, $2);
		}
	| OP_IX2
		{
			$$ = operand_make(OP_IX2, NULL);
		}
	| OP_IXH
		{
			$$ = operand_make(OP_IXH, NULL);
		}
	| OP_IXL
		{
			$$ = operand_make(OP_IXL, NULL);
		}
	| OP_IY displacement_opt
		{
			$$ = operand_make(OP_IY, $2);
		}
	| OP_IY2
		{
			$$ = operand_make(OP_IY2, NULL);
		}
	| OP_IYH
		{
			$$ = operand_make(OP_IYH, NULL);
		}
	| OP_IYL
		{
			$$ = operand_make(OP_IYL, NULL);
		}
	| OP_L
		{
			$$ = operand_make(OP_L, NULL);
		}
	| OP_L2
		{
			$$ = operand_make(OP_L2, NULL);
		}
	| OP_LCK
		{
			$$ = operand_make(OP_LCK, NULL);
		}
	| OP_LW
		{
			$$ = operand_make(OP_LW, NULL);
		}
	| OP_M
		{
			$$ = operand_make(OP_M, NULL);
		}
	| OP_M2
		{
			$$ = operand_make(OP_M2, NULL);
		}
	| OP_MB
		{
			$$ = operand_make(OP_MB, NULL);
		}
	| OP_NC
		{
			$$ = operand_make(OP_NC, NULL);
		}
	| OP_NZ
		{
			$$ = operand_make(OP_NZ, NULL);
		}
	| OP_P
		{
			$$ = operand_make(OP_P, NULL);
		}
	| OP_PE
		{
			$$ = operand_make(OP_PE, NULL);
		}
	| OP_PO
		{
			$$ = operand_make(OP_PO, NULL);
		}
	| OP_PSW
		{
			$$ = operand_make(OP_PSW, NULL);
		}
	| OP_QUESTION
		{
			$$ = operand_make(OP_QUESTION, NULL);
		}
	| OP_R
		{
			$$ = operand_make(OP_R, NULL);
		}
	| OP_SP displacement_opt
		{
			$$ = operand_make(OP_SP, $2);
		}
	| OP_SR
		{
			$$ = operand_make(OP_SR, NULL);
		}
	| OP_USP
		{
			$$ = operand_make(OP_USP, NULL);
		}
	| OP_W
		{
			$$ = operand_make(OP_W, NULL);
		}
	| OP_XM
		{
			$$ = operand_make(OP_XM, NULL);
		}
	| OP_XSR
		{
			$$ = operand_make(OP_XSR, NULL);
		}
	| OP_YSR
		{
			$$ = operand_make(OP_YSR, NULL);
		}
	| OP_Z
		{
			$$ = operand_make(OP_Z, NULL);
		}
	;

primary_noparen
	: IDENTIFIER
		{
			$$ = expression_identifier($1);
		}
	| STRING
		{
			$$ = expression_string($1);
		}
	| INTEGER
		{
			$$ = expression_integer($1);
		}
	| '$'
		{
			$$ = expression_identifier(strdup("$"));
		}
	;

primary
	: primary_noparen
	| '(' expression ')'
		{
			$$ = $2;
		}
	;

unary_noparen
	: primary_noparen
	| '+' unary
		{
			$$ = $2;
		}
	| '-' unary
		{
			$$ = expression_unary('_', $2);
		}
	| '~' unary
		{
			$$ = expression_unary('~', $2);
		}
	;

unary
	: unary_noparen
	| '(' expression ')'
		{
			$$ = $2;
		}
	;

factor_noparen
	: unary_noparen
	| factor "<<" primary
		{
			$$ = expression_binary(LL, $1, $3);
		}
	| factor ">>" primary
		{
			$$ = expression_binary(GG, $1, $3);
		}
	| factor ">>>" primary
		{
			$$ = expression_binary(GGG, $1, $3);
		}
	;

factor
	: factor_noparen
	| '(' expression ')'
		{
			$$ = $2;
		}
	;

term_noparen
	: factor_noparen
	| term '*' factor
		{
			$$ = expression_binary('*', $1, $3);
		}
	| term '/' factor
		{
			$$ = expression_binary('/', $1, $3);
		}
	| term "//" factor
		{
			$$ = expression_binary(DD, $1, $3);
		}
	| term '%' factor
		{
			$$ = expression_binary('%', $1, $3);
		}
	| term "%%" factor
		{
			$$ = expression_binary(MM, $1, $3);
		}
	;

term
	: term_noparen
	| '(' expression ')'
		{
			$$ = $2;
		}
	;

displacement
	: '+' term
		{
			$$ = $2;
		}
	| '-' term
		{
			$$ = expression_unary('_', $2);
		}
	| displacement '+' term
		{
			$$ = expression_binary('+', $1, $3);
		}
	| displacement '-' term
		{
			$$ = expression_binary('-', $1, $3);
		}
	;

displacement_opt
	:
		{
			$$ = NULL;
		}
	| displacement
	;

sum_noparen
	: term_noparen
	| sum '+' term
		{
			$$ = expression_binary('+', $1, $3);
		}
	| sum '-' term
		{
			$$ = expression_binary('-', $1, $3);
		}
	;

sum
	: sum_noparen
	| '(' expression ')'
		{
			$$ = $2;
		}
	;

conjunction_noparen
	: sum_noparen
	| conjunction '&' sum
		{
			$$ = expression_binary('&', $1, $3);
		}
	;

conjunction
	: conjunction_noparen
	| '(' expression ')'
		{
			$$ = $2;
		}
	;

expression_noparen
	: conjunction_noparen
	| expression '|' conjunction
		{
			$$ = expression_binary('|', $1, $3);
		}
	| expression '^' conjunction
		{
			$$ = expression_binary('^', $1, $3);
		}
	;

expression
	: expression_noparen
	| '(' expression ')'
		{
			$$ = $2;
		}
	;

%%

void yyerror(const char * s)
{
	fprintf(stderr, "Error in line %d at %d: %s\n", yylloc.first_line, yylloc.first_column, s);

}

""", file = file)

LABNUM = 0

def generate_asm_tests():
	if len(sys.argv) > 4:
		cpu = sys.argv[4].lower()
	else:
		ix = sys.argv[3].rfind('-')
		cpu = sys.argv[3][ix + 1:]

	def make_tests(cpu, tables, textfile, binfile, prefix = ''):
		global LABNUM
		for opcode, instruction in enumerate(tables[prefix]):
			if type(instruction) is Instruction:
				if instruction.style == 'duplicate':
					continue # only print the first one
				for immsize in range(3):
					hasimm = False
					for form in instruction.forms:
						if form.mnem.startswith('.'):
							# ez80 .sis etc. prefixes
							continue
						immdata = 0x12
						binprefix = b''
						textprefix = []
						binform = bytes(int(text, 16) for text in prefix.split(' ')) if prefix != '' else b''
						binform += bytes([opcode])
						args = []
						labels = []
						opsuffix = ''
						for arg in form.args:
							arg = arg.lower()
							if arg == 'pc+#':
								arg = f'lab{LABNUM}'
								binform += bytes([0])
								LABNUM += 1
								labels.append(arg + ':')
							elif arg == 'pc+##':
								arg = f'lab{LABNUM}'
								binform += bytes([0x80, 0])
								LABNUM += 1
								labels.append(arg + ':')
								arg += " + 0x80"
							elif arg == 'pc+###':
								arg = f'lab{LABNUM}'
								binform += bytes([0, 0x80, 0])
								LABNUM += 1
								labels.append(arg + ':')
								arg += " + 0x8000"
							if arg == '(pc+##)':
								label = f'lab{LABNUM}'
								arg = f'<{label}>'
								binform += bytes([0, 0])
								LABNUM += 1
								labels.append(label + ':')
							if '####' in arg:
								assert form.mnem in {'EPUM', 'MEPU', 'EPUI', 'EPUF'}
								arg = '0'
								binform += b'\0' * 4
							if '##~' in arg:
								hasimm = True
								if immsize == 0:
									binform += bytes([immdata + 0x22, immdata])
									immval = immdata * 0x0101 + 0x22
									immdata += 0x44
									arg = arg.replace('##~', f'0x{immval:04X}')
								elif immsize == 1:
									# ddir ib
									#textprefix.append('ddir\tib')
									binprefix = b'\xDD\xC3'
									binform += bytes([immdata + 0x44, immdata + 0x22, immdata])
									immval = immdata * 0x010101 + 0x2244
									immdata += 0x66
									arg = arg.replace('##~', f'0x{immval:06X}')
								elif immsize == 2:
									# ddir iw
									#textprefix.append('ddir\tiw')
									binprefix = b'\xFD\xC3'
									binform += bytes([immdata + 0x66, immdata + 0x44, immdata + 0x22, immdata])
									immval = immdata * 0x01010101 + 0x224466
									immdata += 0x88
									arg = arg.replace('##~', f'0x{immval:08X}')
							if '#~' in arg:
								hasimm = True
								if immsize == 0:
									binform += bytes([immdata])
									immval = immdata
									immdata += 0x22
									arg = arg.replace('#~', f'0x{immval:02X}')
								elif immsize == 1:
									# ddir ib
									#textprefix.append('ddir\tib')
									binprefix = b'\xDD\xC3'
									binform += bytes([immdata + 0x22, immdata])
									immval = immdata * 0x0101 + 0x22
									immdata += 0x44
									arg = arg.replace('#~', f'0x{immval:04X}')
								elif immsize == 2:
									# ddir iw
									#textprefix.append('ddir\tiw')
									binprefix = b'\xFD\xC3'
									binform += bytes([immdata + 0x44, immdata + 0x22, immdata])
									immval = immdata * 0x010101 + 0x2244
									immdata += 0x66
									arg = arg.replace('#~', f'0x{immval:06X}')
							if '##' in arg:
								if cpu == 'ez80' or cpu == 'ez80_adl':
									hasimm = True
									if immsize == 0:
										if cpu == 'ez80':
											immval = (immdata * 0x0101) + 0x22
											arg = arg.replace('##', f'0x{immval:04X}')
											binform += bytes([immdata + 0x22, immdata])
											immdata += 0x44
										else:
											immval = (immdata * 0x010101) + 0x2244
											arg = arg.replace('##', f'0x{immval:04X}')
											binform += bytes([immdata + 0x44, immdata + 0x22, immdata])
											immdata += 0x66
									elif immsize == 1:
										immval = (immdata * 0x0101) + 0x22
										arg = arg.replace('##', f'0x{immval:04X}')
										binprefix = b'\x40'
										binform += bytes([immdata + 0x22, immdata])
										immdata += 0x44
										opsuffix = '.sis'
									elif immsize == 2:
										immval = (immdata * 0x010101) + 0x2244
										arg = arg.replace('##', f'0x{immval:04X}')
										binprefix = b'\x5B'
										binform += bytes([immdata + 0x44, immdata + 0x22, immdata])
										immdata += 0x66
										opsuffix = '.lil'
								else:
									immval = (immdata * 0x0101) + 0x22
									arg = arg.replace('##', f'0x{immval:04X}')
									binform += bytes([immdata + 0x22, immdata])
									immdata += 0x44
							if '0ff00h+#' in arg:
								arg = arg.replace('0ff00h+#', f'0x{0xFF00+immdata:04X}')
								binform += bytes([immdata])
								immdata += 0x22
							if '#' in arg:
								arg = arg.replace('#', f'0x{immdata:02X}')
								binform += bytes([immdata])
								immdata += 0x22
							if '0ff00h+c' in arg:
								arg = arg.replace('0ff00h+c', f'c+0xFF00')
							args.append(arg)
						if prefix in ['DD CB', 'FD CB']:
							# CB prefix is clunky
							binform = binform[:2] + binform[3:] + binform[2:3]
						binform = binprefix + binform
						print('; ' + ' '.join(f'{current_byte:02X}' for current_byte in binform), file = textfile)
						for line in textprefix:
							print('\t' + line, file = textfile)
						print('\t' + form.mnem.lower() + opsuffix + '\t' + ', '.join(args), file = textfile)
						for label in labels:
							print(label, file = textfile)
						binfile.write(binform)
					if not hasimm or cpu not in {'z380', 'ez80', 'ez80_adl'}:
						break
			elif instruction is None:
				# this might be a subtable
				newprefix = (prefix + ' ' + f'{opcode:02X}').lstrip()
				if newprefix in tables:
					make_tests(cpu, tables, textfile, binfile, newprefix)
			elif type(instruction) is str:
				# this is a longer version of another subtable, ignore it
				continue
			else:
				print('\t; ' + str(type(instruction)), file = textfile)

	with open(sys.argv[3] + '.asm', 'w') as textfile:
		with open(sys.argv[3] + '.bin', 'wb') as binfile:
			print(f'\t; This is an automatically generated test file for the {cpu}', file = textfile)
			print(f'\t.{cpu}', file = textfile)
			make_tests(cpu, T[cpu], textfile, binfile)
			if cpu == 'ez80':
				print(f'\t.adl=1', file = textfile)
				make_tests(cpu + '_adl', T[cpu], textfile, binfile)

