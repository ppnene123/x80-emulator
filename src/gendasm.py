#! /usr/bin/python3

import os
import sys
from database import *

def generate_disasm():
	asm = sys.modules[__name__] # TODO: better structure
	for cpu in asm.CPUS:
		ARGS = set()
		print(f"void context_parse_{cpu}(parse_context_t * context)")
		print("""{
	const char * format;
	uint8_t op;""")
		print("\top = context_fetch(context);")
		has_undefined = False
		for prefix in asm.T[cpu]:
			for op, ins in enumerate(asm.T[cpu][prefix]):
				if ins is None:
					newprefix = (prefix + f" {op:02X}").strip()
					if newprefix not in asm.T[cpu]:
						has_undefined = True
						break
				elif type(ins) is asm.Instruction and ins.style is not None \
					and ins.style == 'duplicate' \
						and prefix != '' and asm.T[cpu][''][op] is not None and ins.forms[0] is asm.T[cpu][''][op].forms[0]:
							has_undefined = True
							break
				elif type(ins) is str and ins == f"{prefix} {op:02X}".strip()[3:]:
					has_undefined = True
					break
			if has_undefined:
				break
		has_restart = has_undefined
		if has_restart:
			print("restart:")
		for prefix in asm.T[cpu]:
			if prefix != '':
				print(f"parse_{prefix.replace(' ', '_')}:")
				if prefix.endswith(' CB'):
					print(f"\tcontext->imm[0] = context_fetch_word(context, {1 if cpu != 'z380' else 'context->dspsize'});")
				print("\top = context_fetch(context);")
			print("\tswitch(op)")
			print("\t{")
			has_current_undefined = False
			revert_to_nop = False
			for op, ins in enumerate(asm.T[cpu][prefix]):
				if ins is None:
					newprefix = (prefix + f" {op:02X}").strip()
					if newprefix not in asm.T[cpu]:
						has_current_undefined = True
					else:
						print(f"\tcase 0x{op:02X}:")
						print(f"\t\tgoto parse_{newprefix.replace(' ', '_')};")
					continue
				if type(ins) is asm.Instruction and ins.style is not None:
					if ins.style == 'duplicate':
						if prefix != '' and asm.T[cpu][''][op] is not None and ins.forms[0] is asm.T[cpu][''][op].forms[0]:
							has_current_undefined = True
							continue
						elif prefix == 'ED' and ins.forms[0].mnem.lower() == 'nop':
							revert_to_nop = True
							continue
					print(f"\t\t/* {ins.style} */")
				if type(ins) is str and ins == f"{prefix} {op:02X}".strip()[3:]:
					has_current_undefined = True
					continue
				print(f"\tcase 0x{op:02X}:")
				if type(ins) is str:
					print(f"\t\tgoto parse_{ins.replace(' ', '_')};")
				else:
					form = ins.forms[0]
					for arg in form.args:
						if '#' in arg:
							ARGS.add(arg.lower())
					args0 = ', '.join(form.args).lower()
					args = ''
					immcount = 0
					i = 0
					while i < len(args0):
						immsize = None
						D = "context->dspsize"
						I = "context->immsize"
						for intext, immsize, fmt in [
								('pc+###', 3, '%3R'),
								('pc+##',  2, '%2R'),
								('pc+#',   1, '%1R'),
								('+#~',    D, '%dD'),
								('+##',    2, '%2D'),
								('+#',     1, '%1D'),
								('##~',    I, '%iU'),
								('####',   4, '%4U'),
								('##',     2, '%2U'),
								('#',      1, '%1U')
							]:
							if cpu == 'ez80' and intext == '##':
								immsize = I
								fmt = '%iU'
							if args0[i:].startswith(intext):
								if not (prefix.endswith(' CB') and immcount == 0):
									print(f"\t\tcontext->imm[{immcount}] = context_fetch_word(context, {immsize});")
								args += fmt
								i += len(intext)
								immcount += 1
								break
						else:
							args += args0[i]
							i += 1
					if '#' in args:
						print(args, file = sys.stderr); exit(1)
					if args != "":
						args = "\\t" + args
					if cpu == 'ez80' and form.mnem.startswith('.'):
						print("\t\tif(context->suffix != NULL)")
						print("\t\t{")
						print("\t\t\tcontext_spill(context, stdout, 1);")
						print("\t\t}")
						print(f"\t\tcontext->suffix = \"{form.mnem.lower()}\";")
						if form.mnem.lower().endswith('is'):
							print("\t\tcontext->immsize = 2;")
						else:
							print("\t\tcontext->immsize = 3;")
						print(f"\t\top = context_fetch(context);")
						assert has_restart
						print(f"\t\tgoto restart;")
					else:
						print(f"\t\tformat = \"{form.mnem.lower()}{args}\";")
						if cpu == 'z380' and form.mnem.lower() == 'ddir':
							if 'IB' in form.args[0]:
								print("\t\tcontext->immsize = 3;")
								print("\t\tcontext->dspsize = 2;")
							elif 'IW' in form.args[0]:
								print("\t\tcontext->immsize = 4;")
								print("\t\tcontext->dspsize = 3;")
							else:
								print("\t\tcontext->immsize = 2;")
								print("\t\tcontext->dspsize = 1;")
							print(f"\t\tgoto print_ddir;")
						else:
							print(f"\t\tbreak;")
			if revert_to_nop:
				print("""\tdefault:
	\t\tformat = \"nop\";
	\t\tbreak;""")
			elif has_current_undefined:
				print("\tdefault:")
				if cpu == 'ez80':
					print("\t\t\tcontext_spill(context, stdout, is_ez80_prefix(context) ? 2 : 1);")
					print("\t\t\tcontext->suffix = NULL;")
				else:
					print("\t\tcontext_spill(context, stdout, 1);")
				assert has_restart
				print("\t\tgoto restart;")
			print("\t}")
			print("\tgoto print;")
		if cpu == 'z380':
			print("""print_ddir:
	context_format(context, stdout, format);
	context_empty(context);
	return;""")
		print("""print:
	context_format(context, stdout, format);
	context_empty(context);""")

		if cpu in {'z380', 'ez80'}:
			print("""	context->dspsize = 1;
	context->immsize = context->defaultimmsize;""")
		if cpu == 'ez80':
			print("context->suffix = NULL;")
		print("}")

		print(cpu, sorted(ARGS), file = sys.stderr)

	print("enum\n{")
	for cpu in asm.CPUS:
		print(f"\tDASM_{cpu.upper()},")
		if cpu == 'ez80':
			print(f"\tDASM_{cpu.upper()}_ADL,")
	print("};")

	print("int get_default_immsize(int cpu)\n{")
	if 'ez80' in asm.CPUS:
		print(f"\tif(cpu == DASM_EZ80_ADL)\n\t{{\n\t\treturn 3;\n\t}}")
	print("\treturn 2;\n}")

	print("int get_cpu_address_width(int cpu)\n{")
	print("\tswitch(cpu)\n\t{")
	if 'z380' in asm.CPUS:
		print("\tcase DASM_Z380:\n\t\treturn 4;")
	if 'ez80' in asm.CPUS:
		print("\tcase DASM_EZ80_ADL:\n\t\treturn 3;")
	# TODO: Rabbit CPUs
	print("\tdefault:\n\t\treturn 2;")
	print("\t}\n}")

	print("int get_cpu(const char * name)\n{\n")
	started = ''
	for cpu in asm.CPUS:
		print(f"\t{started}if(strcasecmp(name, \"{cpu}\") == 0)\n\t{{\n\t\treturn DASM_{cpu.upper()};\n\t}}")
		started = 'else '
		if cpu == 'ez80':
			print(f"\telse if(strcasecmp(name, \"ez80_adl\") == 0)\n\t{{\n\t\treturn DASM_{cpu.upper()}_ADL;\n\t}}")
	print("\treturn 0;\n}")

	print("void context_parse(parse_context_t * context)\n{\n\tswitch(context->cpu)\n\t{")
	for cpu in asm.CPUS:
		print(f"\tcase DASM_{cpu.upper()}:")
		if cpu == 'ez80':
			print(f"\tcase DASM_{cpu.upper()}_ADL:")
		print(f"\t\tcontext_parse_{cpu}(context);\n\t\tbreak;")
	print("\t}\n}")

