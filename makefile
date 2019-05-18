all:
	gcc -pthread -o primeGenOld primeGenOld.c
	gcc -pthread -o primeGen primeGen.c
	gcc -o primeGenS primeGenS.c