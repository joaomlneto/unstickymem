#include <stdio.h>
#include <unstickymem/unstickymem.h>

int main() {
  optimize_numa_placement();
  printf("Hello world\n");
  return 0;
}
