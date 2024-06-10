#include "../klib/syscalls.h"

int p_main()
{
	char str[] = "Hello from process x\n";
	str[19] = '0' + (char) get_pid();

	start_process(1);

	for (int i = 0; i < 5; ++i)
	{
		print(str);
		pause();
	}

	return 0;
}
