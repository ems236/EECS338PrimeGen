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
#define MAX 1000
#define NEXT 0
#define WRITE 1
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
int minType = NEXT;

//writeMin is lowest number the "consumer" hasn't checked. IE the sieve can set anything below writeMin to 0 while running. 
//(happens because bounded buffer loops around)  
int writeMin = 2;

//controls access for shared variables.  Allows multiple readers.
struct rwStruct
{
	sem_t sem, readerSem, writeSem;
	int readers;
};

//semaphors to control access to global vars.  Grouped into prime and min because we almost always acces those as a group.  
//struct for readEnter/Exit.  Convenient semaphor implementation for allowing multiple readers
struct rwStruct primeMutex, minMutex;

void readEnter(struct rwStruct *c)
{
	sem_wait(&(c->writeSem));
	sem_wait(&(c->readerSem));
	c->readers++;
	//if first reader wait
	if(c->readers == 1)
	{
		sem_wait(&(c->sem));
	}
	sem_post(&(c->readerSem));
	sem_post(&(c->writeSem));
}

void readExit(struct rwStruct *c)
{
	sem_wait(&(c->readerSem));
	c->readers--;
	if(c->readers == 0)
	{
		sem_post(&(c->sem));
	}
	sem_post(&(c->readerSem));
}

void writeEnter(struct rwStruct *c)
{
	sem_wait(&(c->writeSem));
	sem_wait(&(c->sem));
}

void writeExit(struct rwStruct *c)
{
	sem_post(&(c->sem));
	sem_post(&(c->writeSem));
}

//min functions used for sieve minval
void newMin();
int min(int num1, int num2);

//sieve "producer" thread
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

	//not currently used
	int maxPrime;
	printf("How many prime numbers do you want to generate?\r\n");
	scanf("%d", &maxPrime);
	printf("%d\r\n", maxPrime);

	sem_init(&(minMutex.sem), 0, 1);
	sem_init(&(minMutex.readerSem), 0, 1);
	sem_init(&(minMutex.writeSem), 0, 1);
	minMutex.readers = 0;

	sem_init(&(primeMutex.sem), 0, 1);
	sem_init(&(primeMutex.readerSem), 0, 1);
	sem_init(&(primeMutex.writeSem), 0, 1);
	primeMutex.readers = 0;

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
		readEnter(&minMutex);
		//get a local value of readMin.
		int rMin = readMin;
		readExit(&minMutex);

		while(i < rMin && count < MAX)
		{
			//found a prime #
			if(buffer[i % N] == 1)
			{
				printf("Prime found at %d \r\n", i);
				writeEnter(&primeMutex);
				//push new prime to the prime and next arrays.
				primes[count] = i;
				next[count] = 2 * i;
				count++;
				writeExit(&primeMutex);

				//update min if the min is dependent on writemin
				readEnter(&minMutex);
				if(minType == WRITE)
				{
					readExit(&minMutex);
					//updates global readMin
					newMin(-1);
				}
				else
				{
					readExit(&minMutex);
				}

				if(count % (MAX / 10) == 0)
				{
					printf("%d tenths done; curret prime = %u", count / (MAX / 10), primes[count]);
				}
			}


			//reset cell in buffer as it moves through so the sieve thread can work.
			buffer[i % N] = 1;

			writeEnter(&minMutex);
			//writing is more restrictive than reading so no harm in reading during the write section
			rMin = readMin;
			writeMin = i + 1;
			writeExit(&minMutex);
			
			i++;
		}

		//update min. Shouldn't get here too often.
		//newMin(-1);
	}
	

	pthread_join(tid, NULL);
	printf("Finished\r\n");
	/*
	for(i = 0; i < count; i++)
	{
		printf("i: %d p: %d\r\n", i, primes[i]);
	}
	*/
	sem_destroy(&(primeMutex.sem));
	sem_destroy(&(primeMutex.readerSem));
	sem_destroy(&(primeMutex.writeSem));
	sem_destroy(&(minMutex.sem));
	sem_destroy(&(minMutex.readerSem));
	sem_destroy(&(minMutex.writeSem));

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
			readEnter(&primeMutex);
			localCount = count;
			readExit(&primeMutex);
		}

		//repeatedly loop throug the next multiples of evey prime.  set position in buffer to 0 if its available.
		//printf("%d %d\r\n", offset, localCount);
		int i;
		for(i = offset; i < localCount && localCount < MAX; i += numThreads)
		{
			if(offset == 1)
			{
				//printf("%d %d %d\n", i, offset, numThreads);
			}

			//get local version of the next multiple.
			readEnter(&primeMutex);
			int x = next[i];
			int p = primes[i];
			//update localCount
			localCount = count;
			readExit(&primeMutex);

			//Testing if x's position in the bounded buffer is available for the sieve 
			//ie it's doesn't loop all the way back around into the section of the array the consumer is processing
			//improve boolean logic later, this should work but it's not very cleanly written
			//implement better semaphors, reading vs writing
			readEnter(&minMutex);
			bool valid;
			if(readMin % N >= writeMin % N)
			{
				valid = ((x % N >= readMin % N && x - readMin < N) || (x % N < writeMin && x - writeMin < N)) && readMin != writeMin + N;
			}
			else
			{
				valid = x % N >= readMin % N && x % N < writeMin % N && x - readMin < N;
			}

			int rMin = readMin;
			int wMin = writeMin;
			readExit(&minMutex);

			if(valid)
			{
				//0 index in the buffer.
				buffer[x % N] = 0;

				writeEnter(&primeMutex);
				//increment next
				printf("index %d p: %d valid: %d readMin: %d writeMin: %d offset: %d\r\n", x, p, valid, rMin, wMin, offset); 
				next[i] += primes[i];
				writeExit(&primeMutex);

				//update min if this was the min.
				//Not all stuff here directly deals with writing min vals, but need to prevent checking if x == nextMin while calculating a new min.
				//int semVal;
				//sem_getvalue(&(minMutex.sem), &semVal);
				//printf("Waitng for mutex %d val: %d\r\n", offset, semVal);
				//fflush(stdout);
				readEnter(&minMutex);
				//printf("in mutex %d\r\n", offset);
				//printf("x: %d readmin: %d\r\n", x, readMin);
				//fflush(stdout);
				//Problems when read this while new readmin is being calculated
				if(x == readMin)
				{
					//updates global readMin
					readExit(&minMutex);
					newMin(offset);
					//printf("Exitting mutex %d\r\n", offset);
					//fflush(stdout);
				}
				else
				{
					//printf("Exitting mutex %d\r\n", offset);
					//fflush(stdout);
					readExit(&minMutex);
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
	//
	writeEnter(&minMutex);
	readEnter(&primeMutex);
	int nextMin = next[0];
	//printf("next 2: %d offset: %d\r\n", next[0], offset);
	int i;
	for(i = 1; i < count; i++)
	{
		if(next[i] < nextMin)
		{
			//minimum of the next multiples to sieve
			nextMin = next[i];
			p = primes[i];
			//printf("nextMin: %d offset: %d\r\n", next[i], offset);
		}
	}
	readExit(&primeMutex);

	//set read min to either the min of the next array, 2 * write min (prevents a new prime being found that has its next multiple below readmin), 
	//and N + writeMin to prevent readmin from looping around buffer and passing writemin. 
	printf("nextMin: %d, otherMin: %d\r\n", nextMin, min(2 * writeMin, N + writeMin));
	if(nextMin <= min(2 * writeMin, N + writeMin))
	{
		minType = NEXT;
		readMin = nextMin;
		//printf("set nextMin:%d\r\n", readMin);
	}
	else
	{
		minType = WRITE;
		readMin = min(2 * writeMin, N + writeMin);
		//printf("set nextMin:%d\r\n", readMin);
	}
	printf("%d + %d + %d\r\n", readMin, p, offset);
	fflush(stdout);
	writeExit(&minMutex);
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

