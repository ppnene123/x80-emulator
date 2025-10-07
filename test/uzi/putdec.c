
#include "system.h"

int main(int argc, char ** argv, char ** envp) __smallc
{
	(void) argc;
	(void) argv;
	(void) envp;

	write(1, "Hello!\n", 7);
	putint(12345);
	write(1, "\n", 1);
	return 123;
}

