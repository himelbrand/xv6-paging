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

	#if AQ

	int i, j;
	char *arr[16];
	char input[10];

	printf(1, "myMemTest: testing AQ... \n");

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
	printf(1, "Called sbrk(PGSIZE) for the 15th time, no page fault should occur and two pages in swap file.\nPress any key...\n");
	gets(input, 10);

   /*Allocate page 19.
		For this allocation, SCFIFO will move page 6 meaning arr[2] to swap.
	*/
	arr[15] = sbrk(PGSIZE);
	printf(1, "arr[15]=0x%x\n", arr[15]);
	printf(1, "Called sbrk(PGSIZE) for the 15th time, no page fault should occur and two pages in swap file.\nPress any key...\n");
	gets(input, 10);

	/*
	Access page 3, causing a PGFLT, since it is in the swap file. It would be
	hot-swapped with page 4. Page 4 is accessed next, so another PGFLT is invoked,
	and this process repeats a total of 5 times.
	*/
	for (i = 0; i < 5; i++) {
		
			arr[6][0] = 'k';
			arr[i][0] = 'k';

		
			
		printf(1, "%d ^P\n",i);
		gets(input, 10);
	}
	printf(1, "5 page faults should have occurred.\nPress any key...\n");
	gets(input, 10);

	/*
	If DEBUG flag is defined as != 0 this is just another example showing 
	that because SCFIFO doesn't page out accessed pages, no needless page faults occurr.
	*/
	if(DEBUG){
		for (i = 0; i < 5; i++) {
			printf(1, "Writing to address 0x%x\n", arr[i]);
			arr[i][0] = 't';
		}
		//printf(1, "No page faults should have occurred.\nPress any key...\n");
		gets(input, 10);
	}

	if (fork() == 0) {
		printf(1, "Child code running.\n");
		printf(1, "View statistics for pid %d, then press any key...\n", getpid());
		gets(input, 10);

		/*
		The purpose of this write is to create a PGFLT in the child process, and
		verify that it is caught and handled properly.
		*/
		// Allocate all remaining 13 physical pages
		j=0;
		arr[7][j] = 'k';
			printf(1, "1 page fault should have occurred.\nPress any key...\n");
	gets(input, 10);
		
		// printf(1, "Added Page, View statistics for pid %d, then press any key...\n", getpid());
		// gets(input, 10);


		// 	for (i = 0; i < 8; i++) 
		// for (j = 0; j < PGSIZE; j++)
			arr[6][0] = 'k';
	
		// sbrk(-5 * PGSIZE);
		// printf(1, "Deallocated all extra pages.\nPress any key to exit the child code.\n");
		// gets(input, 10);
		
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
 #elif SCFIFO
	int i;
	char *arr[16];
	char input[10];

	printf(1, "myMemTest: testing SCFIFO... \n");

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
	printf(1, "Called sbrk(PGSIZE) for the 15th time, no page fault should occur and two pages in swap file.\nPress any key...\n");
	gets(input, 10);

  

	/*
	Access page 3, causing a PGFLT, since it is in the swap file. It would be
	hot-swapped with page 4. Page 4 is accessed next, so another PGFLT is invoked,
	and this process repeats a total of 5 times.
	*/
	for (i = 0; i < 5; i++) {
			arr[i+1][0] = 'k';
			arr[i][0] = 'k';
	}
	printf(1, "5 page faults should have occurred.\nPress any key...\n");
	gets(input, 10);
//arr[7][0] = 'k';
	/*
	If DEBUG flag is defined as != 0 this is just another example showing 
	that because SCFIFO doesn't page out accessed pages, no needless page faults occurr.
	*/
	if(DEBUG){
		for (i = 0; i < 5; i++) {
			printf(1, "Writing to address 0x%x\n", arr[i]);
			arr[i][0] = 't';
		}
		//printf(1, "No page faults should have occurred.\nPress any key...\n");
		gets(input, 10);
	}

	if (fork() == 0) {
		printf(1, "Child code running.\n");
		printf(1, "View statistics for pid %d, then press any key...\n", getpid());
		gets(input, 10);
 /*Allocate page 19.
		For this allocation, SCFIFO will move page 6 meaning arr[2] to swap.
	*/
	arr[15] = sbrk(PGSIZE);
	printf(1, "arr[15]=0x%x\n", arr[15]);
	printf(1, "Called sbrk(PGSIZE) for the 15th time, no page fault should occur and two pages in swap file.\nPress any key...\n");
	gets(input, 10);
		/*
		The purpose of this write is to create a PGFLT in the child process, and
		verify that it is caught and handled properly.
		*/
		// Allocate all remaining 13 physical pages
		for(i=0;i<PGSIZE;i++)
		arr[0][0] = 'k';
			printf(1, "1 page fault should have occurred.\nPress any key...\n");
	gets(input, 10);
		
		// printf(1, "Added Page, View statistics for pid %d, then press any key...\n", getpid());
		// gets(input, 10);


		// 	for (i = 0; i < 8; i++) 
		// for (j = 0; j < PGSIZE; j++)
		// 	arr[7][j] = 'k';
	
		// sbrk(-5 * PGSIZE);
		// printf(1, "Deallocated all extra pages.\nPress any key to exit the child code.\n");
		// gets(input, 10);
		
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

	#elif NFUA
	
	// int i, j;
	// char *arr[16];
	// char input[10];

	// TODO add by Hanan
	printf(1, "myMemTest: testing NFUA... \n");

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