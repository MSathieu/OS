#include <capability.h>
#include <ctype.h>
#include <ipccalls.h>
#include <linked_list.h>
#include <sched.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

struct fd {
  struct linked_list_member list_member;
  size_t fd;
  size_t mount;
  size_t file_num;
  size_t position;
};
struct process {
  struct linked_list_member list_member;
  pid_t pid;
  struct linked_list fd_list;
  size_t next_fd;
};
struct mount {
  const char* path;
  pid_t pid;
};

static struct mount mounts[512];
static struct linked_list process_list;
static bool ready;

static int64_t mount_handler(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t address, uint64_t size) {
  if (arg0 || arg1 || arg2) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (!has_ipc_caller_capability(CAP_NAMESPACE_FILESYSTEMS, CAP_VFSD_MOUNT)) {
    syslog(LOG_DEBUG, "No permission to mount filesystem");
    return -IPC_ERR_INSUFFICIENT_PRIVILEGE;
  }
  if (size == 1) {
    syslog(LOG_DEBUG, "No mount path specified");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  pid_t pid = get_caller_spawned_pid();
  if (!pid) {
    syslog(LOG_DEBUG, "Not currently spawning a process");
    return -IPC_ERR_PROGRAM_DEFINED;
  }
  char* buffer = malloc(size);
  memcpy(buffer, (void*) address, size);
  if (buffer[size - 1]) {
    free(buffer);
    syslog(LOG_DEBUG, "Path isn't null terminated");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (buffer[0] != '/') {
    free(buffer);
    syslog(LOG_DEBUG, "Path isn't absolute");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (buffer[size - 2] != '/') {
    free(buffer);
    syslog(LOG_DEBUG, "Path isn't a directory");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  for (size_t i = 0; i < size - 1; i++) {
    if (!isalnum(buffer[i]) && buffer[i] != '/') {
      free(buffer);
      syslog(LOG_DEBUG, "Path contains invalid characters");
      return -IPC_ERR_INVALID_ARGUMENTS;
    }
  }
  for (size_t i = 0; i < 512; i++) {
    if (!mounts[i].path) {
      mounts[i].path = buffer;
      mounts[i].pid = pid;
      return 0;
    }
  }
  free(buffer);
  syslog(LOG_WARNING, "Reached maximum number of mounts");
  return -IPC_ERR_PROGRAM_DEFINED;
}
static int64_t open_file_handler(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t address, uint64_t size) {
  if (arg0 || arg1 || arg2) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (!ready) {
    return -IPC_ERR_RETRY;
  }
  char* buffer = malloc(size);
  memcpy(buffer, (void*) address, size);
  if (buffer[size - 1]) {
    free(buffer);
    syslog(LOG_DEBUG, "Path isn't null terminated");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (buffer[0] != '/') {
    free(buffer);
    syslog(LOG_DEBUG, "Path isn't absolute");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  int mount_i = -1;
  size_t mount_size = 0;
  for (size_t i = 0; i < 512; i++) {
    if (mounts[i].path && strlen(mounts[i].path) > mount_size && !strncmp(mounts[i].path, buffer, strlen(mounts[i].path))) {
      mount_i = i;
      mount_size = strlen(mounts[i].path);
    }
  }
  if (mount_i == -1) {
    free(buffer);
    return -IPC_ERR_RETRY;
  }
  long file_num = -IPC_ERR_INVALID_PID;
  while (file_num == -IPC_ERR_INVALID_PID) {
    file_num = send_pid_ipc_call(mounts[mount_i].pid, IPC_VFSD_FS_GET_FILE_NUM, 0, 0, 0, (uintptr_t) buffer + mount_size, size - 1);
    if (file_num == -IPC_ERR_INVALID_PID) {
      sched_yield();
    }
  }
  free(buffer);
  if (file_num < 0) {
    return -IPC_ERR_PROGRAM_DEFINED;
  }
  pid_t caller_pid = get_ipc_caller_pid();
  struct process* process = 0;
  for (struct process* loop_process = (struct process*) process_list.first; loop_process; loop_process = (struct process*) loop_process->list_member.next) {
    if (loop_process->pid == caller_pid) {
      process = loop_process;
      break;
    }
  }
  if (!process) {
    process = calloc(1, sizeof(struct process));
    process->pid = caller_pid;
    insert_linked_list(&process_list, &process->list_member);
  }
  struct fd* fd = calloc(1, sizeof(struct fd));
  fd->mount = mount_i;
  fd->file_num = file_num;
  fd->fd = process->next_fd++;
  insert_linked_list(&process->fd_list, &fd->list_member);
  return fd->fd;
}
static int64_t close_file_handler(uint64_t fd_num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
  if (arg1 || arg2 || arg3 || arg4) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  pid_t caller_pid = get_ipc_caller_pid();
  for (struct process* process = (struct process*) process_list.first; process; process = (struct process*) process->list_member.next) {
    if (process->pid == caller_pid) {
      for (struct fd* fd = (struct fd*) process->fd_list.first; fd; fd = (struct fd*) fd->list_member.next) {
        if (fd->fd == fd_num) {
          remove_linked_list(&process->fd_list, &fd->list_member);
          free(fd);
          return 0;
        }
      }
      break;
    }
  }
  syslog(LOG_DEBUG, "File descriptor doesn't exist");
  return -IPC_ERR_INVALID_ARGUMENTS;
}
static int64_t read_file_handler(uint64_t fd_num, uint64_t arg1, uint64_t arg2, uint64_t address, uint64_t size) {
  if (arg1 || arg2) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  pid_t caller_pid = get_ipc_caller_pid();
  for (struct process* process = (struct process*) process_list.first; process; process = (struct process*) process->list_member.next) {
    if (process->pid == caller_pid) {
      for (struct fd* fd = (struct fd*) process->fd_list.first; fd; fd = (struct fd*) fd->list_member.next) {
        if (fd->fd == fd_num) {
          send_pid_ipc_call(mounts[fd->mount].pid, IPC_VFSD_FS_READ, fd->file_num, fd->position, 0, address, size);
          fd->position += size;
          return 0;
        }
      }
      break;
    }
  }
  syslog(LOG_DEBUG, "File descriptor doesn't exist");
  return -IPC_ERR_INVALID_ARGUMENTS;
}
static int64_t write_file_handler(uint64_t fd_num, uint64_t arg1, uint64_t arg2, uint64_t address, uint64_t size) {
  if (arg1 || arg2) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  pid_t caller_pid = get_ipc_caller_pid();
  for (struct process* process = (struct process*) process_list.first; process; process = (struct process*) process->list_member.next) {
    if (process->pid == caller_pid) {
      for (struct fd* fd = (struct fd*) process->fd_list.first; fd; fd = (struct fd*) fd->list_member.next) {
        if (fd->fd == fd_num) {
          send_pid_ipc_call(mounts[fd->mount].pid, IPC_VFSD_FS_WRITE, fd->file_num, fd->position, 0, address, size);
          fd->position += size;
          return 0;
        }
      }
      break;
    }
  }
  syslog(LOG_DEBUG, "File descriptor doesn't exist");
  return -IPC_ERR_INVALID_ARGUMENTS;
}
static int64_t seek_file_handler(uint64_t fd_num, uint64_t position, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
  if (arg2 || arg3 || arg4) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  pid_t caller_pid = get_ipc_caller_pid();
  for (struct process* process = (struct process*) process_list.first; process; process = (struct process*) process->list_member.next) {
    if (process->pid == caller_pid) {
      for (struct fd* fd = (struct fd*) process->fd_list.first; fd; fd = (struct fd*) fd->list_member.next) {
        if (fd->fd == fd_num) {
          fd->position = position;
          return 0;
        }
      }
      break;
    }
  }
  syslog(LOG_DEBUG, "File descriptor doesn't exist");
  return -IPC_ERR_INVALID_ARGUMENTS;
}
static int64_t set_ready_handler(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
  if (arg0 || arg1 || arg2 || arg3 || arg4) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (!has_ipc_caller_capability(CAP_NAMESPACE_FILESYSTEMS, CAP_VFSD_MOUNT)) {
    syslog(LOG_DEBUG, "No permission to set ready flag");
    return -IPC_ERR_INSUFFICIENT_PRIVILEGE;
  }
  ready = 1;
  return 0;
}
int main(void) {
  drop_capability(CAP_NAMESPACE_KERNEL, CAP_KERNEL_PRIORITY);
  register_ipc(1);
  ipc_handlers[IPC_VFSD_CLOSE] = close_file_handler;
  ipc_handlers[IPC_VFSD_SEEK] = seek_file_handler;
  ipc_handlers[IPC_VFSD_SET_READY] = set_ready_handler;
  ipc_handlers[IPC_VFSD_OPEN] = open_file_handler;
  ipc_handlers[IPC_VFSD_MOUNT] = mount_handler;
  ipc_handlers[IPC_VFSD_WRITE] = write_file_handler;
  ipc_handlers[IPC_VFSD_READ] = read_file_handler;
  while (1) {
    handle_ipc();
  }
}
