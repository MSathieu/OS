#pragma once
#include <sys/types.h>
#define PROT_NONE 0
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4
#define MAP_PRIVATE 0
#define MAP_SHARED 1
#define MAP_FIXED 2
#define MAP_ANONYMOUS 4
#define MAP_FAILED (void*) -1
#define POSIX_MADV_NORMAL 0
#define POSIX_MADV_SEQUENTIAL 1
#define POSIX_MADV_RANDOM 2
#define POSIX_MADV_WILLNEED 3
#define POSIX_MADV_DONTNEED 4

void* mmap(void* address, size_t size, int prot, int flags, int file, off_t offset);
int mprotect(void* address, size_t size, int prot);
int munmap(void* address, size_t size);
int posix_madvise(void* address, size_t size, int advice);
