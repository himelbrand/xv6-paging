#include "types.h"
#include "stat.h"
#include "user.h"
#include "syscall.h"

#define PGSIZE 4096
#define DEBUG 0

int
main(int argc, char *argv[]){

	#if AQ

	int i, j;
	char *arr[16];
	char input[10];

	// TODO delete
	printf(1, "myMemTest: testing AQ... \n");

	// Allocate all remaining 13 physical pages
	for (i = 0; i < 13; ++i) {
		arr[i] = sbrk(PGSIZE);
		printf(1, "arr[%d]=0x%x\n", i, arr[i]);
	}
	printf(1, "Called sbrk(PGSIZE) 13 times - all physical pages taken.\nPress any key...\n");
	gets(input, 10);

	/*
	Allocate page 14.
	For this allocation, SCFIFO will consider moving page 0 to disk, but because it has been accessed, page 1 will be moved instead.
	Afterwards, page 1 is in the swap file, the rest are in memory.
	*/
	arr[13] = sbrk(PGSIZE);
	printf(1, "arr[12]=0x%x\n", arr[13]);
	printf(1, "Called sbrk(PGSIZE) for the 14th time, no page fault should occur and one page in swap file.\nPress any key...\n");
	gets(input, 10);

	/*
	Allocate page 15.
	For this allocation, SCFIFO will consider moving page 2 to disk, but because it has been accessed, page 3 will be moved instead.
	Afterwards, pages 1 & 3 are in the swap file, the rest are in memory.
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
		for (j = 0; j < PGSIZE; j++)
			arr[i][j] = 'k';
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
		arr[15] = sbrk(PGSIZE);
		printf(1, "Added Page, View statistics for pid %d, then press any key...\n", getpid());
		gets(input, 10);


		for (i = 0; i < 8; i++) {
			for (j = 0; j < PGSIZE; j++)
				arr[7][j] = 'k';
		}
		
		printf(1, "A Page fault should have occurred in child proccess.\nPress any key to exit the child code.\n");
		gets(input, 10);

		exit();
		
	}
	else {
        printf(1, "Parent code running.\n");
		wait();

		/*
		Deallocate all the pages.
		*/
		sbrk(-15 * PGSIZE);
		printf(1, "Deallocated all extra pages.\nPress any key to exit the father code.\n");
		gets(input, 10);
	}
 #elif SCFIFO
	int i, j;
	char *arr[16];
	char input[10];

	// TODO delete
	printf(1, "myMemTest: testing SCFIFO... \n");

	// Allocate all remaining 12 physical pages
	for (i = 0; i < 13; ++i) {
		arr[i] = sbrk(PGSIZE);
		printf(1, "arr[%d]=0x%x\n", i, arr[i]);
	}
	printf(1, "Called sbrk(PGSIZE) 13 times - all physical pages taken.\nPress any key...\n");
	gets(input, 10);

	/*
	Allocate page 16.
	For this allocation, SCFIFO will consider moving page 0 to disk, but because it has been accessed, page 1 will be moved instead.
	Afterwards, page 1 is in the swap file, the rest are in memory.
	*/
	arr[13] = sbrk(PGSIZE);
	printf(1, "arr[12]=0x%x\n", arr[13]);
	printf(1, "Called sbrk(PGSIZE) for the 14th time, no page fault should occur and one page in swap file.\nPress any key...\n");
	gets(input, 10);

	/*
	Allocate page 17.
	For this allocation, SCFIFO will consider moving page 2 to disk, but because it has been accessed, page 3 will be moved instead.
	Afterwards, pages 1 & 3 are in the swap file, the rest are in memory.
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
		for (j = 0; j < PGSIZE; j++)
			arr[i][j] = 'k';
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

		arr[15] = sbrk(PGSIZE);
		printf(1, "Added Page, View statistics for pid %d, then press any key...\n", getpid());
		gets(input, 10);


		for (i = 0; i < 8; i++) {
			for (j = 0; j < PGSIZE; j++)
				arr[7][j] = 'k';
		}
		
		printf(1, "A Page fault should have occurred in child proccess.\nPress any key to exit the child code.\n");
		gets(input, 10);

		exit();
	}
	else {
        printf(1, "Parent code running.\n");
		wait();

		/*
		Deallocate all the pages.
		*/
		sbrk(-15 * PGSIZE);
		printf(1, "Deallocated all extra pages.\nPress any key to exit the father code.\n");
		gets(input, 10);
	}
 #elif NFUA
	int i, j;
	char *arr[16];
	char input[10];

	// TODO delete
	printf(1, "myMemTest: testing NFU... \n");

	// Allocate all remaining 13 physical pages
	for (i = 0; i < 13; ++i) {
		arr[i] = sbrk(PGSIZE);
		printf(1, "arr[%d]=0x%x\n", i, arr[i]);
	}
	printf(1, "Called sbrk(PGSIZE) 13 times - all physical pages taken.\nPress any key...\n");
	gets(input, 10);

	/*
	Allocate page 14.
	For this allocation, NFU will choose to move to disk the page that hasn't been accessed the longest (in this case page 1).
	Afterwards, page 1 is in the swap file, the rest are in memory.
	*/
	arr[13] = sbrk(PGSIZE);
	printf(1, "arr[13]=0x%x\n", arr[13]);
	printf(1, "Called sbrk(PGSIZE) for the 14th time, no page fault should occur and one page in swap file.\nPress any key...\n");
	gets(input, 10);

	/*
	Allocate page 15.
	For this allocation, NFU will choose to move to disk the page that hasn't been accessed the longest (in this case page 3)
	Afterwards, pages 1 & 3 are in the swap file, the rest are in memory.
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
		printf(1, "Writing to address 0x%x\n", arr[i]);
		for (j = 0; j < PGSIZE; j++){
			arr[i][j] = 'k';
		}
	}
	printf(1, "5 page faults should have occurred.\nPress any key...\n");
	gets(input, 10);

	/*
	If DEBUG flag is defined as != 0 this is just another example showing 
	that because NFU doesn't page out accessed pages, no needless page faults occurr.
	*/
	if(DEBUG){
		for (i = 0; i < 5; i++){
			printf(1, "Writing to address 0x%x\n", arr[i]);
			arr[i][0] = 'k';
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
		arr[5][0] = 'k';
		printf(1, "Page faults should have occurred in child proccess.\nPress any key to exit the child code.\n");
		gets(input, 10);

		exit();
	}
	else {
		wait();

		/*
		Deallocate all the pages.
		*/
		sbrk(-14 * PGSIZE);
		printf(1, "Deallocated all extra pages.\nPress any key to exit the father code.\n");
		gets(input, 10);
	}


	

#elif LAPA
	
	int i, j;
	char *arr[16];
	char input[10];

	printf(1, "myMemTest: testing NFU... \n");

	// Allocate all remaining 13 physical pages
	for (i = 0; i < 13; ++i) {
		arr[i] = sbrk(PGSIZE);
		printf(1, "arr[%d]=0x%x\n", i, arr[i]);
	}
	printf(1, "Called sbrk(PGSIZE) 13 times - all physical pages taken.\nPress any key...\n");
	gets(input, 10);

	/*
	Allocate page 14.
	For this allocation, NFU will choose to move to disk the page that hasn't been accessed the longest (in this case page 1).
	Afterwards, page 1 is in the swap file, the rest are in memory.
	*/
	arr[13] = sbrk(PGSIZE);
	printf(1, "arr[13]=0x%x\n", arr[13]);
	printf(1, "Called sbrk(PGSIZE) for the 14th time, no page fault should occur and one page in swap file.\nPress any key...\n");
	gets(input, 10);

	/*
	Allocate page 15.
	For this allocation, NFU will choose to move to disk the page that hasn't been accessed the longest (in this case page 3)
	Afterwards, pages 1 & 3 are in the swap file, the rest are in memory.
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
		for (j = 0; j < PGSIZE; j++){
			arr[i][j] = 'k';
		}
	}
	printf(1, "5 page faults should have occurred.\nPress any key...\n");
	gets(input, 10);

	/*
	If DEBUG flag is defined as != 0 this is just another example showing 
	that because NFU doesn't page out accessed pages, no needless page faults occurr.
	*/
	if(DEBUG){
		for (i = 0; i < 5; i++){
			printf(1, "Writing to address 0x%x\n", arr[i]);
			arr[i][0] = 't';
		}
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

		arr[15] = sbrk(PGSIZE);
		printf(1, "Added Page, View statistics for pid %d, then press any key...\n", getpid());
		gets(input, 10);

		for (i = 0; i < 8; i++) {
			for (j = 0; j < PGSIZE; j++)
				arr[7][j] = 'k';
		}
		
		printf(1, "A Page fault should have occurred in child proccess.\nPress any key to exit the child code.\n");
		gets(input, 10);

		exit();
	}
	else {
		wait();

		/*
		Deallocate all the pages.
		*/
		sbrk(-14 * PGSIZE);
		printf(1, "Deallocated all extra pages.\nPress any key to exit the father code.\n");
		gets(input, 10);
	}

	printf(1, "Called sbrk(PGSIZE) 13 times - all physical pages taken.\nPress any key...\n");
	gets(input, 10);



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