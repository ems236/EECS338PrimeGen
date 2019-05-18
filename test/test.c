#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{
	int* x = (int *) malloc(203280221 * sizeof(int));
	x[0] = 2;
	x[9] = 100002;
	x[203280220] = 12;
	printf("%d, %d \r\n", x[0], x[203280220]);
	/*
	printf("int: %lu\r\n", sizeof(int));
	printf("unsigned long: %lu\r\n", sizeof(unsigned long int));

	int x = 2147483647;
	x += 1;
	printf("num: %u / %d\r\n", x, x);
	*/
	free(x);
	return(0);
}