#include "timingtest.h"
#include <stdio.h>

#define D 10000
#define T 10000

void FunctionToTest(int step) {

  int a[D];
  int k, i;
  
  printf("my function starts\n");
  for (i=0, k=0; i<T; i++) {
    a[k] = i;
    k+=1+step;
    if (k>D) k=k%D;
  }
  printf("my function ends\n");
    
}

int main() {
  
  const int numtests = 10; // number of test runs
  const int pmc_num = 0x00000000; // program monitor counter number
  int clockcounts[numtests]; // list of clock counts
  int pmccounts[numtests]; // list of PMC counts
  int clock1, clock2, pmc1, pmc2; // counter values before and after each test
  int i; // loop counter

  for (i = 0; i < numtests; i++) {
    // loop for repeated tests
    serialize(); // prevent out-of-order execution
    clock1 = (int)readtsc(); // read clock
    pmc1 = (int)readpmc(pmc_num);// read PMC
    FunctionToTest(i); // your code to test
    serialize(); // serialize again
    clock2 = (int)readtsc(); // read again after test
    pmc2 = (int)readpmc(pmc_num);
    clockcounts[i] = clock2-clock1;// store differences in list
    pmccounts[i] = pmc2-pmc1;
  }

  // print out results
  printf("\nTest results\nclock counts PMC counts");
  for (i = 0; i < numtests; i++) {
    printf("\n%10i %10i", clockcounts[i], pmccounts[i]);
  }
  printf("\n");


  return 0;

}
