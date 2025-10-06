
# needed for diff <(...) syntax
# https://stackoverflow.com/questions/589276/how-can-i-use-bash-syntax-in-makefile-targets
SHELL:=/bin/bash

SRCDIR=.
CPUDIR=${SRCDIR}/cpu
OUTDIR=../out
HTMLDIR=../html

CFLAGS=-Wall
#CFLAGS+= -Werror
CFLAGS+= -O3
#CFLAGS+= -g

EMUCPUS=i80 i85 vm1 sm83 z80 z180 z280 z380 ez80 r800
TESTCPUS=i80 i85 vm1 sm83 z80 z180 z280 z380 ez80 r800

all: ${OUTDIR} ${HTMLDIR} ${HTMLDIR}/tables.html ${OUTDIR}/assembler ${OUTDIR}/disassembler ${OUTDIR}/emulator tests

${OUTDIR}:
	mkdir -p ${OUTDIR}

${HTMLDIR}:
	mkdir -p ${HTMLDIR}

clean:
	rm -f ${OUTDIR}/*

distclean: clean
	rm -rf *~ ${CPUDIR}/__pycache__ ${HTMLDIR}/*

tests: $(patsubst %,${OUTDIR}/test-%.diff,${TESTCPUS})

.PHONY: all clean distclean tests

${HTMLDIR}/tables.html: ${CPUDIR}/generate.py ${CPUDIR}/isa.dat ${CPUDIR}/database.py ${CPUDIR}/genhtml.py
	python3 $< html $(word 2,$^) ${HTMLDIR}/tables

# assembler.h is the first one, it is not used as a source
${OUTDIR}/assembler: ${SRCDIR}/assembler.h ${SRCDIR}/assembler.c ${OUTDIR}/parser.tab.c ${OUTDIR}/parser.yy.c
	gcc -o $@ $(filter-out $<,$^) ${CFLAGS}

%.yy.c: %.lex
	flex -o $@ $<

%.tab.c: %.y
	bison -o $@ -d $<

${OUTDIR}/parser.lex ${OUTDIR}/parser.y: ${CPUDIR}/isa.dat ${CPUDIR}/generate.py ${CPUDIR}/database.py ${CPUDIR}/genasm.py
	python3 $(word 2,$^) parse $< ${OUTDIR}/parser

${OUTDIR}/disassembler: ${SRCDIR}/disassembler.c ${SRCDIR}/disassembler.h ${OUTDIR}/disassembler.gen.c
	gcc -o $@ $< ${CFLAGS}

${OUTDIR}/disassembler.gen.c: ${CPUDIR}/isa.dat ${CPUDIR}/generate.py ${CPUDIR}/database.py ${CPUDIR}/gendasm.py
	python3 $(word 2,$^) disasm $< > $@

${OUTDIR}/emulator: ${SRCDIR}/emumain.c ${OUTDIR}/emulator.gen.c ${OUTDIR}/disassembler.gen.c ${OUTDIR}/assembler $(patsubst %,${CPUDIR}/%.c,${EMUCPUS}) $(patsubst %,${OUTDIR}/cpu_obj.%,${EMUCPUS})
	# ${SRCDIR}/assembler.c removed, it is included in emulator
	gcc -o $@ ${SRCDIR}/emumain.c $(patsubst %,${OUTDIR}/cpu_obj.%,${EMUCPUS}) ${OUTDIR}/parser.tab.c ${OUTDIR}/parser.yy.c ${CFLAGS}

${OUTDIR}/emulator.gen.c: ${CPUDIR}/isa.dat ${CPUDIR}/generate.py ${CPUDIR}/database.py ${CPUDIR}/genemu.py
	python3 $(word 2,$^) emu $< $@

${OUTDIR}/cpu_obj.%: ${CPUDIR}/cpu.c ${CPUDIR}/cpu.h ${CPUDIR}/common.c ${CPUDIR}/common.h ${OUTDIR}/emulator.gen.c ${OUTDIR}/disassembler.gen.c ${OUTDIR}/assembler ${CPUDIR}/%.c
	gcc -c -o $@ ${CPUDIR}/cpu.c -DCPU_`echo $(suffix $@) | sed 's/^\.//' | tr '[:lower:]' '[:upper:]'` ${CFLAGS}

${OUTDIR}/test-%.diff: ${CPUDIR}/generate.py ${CPUDIR}/isa.dat ${OUTDIR}/assembler ${CPUDIR}/database.py ${CPUDIR}/genasm.py
	python3 $< parse-test $(word 2,$^) $(basename $@)
	$(word 3,$^) $(basename $@).asm -o $(basename $@).out
	diff -y <(hex $(basename $@).bin) <(hex $(basename $@).out) > $@

