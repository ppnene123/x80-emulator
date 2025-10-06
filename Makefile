
all:
	make -C src all

clean:
	rm -rf out
	make -C src clean

distclean: clean
	rm -rf *~ html
	make -C src distclean

