#include <capability.h>
#include <ctype.h>
#include <fcntl.h>
#include <ipccalls.h>
#include <linked_list.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#define TMP_MAGIC 0x5381835340924865

struct file {
  struct linked_list_member list_member;
  size_t file_i;
  char* path;
  void* data;
  size_t size;
};

static struct linked_list files_list;
static size_t next_file_i = 0;

static int64_t open_handler(uint64_t flags, uint64_t arg1, uint64_t arg2, uint64_t address, uint64_t size) {
  if (arg1 || arg2) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (!has_ipc_caller_capability(CAP_NAMESPACE_FILESYSTEMS, CAP_VFSD)) {
    syslog(LOG_DEBUG, "Not allowed to open file");
    return -IPC_ERR_INSUFFICIENT_PRIVILEGE;
  }
  if (size == 1) {
    syslog(LOG_DEBUG, "No file path specified");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  char* buffer = malloc(size);
  memcpy(buffer, (void*) address, size);
  if (buffer[size - 1]) {
    free(buffer);
    syslog(LOG_DEBUG, "Path isn't null terminated");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  for (struct file* file = (struct file*) files_list.first; file; file = (struct file*) file->list_member.next) {
    if (!strcmp(file->path, buffer)) {
      free(buffer);
      if (flags & O_TRUNC) {
        free(file->data);
        file->size = 0;
      }
      return file->file_i;
    }
  }
  if (flags & O_CREAT) {
    for (size_t i = 0; i < size - 1; i++) {
      if (!isprint(buffer[i]) || buffer[i] == '/') {
        free(buffer);
        syslog(LOG_DEBUG, "Path contains invalid characters");
        return -IPC_ERR_INVALID_ARGUMENTS;
      }
    }
    struct file* file = calloc(1, sizeof(struct file));
    file->file_i = next_file_i++;
    file->path = buffer;
    insert_linked_list(&files_list, &file->list_member);
    return file->file_i;
  } else {
    free(buffer);
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
}
static int64_t read_handler(uint64_t file_num, uint64_t offset, uint64_t arg2, uint64_t address, uint64_t size) {
  if (arg2) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (!has_ipc_caller_capability(CAP_NAMESPACE_FILESYSTEMS, CAP_VFSD)) {
    syslog(LOG_DEBUG, "Not allowed to read file");
    return -IPC_ERR_INSUFFICIENT_PRIVILEGE;
  }
  struct file* file = 0;
  for (struct file* file_i = (struct file*) files_list.first; file_i; file_i = (struct file*) file_i->list_member.next) {
    if (file_i->file_i == file_num) {
      file = file_i;
      break;
    }
  }
  if (!file) {
    syslog(LOG_DEBUG, "Invalid file number");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (offset >= file->size) {
    return 0;
  }
  if (size > file->size - offset) {
    size = file->size - offset;
  }
  memcpy((void*) address, file->data + offset, size);
  return size;
}
static int64_t write_handler(uint64_t file_num, uint64_t offset, uint64_t arg2, uint64_t address, uint64_t size) {
  if (arg2) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (!has_ipc_caller_capability(CAP_NAMESPACE_FILESYSTEMS, CAP_VFSD)) {
    syslog(LOG_DEBUG, "Not allowed to write to file");
    return -IPC_ERR_INSUFFICIENT_PRIVILEGE;
  }
  if (offset > (size_t) 1 << 32) {
    syslog(LOG_DEBUG, "Offset is too big");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  struct file* file = 0;
  for (struct file* file_i = (struct file*) files_list.first; file_i; file_i = (struct file*) file_i->list_member.next) {
    if (file_i->file_i == file_num) {
      file = file_i;
      break;
    }
  }
  if (!file) {
    syslog(LOG_DEBUG, "Invalid file number");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  size_t total_size = offset + size;
  if (total_size > file->size) {
    if (file->data) {
      file->data = realloc(file->data, total_size);
    } else {
      file->data = malloc(total_size);
    }
    memset(file->data + file->size, 0, total_size - file->size);
    file->size = total_size;
  }
  memcpy(file->data + offset, (void*) address, size);
  return size;
}
int main(void) {
  drop_capability(CAP_NAMESPACE_KERNEL, CAP_KERNEL_PRIORITY);
  register_ipc(1);
  ipc_handlers[IPC_VFSD_FS_OPEN] = open_handler;
  ipc_handlers[IPC_VFSD_FS_WRITE] = write_handler;
  ipc_handlers[IPC_VFSD_FS_READ] = read_handler;
  while (1) {
    handle_ipc();
  }
}
