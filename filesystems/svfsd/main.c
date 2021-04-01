#include <capability.h>
#include <ipccalls.h>
#include <monocypher.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#define SVFS_MAGIC 0x55ce581be6Bfa5a6

struct svfs_file {
  uint8_t name[256];
  uint64_t offset;
  uint64_t size;
  uint8_t hash[64];
};
struct svfs_header {
  uint8_t signature[64];
  uint64_t magic;
  uint32_t version;
  uint32_t nfiles;
  struct svfs_file files[];
};

static struct svfs_header* header;
static pid_t parent_pid;
__attribute__((weak)) extern int _binary_public_key_start;

static int64_t open_handler(uint64_t mode, uint64_t arg1, uint64_t arg2, uint64_t address, uint64_t size) {
  if (arg1 || arg2) {
    syslog(LOG_DEBUG, "Reserved argument is set");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (!has_ipc_caller_capability(CAP_NAMESPACE_FILESYSTEMS, CAP_VFSD)) {
    syslog(LOG_DEBUG, "Not allowed to open file");
    return -IPC_ERR_INSUFFICIENT_PRIVILEGE;
  }
  if (mode) {
    return -IPC_ERR_INSUFFICIENT_PRIVILEGE;
  }
  for (size_t i = 0; i < header->nfiles; i++) {
    if (size != strlen((char*) header->files[i].name) + 1) {
      continue;
    }
    if (!strcmp((char*) header->files[i].name, (char*) address)) {
      return i;
    }
  }
  return -IPC_ERR_INVALID_ARGUMENTS;
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
  if (file_num >= header->nfiles) {
    syslog(LOG_DEBUG, "Invalid file number");
    return -IPC_ERR_INVALID_ARGUMENTS;
  }
  if (offset >= header->files[file_num].size) {
    return 0;
  }
  if (size > header->files[file_num].size - offset) {
    size = header->files[file_num].size - offset;
  }
  return send_pid_ipc_call(parent_pid, IPC_VFSD_FS_READ, sizeof(struct svfs_header) + header->nfiles * sizeof(struct svfs_file) + header->files[file_num].offset + offset, 0, 0, address, size);
}
int main(void) {
  drop_capability(CAP_NAMESPACE_KERNEL, CAP_KERNEL_PRIORITY);
  register_ipc(1);
  parent_pid = getppid();
  header = calloc(1, sizeof(struct svfs_header));
  send_pid_ipc_call(parent_pid, IPC_VFSD_FS_READ, 0, 0, 0, (uintptr_t) header, sizeof(struct svfs_header));
  if (header->magic != SVFS_MAGIC || header->version) {
    syslog(LOG_ERR, "Filesystem has invalid magic value or unknown version");
    return 1;
  }
  if (header->nfiles > 4096) {
    return 1;
  }
  header = realloc(header, sizeof(struct svfs_header) + header->nfiles * sizeof(struct svfs_file));
  memset((void*) header + sizeof(struct svfs_header), 0, header->nfiles * sizeof(struct svfs_file));
  send_pid_ipc_call(parent_pid, IPC_VFSD_FS_READ, sizeof(struct svfs_header), 0, 0, (uintptr_t) header + sizeof(struct svfs_header), header->nfiles * sizeof(struct svfs_file));
  if (&_binary_public_key_start) {
    if (crypto_check(header->signature, (uint8_t*) &_binary_public_key_start, (uint8_t*) header + offsetof(struct svfs_header, magic), sizeof(struct svfs_header) - offsetof(struct svfs_header, magic) + header->nfiles * sizeof(struct svfs_file))) {
      syslog(LOG_ERR, "Filesystem has invalid signature");
      return 1;
    }
    for (size_t i = 0; i < header->nfiles; i++) {
      uint8_t* file = calloc(header->files[i].size, 1);
      send_pid_ipc_call(parent_pid, IPC_VFSD_FS_READ, sizeof(struct svfs_header) + header->nfiles * sizeof(struct svfs_file) + header->files[i].offset, 0, 0, (uintptr_t) file, header->files[i].size);
      uint8_t hash[64];
      crypto_blake2b(hash, file, header->files[i].size);
      if (memcmp(hash, header->files[i].hash, 64)) {
        syslog(LOG_ERR, "File %s has invalid hash", header->files[i].name);
        return 1;
      }
      free(file);
    }
  }
  ipc_handlers[IPC_VFSD_FS_OPEN] = open_handler;
  ipc_handlers[IPC_VFSD_FS_READ] = read_handler;
  while (1) {
    handle_ipc();
  }
}
