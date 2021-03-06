#include <__/syscall.h>
#include <ipccalls.h>
#include <string.h>

extern char** environ;
static bool has_arguments;

pid_t spawn_process_raw(const char* file) {
  has_arguments = 0;
  char os_file[5];
  strncpy(os_file, file, 5);
  return _syscall(_SYSCALL_SPAWN, os_file[0], os_file[1], os_file[2], os_file[3], os_file[4]);
}
void add_argument(const char* arg) {
  has_arguments = 1;
  send_ipc_call("argd", IPC_ARGD_ADD, 0, 0, 0, (uintptr_t) arg, strlen(arg) + 1);
}
void start_process(void) {
  for (size_t i = 0; environ[i]; i++) {
    send_ipc_call("envd", IPC_ENVD_ADD, 0, 0, 0, (uintptr_t) environ[i], strlen(environ[i]) + 1);
  }
  _syscall(_SYSCALL_START, has_arguments, (bool) environ[0], 0, 0, 0);
}
