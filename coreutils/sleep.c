#include <stdlib.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
  if (argc != 2) {
    return 1;
  }
  sleep(atol(argv[1]));
}
