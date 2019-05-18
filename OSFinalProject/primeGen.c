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
#define N 1000
#define MAX 203280221
#define NEXT 0
#define WRITE 1
int buffer[N];

int numThreads;

int maxPrime = MAX;

//stores the prime numbers that have been generated.  
//count is length of the array
int count = 0;
int *primes;
//store the next iteration of each prime in the sieve.  
//Will store the next multiple of the prime number at the corresponding index. ie would go from {4, 6, 10} -> {6, 6, 10} -> {6, 9, 10}...
int *next;

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
int min(int num1, int num2m);

//sieve "producer" thread
void* sieve();


//searches through the generated prime array to check if a number is prime using binary search
int primeSearch(int arr[], int min, int max, int num);


int main(int argc, char *argv[])
{

	int numCores;
	printf("How many cores are available? (At least 2) \r\n");
	scanf("%d", &numCores);
	numThreads = numCores - 1;

	printf("How many prime numbers do you want to generate? (Up to %d) \r\n", MAX);
	scanf("%d", &maxPrime);

	primes = (int *) malloc(203280221 * sizeof(int));
	next = (int *) malloc(203280221 * sizeof(int));



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
		int *threadNum = (int*) malloc(sizeof(int));
		*threadNum = i;
		pthread_create(&tid, &attr, sieve, ((void *)threadNum));
	}

	//read buffer for prime numbers. 
	//start at 2 because 0 and 1 are weird
	i = 2;

	//count only modified by this thread, so doesn't need to be protected.
	while(count < maxPrime)
	{	 
		readEnter(&minMutex);
		//get a local value of readMin.  Spends less time in critical section, reduces waiting.
		int rMin = readMin;
		readExit(&minMutex);

		while(i < rMin && count < maxPrime)
		{
			//found a prime #
			if(buffer[i % N] == 1)
			{
				//nothing fancy for writing
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
					//updates global readMin
					readExit(&minMutex);
					newMin();
					
				}
				else
				{
					readExit(&minMutex);
				}

				if(count % (maxPrime / 10) == 0)
				{
					if((count / (maxPrime / 10)) == 10){

					}
					else{
						printf("%d tenths done\n", count / (maxPrime / 10));
					}
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

	}
	

	pthread_join(tid, NULL);
	printf("Finished finding %d primes, all numbers attached in output.txt\r\n", maxPrime);
	
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
		fprintf(output, "%u\r\n", primes[i]);
	}
    fclose(output);

    //asks if user wants to check if a number is prime, can check multiple times, exits otherwise
    bool searcher = true;
    int isPrime;

    while(searcher){
    	printf("Would you like to check if a positive number is prime? (Enter 0 if not)\r\n");
    	scanf("%u", &isPrime);
    	if(isPrime == 0){
    		searcher = false;
    		printf("Code finished.\n\r");
    	}
    	else{
    		int check = primeSearch(primes,0,maxPrime,isPrime);
    		if(check == -1){
    			printf("%u is not a prime number\r\n", isPrime);
    		}
    		else{
    			printf("%u is a prime number\r\n", isPrime);
    		}

    	}


    }

    free(primes);
    free(next);
	return 0;
}

//sieve thread
void* sieve(void *threadNum)
{
	int offset = *((int *)threadNum);
	//sieve for all the prime numbers in this array
	//local version of count.  Allows less critical sections
	int localCount = 0;
	while(localCount < maxPrime)
	{
		//deadlock if localCount is 0 because it only updates in the for loop.
		if(localCount <= offset)
		{
			readEnter(&primeMutex);
			localCount = count;
			readExit(&primeMutex);
		}

		//repeatedly loop throug the next multiples of evey prime.  set position in buffer to 0 if its available.
		int i;
		for(i = offset; i < localCount && localCount < maxPrime; i += numThreads)
		{

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
				next[i] += primes[i];
				writeExit(&primeMutex);

				//update min if this was the min.
				readEnter(&minMutex);
				//Problems when read this while new readmin is being calculated
				if(x == readMin)
				{
					//updates global readMin
					readExit(&minMutex);
					newMin();
					
				}
				else
				{
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
void newMin()
{
	int p = 2;
	//
	writeEnter(&minMutex);
	readEnter(&primeMutex);
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
	readExit(&primeMutex);

	//set read min to either the min of the next array, 2 * write min (prevents a new prime being found that has its next multiple below readmin), 
	//and N + writeMin to prevent readmin from looping around buffer and passing writemin. 
	//printf("nextMin: %d, otherMin: %d\r\n", nextMin, min(2 * writeMin, N + writeMin));
	if(nextMin <= min(2 * writeMin, N + writeMin))
	{
		minType = NEXT;
		readMin = nextMin;
		
	}
	else
	{
		minType = WRITE;
		readMin = min(2 * writeMin, N + writeMin);
	}
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

int primeSearch(int arr[], int min, int max, int num)
{
   if (max >= min)
   {
        int mid = min + (max - min)/2;
 
        //If the element is found
        if (arr[mid] == num)  
            return num;
 
        //if the element in the array is larger than the search number
        if (arr[mid] > num) 
            return primeSearch(arr, min, mid-1, num);

        //if the array element is less than the search number
        return primeSearch(arr, mid+1, max, num);
   }
 
   //if the search number is not found
   return -1;
}

