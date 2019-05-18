#include <stdio.h>
#include <string.h>

int main()
{
	int N = 10000;

	int numbers[N];
	int i;
	//set all to 1

	for(i = 0; i < N; i++)
	{
		numbers[i] = 1;
	}
		//single threaded sieve function
	for(i = 2; i < N; i++)
	{
		if(numbers[i] == 1)
		{	
			int j;
			for(j = 2; i * j < N; j++)
			{
				numbers[i * j] = 0;
			}
		}
	}

	int k = 101;
	if(numbers[k] == 1)
	{
		printf("%d is a prime number\r\n", k);
	}
	
	return 0;
}