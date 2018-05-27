#include "types.h"
#include "user.h"


#define AGE_INC 0x80000000            // adding 1 to the counter msb
#define SHIFT_COUNTER(x) (x >> 1);    // for shifting the counter


// NOTE: 	ageUpdate work as expected
// 			must make sure age resets to 0
//			not to get stuck in infi loop

int ageUpdateTest(uint age){
  	
  	printf(1, "ageUpdateTest got age: 0x%x\n", age);
    // first shift the age counter right
    age = SHIFT_COUNTER(age);

    printf(1, "ageUpdateTest age after shift: 0x%x\n", age);
    age = age | AGE_INC;

    printf(1, "ageUpdateTest age after bitwise or: 0x%x\n", age);

    return age;
}



int
main(int argc, char* argv[]){

	// testing ageUpdate
	uint age = 0x0;
	uint maxAge = 0xffffffff;

	while(age != maxAge){
		printf(1,"age before update is: %x\n", age);
		age = ageUpdateTest(age);
		printf(1,"age after update is: %x\n", age);
	}

	printf(1,"End of tempTests\n");
	
	return 0;

}