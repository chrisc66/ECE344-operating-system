#include "common.h"
#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include "ctype.h"

int calculateFactorial (int value){
	if (value == 1) 
		return 1;
	return value * calculateFactorial(value-1);
}

void errorMsg (){
	printf("Huh?\n");
	return;
}

int
main(int argc, char* argv[])
{
	double inputValue = atof(argv[1]);
	int intValue = ceil(inputValue);
	// check for only one argument
	if (argc == 1 || argc > 2){
		errorMsg();
	}
	// check for digit, integer, equals 0
	else if (!isdigit(*argv[1]) || intValue != inputValue || intValue == 0){
		errorMsg();
	}
	// check for value greater than 12
	else if (intValue > 12){
		printf("Overflow\n");
	}
	// calculate factorial
	else {
		int factValue = calculateFactorial(intValue);
		printf("%d\n", factValue);
	}
	return 0;

}