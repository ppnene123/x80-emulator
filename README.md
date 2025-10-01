
# An emulator/assembler/disassembler package supporting the 8-bit Intel 8080/Zilog Z80 family

**Much of the project is untested and comes with no warranty.** However the core passes the popular 8080, Z80 an 8085 instruction exerciser suites.

This project grew out of a desire to research, document and compare several derivatives of the Intel 8080 microprocessor, arguably the progenitor to the ever abundant x86 architecture family.

The project includes an assembler, a disassembler, an emulator, with the goal being making the CPU emulation core a standalone library.
It also includes several automatically generated HTML tables summarizing and comparing the instruction sets.

The emulator currently emulates the Intel 8080, Intel 8085, KR580VM1, Sharp SM83, Zilog Z80, Zilog Z180, Zilog Z280, Zilog Z380, Zilog eZ80, ASCII R800 processors, and can create a very basic CP/M environment.

The file `tools/cpudisc.com` can distinguish between the above CPUs using undocumented and processor specific instructions.

# Overview

The family started with the CTC Datapoint 2200, which was not a microprocessor per se but a programmable terminal.
CTC requested Intel to create a single chip implementation, the Intel 8008, but eventually they decided not to use the 8008 in their terminals.
The Datapoint 2200 became the foundation of a series of similar terminals, including the 2200 Version II and the 5500.

The Intel 8008 was produced alongside the reportedly first microprocessor, the 4-bit Intel 4004 (not compatible with it, not implemented in this package), and by the same lead.
Once it became clear that CTC was not interested in the product, Intel started selling the 8008 to third parties, and followed it up by the source code compatible (but not binary compatible) 8080 and 8085.

Many of the original team later left Intel to found Zilog and produce a series of 8080 compatible microprocessors, starting with the wildly popular Z80, used in multiple home computers.
Zilog also created a series of upgrades to the Z80, including the Z180, the 16-bit Z800/Z280, the 32-bit Z380 and the 24-bit eZ80.
Most of these are not compatible with each other.

Other derivatives of the chips include the Sharp SM83, used as the CPU for the Gameboy, the 16-bit R800 from ASCII corporation, the КР580ВМ1 (KR580VM1) from the Soviet Union and the Rabbit series of processors.

# Comilation

Running `make` should generate the binaries and HTML tables in the `out` directory.
This includes the `assembler`, `disassembler` and `emulator`.
The `emulator` takes a CP/M binary (as a `.COM`, `.PRL` or CP/M Plus `.COM` executable) and executes and/or debugs it.
A rudimentary inline assembly is also provided.

# Resources

This project was inspired and supported by several resources, some of which are included here.

* Intel and Zilog manuals
* [Some excellent tables summarizing old processor architectures](https://pastraiser.com/)
* [Summary of the 8080 and Z80 instruction sets, webpage long defunct](http://nemesis.lonestar.org/computers/tandy/software/apps/m4/qd/opcodes.html)
* [The Undocumented Z80 Documented](http://z80.info/zip/z80-documented.pdf) and [Z80 Flag Affection](http://www.z80.info/z80sflag.htm)
* [Z80 and R800 assembly/machine language op-code tables](https://www.angelfire.com/art2/unicorndreams/msx/Z80R800.html)
* [Documentation for the KR580VM1 (in Russian)](https://code.google.com/archive/p/radio86/wikis/KP580BM1.wiki)

and many more.

