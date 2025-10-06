#! /usr/bin/python3

import os
import sys

class InsForm(object):
	def __init__(self, mnem, *args):
		self.mnem = mnem
		self.args = list(args)

	def __hash__(self):
		return hash((self.mnem, tuple(self.args)))

	def __eq__(self, other):
		return type(self) == type(other) and self.mnem == other.mnem and self.args == other.args

	def __repr__(self):
		return self.mnem + (" " + ",".join(self.args) if len(self.args) > 0 else "")

class Instruction(object):
	def __init__(self, *forms, ddir = None, base = None, style = None, duplicate = None, unprefixed = None):
		if duplicate is not None:
			assert style is None or style == 'duplicate'
			style = 'duplicate'
		self.forms = list(forms)
		self.ddir = ddir
		self.base = base
		self.style = style
		self.duplicate = duplicate
		self.unprefixed = unprefixed # used for VM1 28/38 prefixes

	def __repr__(self):
		return repr(self.forms[0])

	def __eq__(self, other):
		return type(self) == type(other) and self.base == other.base

	def __ne__(self, other):
		return not (self == other)

def ilogb(value):
	result = -1
	while value > 0:
		result += 1
		value >>= 1
	return result

def get(value, empty = None):
	if value == '-':
		return empty
	elif value.startswith('"'):
		return value[1:-1]
	else:
		return value

def replacestring(item, replaces, index = 0):
	if index >= len(replaces):
		yield item
	elif replaces[index][0] in item:
		for value in replaces[index][1]:
			for result in replacestring(item.replace(replaces[index][0], value), replaces, index + 1):
				yield result
	else:
		for result in replacestring(item, replaces, index + 1):
			yield result

def replace(item, replaces):
	if item in replaces:
		for elem in replaces[item]:
			yield elem
	else:
		yield item

def replacelist0(ls, replaces, result, index = 0):
	if index >= len(ls):
		yield result
	else:
		for item_options in replace(ls[index], replaces):
			for item in item_options.split('|'):
				for result0 in replacelist0(ls, replaces, result + [item], index + 1):
					yield result0

def replacelist(ls, replaces):
	return replacelist0(ls, replaces, [])

#GENERATE_EMULATOR = sys.argv[1] == 'emu'

T = {}

state = None

# $ARCHITECTURES

CPUS = []
CPUNAMES = {}
PREDECESSOR = {}
PREDECESSORS = {}
SYNTAXPREDECESSOR = {}

# $ENCODINGS

LAYOUTS = {}

current_operand = None
current_encodings = None
current_heading = None
current_branches = None

# $INSTRUCTIONS

opset = set()

# $DESCRIPTIONS

CPUSEQUENCE = []
DESCRIPTIONS = {}
REFERENCES = {} # links from references to (actual href, name) pairs
current_cpu = None

def load_database(fp):
	global T, state
	# $ARCHITECTURES
	global CPUS, CPUNAMES, PREDECESSOR, PREDECESSORS, SYNTAXPREDECESSOR
	# $ENCODINGS
	global LAYOUTS, current_operand, current_encodings, current_heading, current_branches
	# $INSTRUCTIONS
	global opset
	# $DESCRIPTIONS
	global CPUSEQUENCE, DESCRIPTIONS, REFERENCES, current_cpu

	line = ''
	for line0 in fp:
		#z80||ED|10 001 aaa|ADC|A, op8(14/24)|

		if '//' in line0:
			line0 = line0[:line0.find('//')]

		line0 = line0.strip()

		if line.endswith('\\'):
			line = line[:-1].strip() + '\t' + line0
		else:
			line = line0

		if line.endswith('\\'):
			continue

		if line.startswith('$'):
			state = line
			continue
		elif state is None or line == '':
			continue

		if state == '$ARCHITECTURES':
			parts = line.split('\t')
			#z180|z80|Hitachi HD64180, Zilog Z180
			designation = parts.pop(0)
			assert designation not in CPUS
			CPUS.append(designation)
			predecessor = parts.pop(0)
			syntactic = not predecessor.startswith('~')
			if not syntactic:
				predecessor = predecessor[1:]
			PREDECESSORS[designation] = {designation}
			if predecessor != '-':
				assert predecessor in CPUS
				PREDECESSOR[designation] = predecessor
				PREDECESSORS[designation].update(PREDECESSORS[predecessor])
				if syntactic:
					SYNTAXPREDECESSOR[designation] = predecessor
			T[designation] = {}
			name = parts.pop(0)
			CPUNAMES[designation] = name

		elif state == '$ENCODINGS':

			if line.startswith('='):
				if current_operand is not None:
					for opd in current_operand:
						assert opd not in LAYOUTS
						if current_encodings is not None:
							LAYOUTS[opd] = current_encodings
							#print(opd, current_encodings, file = sys.stderr)
						else:
							LAYOUTS[opd] = current_branches
							#print(opd, current_branches, file = sys.stderr)
				current_operand = [part.strip() for part in line[1:].split(',')]
				current_encodings = []
				current_heading = None
				current_branches = None
			else:
				#6|(HL)|(IX+#~)|(IY+#~)
				parts = line.split('\t')
				index = parts.pop(0)
				if index == 'for':
					assert len(current_encodings) == 0
					assert current_heading is None
					current_heading = [get(part, '') for part in parts]
					current_branches = None
					continue
				elif index == 'if':
					current_encodings = None
					if current_branches is None:
						current_branches = {}
					case = get(parts.pop(0), '')
					goto = parts.pop(0)
					current_branches[case] = goto
					continue
				if index != '-':
					#print(line, current_encodings, file = sys.stderr)
					assert index == str(len(current_encodings))
				if parts[1:].count('-') == len(parts) - 1:
					# single case
					value = get(parts.pop(0))
					current_encodings.append(value)
				elif current_heading is None:
					assert len(parts) == 1
					value = get(parts.pop(0))
					current_encodings.append(value)
				else:
					assert len(parts) == len(current_heading)
					parts = list(map(get, parts))
					current_encodings.append(dict(zip(current_heading, parts)))

		elif state == '$INSTRUCTIONS':
			# parse line:

			# <cpu> <attributes> <prefix> <bit layout> <instructions>

			parts = line.split('\t')
			basecpu = parts.pop(0)
			overwrite = basecpu.startswith('^')
			if overwrite:
				# a prefixed ^ means that it can overwrite the instruction in the base instruction set
				basecpu = basecpu[1:]
			only = basecpu.startswith('!')
			if only:
				# a prefixed ! means that it doesn't get inherited by instruction sets based on this
				basecpu = basecpu[1:]

			# attributes are called DDIR because they are prominently used on the Z380 CPU
			ddir = parts.pop(0)

			# the '*' is just a reminder that it can have other prefixes too, it isn't used by this script
			opprefix = parts.pop(0).replace('*', '').strip()

			encode = parts.pop(0).replace(' ', '')
			syntaxes = {}
			exclude = ''
			last_syntax = None
			while len(parts) > 0:
				if parts[0].startswith('!'):
					exclude = parts.pop(0)
					break
				elif parts[0].endswith(':'):
					syntaxls = [syntax.strip() for syntax in parts.pop(0)[:-1].split(',')]
					while parts[0] == '':
						parts.pop(0)
				else:
					syntaxls = ['']
				mnem = parts.pop(0)
				if len(parts) > 0 and not parts[0].startswith('!') and not parts[0].endswith(':'):
					ops = parts.pop(0)
				else:
					ops = ''
				if syntaxls == ['+']:
					for syntax in last_syntax:
						syntaxes[syntax].append((mnem, ops))
				else:
					for syntax in syntaxls:
						syntaxes[syntax] = [(mnem, ops)]
					last_syntax = syntaxls
			if not exclude.startswith('!'):
				exclude = None
			else:
				output = []
				for rule in exclude[1:].split(','):
					ix = rule.find('>')
					if ix != -1:
						prefix = rule[:ix]
						rule = rule[ix+1:]
					else:
						prefix = None
					ix = rule.find('=')
					if ix != -1:
						symbol = rule[:ix]
						try:
							value = int(rule[ix+1:])
						except ValueError:
							value = rule[ix+1:]
					else:
						prefix = rule
						symbol = None
						value = None
					output.append((prefix, symbol, value))
				exclude = output

			for cpu in CPUS:
				if only and cpu != basecpu or basecpu not in PREDECESSORS[cpu]:
					continue
				#(mnem, ops) = (mnem0, ops0) = syntaxes['']
				synt = syntaxes['']
				(mnem0, ops0) = syntaxes[''][0]
				predcpu = cpu
				while True:
					if predcpu in syntaxes:
						#(mnem, ops) = syntaxes[predcpu]
						synt = syntaxes[predcpu]
						break
					elif predcpu in SYNTAXPREDECESSOR:
						predcpu = SYNTAXPREDECESSOR[predcpu]
					else:
						break
				#if mnem.startswith('=>'):
				if len(synt) == 1 and synt[0][0].startswith('=>'):
					if opprefix not in T[cpu]:
						T[cpu][opprefix] = [None] * 256
					#T[cpu][opprefix][int(encode, 2)] = mnem[2:]
					T[cpu][opprefix][int(encode, 2)] = synt[0][0][2:]
					continue

				synt = synt[:]
				for index in range(len(synt)):
					synt[index] = (synt[index][0], [op.strip() for op in synt[index][1].split(',')])
				(mnem, ops) = synt[0]
				ops0 = [op.strip() for op in ops0.split(',')]
				ch = ord('a')
				modes = []
				modeset = {}
				for op in ops:
					if op not in LAYOUTS:
						opset.add(op)
					else:
						values0 = values = LAYOUTS[op]
						while type(values) is not list:
							if type(values) is dict:
								currcpu = cpu
								while True:
									if currcpu in values:
										values = values[currcpu]
										break
									currcpu = SYNTAXPREDECESSOR.get(currcpu, '')
							if type(values) is str:
								values = LAYOUTS[values]
						while type(values0) is not list:
							if type(values0) is dict:
								if 'deft' in values0:
									values0 = values0['deft']
								else:
									values0 = values0['']
							if type(values0) is str:
								values0 = LAYOUTS[values0]
						if len(values) != 1:
							template = chr(ch)
							if template not in encode:
								template = chr(ch - 1)
							else:
								ch += 1
							modeset[template] = len(modes)
						else:
							template = None
						modes.append((op, template, values, values0))
				syms = []
				for sym in sorted(set(encode)):
					if sym in {'0', '1'}:
						continue
					syms.append(1 << encode.count(sym))

				iters = []
				values = []
				while True:
					while len(iters) < len(syms):
						_iter = iter(range(syms[len(iters)]))
						values.append(next(_iter))
						iters.append(_iter)

					prefs = None
					for op, template, opts, opts0 in modes:
						if template is None:
							text = opts[0]
						else:
							text = opts[values[ord(template) - ord('a')]]
						if type(text) is dict:
							if prefs is None:
								prefs = set(text.keys())
							else:
								prefs.intersection_update(text.keys())

					if prefs is None:
						prefs = ['']
					else:
						prefs = sorted(prefs)

					if len(prefs) != 0:
						for pref in prefs:
							valid = True
							if exclude is not None:
								for (prefix, symbol, value) in exclude:
									if prefix is not None:
										if pref != prefix:
											continue
									if symbol is not None:
										if type(value) is str:
											assert cpu in {'dp22', 'dp22v2', 'i8'}
											if values[ord(symbol) - ord('a')] == values[ord(value) - ord('a')]:
												valid = False
												break
										elif values[ord(symbol) - ord('a')] == value:
											valid = False
											break
									else:
										valid = False
										break
							if not valid:
								continue

							new_encode = encode
							new_synt = [(mnem, ops[:]) for mnem, ops in synt]

							new_ops0 = ops0[:]
							new_mnem0 = mnem0

							for index, value in enumerate(values):
								char = chr(ord('a') + index)
								length = new_encode.count(char)
								assert length != 0
								if length != 0:
									new_encode = new_encode.replace(char * length, f'{value:0{length}b}')

							for op, template, opts, opts0 in modes:
								if template is None:
									value = 0
								else:
									value = values[ord(template) - ord('a')]
								new_op = opts[value]
								if type(new_op) is dict:
									new_op = new_op[pref]
								new_op0 = opts0[value]
								if type(new_op0) is dict:
									if pref not in new_op0:
										print("Error", new_op0, line)
									new_op0 = new_op0[pref]
								if new_op is None:
									valid = False
									break
								if cpu == 'z380' and new_op in {'IXH', 'IYH'}:
									new_op = new_op.replace('H', 'U')

								if op in new_ops0:
									new_ops0[new_ops0.index(op)] = new_op0
								if op == 'str':
									new_mnem0 = new_mnem0.replace('{U}', ['U', ''][value >> 1]).replace('{I}', ['I', 'D'][value & 1]).replace('{R}', ['', 'R'][value >> 1])

								for v in range(len(new_synt)):
									(new_mnem2, new_ops2) = new_synt[v]
									if cpu == 'z80' and new_op in {'IXH', 'IXL', 'IYH', 'IYL'} and not new_mnem2.startswith('*'):
										new_mnem2 = '*' + new_mnem2
									new_ops2[new_ops2.index(op)] = new_op
									if op == 'str':
										# O{U}T{I}{R}
										if cpu == 'r800':
											new_mnem2 = new_mnem2.replace('{R}', ['', 'M'][value >> 1])
											for index in range(len(new_ops2)):
												new_ops2[index] = new_ops2[index].replace('{I}', ['++', '--'][value & 1])
										else:
											new_mnem2 = new_mnem2.replace('{U}', ['U', ''][value >> 1]).replace('{I}', ['I', 'D'][value & 1]).replace('{R}', ['', 'R'][value >> 1])
									new_synt[v] = (new_mnem2, new_ops2)

							if not valid:
								continue
							if pref != '':
								if opprefix == '':
									new_prefix = pref
								else:
									new_prefix = pref + ' ' + opprefix
							else:
								new_prefix = opprefix

							style = {'*': 'undocumented', '~': 'duplicate', '$': 'fictional', '=': 'updated'}.get(new_synt[0][0][0] if len(new_synt[0][0]) > 0 else '', None)

							for v in range(len(new_synt)):
								(new_mnem2, new_ops2) = new_synt[v]
								while '<' in new_mnem2:
									ix = new_mnem2.find('<')
									new_mnem2 = new_mnem2[:ix] + new_ops2.pop(0) + new_mnem2[ix+1:]

								index = 0
								while index < len(new_ops2):
									if '#~' in new_ops2[index] and cpu != 'z380':
										new_ops2[index] = new_ops2[index].replace('#~', '#')
										index += 1
									else:
										index += 1

								while '' in new_ops2:
									new_ops2.remove('')

								if new_mnem2.startswith('$') and basecpu != cpu:
									# only include fictional marking at CPU of introduction
									new_mnem2 = new_mnem2[1:]

								if len(new_mnem2) > 0 and new_mnem2[0] in {'*', '~', '$', '='}:
									new_mnem2 = new_mnem2[1:]

								new_synt[v] = (new_mnem2, new_ops2)

							while '' in new_ops0:
								new_ops0.remove('')

							if len(new_mnem0) > 0 and new_mnem0[0] in {'*', '~', '$', '='}:
								new_mnem0 = new_mnem0[1:]

							opcode = int(new_encode, 2)
							if cpu in {'z280', 'z380', 'vm1'}:
								new_ddir = set(ddir.split(' '))
								new_ddir.discard('')
								if cpu != 'vm1':
									# remove vm1 specific elements
									new_ddir.discard('MB')
									new_ddir.discard('RS')
									new_ddir.discard('CS')
								if cpu != 'z280':
									# remove z280 specific elements
									new_ddir.discard('P')
									new_ddir.discard('(P)')
								if cpu != 'z380':
									# remove z380 specific elements
									new_ddir.discard('L')
									new_ddir.discard('X')
								if cpu == 'z380':
									for v in range(len(new_synt)):
										for op in new_synt[v][1]:
											if '#~' in op:
												new_ddir.add('I')
												break
											if 'I' in new_ddir:
												break
							else:
								new_ddir = None

							forms = []
							for new_mnem2, new_ops2 in new_synt:
								new_opses = []
								for new_ops1 in replacelist(new_ops2, {'[A]': ['A', ''], '[HL]': ['HL', ''], '[DEHL]': ['DEHL', ''], '([B]C)': ['(BC)', '(C)'], '[F]': ['', 'F']}):
									while '' in new_ops1:
										new_ops1.remove('')
									new_opses.append(new_ops1)

								if True: #GENERATE_EMULATOR:
									for index in range(len(new_ops0)):
										#if cpu != 'r800' and '[' in new_ops0[index]:
										new_ops0[index] = new_ops0[index].replace('[', '').replace(']', '') # TODO: verify that this works
										# [A]
										# [DEHL]
										# [F]
										# [HL]

								# TODO: should depend on the particular element in new_opses, but should be okay
								# theoretical issue: op hl,(hl) vs opw (hl)
								ismem = len(new_opses[0]) > 0 and new_opses[0][0].startswith('(')

								if cpu in {'z280', 'z380'}:
									new_mnems = list(replacestring(new_mnem2, [('[W]', ['W', '']), ('[W*]', ['W'] if ismem else ['W', ''])]))
								elif cpu == 'r800':
									new_mnems = list(replacestring(new_mnem2, [('[A]', ['', 'A'])]))
								elif cpu == 'z80' and '[LI1]' in new_mnem2:
									new_mnems = list(replacestring(new_mnem2, [('[LI1]', ['L', 'I', '1'])]))
								else:
									new_mnems = [new_mnem2.replace('[W]', '').replace('[W*]', '')]

								if True: #GENERATE_EMULATOR:
									new_mnem0 = new_mnem0.replace('[W]', 'W').replace('[W*]', 'W').replace('[LI1]', '1')
									# DEC[W*], INC[W*], LD[W*]
									# IN[W], OUT[W], LD[W]

								for new_mnem3 in new_mnems:
									for new_ops3 in new_opses:
										forms.append(InsForm(new_mnem3, *new_ops3))
										if cpu == 'z280' and new_mnem3 == 'LD' and new_ops3[0] in {'HL', 'IX', 'IY'} and new_ops3[1] == '##':
											# weird z280 specific exception
											forms.append(InsForm('LDA', new_ops3[0], '(##)'))

							result = Instruction(*forms, ddir = new_ddir, base = InsForm(new_mnem0, *new_ops0), style = style)
							if new_prefix not in T[cpu]:
								T[cpu][new_prefix] = [None] * 256
							if (T[cpu][new_prefix][opcode] is None) == overwrite:
								print("Problem:", cpu, new_prefix, f'{opcode:02X}', T[cpu][new_prefix][opcode], result, line, file = sys.stderr)
								if basecpu == cpu:
									# overwrite nonetheless
									T[cpu][new_prefix][opcode] = result
							elif result.forms[0].mnem == '-' and result.forms[0].args == []:
								T[cpu][new_prefix][opcode] = None
							else:
								T[cpu][new_prefix][opcode] = result

					while len(values) > 0:
						try:
							values[-1] = next(iters[-1])
							break
						except StopIteration:
							values.pop()
							iters.pop()
					else:
						break

		elif state == '$DESCRIPTIONS':

			if line.startswith('='):
				CPUSEQUENCE.append(line[1:].strip())
				DESCRIPTIONS[CPUSEQUENCE[-1]] = []

			elif line.startswith('>'):

				DESCRIPTIONS[CPUSEQUENCE[-1]].append(('text', line[1:].strip()))

			else:

				ix = line.find(':')
				key = line[:ix].strip()
				value = line[ix + 1:].strip()

				DESCRIPTIONS[CPUSEQUENCE[-1]].append((key, value))

def complete_database():
	global REFERENCES
	if '28' not in T['vm1']:
		T['vm1']['28'] = [None] * 256
	if '38' not in T['vm1']:
		T['vm1']['38'] = [None] * 256
	if '28 38' not in T['vm1']:
		T['vm1']['28 38'] = [None] * 256

	for cpu in ['r2000', 'r3000', 'r4000', 'r6000']: # TODO: actually check new instructions for r6000
		for prefix in ['76', '76 CB', '76 DD', '76 DD CB', '76 ED', '76 FD', '76 FD CB']:
			if prefix not in T[cpu]:
				T[cpu][prefix] = [None] * 256

	def alter(ls, alters):
		ls = ls[:]
		for key, value in alters.items():
			if key in ls:
				ls[ls.index(key)] = value
		return ls

	# fill up undefined opcodes
	for i in range(256):
		for pref in ['DD', 'FD']:
			if T['z80'][pref][i] is None and T['z80'][''][i] is not None:
				ins = T['z80'][''][i]
				T['z80'][pref][i] = Instruction(*ins.forms, duplicate = ('', i))
		if T['z80']['ED'][i] is None:
			T['z80']['ED'][i] = Instruction(InsForm('NOP'), duplicate = ('', 0))

		for pref in ['DD', 'FD']:
			if T['z80n'][pref][i] is None and T['z80n'][''][i] is not None:
				ins = T['z80n'][''][i]
				T['z80n'][pref][i] = Instruction(*ins.forms, duplicate = ('', i))
		if T['z80n']['ED'][i] is None:
			T['z80n']['ED'][i] = Instruction(InsForm('NOP'), duplicate = ('', 0))

		def make_vm1_duplicate(ins, *prefixes):
			prefixes = set(prefixes)
			duplicate = False
			mnem_prefix = ''
			oldprefix = ''

			has_mb = 'CS' in ins.ddir or 'MB' in ins.ddir or 'M' in ins.forms[0].args
			has_rs = 'RS' in ins.ddir or 'H' in ins.forms[0].args or 'L' in ins.forms[0].args or 'M' in ins.forms[0].args

			alterdict = {}

			if '28' in prefixes:
				mnem_prefix = 'MB '
				if not has_mb:
					duplicate = True
					if '38' in prefixes and has_rs:
						oldprefix = '38'
				elif 'CS' in ins.ddir:
					mnem_prefix = 'CS '
				elif 'M' in ins.forms[0].args:
					alterdict['M'] = 'M1'

			if '38' in prefixes:
				mnem_prefix += 'RS '
				if not has_rs:
					duplicate = True
					if '28' in prefixes and has_mb:
						oldprefix = '28'
				else:
					if 'H' in ins.forms[0].args:
						alterdict['H'] = 'H1'
					if 'L' in ins.forms[0].args:
						alterdict['L'] = 'L1'

			if len(alterdict) > 0:
				new_forms = [InsForm(mnem_prefix + insf.mnem, *alter(insf.args, alterdict)) for insf in ins.forms]
				new_base = InsForm(mnem_prefix + ins.base.mnem, *alter(ins.base.args, alterdict)) if ins.base is not None else None
			else:
				new_forms = [InsForm(mnem_prefix + insf.mnem, *insf.args) for insf in ins.forms]
				new_base = InsForm(mnem_prefix + ins.base.mnem, *ins.base.args) if ins.base is not None else None
			old_code = (oldprefix, i)

			if duplicate:
				return Instruction(*new_forms, base = new_base, duplicate = old_code)
			else:
				return Instruction(*new_forms, base = new_base, unprefixed = old_code)

		if T['vm1']['28'][i] is None and T['vm1'][''][i] is not None:
			ins = T['vm1'][''][i]
			T['vm1']['28'][i] = make_vm1_duplicate(ins, '28')

		if T['vm1']['38'][i] is None and T['vm1'][''][i] is not None:
			ins = T['vm1'][''][i]
			T['vm1']['38'][i] = make_vm1_duplicate(ins, '38')

		if T['vm1']['28 38'][i] is None and T['vm1'][''][i] is not None:
			ins = T['vm1'][''][i]
			T['vm1']['28 38'][i] = make_vm1_duplicate(ins, '28', '38')

		for cpu in ['r2000', 'r3000', 'r4000', 'r6000']:
			# TODO: insert possible prefixes into database instead
			for prefix in ['', 'CB', 'DD', 'DD CB', 'ED', 'FD', 'FD CB']:
				newprefix = '76'
				if prefix != '':
					newprefix += ' ' + prefix
				if T[cpu][newprefix][i] is None and T[cpu][prefix][i] is not None:
					ins = T[cpu][prefix][i]
					#print(', '.join(insf.mnem for insf in ins.forms), file = sys.stderr)
					new_forms = [InsForm('ALTD ' + insf.mnem, *insf.args) for insf in ins.forms]
					insf0 = ins.forms[0]
					# r3000: LDDSR, LDISR, SETUSR, SURES, UMA, UMS
					# r4000: CBM, CONVC, CONVD, COPY, COPYR, EXP, FLAG, IDET, JRE, LSDDR, LSDR, LSIDR, LSIR, LLCALL, LLJP, LLRET, LSDDR, LSIDR, MULU, RDMODE, RLB, RRB, SETSYSP, SETUSRP, SYSCALL, SYSRET
					if insf0.mnem in {'CALL', 'CBM', 'CONVC', 'CONVD', 'COPY', 'COPYR', 'EXP', 'EXX', 'FLAG', 'IDET', 'IOE', 'IPSET', 'IPRES', 'JP', 'JR', 'JRE', 'LCALL', 'LDD', 'LDDR', 'LDDSR', 'LDI', 'LDIR', 'LDISR', 'LDP', 'LJP', 'LLCALL', 'LLJP', 'LLRET', 'LRET', 'LSDDR', 'LSDR', 'LSIDR', 'LSIR', 'MUL', 'MULU', 'NOP', 'PUSH', 'RDMODE', 'RET', 'RETI', 'RLB', 'RRB', 'RST', 'SETSYSP', 'SETUSR', 'SETUSRP', 'SURES', 'SYSCALL', 'SYSRET', 'UMA', 'UMS'}:
						# no flag or register effect
						T[cpu][newprefix][i] = Instruction(*new_forms, duplicate = (prefix, i))
					elif insf0.mnem in {'LD', 'POP'}:
						# no effect on flags (except for AF)
						# r4000: JK, BCDE, JKHL
						if insf0.args[0] in {'A', 'B', 'C', 'D', 'E', 'H', 'L', 'AF', 'BC', 'DE', 'HL', 'JK', 'BCDE', 'JKHL'}:
							T[cpu][newprefix][i] = Instruction(*new_forms, unprefixed = ('', i))
						elif insf0.args[0][0] == 'P' and (len(insf0.args) == 1 or insf0.args[1][0] == 'P' or insf0.args[1][:2] == '(P'):
							# P?, P?+??, (P?+??)
							T[cpu][newprefix][i] = Instruction(*new_forms, unprefixed = ('', i))
						else:
							T[cpu][newprefix][i] = Instruction(*new_forms, duplicate = (prefix, i))
					# r4000: LDF, LDL
					elif insf0.mnem in {'DEC', 'INC', 'LDF', 'LDL'}:
						# no effect on flags for 16-bit instruction
						if insf0.args[0] in {'IX', 'IY'}:
							T[cpu][newprefix][i] = Instruction(*new_forms, duplicate = (prefix, i))
						else:
							T[cpu][newprefix][i] = Instruction(*new_forms, unprefixed = ('', i))
					elif insf0.mnem in {'EX'}:
						# only when exchanging (SP) with HL
						if insf0.args == ['(SP)', 'HL']:
							T[cpu][newprefix][i] = Instruction(*new_forms, unprefixed = ('', i))
						else:
							T[cpu][newprefix][i] = Instruction(*new_forms, duplicate = (prefix, i))
					# r4000: CLR, DWJNZ, FSYSCALL, IBOX, SBOX, SLL, TEST
					elif insf0.mnem in {'ADC', 'ADD', 'AND', 'BIT', 'BOOL', 'CCF', 'CLR', 'CP', 'CPL', 'DJNZ', 'DWJNZ', 'FSYSCALL', 'IBOX', 'NEG', 'OR', 'RLA', 'RLC', 'RLCA', 'RRA', 'RRC', 'RRCA', 'SBC', 'SBOX', 'SCF', 'SLA', 'SLL', 'SRA', 'SRL', 'SUB', 'TEST', 'XOR'}:
						# always has flag effect
						T[cpu][newprefix][i] = Instruction(*new_forms, unprefixed = ('', i))
					elif insf0.mnem in {'RES', 'SET'}:
						# only for register target
						if insf0.args[1] in ['A', 'B', 'C', 'D', 'E', 'H', 'L']:
							T[cpu][newprefix][i] = Instruction(*new_forms, unprefixed = ('', i))
						else:
							T[cpu][newprefix][i] = Instruction(*new_forms, duplicate = (prefix, i))
					elif insf0.mnem in {'RR', 'RRC', 'RL', 'RLC'}:
						if insf0.args[0] != '8':
							T[cpu][newprefix][i] = Instruction(*new_forms, unprefixed = ('', i))
						else:
							T[cpu][newprefix][i] = Instruction(*new_forms, duplicate = (prefix, i))
					elif insf0.mnem in {'IOE', 'IOI'}:
						# these are also prefixes, ignore them
						pass
					else:
						# TODO
						#print("Warning, unrecognized Rabbit instruction:", cpu, insf0.mnem, file = sys.stderr) #; exit()
						#T[cpu][newprefix][i] = Instruction(*new_forms)
						pass

	for cpu, description in DESCRIPTIONS.items():
		current_referent = cpu
		if cpu not in REFERENCES:
			current_reference = cpu
		for key, value in description:
			if key == 'ref':
				current_reference = value
			elif key == 'name':
				REFERENCES[current_reference] = (current_referent, value)

	for tables in T.values():
		for key in list(tables.keys()):
			if all(entry is None for entry in tables[key]):
				del tables[key]

	REFERENCES['i8i80'] = 'i8', "Intel 8008"

