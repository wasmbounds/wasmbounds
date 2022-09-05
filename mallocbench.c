#include <stdlib.h>

#define DATA_SZ 16 * 1024 * 1024

int main(int argc, char **argv) {
  volatile int *data =
      (volatile int *)malloc(DATA_SZ * sizeof(int)); // allocate 16M of data
  int i;
  for (i = 0; i < DATA_SZ; i++) {
    data[i] = i * 2;
  }
  free((void *)data);
  return 0;
}
