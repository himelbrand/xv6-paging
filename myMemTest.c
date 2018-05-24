#include "types.h"
#include "stat.h"
#include "user.h"
#include "syscall.h"

#define PGSIZE 4096
#define DEBUG 0

void printContinueMSG(void){
	printf(1,"\nPress ^P to see pages info then the return key to continue...\n");
}

int
main(int argc, char *argv[]){

	#ifndef NONE

   int i;
	char *arr[16];
	char input[10];
	#ifdef SCFIFO
	printf(1, "myMemTest: testing SCFIFO... \n");
	#endif
	#ifdef AQ
	printf(1, "myMemTest: testing AQ... \n");
	#endif
	// Allocate all remaining 13 physical pages
	for (i = 0; i < 13; ++i) {
		arr[i] = sbrk(PGSIZE);
		printf(1, "arr[%d]=0x%x\n", i, arr[i]);
	}
	printf(1, "Called sbrk(PGSIZE) 13 times - all physical pages should be taken\n");
	printContinueMSG();
	gets(input, 10);

	/*
	Allocate page 17.
	For this allocation, SCFIFO will move page 4 meaning arr[0] to swap.
	*/
	arr[13] = sbrk(PGSIZE);
	printf(1, "arr[13]=0x%x\n", arr[13]);
	printf(1, "Called sbrk(PGSIZE) for the 14th time, no page fault should occur and one page in swap file\n");
	printContinueMSG();
	gets(input, 10);

	/*
	Allocate page 18.
	For this allocation, SCFIFO will move page 5 meaning arr[1] to swap.
	*/
	arr[14] = sbrk(PGSIZE);
	printf(1, "arr[14]=0x%x\n", arr[14]);
	printf(1, "Called sbrk(PGSIZE) for the 15th time, no page fault should occur and two pages in swap file.\n");
	printContinueMSG();
	gets(input, 10);


	/*Allocate page 19.
		For this allocation, SCFIFO will move page 6 meaning arr[2] to swap.
	*/
	arr[15] = sbrk(PGSIZE);
	printf(1, "arr[15]=0x%x\n", arr[15]);
	printf(1, "Called sbrk(PGSIZE) for the 15th time, no page fault should occur and two pages in swap file.");
	printContinueMSG();
	gets(input, 10);
  
	/*
	Access page 3, causing a PGFLT, since it is in the swap file. It would be
	hot-swapped with page 4. Page 4 is accessed next, so another PGFLT is invoked,
	and this process repeats a total of 5 times.
	*/
	for (i = 0; i <= 5; i++) {
			if(i==1)arr[6][0] = 'k';//to cause change in the policies AQ and SCFIFO results
			arr[i][0] = 'k';
	}
	#ifdef SCFIFO
	printf(1, "6 page faults should have occurred.\n");
	#endif
	#ifdef AQ
	printf(1, "6 page faults should have occurred.\n");
	#endif
	#ifdef NFUA
	printf(1, "NFUA... \n");
	#endif
	#ifdef LAPA
	printf(1, "LAPA... \n");
	#endif
	printContinueMSG();
	gets(input, 10);

	if (fork() == 0) {
		printf(1, "Child code running.\n");
		printContinueMSG();
		gets(input, 10);
 
		
		arr[6][i] = 'k';
	#ifdef SCFIFO
	printf(1, "no page faults should have occurred.\n");
	#endif
	#ifdef AQ
	printf(1, "one page fault should have occurred.\n");
	#endif
	#ifdef NFUA
	printf(1, "NFUA... \n");
	#endif
	#ifdef LAPA
	printf(1, "LAPA... \n");
	#endif
	printContinueMSG();
	gets(input, 10);
		
		exit();
		
	}
	else {
        printf(1, "Parent code running.\n");
		wait();

		/*
		Deallocate all the pages.
		*/
		sbrk(-16 * PGSIZE);
		printf(1, "Deallocated all extra pages.\nPress any key to exit the father code.\n");
		gets(input, 10);
	}
	#else
	char* arr[50];
	int i = 50;
	char input[10];

	printf(1, "Commencing user test for default paging policy.\nNo page faults should occur.\n");
	for (i = 0; i < 50; i++) {
		arr[i] = sbrk(PGSIZE);
		printf(1, "arr[%d]=0x%x\n", i, arr[i]);
	}
	gets(input,10);
	#endif
	exit();
}