#! /usr/bin/python3

import os
import sys
from database import *

import genhtml, genemu, genasm, gendasm

# TODO: z380, must check previous DDIR instruction before emitting

def main():
	with open(sys.argv[2], 'r') as fp:
		load_database(fp)
	complete_database()

	mode = sys.argv[1]
	if mode == 'html':
		genhtml.generate_html()
	elif mode == 'emu':
		genemu.generate_emulator()
	elif mode == 'parse':
		genasm.generate_parser()
	elif mode == 'parse-test':
		genasm.generate_asm_tests()
	elif mode == 'disasm':
		gendasm.generate_disasm()
	else:
		print("Error: unknown mode " + repr(mode), file = sys.stderr)

if __name__ == '__main__':
	main()

