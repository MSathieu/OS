#include <ipc.h>
#include <stdio.h>
#include <stdlib.h>

int putc(int c, FILE* file) {
  if (file == stdout) {
    char arg[2] = {0};
    arg[0] = c;
    send_ipc_call("ttyd", IPC_CALL_MEMORY_SHARING, 0, 0, 0, (uintptr_t) arg, 2);
    return c;
  }
  exit(1);
}