
LINK=external/link

all: diag.com diagm.prl diag3.com

clean:
	rm -rf *.o *.rsx *.com *.prl

distclean: clean
	rm -rf *~

diag.com: diag.s
	z80-coff-as $< -o com.o --defsym FORMAT_COM=1
	z80-coff-ld -Ttext=0x100 com.o -o $@

diagm.prl: diag.s
	z80-coff-as diag.s -o prl.o --defsym FORMAT_PRL=1
	${LINK} -Fprl prl.o -o $@

diag1.rsx: diag.s
	z80-coff-as diag.s -o rsx1.o --defsym FORMAT_RSX=1 --defsym RSX_INDEX=1
	${LINK} -Fprl rsx1.o -o diag1.rsx

diag2.rsx: diag.s
	z80-coff-as diag.s -o rsx2.o --defsym FORMAT_RSX=1 --defsym RSX_INDEX=2
	${LINK} -Fprl rsx2.o -o diag2.rsx

diag3.com: diag.s diag1.rsx diag2.rsx
	z80-coff-as diag.s -o cpm3.o --defsym FORMAT_CPM3=1
	${LINK} -Fcpm3 cpm3.o -o $@ -Srsx=rsx1=diag1.rsx,rsx2=diag2.rsx

