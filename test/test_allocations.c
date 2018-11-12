#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <unstickymem/unstickymem.h>

#define SIZE      100 * 1024 * 1024
#define NUM_ELEMS SIZE / sizeof(int)

int main() {
  unstickymem_nop();/*
  // check mallocs
  printf("malloc 1GB\n");
  void *large_malloc = malloc(1024*1024*1024); // 1GB
  printf("malloc 1KB\n");
  void *medium_malloc = malloc(1024*1024); // 1MB
  printf("malloc 4K (page size)\n");
  void *page_malloc = malloc(4*1024); // 4KB
  printf("malloc sizeof int\n");
  void *small_malloc = malloc(sizeof(int));*/
  // get 1GB of memory aligned to 1GB boundary
  unstickymem_print_memory();
  printf("posix_memalign 1GB aligned to 1GB boundary\n");
  void *large_aligned;
  posix_memalign(&large_aligned, 1024*1024*1024, 1024*1024*1024);
  unstickymem_print_memory();
  /*// check mmaps
  printf("mmap 1GB\n");
  void *large_mmap = mmap(NULL, 1024*1024*1024, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  printf("mmap 4KB (page size)\n");
  void *small_mmap = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);*/
  return 0;
}
