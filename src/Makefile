
# needed for diff <(...) syntax
# https://stackoverflow.com/questions/589276/how-can-i-use-bash-syntax-in-makefile-targets
SHELL:=/bin/bash

SRCDIR=.
OUTDIR=../out

CFLAGS=-Wall
#CFLAGS+= -Werror
CFLAGS+= -O3
#CFLAGS+= -g

EMUCPUS=i80 i85 vm1 sm83 z80 z180 z280 z380 ez80 r800
TESTCPUS=i80 i85 vm1 sm83 z80 z180 z280 z380 ez80 r800

all: ${OUTDIR} ${OUTDIR}/tables.html ${OUTDIR}/assembler ${OUTDIR}/disassembler ${OUTDIR}/emulator tests

${OUTDIR}:
	mkdir -p ${OUTDIR}

clean:
	rm -f ${OUTDIR}/*

distclean: clean
	rm -rf *~ ${SRCDIR}/__pycache__

tests: $(patsubst %,${OUTDIR}/test-%.diff,${TESTCPUS})

.PHONY: all clean distclean tests

${OUTDIR}/tables.html: ${SRCDIR}/generate.py ${SRCDIR}/isa.dat ${SRCDIR}/database.py ${SRCDIR}/genhtml.py
	python3 $< html $(word 2,$^) ${OUTDIR}/tables

# assembler.h is the first one, it is not used as a source
${OUTDIR}/assembler: ${SRCDIR}/assembler.h ${SRCDIR}/assembler.c ${OUTDIR}/parser.tab.c ${OUTDIR}/parser.yy.c
	gcc -o $@ $(filter-out $<,$^) ${CFLAGS}

%.yy.c: %.lex
	flex -o $@ $<

%.tab.c: %.y
	bison -o $@ -d $<

${OUTDIR}/parser.lex ${OUTDIR}/parser.y: ${SRCDIR}/isa.dat ${SRCDIR}/generate.py ${SRCDIR}/database.py ${SRCDIR}/genasm.py
	python3 $(word 2,$^) parse $< ${OUTDIR}/parser

${OUTDIR}/disassembler: ${SRCDIR}/disassembler.c ${SRCDIR}/disassembler.h ${OUTDIR}/disassembler.gen.c
	gcc -o $@ $< ${CFLAGS}

${OUTDIR}/disassembler.gen.c: ${SRCDIR}/isa.dat ${SRCDIR}/generate.py ${SRCDIR}/database.py ${SRCDIR}/gendasm.py
	python3 $(word 2,$^) disasm $< > $@

${OUTDIR}/emulator: ${SRCDIR}/emumain.c ${OUTDIR}/emulator.gen.c ${OUTDIR}/disassembler.gen.c ${OUTDIR}/assembler $(patsubst %,${SRCDIR}/%.c,${EMUCPUS}) $(patsubst %,${OUTDIR}/cpu_obj.%,${EMUCPUS})
	# ${SRCDIR}/assembler.c removed, it is included in emulator
	gcc -o $@ ${SRCDIR}/emumain.c $(patsubst %,${OUTDIR}/cpu_obj.%,${EMUCPUS}) ${OUTDIR}/parser.tab.c ${OUTDIR}/parser.yy.c ${CFLAGS}

${OUTDIR}/emulator.gen.c: ${SRCDIR}/isa.dat ${SRCDIR}/generate.py ${SRCDIR}/database.py ${SRCDIR}/genemu.py
	python3 $(word 2,$^) emu $< $@

${OUTDIR}/cpu_obj.%: ${SRCDIR}/cpu.c ${SRCDIR}/cpu.h ${SRCDIR}/common.c ${SRCDIR}/common.h ${OUTDIR}/emulator.gen.c ${OUTDIR}/disassembler.gen.c ${OUTDIR}/assembler ${SRCDIR}/%.c
	gcc -c -o $@ ${SRCDIR}/cpu.c -DCPU_`echo $(suffix $@) | sed 's/^\.//' | tr '[:lower:]' '[:upper:]'` ${CFLAGS}

${OUTDIR}/test-%.diff: ${SRCDIR}/generate.py ${SRCDIR}/isa.dat ${OUTDIR}/assembler ${SRCDIR}/database.py ${SRCDIR}/genasm.py
	python3 $< parse-test $(word 2,$^) $(basename $@)
	$(word 3,$^) $(basename $@).asm -o $(basename $@).out
	diff -y <(hex $(basename $@).bin) <(hex $(basename $@).out) > $@

