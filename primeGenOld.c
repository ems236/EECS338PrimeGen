#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>

//Bounded buffer of size N to use for calculating primes
#define N 100
#define MAX 500
int buffer[N];

int numThreads;

//stores the prime numbers that have been generated.  
//count is length of the array
int count = 0;
int primes[MAX];
//store the next iteration of each prime in the sieve.  
//Will store the next multiple of the prime number at the corresponding index. ie would go from {4, 6, 10} -> {6, 6, 10} -> {6, 9, 10}...
int next[MAX];

//control variables for bounded buffers.
//readMin is the lowest number the sieve has not checked.
int readMin = 4;

//writeMin is lowest number the "consumer" hasn't checked. IE the sieve can set anything below writeMin to 0 while running. 
//(happens because bounded buffer loops around)  
int writeMin = 1;

//semaphors to control access to global vars.  Grouped into prime and min because we almost always acces those as a group.  
sem_t primeMutex, minMutex;

/************************
CURRENTLY 2 THREADS, 1 sort of producer (the sieve) and 1 sort of consumer
Plan on enabling multiple sieve threads
Plan for implementation:
	give each thread an offset from [1, T] where T is the number of threads.  
	instead of i++ in the inner loop, have i += T.
	divides work relatively evenly and allows for more concurrency.
************************/

/***********************
currently runs infindefinitely.  Need to actually implement stopping at a max instead of while true.
Also probably need to change a lot of these ints to long ints or long long ints.
***********************/
void newMin();
int min(int num1, int num2);
void* sieve();

int main(int argc, char *argv[])
{
	//get upper bound to generate
	//int maxNum = atoi(argv[1]);

	int numCores;
	printf("How many cores are available?\r\n");
	scanf("%d", &numCores);
	printf("%d\r\n", numCores);
	numThreads = numCores - 1;

	int maxPrime;
	printf("How many prime numbers do you want to generate?\r\n");
	scanf("%d", &maxPrime);
	printf("%d\r\n", maxPrime);

	sem_init(&primeMutex, 0, 1);
	sem_init(&minMutex, 0, 1);

	
	pthread_t tid;
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	
	//set buffer elements to 1
	int i;
	for(i = 0; i < N; i++)
	{
		buffer[i] = 1;
	}
	
	//one core reserved for "consumer"
	for(i = 0; i < numCores - 1; i++)
	{
		int *threadNum = malloc(sizeof(int));
		*threadNum = i;
		pthread_create(&tid, &attr, sieve, ((void *)threadNum));
	}

	//read buffer for prime numbers. 
	//start at 2 because 0 and 1 are weird
	i = 2;

	//count only modified by this thread, so doesn't need to be protected.
	while(count < MAX)
	{	
		//get a local value of readMin.  Spends less time in critical section, reduces waiting. 
		sem_wait(&minMutex);
		int rMin = readMin;
		sem_post(&minMutex);

		while(i < rMin)
		{
			//found a prime #
			if(buffer[i % N] == 1)
			{
				printf("Prime found at %d \r\n", i);
				sem_wait(&primeMutex);
				//push new prime to the prime and next arrays.
				primes[count] = i;
				next[count] = 2 * i;
				count++;
				sem_post(&primeMutex);
			}

			//update writeMin and readMin
			sem_wait(&minMutex);
			rMin = readMin;
			writeMin = i + 1;
			sem_post(&minMutex);

			//reset cell in buffer as it moves through so the sieve thread can work.
			buffer[i % N] = 1;
			i++;
		}
	}
	

	pthread_join(tid, NULL);
	printf("Finished\r\n");
	/*
	for(i = 0; i < count; i++)
	{
		printf("i: %d p: %d\r\n", i, primes[i]);
	}
	*/
	sem_destroy(&primeMutex);
	sem_destroy(&minMutex);

	FILE *output;
	//overwrite then append
    output = fopen("output.txt","w");
    fputs("", output);
    fclose(output);

    output = fopen("output.txt", "a");
    for(i = 0; i < count; i++)
	{
		fprintf(output, "%d\r\n", primes[i]);
	}
    fclose(output);

	return 0;
}

//sieve thread
void* sieve(void *threadNum)
{
	int offset = *((int *)threadNum);
	//sieve for all the prime numbers in this array
	//local version of count.  Allows less critical sections
	printf("Child running: %d\r\n", offset); 
	int localCount = 0;
	while(localCount < MAX)
	{
		//deadlock if localCount is 0 because it only updates in the for loop.
		if(localCount <= offset)
		{
			sem_wait(&primeMutex);
			localCount = count;
			sem_post(&primeMutex);
		}

		//repeatedly loop throug the next multiples of evey prime.  set position in buffer to 0 if its available.
		//printf("%d %d\r\n", offset, localCount);
		int i;
		for(i = offset; i < localCount; i += numThreads)
		{
			if(offset == 1)
			{
				//printf("%d %d %d\n", i, offset, numThreads);
			}

			//get local versions of the minimums to reduce time in critical section.  
			sem_wait(&minMutex);
			int rMin = readMin;
			int wMin = writeMin;
			sem_post(&minMutex);

			//get local version of the next multiple.
			sem_wait(&primeMutex);
			int x = next[i];
			//update localCount
			localCount = count;
			sem_post(&primeMutex);

			//Testing if x's position in the bounded buffer is available for the sieve 
			//ie it's doesn't loop all the way back around into the section of the array the consumer is processing
			//improve boolean logic later, this should work but it's not very cleanly written
			//implement better semaphors, reading vs writing
			bool valid;
			if(rMin % N >= wMin % N)
			{
				valid = (x % N >= rMin % N && x - rMin < N) || (x % N < wMin && x - wMin < N);
			}
			else
			{
				valid = x % N >= rMin % N && x % N < wMin % N && x - rMin < N;
			}
			if(valid)
			{
				printf("index %d valid: %d readMin: %d writeMin: %d offset: %d\r\n", x, valid, rMin, wMin, offset); 
			}

			if(valid)
			{
				//0 index in the buffer.
				buffer[x % N] = 0;

				sem_wait(&primeMutex);
				//increment next
				next[i] += primes[i];
				sem_post(&primeMutex);

				//update min if this was the min.
				//update rmin, matters if multiple sieves
				if(x == rMin)
				{
					//updates global readMin
					newMin(offset);
				}
			}

			//if not valid, busy wait (go to next one). possible semaphor use if x is the read min  
		}
	}

	free(threadNum);
	pthread_exit(0);
}

//updates global readmin.
void newMin(int offset)
{
	int p = 2;
	sem_wait(&primeMutex);
	int nextMin = next[0];
	int i;
	for(i = 1; i < count; i++)
	{
		if(next[i] < nextMin)
		{
			//minimum of the next multiples to sieve
			nextMin = next[i];
			p = primes[i];
		}
	}
	sem_post(&primeMutex);

	sem_wait(&minMutex);
	//set read min to either the min of the next array, 2 * write min (prevents a new prime being found that has its next multiple below readmin), 
	//and N + writeMin to prevent readmin from looping around buffer and passing writemin. 
	readMin = min(nextMin, min(2 * writeMin, N + writeMin));
	printf("%d + %d + %d\r\n", readMin, p, offset);
	sem_post(&minMutex);
}

//returns the min of 2 numbers.
int min(int num1, int num2)
{
	if(num1 < num2)
	{
		return num1;
	}

	else
	{
		return num2;
	}
}

