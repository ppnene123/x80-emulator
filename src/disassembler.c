
#include "disassembler.h"

int main(int argc, char * argv[])
{
	FILE * file;
	parse_context_t Cxt;
	int cpu = DASM_Z80;
	const char * input_file_name = NULL;

	for(int i = 1; i < argc; i++)
	{
		if(argv[i][0] == '-')
		{
			if(argv[i][1] == 'c')
			{
				if(argv[i][2])
					cpu = get_cpu(&argv[i][2]);
				else
					cpu = get_cpu(argv[++i]);
			}
			else
			{
				fprintf(stderr, "Unknown flag: '%s'\n", argv[i]);
			}
		}
		else
		{
			input_file_name = argv[i];
		}
	}

	if(input_file_name == NULL)
	{
		fprintf(stderr, "Expected input file\n");
		exit(0);
	}

	file = fopen(input_file_name, "rb");

	context_init_with_file(&Cxt, file, cpu);

	while(!feof((FILE *)Cxt.data))
	{
		context_parse(&Cxt);
	}

	fclose(file);
	return 0;
}

