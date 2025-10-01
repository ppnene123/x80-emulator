#! /usr/bin/python3

import os
import sys
from database import *

SEPARATE_CPUS = True

# vm1 specific
ALTMEMBANK = ''
ALTHLPAIR = None

def get_cpu_macro(cpu):
	return {
		'dp22': 'dp',
	}.get(cpu, cpu).upper()

def generate_emulator():
	global T, cpus, prefixes, unimps
	global IREAD, DREAD, DREADDONE
	global ALTMEMBANK, ALTHLPAIR
	cpus = ['z80', 'z180', 'z280', 'z380', 'ez80', 'i80', 'i85', 'vm1', 'sm83', 'r800', 'dp22'] #, 'dp22v2', 'i8']
	nocpus = set(T.keys())
	nocpus.difference_update(cpus)
	for key in nocpus:
		del T[key]
	prefixes = set()
	for cpu in cpus:
		prefixes.update(T[cpu].keys())

	ACCARGS = {
		'A': 'HI(cpu->af',
		'B': 'HI(cpu->bc',
		'C': 'LO(cpu->bc',
		'D': 'HI(cpu->de',
		'E': 'LO(cpu->de',
		'H': 'HI(cpu->hl',
		'L': 'LO(cpu->hl',
		'IXH': 'HI(cpu->ix',
		'IXL': 'LO(cpu->ix',
		'IYH': 'HI(cpu->iy',
		'IYL': 'LO(cpu->iy',
		'I': 'HI(cpu->ir',
		'R': 'LO(cpu->ir',
	}

	MEMARGS = {
		'(HL)': 'cpu->hl',
		'(IX+#)': 'ADDADDR(cpu->ix, d)',
		'(IX+#~)': 'ADDADDR(cpu->ix, d)',
		'(IY+#)': 'ADDADDR(cpu->iy, d)',
		'(IY+#~)': 'ADDADDR(cpu->iy, d)',
		'(##)': 'd',
		'(##~)': 'd',
		'(HL+##)': 'ADDADDR(cpu->ix, d)',
		'(IX+##)': 'ADDADDR(cpu->ix, d)',
		'(IY+##)': 'ADDADDR(cpu->iy, d)',
		'(SP+##)': 'ADDADDR(cpu->sp, d)',
		'(SP+#~)': 'ADDADDR(cpu->sp, d)',
		'(PC+##)': 'ADDADDR(cpu->pc, d)',
		'(HL+IX)': 'ADDADDR(cpu->hl, cpu->ix)',
		'(HL+IY)': 'ADDADDR(cpu->hl, cpu->iy)',
		'(IX+IY)': 'ADDADDR(cpu->ix, cpu->iy)',
		'(SP)': 'cpu->sp',
		'(BC)': 'cpu->bc',
		'(DE)': 'cpu->de',
	}

	VARARGS = {
		'0': '0',
		'#': 'i',
		'##': 'i',
		'##~': 'i',
		'AF': 'cpu->af',
		'SR': 'cpu->z380.sr',
		'BC': 'cpu->bc',
		'DE': 'cpu->de',
		'HL': 'cpu->hl',
		'IX': 'cpu->ix',
		'IY': 'cpu->iy',
		'SP': 'cpu->sp',
		'PC+#': '(cpu->pc + d)',
		'PC+##': '(cpu->pc + d)',
		'PC+###': '(cpu->pc + d)',
		'MB': 'cpu->ez80.mbase',
	}
	IREAD = False
	DREAD = False
	DREADDONE = False # checks if the displacement has already been read (for DD/FD prefixed bitwise operations)
	def preparearg(name):
		global IREAD, DREAD, DREADDONE
		if name == '#':
			assert IREAD is False
			IREAD = True
			return 'i = (int8_t)fetchbyte(cpu)', 'i'
		elif name == '##':
			assert IREAD is False
			IREAD = True
			return 'i = (int16_t)fetchword(cpu, 2)', 'i'
		elif name == '##~':
			assert IREAD is False
			IREAD = True
			#return 'i = extendsign(IMMSIZE, fetchword(cpu, IMMSIZE))', 'i'
			return 'i = fetchword(cpu, IMMSIZE)', 'i' # TODO: double check
		elif name == '(#)':
			assert DREAD is False
			DREAD = True
			if DREADDONE:
				return
			return 'd = (uint8_t)fetchbyte(cpu)', 'd'
		elif name in {'PC+#', 'IX+#', 'IY+#', 'SP+#'}:
			# SP+# used by sm83
			assert DREAD is False
			DREAD = True
			if DREADDONE:
				return None, 'd'
			return 'd = (int8_t)fetchbyte(cpu)', 'd'
		elif name in {'(SP+##)', '(##)', '(PC+##)', '(IX+##)', '(IY+##)', '(HL+##)', 'PC+##'}:
			assert DREAD is False
			DREAD = True
			if DREADDONE:
				return None, 'd'
			return 'd = (int16_t)fetchword(cpu, 2)', 'd'
		elif name == 'PC+###':
			assert DREAD is False
			DREAD = True
			if DREADDONE:
				return None, 'd'
			return 'd = extendsign(3, fetchword(cpu, 3))', 'd'
		elif name in {'(IX+#~)', '(IY+#~)', '(SP+#~)'}:
			assert DREAD is False
			DREAD = True
			if DREADDONE:
				return None, 'd'
			return 'd = extendsign(DSPSIZE, fetchword(cpu, DSPSIZE))', 'd'
		elif name == '(##~)':
			assert DREAD is False
			DREAD = True
			if DREADDONE:
				return None, 'd'
			#return 'd = extendsign(IMMSIZE, fetchword(cpu, IMMSIZE))', 'd'
			return 'd = fetchword(cpu, IMMSIZE)', 'd' # TODO: double check
		else:
			if '#' in name:
				print(name, file = sys.stderr)
			assert '#' not in name
			return

	def getregister(regdict, name):
		global ALTHLPAIR
		text = regdict[name]
		if ALTHLPAIR is not None:
			text = text.replace('cpu->hl', ALTHLPAIR)
		return text

	def unascii(name):
		return name.replace('[', '(').replace(']', ')').replace('.', '')

	def getaddress(name):
		# only call/called when name in MEMARGS
		global ALTMEMBANK, ALTHLPAIR
		text = getregister(MEMARGS, name)
		if ALTMEMBANK != '':
			# vm1 specific
			text += ALTMEMBANK
		return text

	def readbytearg(name):
		if name in ACCARGS:
			return 'GET' + getregister(ACCARGS, name) + ')'
		elif name in MEMARGS:
			# Note: could test for SP, but we don't use byte stack access
			return 'readbyte(cpu, ' + getaddress(name) + ')'
		elif name in VARARGS:
			return getregister(VARARGS, name)
		else:
			print(name, file = sys.stderr)
			assert False

	def writebytearg(name, value):
		if name in ACCARGS:
			return 'SET' + getregister(ACCARGS, name) + ', ' + value + ')'
		elif name in MEMARGS:
			# Note: could test for SP, but we don't use byte stack access
			return 'writebyte(cpu, ' + getaddress(name) + ', ' + value + ')'
		elif name in VARARGS:
			return getregister(VARARGS, name) + ' = ' + value
		else:
			print(name, file = sys.stderr)
			assert False

	def readbytereg(name, variant):
		assert name in ACCARGS
		return 'GET' + ACCARGS[name] + {'0': '', '1': '2'}[variant] + ')'

	def writebytereg(name, variant, value):
		assert name in ACCARGS
		return 'SET' + ACCARGS[name] + {'0': '', '1': '2'}[variant] + ', ' + value + ')'
	
	ACCESSOR = {
		'WORDSIZE': 'WORDW(',
		'ADDRSIZE': 'WORDX(',
		'OPNDSIZE': 'WORDL(',
		'DWORDSIZE': 'LONG(',
	}
	def readwordarg(size, name):
		if name in MEMARGS:
			if name.startswith('(SP'):
				# needed for vm1, actually (SP) is enough
				return 'readword_stack(cpu, ' + size + ', ' + getaddress(name) + ')'
			else:
				return 'readword(cpu, ' + size + ', ' + getaddress(name) + ')'
		elif name in VARARGS:
			return 'GET' + ACCESSOR[size] + getregister(VARARGS, name) + ')'
		elif name == 'I':
			return 'GETIREG()'
		elif name == 'SP+#':
			# sm83
			return 'addword(cpu, WORDSIZE, cpu->sp, d)'
		else:
			print(name, file = sys.stderr)
			assert False

	def writewordarg(size, name, value):
		if name in MEMARGS:
			if name.startswith('(SP'):
				# needed for vm1, actually (SP) is enough
				return 'writeword_stack(cpu, ' + size + ', ' + getaddress(name) + ', ' + value + ')'
			else:
				return 'writeword(cpu, ' + size + ', ' + getaddress(name) + ', ' + value + ')'
		elif name in VARARGS:
			return 'SET' + ACCESSOR[size] + getregister(VARARGS, name) + ', ' + value + ')'
		elif name == 'I':
			return 'SETIREG(' + value + ')'
		else:
			print(name, file = sys.stderr)
			assert False

	# DSPSIZE: 1, z380: 1-2-3
	# IMMSIZE: 2, z380: 2-3-4, ez80: 2-3

	# WORDSIZE, SETWORD, GETWORD; ez80: 2 or 3
	# ADDRSIZE, SETWORDX, GETWORDX; z380: 2 or 4; ez80: 2 or 3
	# OPNDSIZE, SETWORDL, GETWORDL; z380: 2 or 4; ez80: 2 or 3

	unimps = set()

	CONDITIONS = {
		'NZ': '!(cpu->af & F_Z)',
		'Z':  '(cpu->af & F_Z)',
		'NC': '!(cpu->af & F_C)',
		'C':  '(cpu->af & F_C)',
		'PO': '!(cpu->af & F_P)',
		'PE': '(cpu->af & F_P)',
		'P':  '!(cpu->af & F_S)',
		'M':  '(cpu->af & F_S)',
	}

	def hasprop(cpu, prefix, opcode, form, prop, default = None):
		if prefix in T[cpu] and type(T[cpu][prefix][opcode]) is Instruction and T[cpu][prefix][opcode].base == form:
			return T[cpu][prefix][opcode].ddir is not None and prop in T[cpu][prefix][opcode].ddir
		else:
			return default

	def assertprop(cpu, prefix, opcode, form, prop, value):
		assert hasprop(cpu, prefix, opcode, form, prop, value) == value

	z80jumps = set()
	for prefix in T['z80'].keys():
		for opcode in range(256):
			if type(T['z80'][prefix][opcode]) == str:
				z80jumps.add(T['z80'][prefix][opcode])

	vm1jumps = set()
	for prefix in T['vm1'].keys():
		for opcode in range(256):
			if type(T['vm1'][prefix][opcode]) == str:
				vm1jumps.add(T['vm1'][prefix][opcode])

	def print_cases(prefix, file, use_cpu = None):
		global T, cpus, prefixes, unimps
		global IREAD, DREAD, DREADDONE
		global ALTMEMBANK, ALTHLPAIR
		DREADDONE = prefix in {'DD CB', 'FD CB'}
		for opcode in range(256):
			ALTMEMBANK = ''
			ALTHLPAIR = None
			indent = '\t' * (1 + (len(prefix) + 1) // 3)
			print(indent + f'case 0x{opcode:02X}:', file = file)
			total = (prefix + f' {opcode:02X}').strip()
			donecpus = set()
			if total in prefixes:
				subcpus1 = [cpu for cpu in cpus if total in T[cpu]]
				if use_cpu is None and len(subcpus1) != len(cpus):
					print("#if " + " || ".join('CPU_' + get_cpu_macro(cpu) for cpu in subcpus1), file = file)
				if total in z80jumps and use_cpu in {None, 'z80'}:
					assert len(subcpus1) > 1
					if use_cpu is None:
						print("#if CPU_Z80", file = file)
					print(indent + "case_" + total.replace(' ', '_') + ":", file = file)
					if use_cpu is None:
						print("#endif", file = file)
				if total in vm1jumps:
					assert len(subcpus1) == 1
					if use_cpu in {None, 'vm1'}:
						print(indent + "case_" + total.replace(' ', '_') + ":", file = file)
				if total in {'DD CB', 'FD CB'}:
					DREAD = False
					code = preparearg("(IX+#~)")
					print('\t' * (1 + (len(prefix) + 1) // 3) + "\t" + code[0] + ";", file = file)
					DREAD = False
				if use_cpu is None or total in T[use_cpu]:
					print(indent + "\top = fetchbyte(cpu);", file = file)
					if total not in {'DD CB', 'FD CB'}:
						print(indent + "\tREFRESH();", file = file)
					print(indent + "\tswitch(op)", file = file)
					print(indent + "\t{", file = file)
					dreaddone = DREADDONE
					print_cases(total, use_cpu = use_cpu, file = file)
					DREADDONE = dreaddone
					print(indent + '\t}', file = file)
				if len(subcpus1) != len(cpus):
				#	print("#endif /* TODO */", file = file)
					donecpus.update(subcpus1)

			for cpu in (cpus if use_cpu is None else [use_cpu]):
				if cpu in donecpus:
					continue
				if prefix not in T[cpu]:
					continue

				if type(T[cpu][prefix][opcode]) is str:
					# go to shorter encoding
					assert cpu in {'z80', 'vm1'}
					#instr = T[cpu][prefix][opcode]
					#subcpus = [cpu0 for cpu0 in cpus if prefix in T[cpu0] and T[cpu0][prefix][opcode] == instr]
					#if len(subcpus) != len(cpus):
					if use_cpu is None:
						if len(donecpus) == 0:
							print("#if CPU_" + get_cpu_macro(cpu), file = file)
						else:
							print("#elif CPU_" + get_cpu_macro(cpu), file = file)
					donecpus.update(subcpus)
					print(indent + "\tgoto " + "case_" + T[cpu][prefix][opcode].replace(' ', '_') + ";", file = file)
					continue

				if T[cpu][prefix][opcode] is None:
					if total in T[cpu]:
						# subtable
						continue
					subcpus = [cpu0 for cpu0 in cpus if total not in T[cpu0] if prefix in T[cpu0] and T[cpu0][prefix][opcode] is None]
					if len(subcpus) != len(cpus):
						if use_cpu is None:
							if len(donecpus) == 0:
								print("#if " + " || ".join('CPU_' + cpu0.upper() for cpu0 in subcpus), file = file)
							else:
								print("#elif " + " || ".join('CPU_' + cpu0.upper() for cpu0 in subcpus), file = file)
						donecpus.update(subcpus)
					if 'z80' in subcpus:
						print(prefix, f'{opcode:02X}', file = sys.stderr)
					print(indent + "\tUNDEFINED();", file = file)
					continue

				assert type(T[cpu][prefix][opcode]) is Instruction

				instr = T[cpu][prefix][opcode]
				subcpus = [cpu0 for cpu0 in cpus if prefix in T[cpu0] and type(T[cpu0][prefix][opcode]) is Instruction and T[cpu0][prefix][opcode].base == instr.base]
				if prefix in T['z280'] and type(T['z280'][prefix][opcode]) is Instruction and T['z280'][prefix][opcode].base.mnem == 'TSTI':
					# tweaking
					subcpus.append('z280')
				if instr.forms[0].mnem in {'IN0', 'OUT0'}:
					# tweaking
					if cpu == 'z380':
						subcpus[:] = ['z380']
					else:
						subcpus.remove('z380')
				if len(subcpus) != len(cpus):
					if use_cpu is None:
						if len(donecpus) == 0:
							print("#if " + " || ".join('CPU_' + cpu0.upper() for cpu0 in subcpus), file = file)
						else:
							print("#elif " + " || ".join('CPU_' + cpu0.upper() for cpu0 in subcpus), file = file)
					donecpus.update(subcpus)
				print(indent + "\t/* " + instr.forms[0].mnem + (" " + ", ".join(instr.forms[0].args) if len(instr.forms[0].args) > 0 else "") + " */", file = file)

				if instr.base is not None and '####' in instr.base.args:
					print(indent + "\t/*** TODO: EPU ***/", file = file)
					continue

				if instr.base is not None:
					form = instr.base
				else:
					assert cpu in {'z80', 'vm1'}
					form = instr.forms[0]

				if instr.style == 'duplicate':
					print(indent + "\t/*** DUPLICATE ***/", file = file)

				if hasprop('z280', prefix, opcode, form, 'P', False):
					if subcpus != ['z280'] and use_cpu is None:
						print("#if CPU_Z280", file = file)
					if use_cpu in {None, 'z280'}:
						print(indent + "\tif(isusermode(cpu))", file = file)
						print(indent + "\t{", file = file)
						print(indent + "\t\texception_trap(cpu, X80_Z280_TRAP_PI);", file = file)
						print(indent + "\t}", file = file)
					if subcpus != ['z280'] and use_cpu is None:
						print("#endif", file = file)
				elif hasprop('z280', prefix, opcode, form, '(P)', False):
					if subcpus != ['z280'] and use_cpu is None:
						print("#if CPU_Z280", file = file)
					if use_cpu in {None, 'z280'}:
						print(indent + "\tif((cpu->z280.ctl[X80_Z280_CTL_TCR] & X80_Z280_TCR_I) && isusermode(cpu))", file = file)
						print(indent + "\t{", file = file)
						print(indent + "\t\texception_trap(cpu, X80_Z280_TRAP_PI);", file = file)
						print(indent + "\t}", file = file)
					if subcpus != ['z280'] and use_cpu is None:
						print("#endif", file = file)

				IREAD = False
				DREAD = False
				immedvars = []
				for arg in form.args:
					code = preparearg(arg)
					if code is not None:
						if code[0] is not None:
							print(indent + "\t" + code[0] + ";", file = file)
						#assert code[1] not in immedvars
						if code[1] in immedvars:
							print(instr, file = sys.stderr)
						immedvars.append(code[1])
				if DREADDONE:
					assert DREAD

				# TODO: a very low quality debug print
				def putimmed(arg):
					while '##' in arg:
						arg = arg.replace('##', '#')
					return arg.replace('#', '%X')
				print(indent + "\tDEBUG(\"->\\t" + instr.forms[0].mnem + ("\\t" + ", ".join(map(putimmed, instr.forms[0].args)) if len(instr.forms[0].args) > 0 else "") + "\\n\"" + ''.join(', ' + var for var in immedvars) + ");", file = file)

				if 'z80' in subcpus:
					# WZ usage
					if '(IX+#)' in form.args or '(IX+#~)' in form.args:
						if subcpus != ['z80'] and use_cpu is None:
							print("#if CPU_Z80", file = file)
						if use_cpu in {None, 'z80'}:
							print(indent + "\tcpu->z80.wz = " + MEMARGS['(IX+#)'] + ";", file = file)
						if subcpus != ['z80'] and use_cpu is None:
							print("#endif", file = file)
					elif '(IY+#)' in form.args or '(IY+#~)' in form.args:
						if subcpus != ['z80'] and use_cpu is None:
							print("#if CPU_Z80", file = file)
						if use_cpu in {None, 'z80'}:
							print(indent + "\tcpu->z80.wz = " + MEMARGS['(IY+#)'] + ";", file = file)
						if subcpus != ['z80'] and use_cpu is None:
							print("#endif", file = file)

				# vm1 specific
				has_cs_prefix = False

				if instr.duplicate is not None:
					dprefix, dopcode = instr.duplicate
					instr0 = T[cpu][dprefix][dopcode]
					if instr0.base is not None:
						form = instr0.base
					else:
						assert cpu in {'z80', 'vm1'}
						form = instr0.forms[0]
				else:
					instr0 = instr

				if instr0.unprefixed is not None:
					assert cpu == 'vm1'
					dprefix, dopcode = instr0.unprefixed
					if 'MB ' in form.mnem:
						ALTMEMBANK = ' | 0x10000'
					if 'CS ' in form.mnem:
						has_cs_prefix = True
					if 'RS ' in form.mnem:
						ALTHLPAIR = 'cpu->hl2'
					instr0 = T[cpu][dprefix][dopcode]
					if instr0.base is not None:
						form = instr0.base
					else:
						assert cpu in {'z80', 'vm1'}
						form = instr0.forms[0]

				if cpu == 'r800':
					form = InsForm(form.mnem, *(unascii(arg) for arg in form.args))
				if form.mnem == 'NOP':
					pass
				elif form.mnem == 'DDIR':
					assert len(form.args) == 1
					args = [arg.strip() for arg in form.args[0].split(',')]
					if 'W' in args:
						print(indent + "\tOPNDSIZE = 2;", file = file)
					elif 'LW' in args:
						print(indent + "\tOPNDSIZE = 4;", file = file)
					else:
						print(indent + "\tOPNDSIZE = cpu->z380.sr & X80_Z380_SR_LW ? 4 : 2;", file = file)
					if 'IB' in args:
						print(indent + "\tSUPSIZE = 1;", file = file)
					elif 'IW' in args:
						print(indent + "\tSUPSIZE = 2;", file = file)
					else:
						print(indent + "\tSUPSIZE = 0;", file = file)
					print(indent + "goto restart;", file = file)
				elif form.mnem in {'.LIL', '.LIS', '.SIL', '.SIS'}:
					print(indent + "\tSIZEPREF = 1;", file = file)
					print(indent + "\tWORDSIZE = " + ('3' if form.mnem[1] == 'L' else '2') + ";", file = file)
					print(indent + "\tIMMSIZE = " + ('3' if form.mnem[3] == 'L' else '2') + ";", file = file)
					print(indent + "goto restart;", file = file)
				elif form.mnem in {'ADC', 'SBC'}:
					assert form.args[0] in {'A', 'IX', 'HL', 'IY'}
					if form.args[0] == 'A':
						# byte operation
						print(indent + "\tx = " + readbytearg(form.args[0]) + ";", file = file)
						print(indent + "\ty = " + readbytearg(form.args[1]) + ";", file = file)
						print(indent + "\tz = " + {'ADC': "addcbyte", 'SBC': "subcbyte"}[form.mnem] + "(cpu, x, y, cpu->af & F_C);", file = file)
						print(indent + "\t" + writebytearg(form.args[0], "z") + ";", file = file)
					else:
						# word operation
						assertprop('z380', prefix, opcode, form, 'L', False)
						assertprop('z380', prefix, opcode, form, 'X', False)
						print(indent + "\tx = " + readwordarg('WORDSIZE', form.args[0]) + ";", file = file)
						if 'z80' in subcpus:
							if subcpus != ['z80'] and use_cpu is None:
								print("#if CPU_Z80", file = file)
							if use_cpu in {None, 'z80'}:
								print(indent + "\tcpu->z80.wz = x + 1;", file = file)
							if subcpus != ['z80'] and use_cpu is None:
								print("#endif", file = file)
						print(indent + "\ty = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
						print(indent + "\tz = " + {'ADC': "addcword", 'SBC': "subcword"}[form.mnem] + "(cpu, WORDSIZE, x, y, cpu->af & F_C);", file = file)
						print(indent + "\t" + writewordarg('WORDSIZE', form.args[0], "z") + ";", file = file)
				elif form.mnem in {'ADCW', 'ADDW', 'SBCW', 'SUBW'}:
					# word operation
					assertprop('z380', prefix, opcode, form, 'L', False)
					assertprop('z380', prefix, opcode, form, 'X', False)
					opn = {'ADCW': 'addcword', 'ADDW': 'addword', 'SBCW': 'subcword', 'SUBW': 'subword'}[form.mnem]
					print(indent + "\tx = " + readwordarg('WORDSIZE', form.args[0]) + ";", file = file)
					print(indent + "\ty = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
					print(indent + "\tz = " + opn + "(cpu, WORDSIZE, x, y" + (", cpu->af & F_C" if 'C' in form.mnem else "") + ");", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', form.args[0], "z") + ";", file = file)
				elif form.mnem == 'DSUB':
					# 8085/vm1
					print(indent + "\tx = cpu->hl;", file = file)
					if len(form.args) == 0 or form.args[0] == 'B':
						print(indent + "\ty = cpu->bc;", file = file)
					elif form.args[0] == 'D':
						print(indent + "\ty = cpu->de;", file = file)
					else:
						assert False
					opn = 'subword' if not has_cs_prefix else 'subcword'
					print(indent + f"\tz = {opn}(cpu, 2, x, y" + (", cpu->af & F_C" if has_cs_prefix else "") + ");", file = file)
					print(indent + "\tcpu->hl = z;", file = file)
				elif form.mnem == 'ADD':
					assert form.args[0] in {'A', 'IX', 'HL', 'IY', 'SP'}
					if form.args[0] == 'A':
						# byte operation
						print(indent + "\tx = " + readbytearg(form.args[0]) + ";", file = file)
						print(indent + "\ty = " + readbytearg(form.args[1]) + ";", file = file)
						print(indent + "\tz = addbyte(cpu, x, y);", file = file);
						print(indent + "\t" + writebytearg(form.args[0], "z") + ";", file = file)
					elif form.args[1] == 'A':
						# byte-to-word operation
						print(indent + "\tx = " + readwordarg('WORDSIZE', form.args[0]) + ";", file = file)
						print(indent + "\ty = (int8_t)" + readbytearg(form.args[1]) + ";", file = file)
						print(indent + "\tz = addword(cpu, WORDSIZE, x, y);", file = file)
						print(indent + "\t" + writewordarg('WORDSIZE', form.args[0], "z") + ";", file = file)
					elif form.args == ['SP', '#']:
						# ADD SP, #
						assert cpu == 'sm83'
						print(indent + "\tx = addword(cpu, WORDSIZE, " + readwordarg('WORDSIZE', 'SP') + ", i);", file = file)
						print(indent + "\t" + writewordarg('WORDSIZE', 'SP', "x") + ";", file = file)
					else:
						# word operation
						assertprop('z380', prefix, opcode, form, 'L', False)
						assertprop('z380', prefix, opcode, form, 'X', True)
						print(indent + "\tx = " + readwordarg('WORDSIZE', form.args[0]) + ";", file = file)
						if 'z80' in subcpus:
							if subcpus != ['z80'] and use_cpu is None:
								print("#if CPU_Z80", file = file)
							if use_cpu in {None, 'z80'}:
								print(indent + "\tcpu->z80.wz = x + 1;", file = file)
							if subcpus != ['z80'] and use_cpu is None:
								print("#endif", file = file)
						print(indent + "\ty = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
						opn = 'addaddress' if not has_cs_prefix else 'addcaddress'
						print(indent + f"\tz = {opn}(cpu, WORDSIZE, x, y" + (", cpu->af & F_C" if has_cs_prefix else "") + ");", file = file)
						print(indent + "\t" + writewordarg('WORDSIZE', form.args[0], "z") + ";", file = file)
				elif form.mnem == 'SUB':
					if len(form.args) == 1:
						# byte operation
						print(indent + "\tx = " + readbytearg('A') + ";", file = file)
						print(indent + "\ty = " + readbytearg(form.args[0]) + ";", file = file)
						print(indent + "\tz = subbyte(cpu, x, y);", file = file)
						print(indent + "\t" + writebytearg('A', "z") + ";", file = file)
					elif form.args[0] == 'A':
						# byte operation
						print(indent + "\tx = " + readbytearg(form.args[0]) + ";", file = file)
						print(indent + "\ty = " + readbytearg(form.args[1]) + ";", file = file)
						print(indent + "\tz = subbyte(cpu, x, y);", file = file)
						print(indent + "\t" + writebytearg(form.args[0], "z") + ";", file = file)
					else:
						assert form.args[0] in {'A', 'IX', 'HL', 'IY', 'SP'}
						# word operation
						assertprop('z380', prefix, opcode, form, 'L', False)
						assertprop('z380', prefix, opcode, form, 'X', True)
						print(indent + "\tx = " + readwordarg('WORDSIZE', form.args[0]) + ";", file = file)
						print(indent + "\ty = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
						print(indent + "\tz = subaddress(cpu, WORDSIZE, x, y);", file = file)
						print(indent + "\t" + writewordarg('WORDSIZE', form.args[0], "z") + ";", file = file)
				elif form.mnem in {'AND', 'OR', 'XOR'}:
					assert len(form.args) == 1
					# byte operation
					opn = {'AND': "andbyte", 'OR': "orbyte", 'XOR': "xorbyte"}[form.mnem]
					print(indent + "\tx = " + readbytearg('A') + ";", file = file)
					print(indent + "\ty = " + readbytearg(form.args[0]) + ";", file = file)
					print(indent + "\tz = " + opn + "(cpu, x, y);", file = file)
					print(indent + "\t" + writebytearg('A', "z") + ";", file = file)
				elif form.mnem in {'ANX', 'ORX', 'XRX'}:
					assert len(form.args) == 0
					# byte operation
					opn = {'ANX': "andbyte", 'ORX': "orbyte", 'XRX': "xorbyte"}[form.mnem]
					print(indent + "\tx = " + readbytearg('(HL)') + ";", file = file)
					print(indent + "\ty = " + readbytearg('A') + ";", file = file)
					print(indent + "\tz = " + opn + "(cpu, x, y);", file = file)
					print(indent + "\t" + writebytearg('(HL)', "z") + ";", file = file)
				elif form.mnem in {'ANDW', 'ORW', 'XORW'}:
					# word operation
					assertprop('z380', prefix, opcode, form, 'L', False)
					assertprop('z380', prefix, opcode, form, 'X', False)
					opn = {'ANDW': 'andword', 'ORW': 'orword', 'XORW': 'xorword'}[form.mnem]
					print(indent + "\tx = " + readwordarg('WORDSIZE', form.args[0]) + ";", file = file)
					print(indent + "\ty = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
					print(indent + "\tz = " + opn + "(cpu, x, y);", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', form.args[0], "z") + ";", file = file)
				elif form.mnem == 'BIT':
					assert len(form.args) == 2
					# byte operation
					print(indent + "\tx = " + readbytearg(form.args[1]) + ";", file = file)
					if form.args[1] in MEMARGS:
						# memory operand
						print(indent + "\tbitbyte_memory(cpu, x, " + form.args[0] + ");", file = file)
					else:
						print(indent + "\tbitbyte(cpu, x, " + form.args[0] + ");", file = file)
				elif form.mnem == 'BTEST':
					print(indent + "\tCPYBIT(cpu->af, F_V, cpu->z380.sr & 1);", file = file)
					print(indent + "\tCPYBIT(cpu->af, F_C, (cpu->z380.sr >> 8) & 1);", file = file)
					print(indent + "\tCPYBIT(cpu->af, F_S, (cpu->z380.sr >> 16) & 1);", file = file)
					print(indent + "\tCPYBIT(cpu->af, F_Z, (cpu->z380.sr >> 24) & 1);", file = file)
				elif form.mnem in {'CALL', 'CALR'}:
					if len(form.args) == 1:
						print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[0]) + ";", file = file)
						print(indent + "\tdo_call(cpu, x);", file = file) # deal with cpu->z80.wz
					else:
						# conditional
						print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[1]) + ";", file = file)
						print(indent + "\tif(" + CONDITIONS[form.args[0]] + ")", file = file)
						print(indent + "\t\tdo_call(cpu, x);", file = file) # deal with cpu->z80.wz
				elif form.mnem == 'CCF':
					print(indent + "\tdo_ccf(cpu);", file = file)
				elif form.mnem == 'CP':
					assert len(form.args) == 1
					# byte operation
					print(indent + "\tx = " + readbytearg('A') + ";", file = file)
					print(indent + "\ty = " + readbytearg(form.args[0]) + ";", file = file)
					print(indent + "\tcmpbyte(cpu, x, y);", file = file)
				elif form.mnem == 'CPW':
					assert len(form.args) == 2 and form.args[0] == 'HL'
					# byte operation
					print(indent + "\tx = " + readwordarg('WORDSIZE', form.args[0]) + ";", file = file)
					print(indent + "\ty = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
					print(indent + "\tsubword(cpu, WORDSIZE, x, y);", file = file)
				elif form.mnem == 'DCMP':
					# vm1
					print(indent + "\tx = cpu->hl;", file = file)
					if form.args[0] == 'B':
						print(indent + "\ty = cpu->bc;", file = file)
					elif form.args[0] == 'D':
						print(indent + "\ty = cpu->de;", file = file)
					else:
						assert False
					opn = 'subword' if not has_cs_prefix else 'subcword'
					print(indent + f"\t{opn}(cpu, 2, x, y" + (", cpu->af & F_C" if has_cs_prefix else "") + ");", file = file)
				elif form.mnem in {'CPD', 'CPDR', 'CPI', 'CPIR'}:
					print(indent + "\tx = " + readbytearg('A') + ";", file = file)
					print(indent + "\ty = " + readbytearg('(HL)') + ";", file = file)
					print(indent + "\ttestcmpstring(cpu, x, y);", file = file)
					print(indent + "\tSETWORDL(cpu->hl, cpu->hl " + {'I': "+", 'D': "-"}[form.mnem[2]] + " 1);", file = file)
					print(indent + "\tSETWORDL(cpu->bc, cpu->bc - 1);", file = file)
					print(indent + "\tif(GETWORDL(cpu->bc) != 0)", file = file)
					print(indent + "\t\tcpu->af |= F_V;", file = file)
					if form.mnem.endswith('R'):
						print(indent + "\tif((cpu->af & F_V) && !(cpu->af & F_Z))", file = file)
						print(indent + "\t{", file = file)
						print(indent + "\t\tSETADDR(cpu->pc, cpu->pc - 2);", file = file)
						if 'z80' in subcpus:
							if subcpus != ['z80'] and use_cpu is None:
								print("#if CPU_Z80", file = file)
							if use_cpu in {None, 'z80'}:
								print(indent + "\t\tcpu->z80.wz = cpu->pc + 1;", file = file)
							if subcpus != ['z80'] and use_cpu is None:
								print("#endif", file = file)
						print(indent + "\t}", file = file)
						if 'z80' in subcpus:
							if subcpus != ['z80'] and use_cpu is None:
								print("#if CPU_Z80", file = file)
							if use_cpu in {None, 'z80'}:
								print(indent + "\telse", file = file)
								print(indent + "\t\tcpu->z80.wz " + {'I': "++", 'D': "--"}[form.mnem[2]] + ";", file = file)
							if subcpus != ['z80'] and use_cpu is None:
								print("#endif", file = file)
					elif 'z80' in subcpus:
						if subcpus != ['z80'] and use_cpu is None:
							print("#if CPU_Z80", file = file)
						if use_cpu in {None, 'z80'}:
							print(indent + "\tcpu->z80.wz " + {'I': "++", 'D': "--"}[form.mnem[2]] + ";", file = file)
						if subcpus != ['z80'] and use_cpu is None:
							print("#endif", file = file)
				elif form.mnem == 'CPL':
					print(indent + "\tz = cplbyte(cpu, " + readbytearg('A') + ");", file = file)
					print(indent + "\t" + writebytearg('A', "z") + ";", file = file)
				elif form.mnem == 'CPLW':
					print(indent + "\tz = cplword(cpu, " + readwordarg('WORDSIZE', 'HL') + ");", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', 'HL', "z") + ";", file = file)
				elif form.mnem == 'DAA':
					print(indent + "\tz = daabyte(cpu, " + readbytearg('A') + ");", file = file)
					print(indent + "\t" + writebytearg('A', "z") + ";", file = file)
				elif form.mnem in {'DECW', 'INCW'} or (form.mnem in {'DEC', 'INC'} and form.args[0] in {'BC', 'DE', 'SP'}):
					print(indent + "\tz = " + {'I': 'incword', 'D': 'decword'}[form.mnem[0]] + "(cpu, " + readwordarg('ADDRSIZE', form.args[0]) + ", 1);", file = file)
					print(indent + "\t" + writewordarg('ADDRSIZE', form.args[0], "z") + ";", file = file)
				elif form.mnem in {'DEC', 'INC'} and form.args[0] not in {'BC', 'DE', 'SP'}:
					print(indent + "\tz = " + form.mnem.lower() + "byte(cpu, " + readbytearg(form.args[0]) + ");", file = file)
					print(indent + "\t" + writebytearg(form.args[0], "z") + ";", file = file)
				elif form.mnem in {'DI', 'EI'}:
					if len(form.args) == 0:
						print(indent + "\t" + {'DI': 'disable_interrupts', 'EI': 'enable_interrupts'}[form.mnem] + "(cpu, INT_DEFAULT);", file = file)
					else:
						print(indent + "\t" + {'DI': 'disable_interrupts', 'EI': 'enable_interrupts'}[form.mnem] + "(cpu, " + readbytearg(form.args[0]) + ");", file = file)
				elif form.mnem == 'DIVUW':
					if form.args[0] == 'HL':
						# z380 style division
						print(indent + "\tx = " + readwordarg('DWORDSIZE', form.args[0]) + ";", file = file)
						print(indent + "\ty = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
						print(indent + "\tz = udivword(cpu, x, y);", file = file)
						print(indent + "\t" + writewordarg('DWORDSIZE', 'HL', "z") + ";", file = file)
					elif form.args[0] == 'DEHL':
						# z280 style division
						print(indent + "\tx = (" + readwordarg('WORDSIZE', 'DE') + " << 16) | " + readwordarg('WORDSIZE', 'HL') + ";", file = file)
						print(indent + "\ty = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
						print(indent + "\tz = udivword(cpu, x, y);", file = file)
						print(indent + "\tif((cpu->af & F_V))", file = file)
						print(indent + "\t{", file = file)
						print(indent + "\t\texception_trap(cpu, X80_Z280_TRAP_DE);", file = file)
						print(indent + "\t}", file = file)
						print(indent + "\t" + writewordarg('WORDSIZE', 'HL', "z") + ";", file = file)
						print(indent + "\t" + writewordarg('WORDSIZE', 'DE', "z >> 16") + ";", file = file)
				elif form.mnem == 'DIVW':
					assert form.args[0] == 'DEHL'
					print(indent + "\tx = (" + readwordarg('WORDSIZE', 'DE') + " << 16) | " + readwordarg('WORDSIZE', 'HL') + ";", file = file)
					print(indent + "\ty = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
					print(indent + "\tz = sdivword(cpu, x, y);", file = file)
					print(indent + "\tif((cpu->af & F_V))", file = file)
					print(indent + "\t{", file = file)
					print(indent + "\t\texception_trap(cpu, X80_Z280_TRAP_DE);", file = file)
					print(indent + "\t}", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', 'HL', "z") + ";", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', 'DE', "z >> 16") + ";", file = file)
				elif form.mnem in {'DIV', 'DIVU'}:
					assert form.args[0] == 'HL'
					print(indent + "\tx = " + readwordarg('WORDSIZE', form.args[0]) + ";", file = file)
					print(indent + "\ty = " + readbytearg(form.args[1]) + ";", file = file)
					print(indent + "\tz = " + {'DIV': 'sdivbyte', 'DIVU': 'udivbyte'}[form.mnem] + "(cpu, x, y);", file = file)
					print(indent + "\tif((cpu->af & F_V))", file = file)
					print(indent + "\t{", file = file)
					print(indent + "\t\texception_trap(cpu, X80_Z280_TRAP_DE);", file = file)
					print(indent + "\t}", file = file)
					print(indent + "\t" + writebytearg('A', "z") + ";", file = file)
					print(indent + "\t" + writebytearg('L', "z >> 8") + ";", file = file)
				elif form.mnem == 'DJNZ':
					print(indent + "\tz = " + readbytearg('B') + " - 1;", file = file)
					print(indent + "\t" + writebytearg('B', "z") + ";", file = file)
					print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[0]) + ";", file = file)
					print(indent + "\tif(" + readbytearg('B') + " != 0)", file = file)
					print(indent + "\t{", file = file)
					print(indent + "\t\tdo_jump_relative(cpu, x);", file = file)
					print(indent + "\t}", file = file)
					if 'z80' in subcpus:
						if subcpus != ['z80'] and use_cpu is None:
							print("#if CPU_Z80", file = file)
						if use_cpu in {None, 'z80'}:
							print(indent + "\tcpu->z80.wz = x;", file = file)
						if subcpus != ['z80'] and use_cpu is None:
							print("#endif", file = file)
				elif form.mnem == 'EX':
					if len(form.args) < 2:
						pass # TODO: dp22
					elif form.args == ["AF", "AF'"]:
						print(indent + "\tflipafp(cpu);", file = file)
					elif form.args[1].endswith("'"):
						assert form.args[0] == form.args[1][:-1]
						if form.args[0] in {'A', 'B', 'C', 'D', 'E', 'H', 'L'}:
							print(indent + "\tx = " + readbytereg(form.args[0], '0') + ";", file = file)
							print(indent + "\ty = " + readbytereg(form.args[0], '1') + ";", file = file)
							print(indent + "\t" + writebytereg(form.args[0], '0', "y") + ";", file = file)
							print(indent + "\t" + writebytereg(form.args[0], '1', "x") + ";", file = file)
						else:
							assert form.args[0] in {'BC', 'DE', 'HL', 'IX', 'IY'}
							print(indent + "\tx = GETWORDL(cpu->" + form.args[0].lower() + ");", file = file)
							print(indent + "\tSETWORDL(cpu->" + form.args[0].lower() + ", GETWORDL(cpu->" + form.args[0].lower() + "2));", file = file)
							print(indent + "\tSETWORDL(cpu->" + form.args[0].lower() + "2, x);", file = file)
					elif form.args[0] == 'A' or form.args == ['H', 'L']:
						print(indent + "\tx = " + readbytearg(form.args[0]) + ";", file = file)
						print(indent + "\ty = " + readbytearg(form.args[1]) + ";", file = file)
						print(indent + "\t" + writebytearg(form.args[1], "x") + ";", file = file)
						print(indent + "\t" + writebytearg(form.args[0], "y") + ";", file = file)
					else:
						print(indent + "\tx = " + readwordarg('OPNDSIZE', form.args[0]) + ";", file = file)
						print(indent + "\ty = " + readwordarg('OPNDSIZE', form.args[1]) + ";", file = file)
						print(indent + "\t" + writewordarg('OPNDSIZE', form.args[0], "y") + ";", file = file)
						print(indent + "\t" + writewordarg('OPNDSIZE', form.args[1], "x") + ";", file = file)
						if 'z80' in subcpus and form.args[0] == '(SP)':
							if subcpus != ['z80'] and use_cpu is None:
								print("#if CPU_Z80", file = file)
							if use_cpu in {None, 'z80'}:
								print(indent + "\tcpu->z80.wz = x;", file = file)
							if subcpus != ['z80'] and use_cpu is None:
								print("#endif", file = file)
				elif form.mnem == 'EXALL':
					print(indent + "\tflipalt(cpu);", file = file)
					print(indent + "\tflipixp(cpu);", file = file)
					print(indent + "\tflipiyp(cpu);", file = file)
				elif form.mnem == 'EXTS':
					if form.args[0] == 'A':
						print(indent + "\tz = (int8_t)" + readbytearg(form.args[0]) + ";", file = file)
						print(indent + "\t" + writewordarg('OPNDSIZE', 'HL', "z") + ";", file = file)
					else:
						assert form.args[0] == 'HL'
						print(indent + "\tcpu->de = cpu->hl & 0x8000 ? 0xFFFF : 0x0000;", file = file)
				elif form.mnem == 'EXTSW':
					print(indent + "\tcpu->hl = (int16_t)cpu->hl;", file = file)
				elif form.mnem == 'EXX':
					print(indent + "\tflipalt(cpu);", file = file)
				elif form.mnem == 'EXXX':
					print(indent + "\tflipixp(cpu);", file = file)
				elif form.mnem == 'EXXY':
					print(indent + "\tflipiyp(cpu);", file = file)
				elif form.mnem == 'HALT' or form.mnem == 'STOP':
					if form.mnem == 'HALT':
						if use_cpu is None:
							print("#if CPU_Z280", file = file)
						if use_cpu in {None, 'z280'}:
							print(indent + "\tif((cpu->z280.ctl[X80_Z280_CTL_MSR] & X80_Z280_MSR_BH))", file = file)
							print(indent + "\t{", file = file)
							print(indent + "\t\texception_trap(cpu, X80_Z280_TRAP_BPOH);", file = file)
							print(indent + "\t}", file = file)
						if use_cpu is None:
							print("#endif", file = file)
					print(indent + "\t/* TODO: not implemented */;", file = file)
					if form.mnem == 'HALT':
						print(indent + "\t/* HALT() */;", file = file)
					elif form.mnem == 'STOP':
						print(indent + "\t/* STOP() */;", file = file)
					print(indent + "\tSETADDR(cpu->pc, cpu->pc - 1);", file = file)
				elif form.mnem == 'IM':
					print(indent + "\tsetim(cpu, " + (form.args[0] if form.args[0] != '?' else '0') + ");", file = file)
				elif form.mnem == 'IN' or form.mnem == 'TSTI': # z280 specific
					if form.mnem == 'IN' and form.args[1] == '(#)':
						print(indent + "\ty = (" + readbytearg('A') + " << 8)|d;", file = file);
						print(indent + "\tz = inputbyte(cpu, y);", file = file)
						print(indent + "\t" + writebytearg(form.args[0], "z") + ";", file = file)
					else:
						print(indent + "\tx = inputbyte(cpu, cpu->bc);", file = file)
						print(indent + "\ttestinbyte(cpu, x);", file = file)
						if form.mnem != 'TSTI' and form.args[0] != 'F':
							print(indent + "\t" + writebytearg(form.args[0], "x") + ";", file = file)
					if 'z80' in subcpus:
						if subcpus != ['z80'] and use_cpu is None:
							print("#if CPU_Z80", file = file)
						if use_cpu in {None, 'z80'}:
							if form.args[1] == '(#)':
								print(indent + "\tcpu->z80.wz = y + 1;", file = file)
							else:
								print(indent + "\tcpu->z80.wz = cpu->bc + 1;", file = file)
						if subcpus != ['z80'] and use_cpu is None:
							print("#endif", file = file)
				elif form.mnem == 'IN0':
					if cpu != 'z380':
						print(indent + "\tx = inputbyte(cpu, d);", file = file)
					else:
						print(indent + "\tx = inputbyte0(cpu, d);", file = file)
					print(indent + "\ttestinbyte(cpu, x);", file = file)
					if len(form.args) > 1:
						print(indent + "\t" + writebytearg(form.args[0], "x") + ";", file = file)
				elif form.mnem == 'INW':
					print(indent + "\tx = inputword(cpu, cpu->bc);", file = file)
					if cpu == 'z380':
						print(indent + "\ttestinword(cpu, x);", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', form.args[0], "x") + ";", file = file)
				elif form.mnem == 'INA':
					print(indent + "\tz = inputbyte(cpu, d);", file = file)
					print(indent + "\t" + writebytearg(form.args[0], "z") + ";", file = file)
				elif form.mnem == 'INAW':
					print(indent + "\tz = inputword(cpu, d);", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', form.args[0], "z") + ";", file = file)
				elif form.mnem in {'IND', 'INDR', 'INI', 'INIR', \
						'IND2', 'IND2R', 'INI2', 'INI2R', \
						'INDM', 'INDMR', 'INIM', 'INIMR', \
						'INDRX', 'INIRX'}:
					if 'z80' in subcpus:
						if subcpus != ['z80'] and use_cpu is None:
							print("#if CPU_Z80", file = file)
						if use_cpu in {None, 'z80'}:
							print(indent + "\tcpu->z80.wz = cpu->bc " + {'I': "+", 'D': "-"}[form.mnem[2]] + " 1;", file = file)
						if subcpus != ['z80'] and use_cpu is None:
							print("#endif", file = file)
					if 'M' in form.mnem:
						print(indent + "\tx = inputbyte(cpu, GETLO(cpu->bc));", file = file)
					elif 'X' in form.mnem or form.mnem.endswith('2R'):
						print(indent + "\tx = inputbyte(cpu, cpu->de);", file = file)
					else:
						print(indent + "\tx = inputbyte(cpu, cpu->bc);", file = file)
					print(indent + "\twritebyte(cpu, cpu->hl, x);", file = file)
					print(indent + "\tSETWORDL(cpu->hl, cpu->hl " + {'I': "+", 'D': "-"}[form.mnem[2]] + " 1);", file = file)
					if 'X' in form.mnem or form.mnem.endswith('2R'):
						print(indent + "\tz = " + readwordarg('WORDSIZE', 'BC') + " - 1;", file = file)
						print(indent + "\t" + writewordarg('WORDSIZE', 'BC', "z") + ";", file = file)
						if form.mnem.endswith('2R'):
							print(indent + "\tz = " + readwordarg('WORDSIZE', 'DE') + {'I': "+", 'D': "-"}[form.mnem[2]] + " 1;", file = file)
							print(indent + "\t" + writewordarg('WORDSIZE', 'DE', "z") + ";", file = file)
						print(indent + "\ttestinstring(cpu, " + readwordarg('WORDSIZE', 'BC') + ", x);", file = file)
					else:
						print(indent + "\tz = " + readbytearg('B') + " - 1;", file = file)
						print(indent + "\t" + writebytearg('B', "z") + ";", file = file)
						if '2' in form.mnem or 'M' in form.mnem:
							print(indent + "\tz = " + readbytearg('C') + {'I': "+", 'D': "-"}[form.mnem[2]] + " 1;", file = file)
							print(indent + "\t" + writebytearg('C', "z") + ";", file = file)
						print(indent + "\t" + {'I': 'testinistring', 'D': 'testindstring'}[form.mnem[2]] + "(cpu, " + readbytearg('B') + ", x);", file = file)
					if form.mnem.endswith('R') or form.mnem.endswith('RX'):
						print(indent + "\tif(!(cpu->af & F_Z))", file = file)
						print(indent + "\t\tSETADDR(cpu->pc, cpu->pc - 2);", file = file)
				elif form.mnem in {'INDW', 'INDRW', 'INIW', 'INIRW'}:
					print(indent + "\tx = inputword(cpu, cpu->de);", file = file)
					print(indent + "\twriteword(cpu, WORDSIZE, cpu->hl, x);", file = file)
					print(indent + "\tSETWORDL(cpu->hl, cpu->hl " + {'I': "+", 'D': "-"}[form.mnem[2]] + " 2);", file = file)
					print(indent + "\tSETWORDW(cpu->bc, cpu->bc - 1);", file = file)
					print(indent + "\ttestinstring(cpu, GETWORDW(cpu->bc), x);", file = file)
					print(indent + "\tif(GETWORDW(cpu->bc) == 0)", file = file)
					print(indent + "\t\tcpu->af |= F_Z;", file = file)
					print(indent + "\tcpu->af |= F_N;", file = file)
					if form.mnem.endswith('R'):
						print(indent + "\tif(!(cpu->af & F_Z))", file = file)
						print(indent + "\t\tSETADDR(cpu->pc, cpu->pc - 2);", file = file)
				elif form.mnem == 'JAF':
					print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[0]) + ";", file = file)
					print(indent + "\tif(getafp(cpu))", file = file)
					print(indent + "\t\tdo_jump(cpu, x);", file = file)
				elif form.mnem == 'JAR':
					print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[0]) + ";", file = file)
					print(indent + "\tif(getalt(cpu))", file = file)
					print(indent + "\t\tdo_jump(cpu, x);", file = file)
				elif form.mnem == 'JP' or cpu == 'i80' and form.mnem == 'JMP':
					if form.args[0] in {'(HL)', '(IX)', '(IY)'}:
						# tricky syntax, simply load register
						print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[0][1:-1]) + ";", file = file)
						print(indent + "\tdo_jump(cpu, x);", file = file)
					elif len(form.args) == 1:
						print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[0]) + ";", file = file)
						if 'z80' in subcpus:
							if subcpus != ['z80'] and use_cpu is None:
								print("#if CPU_Z80", file = file)
							if use_cpu in {None, 'z80'}:
								print(indent + "\tcpu->z80.wz = x;", file = file)
							if subcpus != ['z80'] and use_cpu is None:
								print("#endif", file = file)
						print(indent + "\tdo_jump(cpu, x);", file = file)
					else:
						# conditional
						print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[1]) + ";", file = file)
						if 'z80' in subcpus:
							if subcpus != ['z80'] and use_cpu is None:
								print("#if CPU_Z80", file = file)
							if use_cpu in {None, 'z80'}:
								print(indent + "\tcpu->z80.wz = x;", file = file)
							if subcpus != ['z80'] and use_cpu is None:
								print("#endif", file = file)
						print(indent + "\tif(" + CONDITIONS[form.args[0]] + ")", file = file)
						print(indent + "\t\tdo_jump(cpu, x);", file = file)
				elif form.mnem == 'JR':
					if len(form.args) == 1:
						print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[0]) + ";", file = file)
						print(indent + "\tdo_jump_relative(cpu, x);", file = file)
					else:
						# conditional
						print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[1]) + ";", file = file)
						print(indent + "\tif(" + CONDITIONS[form.args[0]] + ")", file = file)
						print(indent + "\t\tdo_jump_relative(cpu, x);", file = file)
					if 'z80' in subcpus:
						if subcpus != ['z80'] and use_cpu is None:
							print("#if CPU_Z80", file = file)
						if use_cpu in {None, 'z80'}:
							print(indent + "\tcpu->z80.wz = x;", file = file)
						if subcpus != ['z80'] and use_cpu is None:
							print("#endif", file = file)
				elif form.mnem in {'JK', 'JNK'}:
					# 8085
					print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[0]) + ";", file = file)
					print(indent + "\tif(" + ('(cpu->af & F_UI)' if form.mnem[1] == 'K' else '!(cpu->af & F_UI)') + ")", file = file)
					print(indent + "\t\tdo_jump(cpu, x);", file = file)
				elif form.mnem == 'JOF':
					# vm1
					print(indent + "\tx = " + readwordarg('ADDRSIZE', form.args[0]) + ";", file = file)
					print(indent + "\tif((cpu->af & F_V))", file = file)
					print(indent + "\t\tdo_jump(cpu, x);", file = file)
				elif form.mnem in {'LD', 'LDW'}:
					if form.args[0] == '(C)':
						# sm83
						print(indent + "\twritebyte(cpu, 0xFF00 + " + readbytearg('C') + ", " + readbytearg(form.args[1]) + ");", file = file)
					elif form.args[1] == '(C)':
						# sm83
						print(indent + "\tx = readbyte(cpu, 0xFF00 + " + readbytearg('C') + ");", file = file)
						print(indent + "\t" + writebytearg(form.args[0], "x") + ";", file = file)
					elif form.args[0] in {'A', 'B', 'C', 'D', 'E', 'H', 'L', 'IXH', 'IXL', 'IYH', 'IYL'} \
					or form.args[1] in {'A', 'B', 'C', 'D', 'E', 'H', 'L', 'IXH', 'IXL', 'IYH', 'IYL', '#'}:
						if form.args[0] != form.args[1]:
							print(indent + "\tx = " + readbytearg(form.args[1]) + ";", file = file)
							print(indent + "\t" + writebytearg(form.args[0], "x") + ";", file = file)
							if 'z80' in subcpus:
								if form.args[0] == 'A' and form.args[1] in {'(##~)', '(BC)', '(DE)'}:
									# WZ usage
									if subcpus != ['z80'] and use_cpu is None:
										print("#if CPU_Z80", file = file)
									if use_cpu in {None, 'z80'}:
										print(indent + "\tcpu->z80.wz = " + MEMARGS[form.args[1]] + " + 1;", file = file)
									if subcpus != ['z80'] and use_cpu is None:
										print("#endif", file = file)
								elif form.args[0] in {'(##~)', '(BC)', '(DE)'} and form.args[1] == 'A':
									# WZ usage
									if subcpus != ['z80'] and use_cpu is None:
										print("#if CPU_Z80", file = file)
									if use_cpu in {None, 'z80'}:
										print(indent + "\tcpu->z80.wz = ((" + MEMARGS[form.args[0]] + " + 1) & 0xFF) | (GETLO(cpu->af) << 8);", file = file)
									if subcpus != ['z80'] and use_cpu is None:
										print("#endif", file = file)
							if form.args[1] in {'I', 'R'}:
								print(indent + "\ttestldair(cpu, x);", file = file)
					else:
						print(indent + "\tx = " + readwordarg('OPNDSIZE', form.args[1]) + ";", file = file)
						print(indent + "\t" + writewordarg('OPNDSIZE', form.args[0], "x") + ";", file = file)
						if cpu == 'ez80' and form.args[1] == 'I':
							print(indent + "\ttestldair(cpu, x);", file = file)
						if 'z80' in subcpus:
							if form.args[0] in {'BC', 'DE', 'HL', 'SP', 'IX', 'IY'} and form.args[1] == '(##~)' \
							or form.args[1] in {'BC', 'DE', 'HL', 'SP', 'IX', 'IY'} and form.args[0] == '(##~)':
								# WZ usage
								if subcpus != ['z80'] and use_cpu is None:
									print("#if CPU_Z80", file = file)
								arg = form.args[0] if form.args[0].startswith('(') else form.args[1] # '(##~)'
								if use_cpu in {None, 'z80'}:
									print(indent + "\tcpu->z80.wz = " + MEMARGS[arg] + " + 1;", file = file)
								if subcpus != ['z80'] and use_cpu is None:
									print("#endif", file = file)
				elif form.mnem == 'LDH':
					# sm83
					assert cpu == 'sm83'
					if form.args[1] == '(#)':
						print(indent + "\tx = readbyte(cpu, 0xFF00 + d);", file = file)
					else:
						print(indent + "\tx = " + readbytearg(form.args[1]) + ";", file = file)
					if form.args[0] == '(#)':
						print(indent + "\twritebyte(cpu, 0xFF00 + d, x);", file = file)
					else:
						print(indent + "\t" + writebytearg(form.args[0], "x") + ";", file = file)
				elif form.mnem == 'LDA':
					print(indent + "\tx = " + MEMARGS[form.args[1]] + ";", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', form.args[0], "x") + ";", file = file)
				elif form.mnem == 'LDCTL':
					if cpu == 'z280':
						if form.args[0] == '(C)':
							print(indent + "\tz280_sync_registers(cpu);", file = file)
							print(indent + "\tcpu->z280.ctl[GETLO(cpu->bc)] = cpu->" + form.args[1].lower() + ";", file = file)
							print(indent + "\tz280_reload_registers(cpu);", file = file)
						elif form.args[1] == '(C)':
							print(indent + "\tz280_sync_registers(cpu);", file = file)
							print(indent + "\tcpu->" + form.args[0].lower() + " = cpu->z280.ctl[GETLO(cpu->bc)];", file = file)
						elif form.args[0] == 'USP':
							print(indent + "\tz280_sync_registers(cpu);", file = file)
							print(indent + "\tcpu->z280.usp = cpu->" + form.args[1].lower() + ";", file = file)
						elif form.args[1] == 'USP':
							print(indent + "\tz280_sync_registers(cpu);", file = file)
							print(indent + "\tcpu->" + form.args[0].lower() + " = cpu->z280.usp;", file = file)
					elif cpu == 'z380':
						if form.args[0] == 'A':
							print(indent + "\tSETHI(cpu->af, cpu->z380.sr " + {'SR': "", 'DSR': " >> 8", 'XSR': " >> 16", 'YSR': " >> 24"}[form.args[1]] + ");", file = file)
						elif form.args[1] == 'A' or form.args[1] == '#':
							if form.args[0] == 'SR':
								if form.args[1] == 'A':
									print(indent + "\tz380_sync_registers(cpu);", file = file)
									print(indent + "\tcpu->z380.sr = (cpu->z380.sr & 0xFF) | (cpu->af & 0xFF00) | ((cpu->af & 0xFF00) << 8) | ((cpu->af & 0xFF00) << 16);", file = file)
									print(indent + "\tz380_reload_registers(cpu);", file = file)
								else:
									print(indent + "\ti &= 0xFF;", file = file)
									print(indent + "\tz380_sync_registers(cpu);", file = file)
									print(indent + "\tcpu->z380.sr = (cpu->z380.sr & 0xFF) | (i << 8) | (i << 16) | (i << 24);", file = file)
									print(indent + "\tz380_reload_registers(cpu);", file = file)
							else:
								shift = {'DSR': "8", 'XSR': "16", 'YSR': "24"}[form.args[0]]
								print(indent + "\tz380_sync_registers(cpu);", file = file)
								print(indent + "\tcpu->z380.sr = (cpu->z380.sr & ~(0xFF << " + shift + ")) | (GETHI(cpu->af) << " + shift + ");", file = file)
								print(indent + "\tz380_reload_registers(cpu);", file = file)
						elif form.args[0] == 'HL':
							# LDCTL HL, SR
							print(indent + "\t" + writewordarg('OPNDSIZE', form.args[0], "cpu->z380.sr") + ";", file = file)
						else:
							# LDCTL SR, HL
							print(indent + "\tz380_sync_registers(cpu);", file = file)
							print(indent + "\tif(WORDSIZE == 2)", file = file)
							print(indent + "\t\tcpu->z380.sr = (cpu->hl & 0xFFFF) | ((cpu->hl & 0xFF00) << 8) | ((cpu->hl & 0xFF00) << 16);", file = file)
							print(indent + "\telse", file = file)
							print(indent + "\t\tcpu->z380.sr = cpu->hl;", file = file)
							print(indent + "\tz380_reload_registers(cpu);", file = file)
				elif form.mnem in {'LDD', 'LDDR', 'LDI', 'LDIR', \
						'LDDW', 'LDDRW', 'LDIW', 'LDIRW'}:
					if cpu == 'sm83':
						assert form.mnem in {'LDD', 'LDI'}
						print(indent + "\tx = " + readbytearg(form.args[1]) + ";", file = file)
						print(indent + "\t" + writebytearg(form.args[0], "x") + ";", file = file)
						print(indent + "\tz = " + {'I': 'incword', 'D': 'decword'}[form.mnem[2]] + "(cpu, " + readwordarg('ADDRSIZE', 'HL') + ", 1);", file = file)
						print(indent + "\t" + writewordarg('ADDRSIZE', 'HL', "z") + ";", file = file)
					else:
						if 'W' in form.mnem:
							print(indent + "\tx = " + readwordarg('OPNDSIZE', '(HL)') + ";", file = file)
							print(indent + "\t" + writewordarg('OPNDSIZE', '(DE)', "x") + ";", file = file)
						else:
							print(indent + "\tx = " + readbytearg('(HL)') + ";", file = file)
							print(indent + "\t" + writebytearg('(DE)', "x") + ";", file = file)
						print(indent + "\ttestldstring(cpu, x);", file = file)
						if 'W' in form.mnem:
							print(indent + "\tSETWORDL(cpu->hl, cpu->hl " + {'I': "+", 'D': "-"}[form.mnem[2]] + " OPNDSIZE);", file = file)
							print(indent + "\tSETWORDL(cpu->de, cpu->de " + {'I': "+", 'D': "-"}[form.mnem[2]] + " OPNDSIZE);", file = file)
							print(indent + "\tSETWORDW(cpu->bc, cpu->bc - OPNDSIZE);", file = file)
						else:
							print(indent + "\tSETWORDL(cpu->hl, cpu->hl " + {'I': "+", 'D': "-"}[form.mnem[2]] + " 1);", file = file)
							print(indent + "\tSETWORDL(cpu->de, cpu->de " + {'I': "+", 'D': "-"}[form.mnem[2]] + " 1);", file = file)
							print(indent + "\tSETWORDW(cpu->bc, cpu->bc - 1);", file = file)
						print(indent + "\tif(GETWORDL(cpu->bc) != 0)", file = file)
						print(indent + "\t\tcpu->af |= F_V;", file = file)
						if form.mnem.endswith('R'):
							print(indent + "\tif((cpu->af & F_V))", file = file)
							print(indent + "\t{", file = file)
							print(indent + "\t\tSETADDR(cpu->pc, cpu->pc - 2);", file = file)
							if 'z80' in subcpus:
								if subcpus != ['z80'] and use_cpu is None:
									print("#if CPU_Z80", file = file)
								if use_cpu in {None, 'z80'}:
									print(indent + "\t\tcpu->z80.wz = cpu->pc + 1;", file = file)
								if subcpus != ['z80'] and use_cpu is None:
									print("#endif", file = file)
							print(indent + "\t}", file = file)
				elif form.mnem in {'LDUD', 'LDUP'}:
					if form.args[0] == 'A':
						print(indent + "\tz = cpu->read_byte(cpu, getuseraddress(cpu, " + MEMARGS[form.args[1]] + ", " + {'D': 'X80_ACCESS_TYPE_READ', 'P': 'X80_ACCESS_TYPE_FETCH'}[form.mnem[-1]] + "));", file = file)
						print(indent + "\t" + writebytearg('A', "z") + ";", file = file)
					else:
						print(indent + "\tcpu->write_byte(cpu, getuseraddress(cpu, " + MEMARGS[form.args[0]] + ", " + {'D': 'X80_ACCESS_TYPE_READ', 'P': 'X80_ACCESS_TYPE_FETCH'}[form.mnem[-1]] + "), " + readbytearg('A') + ");", file = file)
				elif form.mnem == 'LEA':
					print(indent + "\tz = " + MEMARGS['(' + form.args[1] + ')'] + ";", file = file)
					print(indent + "\t" + writewordarg('OPNDSIZE', form.args[0], "z") + ";", file = file)
				elif form.mnem == 'MLT':
					print(indent + "\tSETSHORT(" + VARARGS[form.args[0]] + ", (uint8_t)GETHI(" + VARARGS[form.args[0]] + ") * (uint8_t)GETLO(" + VARARGS[form.args[0]] + "));", file = file)
				elif form.mnem == 'MTEST':
					print(indent + "\tCPYBIT(cpu->af, F_C, cpu->z380.sr & X80_Z380_SR_LCK);", file = file)
					print(indent + "\tCPYBIT(cpu->af, F_Z, cpu->z380.sr & X80_Z380_SR_LW);", file = file)
					print(indent + "\tCPYBIT(cpu->af, F_S, cpu->z380.sr & X80_Z380_SR_XM);", file = file)
				elif form.mnem in {'MULTUW', 'MULTW', 'MULUW'}:
					# MULUW is r800
					print(indent + "\tx = " + readwordarg('WORDSIZE', 'HL') + ";", file = file)
					print(indent + "\ty = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
					print(indent + "\tz = " + {'MULTUW': "umulword", 'MULTW': "smulword", 'MULUW': "umulword"}[form.mnem] + "(cpu, x, y);", file = file)
					if cpu == 'z280':
						print(indent + "\t" + writewordarg('WORDSIZE', 'HL', "z") + ";", file = file)
						print(indent + "\t" + writewordarg('WORDSIZE', 'DE', "z >> 16") + ";", file = file)
					else:
						print(indent + "\tcpu->hl = z;", file = file)
				elif form.mnem in {'MULTU', 'MULT', 'MULUB'}:
					# MULUB is r800
					print(indent + "\tx = " + readbytearg(form.args[0]) + ";", file = file)
					print(indent + "\ty = " + readbytearg(form.args[1]) + ";", file = file)
					print(indent + "\tz = " + {'MULTU': "umulbyte", 'MULT': "smulbyte", 'MULUB': "umulbyte"}[form.mnem] + "(cpu, x, y);", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', 'HL', "z") + ";", file = file)
				elif form.mnem == 'NEG':
					if form.args == ['HL']:
						# z280
						print(indent + "\tcpu->hl = subword(cpu, WORDSIZE, 0, cpu->hl);", file = file)
					else:
						print(indent + "\tx = GETHI(cpu->af);", file = file)
						print(indent + "\tx = subbyte(cpu, 0, x);", file = file)
						print(indent + "\tSETHI(cpu->af, x);", file = file)
				elif form.mnem == 'NEGW':
					# z380
					print(indent + "\tz = negword(cpu, cpu->hl);", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', 'HL', "z") + ";", file = file)
				elif form.mnem == 'OUT':
					print(indent + "\tx = " + readbytearg(form.args[1]) + ";", file = file)
					if form.args[0] == '(C)':
						print(indent + "\toutputbyte(cpu, cpu->bc, x);", file = file)
					else:
						print(indent + "\ty = (" + readbytearg('A') + "<< 8)|d;", file = file)
						print(indent + "\toutputbyte(cpu, y, x);", file = file)
					if 'z80' in subcpus:
						if subcpus != ['z80'] and use_cpu is None:
							print("#if CPU_Z80", file = file)
						if use_cpu in {None, 'z80'}:
							if form.args[0] == '(C)':
								print(indent + "\tcpu->z80.wz = cpu->bc + 1;", file = file)
							else:
								print(indent + "\tcpu->z80.wz = ((d + 1) & 0xFF) | (GETHI(cpu->af) << 8);", file = file)
						if subcpus != ['z80'] and use_cpu is None:
							print("#endif", file = file)
				elif form.mnem == 'OUT0':
					print(indent + "\tx = " + readbytearg(form.args[1]) + ";", file = file)
					if cpu != 'z380':
						print(indent + "\toutputbyte(cpu, d, x);", file = file)
					else:
						print(indent + "\toutputbyte0(cpu, d, x);", file = file)
				elif form.mnem == 'OUTW':
					print(indent + "\tx = " + readwordarg('WORDSIZE', form.args[1]) + ";", file = file)
					print(indent + "\toutputword(cpu, cpu->bc, x);", file = file)
				elif form.mnem == 'OUTA':
					print(indent + "\toutputbyte(cpu, d, " + readbytearg(form.args[1]) + ");", file = file)
				elif form.mnem == 'OUTAW':
					print(indent + "\toutputword(cpu, d, " + readwordarg('WORDSIZE', form.args[1]) + ");", file = file)
				elif form.mnem in {'OUTD', 'OTDR', 'OUTI', 'OTIR', \
						'OUTD2', 'OTD2R', 'OUTI2', 'OTI2R', \
						'OTDM', 'OTDMR', 'OTIM', 'OTIMR', \
						'OTDRX', 'OTIRX'}:
					direction = 'I' if 'I' in form.mnem else 'D'
					print(indent + "\tx = readbyte(cpu, cpu->hl);", file = file)
					if 'M' in form.mnem:
						print(indent + "\toutputbyte(cpu, GETLO(cpu->bc), x);", file = file)
					elif 'X' in form.mnem or form.mnem.endswith('2R'):
						print(indent + "\toutputbyte(cpu, cpu->de, x);", file = file)
					else:
						print(indent + "\toutputbyte(cpu, cpu->bc, x);", file = file)
					print(indent + "\tSETWORDL(cpu->hl, cpu->hl " + {'I': "+", 'D': "-"}[direction] + " 1);", file = file)
					if 'X' in form.mnem or form.mnem.endswith('2R'):
						print(indent + "\tz = " + readwordarg('WORDSIZE', 'BC') + " - 1;", file = file)
						print(indent + "\t" + writewordarg('WORDSIZE', 'BC', "z") + ";", file = file)
						if form.mnem.endswith('2R'):
							print(indent + "\tz = " + readwordarg('WORDSIZE', 'DE') + {'I': "+", 'D': "-"}[direction] + " 1;", file = file)
							print(indent + "\t" + writewordarg('WORDSIZE', 'DE', "z") + ";", file = file)
						print(indent + "\ttestoutstring(cpu, " + readwordarg('WORDSIZE', 'BC') + ", x);", file = file)
					else:
						print(indent + "\tz = " + readbytearg('B') + " - 1;", file = file)
						print(indent + "\t" + writebytearg('B', "z") + ";", file = file)
						if '2' in form.mnem or 'M' in form.mnem:
							print(indent + "\tz = " + readbytearg('C') + {'I': "+", 'D': "-"}[direction] + " 1;", file = file)
							print(indent + "\t" + writebytearg('C', "z") + ";", file = file)
						print(indent + "\ttestoutstring(cpu, " + readbytearg('B') + ", x);", file = file)
					if form.mnem.endswith('R') or form.mnem.endswith('RX'):
						print(indent + "\tif(!(cpu->af & F_Z))", file = file)
						print(indent + "\t\tSETADDR(cpu->pc, cpu->pc - 2);", file = file)
					if 'z80' in subcpus:
						if subcpus != ['z80'] and use_cpu is None:
							print("#if CPU_Z80", file = file)
						if use_cpu in {None, 'z80'}:
							print(indent + "\tcpu->z80.wz = cpu->bc " + {'I': "+", 'D': "-"}[direction] + " 1;", file = file)
						if subcpus != ['z80'] and use_cpu is None:
							print("#endif", file = file)
				elif form.mnem in {'OUTDW', 'OTDRW', 'OUTIW', 'OTIRW'}:
					direction = 'I' if 'I' in form.mnem else 'D'
					print(indent + "\tx = readword(cpu, WORDSIZE, cpu->hl);", file = file)
					print(indent + "\toutputword(cpu, cpu->de, x);", file = file)
					print(indent + "\tSETWORDL(cpu->hl, cpu->hl " + {'I': "+", 'D': "-"}[direction] + " 2);", file = file)
					print(indent + "\tSETWORDW(cpu->bc, cpu->bc - 1);", file = file)
					print(indent + "\ttestoutstring(cpu, GETWORDW(cpu->bc), x);", file = file)
					print(indent + "\tcpu->af |= F_N;", file = file)
					if form.mnem.endswith('R'):
						print(indent + "\tif(!(cpu->af & F_Z))", file = file)
						print(indent + "\t\tSETADDR(cpu->pc, cpu->pc - 2);", file = file)
				elif form.mnem == 'PCACHE':
					print(indent + "\t/* not implemented, NOP */", file = file)
				elif form.mnem == 'PEA':
					print(indent + "\tx = " + MEMARGS['(' + form.args[0] + ')'] + ";", file = file)
					print(indent + "\tpushword(cpu, OPNDSIZE, x);", file = file)
				elif form.mnem == 'POP':
					if len(form.args) == 0:
						pass # TODO: dp22
					else:
						print(indent + "\tx = popword(cpu, OPNDSIZE);", file = file)
						if form.args[0] == 'AF':
							print(indent + "\tcpu->af = (GETSHORT(x) & AF_MASK) | AF_ONES;", file = file)
						elif form.args[0] == 'AF':
							print(indent + "\tif(OPNDSIZE == 2)", file = file)
							print(indent + "\t\tcpu->z380.sr = (cpu->z380.sr & SR_XM) | (x & (0xFFFF & ~SR_XM)) | ((x & 0xFF00) << 8) | ((x & 0xFF00) << 16);", file = file)
							print(indent + "\telse", file = file)
							print(indent + "\t\tcpu->z380.sr = (cpu->z380.sr & SR_XM) | (x & ~SR_XM);", file = file)
						else:
							print(indent + "\t" + writewordarg('OPNDSIZE', form.args[0], "x") + ";", file = file)
				elif form.mnem == 'PUSH':
					if len(form.args) == 0:
						pass # TODO: dp22
					elif form.args[0] == 'AF':
						print(indent + "\tx = GETSHORT(cpu->af);", file = file)
					else:
						print(indent + "\tx = " + readwordarg('OPNDSIZE', form.args[0]) + ";", file = file)
					print(indent + "\tpushword(cpu, OPNDSIZE, x);", file = file)
				elif form.mnem in {'RES', 'SET'}:
					print(indent + "\tx = " + readbytearg(form.args[1]) + {'RES': " & ~(", 'SET': " | ("}[form.mnem] + "1 << " + form.args[0] + ");", file = file)
					print(indent + "\t" + writebytearg(form.args[1], "x") + ";", file = file)
					if len(form.args) == 3:
						print(indent + "\t" + writebytearg(form.args[2], "x") + ";", file = file)
				elif form.mnem in {'RESC', 'SETC'}:
					print(indent + "\tcpu->z380.sr " + {'RESC': "&= ~", 'SETC': "|= "}[form.mnem] + "X80_Z380_SR_" + form.args[0] + ";", file = file)
				elif form.mnem in {'RET', 'RETI', 'RETN'}:
					if len(form.args) == 0:
						if form.mnem == 'RETN':
							print(indent + "\tdo_before_retn(cpu);", file = file)
						if form.mnem == 'RETI':
							if cpu == 'sm83':
								print(indent + "\tenable_interrupts(cpu, INT_DEFAULT);", file = file)
							else:
								pass # TODO: RETI
						print(indent + "\tdo_ret(cpu);", file = file) # deal with cpu->z80.wz
					else:
						# conditional
						print(indent + "\tif(" + CONDITIONS[form.args[0]] + ")", file = file)
						print(indent + "\t\tdo_ret(cpu);", file = file) # deal with cpu->z80.wz
				elif form.mnem == 'RETB':
					print(indent + "\tcpu->pc = cpu->z380.spc;", file = file)
				elif form.mnem == 'RETIL':
					print(indent + "\tdo_retil(cpu);", file = file)
				elif form.mnem in {'RLA', 'RRA', 'RLCA', 'RRCA'}:
					print(indent + "\tx = " + readbytearg('A') + ";", file = file)
					if form.mnem == 'RLA':
						print(indent + "\ty = (x << 1) | (cpu->af & F_C ? 1 : 0);", file = file)
						print(indent + "\tpostrotateaccleft(cpu, x, y);", file = file)
						print(indent + "\tif((x & 0x80))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'RLCA':
						print(indent + "\ty = (x << 1) | (x >> 7);", file = file)
						print(indent + "\tpostrotateaccleft(cpu, x, y);", file = file)
						print(indent + "\tif((x & 0x80))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'RRA':
						print(indent + "\ty = (x >> 1) | ((cpu->af & F_C ? 1 : 0) << 7);", file = file)
						print(indent + "\tpostrotateaccright(cpu, x, y);", file = file)
						print(indent + "\tif((x & 0x01))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'RRCA':
						print(indent + "\ty = (x >> 1) | (x << 7);", file = file)
						print(indent + "\tpostrotateaccright(cpu, x, y);", file = file)
						print(indent + "\tif((x & 0x01))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					print(indent + "\t" + writebytearg('A', "y") + ";", file = file)
				elif form.mnem in {'RL', 'RR', 'RLC', 'RRC', 'SLA', 'SL1', 'SRA', 'SRL', 'SHL[A]'}:
					print(indent + "\tx = " + readbytearg(form.args[0]) + ";", file = file)
					if form.mnem == 'RL':
						print(indent + "\ty = (x << 1) | (cpu->af & F_C);", file = file)
						print(indent + "\tpostrotatebyte(cpu, y);", file = file)
						print(indent + "\tif((x & 0x80))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'RLC':
						print(indent + "\ty = (x << 1) | (x >> 7);", file = file)
						print(indent + "\tpostrotatebyte(cpu, y);", file = file)
						print(indent + "\tif((x & 0x80))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'RR':
						print(indent + "\ty = (x >> 1) | ((cpu->af & F_C) << 7);", file = file)
						print(indent + "\tpostrotatebyte(cpu, y);", file = file)
						print(indent + "\tif((x & 0x01))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'RRC':
						print(indent + "\ty = (x >> 1) | (x << 7);", file = file)
						print(indent + "\tpostrotatebyte(cpu, y);", file = file)
						print(indent + "\tif((x & 0x01))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'SLA' or form.mnem == 'SHL[A]': # r800
						print(indent + "\ty = x << 1;", file = file)
						print(indent + "\tpostrotatebyte(cpu, y);", file = file)
						print(indent + "\tif((x & 0x80))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'SL1':
						print(indent + "\ty = (x << 1) | 1;", file = file)
						print(indent + "\tpostrotatebyte(cpu, y);", file = file)
						print(indent + "\tif((x & 0x80))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'SRA':
						print(indent + "\ty = (x >> 1) | (x & 0x80);", file = file)
						print(indent + "\tpostrotatebyte(cpu, y);", file = file)
						print(indent + "\tif((x & 0x01))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'SRL':
						print(indent + "\ty = (x >> 1);", file = file)
						print(indent + "\tpostrotatebyte(cpu, y);", file = file)
						print(indent + "\tif((x & 0x01))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					print(indent + "\t" + writebytearg(form.args[0], "y") + ";", file = file)
					if len(form.args) == 2:
						print(indent + "\t" + writebytearg(form.args[1], "y") + ";", file = file)
				elif form.mnem in {'RLW', 'RRW', 'RLCW', 'RRCW', 'SLAW', 'SRAW', 'SRLW'}:
					print(indent + "\tx = " + readwordarg('WORDSIZE', form.args[0]) + ";", file = file)
					if form.mnem == 'RLW':
						print(indent + "\ty = (x << 1) | (cpu->af & F_C);", file = file)
						print(indent + "\tpostrotateword(cpu, y);", file = file)
						print(indent + "\tif((x & 0x8000))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'RLCW':
						print(indent + "\ty = (x << 1) | (x >> 15);", file = file)
						print(indent + "\tpostrotateword(cpu, y);", file = file)
						print(indent + "\tif((x & 0x8000))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'RRW':
						print(indent + "\ty = (x >> 1) | ((cpu->af & F_C) << 15);", file = file)
						print(indent + "\tpostrotateword(cpu, y);", file = file)
						print(indent + "\tif((x & 0x0001))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'RRCW':
						print(indent + "\ty = (x >> 1) | (x << 15);", file = file)
						print(indent + "\tpostrotateword(cpu, y);", file = file)
						print(indent + "\tif((x & 0x01))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'SLAW':
						print(indent + "\ty = x << 1;", file = file)
						print(indent + "\tpostrotateword(cpu, y);", file = file)
						print(indent + "\tif((x & 0x8000))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'SRAW':
						print(indent + "\ty = (x >> 1) | (x & 0x8000);", file = file)
						print(indent + "\tpostrotateword(cpu, y);", file = file)
						print(indent + "\tif((x & 0x0001))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					elif form.mnem == 'SRLW':
						print(indent + "\ty = (x >> 1);", file = file)
						print(indent + "\tpostrotateword(cpu, y);", file = file)
						print(indent + "\tif((x & 0x0001))", file = file)
						print(indent + "\t\tcpu->af |= F_C;", file = file)
					print(indent + "\t" + writewordarg('WORDSIZE', form.args[0], "y") + ";", file = file)
				elif form.mnem == 'ARHL':
					# 8085
					print(indent + "\tcpu->af = (cpu->af & ~0x01) | (cpu->hl & 0x01);", file = file)
					print(indent + "\tcpu->hl = (cpu->hl & 0x8000) | (cpu->hl >> 1);", file = file)
				elif form.mnem == 'RDEL':
					# 8085
					print(indent + "\tx = cpu->de;", file = file)
					print(indent + "\tcpu->de = (cpu->de << 1) | (cpu->af & F_C);", file = file)
					print(indent + "\tcpu->af = (cpu->af & ~0x03) | ((x >> 15) & 1);", file = file)
					print(indent + "\tif(((x ^ cpu->de) & 0x8000))", file = file)
					print(indent + "\t\tcpu->af |= F_V;", file = file)
				elif form.mnem in {'RLD', 'RRD'}:
					print(indent + "\tx = readbyte(cpu, cpu->hl);", file = file)
					if form.mnem == 'RLD':
						print(indent + "\ty = (x << 4) | (GETHI(cpu->af) & 0x0F);", file = file)
						print(indent + "\tx = (GETHI(cpu->af) & 0xF0) | (x >> 4);", file = file)
						print(indent + "\twritebyte(cpu, cpu->hl, y);", file = file)
					elif form.mnem == 'RRD':
						print(indent + "\ty = (x >> 4) | (GETHI(cpu->af) << 4);", file = file)
						print(indent + "\twritebyte(cpu, cpu->hl, y);", file = file)
						print(indent + "\tx = (GETHI(cpu->af) & 0xF0) | (x & 0x0F);", file = file)
					if use_cpu is None:
						print("#if CPU_Z80", file = file)
					if use_cpu in {None, 'z80'}:
						print(indent + "\tcpu->z80.wz = cpu->hl + 1;", file = file)
					if use_cpu is None:
						print("#endif", file = file)
						print("#if CPU_Z180", file = file)
					print(indent + "\ttestrlx(cpu, y);", file = file)
					if use_cpu is None:
						print("#else", file = file)
					print(indent + "\ttestrlx(cpu, x);", file = file)
					if use_cpu is None:
						print("#endif", file = file)
					print(indent + "\tSETHI(cpu->af, x);", file = file)
				elif form.mnem in {'RSMIX', 'STMIX'}:
					print(indent + "\tcpu->ez80.madl = " + {'RSMIX': '0', 'STMIX': '1'}[form.mnem] + ";", file = file)
				elif form.mnem == 'RST':
					print(indent + "\tdo_call(cpu, 0x00" + form.args[0][:-1] + ");", file = file) # deal with cpu->z80.wz
				elif form.mnem == 'RSTV':
					# 8085
					print(indent + "\tif((cpu->af & F_V))", file = file)
					print(indent + "\t\tdo_call(cpu, 0x0040);", file = file)
				elif form.mnem == 'SC':
					print(indent + "\texception_trap(cpu, X80_Z280_TRAP_SC, (uint16_t)i);", file = file)
				elif form.mnem == 'SCF':
					print(indent + "\tdo_scf(cpu);", file = file)
				elif form.mnem == 'SLP':
					print(indent + "\t/* TODO: not implemented */;", file = file)
				elif form.mnem == 'SWAP':
					if cpu == 'sm83':
						print(indent + "\tx = " + readbytearg(form.args[0]) + ";", file = file)
						print(indent + "\t" + writebytearg(form.args[0], "swapnibbles(cpu, x)") + ";", file = file)
					else:
						print(indent + "\tx = " + VARARGS[form.args[0]] + ";", file = file)
						print(indent + "\t" + VARARGS[form.args[0]] + " = (x << 16) | (x >> 16);", file = file)
				elif form.mnem == 'TSET':
					print(indent + "\tx = " + readbytearg(form.args[0]) + ";", file = file)
					print(indent + "\tCPYBIT(cpu->af, F_S, x & 0x80);", file = file)
					print(indent + "\t" + writebytearg(form.args[0], "0xFF") + ";", file = file)
				elif form.mnem == 'TST':
					print(indent + "\tx = " + readbytearg(form.args[0]) + ";", file = file)
					print(indent + "\t(void)andbyte(cpu, GETHI(cpu->af), x);", file = file)
				elif form.mnem == 'TSTIO':
					print(indent + "\tx = inputbyte(cpu, GETLO(cpu->bc));", file = file)
					print(indent + "\t(void)andbyte(cpu, x, i);", file = file)
				elif form.mnem in {'LDHI', 'LDSI'}:
					# 8085
					print(indent + "\tcpu->de = cpu->" + {'H': 'hl', 'S': 'sp'}[form.mnem[2]] + " + i;", file = file)
				elif form.mnem == 'SHLX':
					# 8085
					print(indent + "\twriteword(cpu, 2, cpu->de, cpu->hl);", file = file)
				elif form.mnem == 'LHLX':
					# 8085
					print(indent + "\tcpu->hl = readword(cpu, 2, cpu->de);", file = file)
				elif form.mnem == 'RIM':
					# 8085
					print(indent + "\tSETHI(cpu->af, read_interrupt_mask(cpu));", file = file)
				elif form.mnem == 'SIM':
					# 8085
					print(indent + "\twrite_interrupt_mask(cpu, GETHI(cpu->af));", file = file)
				elif form.mnem in {'MB SMF0', 'MB SMF1'}:
					# vm1
					if form.mnem[-1] == '0':
						print(indent + "\tcpu->af &= ~F_MF;", file = file)
					elif form.mnem[-1] == '1':
						print(indent + "\tcpu->af |= F_MF;", file = file)
				else:
					print("#warning Unimplemented: " + form.mnem, file = file)
					print(indent + "\tUNIMPLEMENTED();", file = file)
					#print(form.mnem, file = sys.stderr)
					#print(form.args, file = sys.stderr)
					unimps.add(form.mnem)

				if len(subcpus) == len(cpus):
					break # done with all the cpus
				# TODO: string
			# TODO: undefined
			if len(donecpus) != 0 and use_cpu is None:
				print("#endif", file = file)
			print(indent + 'break;', file = file)

	if SEPARATE_CPUS:
		for cpu in cpus:
			with open(sys.argv[3].replace('.gen.', '.' + cpu + '.'), 'w') as fp:
				print_cases('', use_cpu = cpu, file = fp)
		with open(sys.argv[3], 'w') as fp:
			for index, cpu in enumerate(cpus):
				fn = os.path.basename(sys.argv[3].replace('.gen.', '.' + cpu + '.'))
				if index == 0:
					print(f"#if CPU_{get_cpu_macro(cpu)}", file = fp)
				else:
					print(f"#elif CPU_{get_cpu_macro(cpu)}", file = fp)
				print(f'# include "{fn}"', file = fp)
			print("#else", file = fp)
			print("# error Unknown CPU", file = fp)
			print("#endif", file = fp)
	else:
		with open(sys.argv[3], 'w') as fp:
			print_cases('', file = fp)

	#print(', '.join(sorted(unimps)), file = sys.stderr)

