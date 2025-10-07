
all:
	make -C src all
	make -C tools all
	make -C test all

clean:
	rm -rf out
	make -C src clean
	make -C tools clean
	make -C test clean

distclean: clean
	rm -rf *~ html
	make -C src distclean
	make -C tools distclean
	make -C test distclean

