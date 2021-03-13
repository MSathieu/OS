#include <cpu/paging.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/scheduler.h>

struct linked_list syscall_processes;

void syscall_wait_ipc(union syscall_args* args) {
  if (args->arg0 || args->arg1 || args->arg2 || args->arg3 || args->arg4) {
    puts("Reserved argument is set");
    terminate_current_task(&args->registers);
    return;
  }
  if (current_task->servicing_syscall_requester) {
    puts("Can't register handler while servicing syscall");
    terminate_current_task(&args->registers);
    return;
  }
  if (current_task->process->syscall_handler) {
    puts("Already waiting for IPC call on different task");
    terminate_current_task(&args->registers);
    return;
  }
  if (current_task->process->syscall_queue.first) {
    current_task->servicing_syscall_requester = (struct task*) current_task->process->syscall_queue.first;
    remove_linked_list(&current_task->process->syscall_queue, &current_task->servicing_syscall_requester->list_member);
    args->syscall = current_task->servicing_syscall_requester->registers.rdi & 255;
    args->arg0 = current_task->servicing_syscall_requester->registers.rsi;
    args->arg1 = current_task->servicing_syscall_requester->registers.rdx;
    args->arg2 = current_task->servicing_syscall_requester->registers.rcx;
    args->arg3 = current_task->servicing_syscall_requester->registers.r8;
    args->arg4 = current_task->servicing_syscall_requester->registers.r9;
  } else {
    current_task->process->syscall_handler = current_task;
    block_current_task(&args->registers);
  }
}
void syscall_handle_ipc(union syscall_args* args) {
  size_t pid = args->syscall >> 8;
  struct process* called_process = 0;
  for (struct process* process = (struct process*) syscall_processes.first; process; process = (struct process*) process->list_member.next) {
    if (process->pid == pid) {
      called_process = process;
      break;
    }
  }
  if (!called_process) {
    args->return_value = -IPC_ERR_INVALID_PID;
    return;
  }
  size_t syscall = args->syscall & 255;
  uintptr_t mapping_start, mapping_end;
  if (syscall & IPC_CALL_MEMORY_SHARING) {
    if (!called_process->accepts_shared_memory) {
      puts("Called process doesn't accept shared memory");
      terminate_current_task(&args->registers);
      return;
    }
    if (called_process->last_physical_mappings_addr) {
      args->return_value = -IPC_ERR_RETRY;
      return;
    }
    mapping_start = args->arg3 / 0x1000 * 0x1000;
    if (!args->arg4 || args->arg4 > 0x8000000) {
      puts("Can't share this much memory");
      terminate_current_task(&args->registers);
      return;
    }
    if (__builtin_uaddl_overflow(args->arg3, args->arg4 + 0xfff, &mapping_end)) {
      puts("Can' share this memory region");
      terminate_current_task(&args->registers);
      return;
    }
    mapping_end = mapping_end / 0x1000 * 0x1000;
    if (mapping_end >= 0x800000000000) {
      puts("Can't share this memory region");
      terminate_current_task(&args->registers);
      return;
    }
    if (!current_task->sharing_memory && mapping_end >= PAGING_USER_PHYS_MAPPINGS_START) {
      puts("Can't share this memory region");
      terminate_current_task(&args->registers);
      return;
    }
    for (size_t i = mapping_start; i < mapping_end; i += 0x1000) {
      if (!is_page_mapped(i, syscall & IPC_CALL_MEMORY_SHARING_RW_MASK)) {
        puts("Requested page isn't mapped or doesn't have the required permissions");
        terminate_current_task(&args->registers);
        return;
      }
    }
  }
  if (!called_process->syscall_handler) {
    if (syscall & IPC_CALL_MEMORY_SHARING) {
      args->return_value = -IPC_ERR_RETRY;
      return;
    }
    insert_linked_list(&called_process->syscall_queue, &current_task->list_member);
    block_current_task(&args->registers);
    return;
  }
  current_task->blocked = 1;
  called_process->syscall_handler->servicing_syscall_requester = current_task;
  uint64_t arg0 = args->arg0;
  uint64_t arg1 = args->arg1;
  uint64_t arg2 = args->arg2;
  uint64_t arg3 = args->arg3;
  uint64_t arg4 = args->arg4;
  called_process->syscall_handler->blocked = 0;
  switch_task(called_process->syscall_handler, &args->registers);
  called_process->syscall_handler = 0;
  args->syscall = syscall;
  args->arg0 = arg0;
  args->arg1 = arg1;
  args->arg2 = arg2;
  args->arg4 = arg4;
  if (syscall & IPC_CALL_MEMORY_SHARING) {
    current_task->sharing_memory = 1;
    current_task->process->last_physical_mappings_addr = current_task->process->physical_mappings_addr;
    user_physical_mappings_addr = current_task->process->physical_mappings_addr;
    args->arg3 = user_physical_mappings_addr + arg3 % 0x1000;
    for (size_t i = mapping_start; i < mapping_end; i += 0x1000) {
      map_physical(convert_to_physical(i, current_task->servicing_syscall_requester->process->address_space), 0x1000, 1, syscall & IPC_CALL_MEMORY_SHARING_RW_MASK, 0);
    }
    current_task->process->physical_mappings_addr = user_physical_mappings_addr;
  } else {
    args->arg3 = arg3;
  }
}
void syscall_return_ipc(union syscall_args* args) {
  if (args->arg1 || args->arg2 || args->arg3 || args->arg4) {
    puts("Reserved argument is set");
    terminate_current_task(&args->registers);
    return;
  }
  if (current_task->sharing_memory) {
    for (size_t i = current_task->process->last_physical_mappings_addr; i < current_task->process->physical_mappings_addr; i += 0x1000) {
      unmap_page(i);
    }
    current_task->process->physical_mappings_addr = current_task->process->last_physical_mappings_addr;
    current_task->process->last_physical_mappings_addr = 0;
    current_task->sharing_memory = 0;
  }
  struct task* requester = current_task->servicing_syscall_requester;
  if (!requester) {
    puts("No syscall to return from");
    terminate_current_task(&args->registers);
    return;
  }
  current_task->servicing_syscall_requester = 0;
  requester->blocked = 0;
  requester->registers.rax = args->arg0;
  schedule_task(requester, &args->registers);
}
void syscall_block_ipc_call(union syscall_args* args) {
  if (args->arg0 || args->arg1 || args->arg2 || args->arg3 || args->arg4) {
    puts("Reserved argument is set");
    terminate_current_task(&args->registers);
    return;
  }
  struct task* requester = current_task->servicing_syscall_requester;
  if (!requester) {
    puts("Not currently handling IPC call");
    terminate_current_task(&args->registers);
    return;
  }
  if (current_task->sharing_memory) {
    puts("Can't block while sharing memory");
    terminate_current_task(&args->registers);
    return;
  }
  insert_linked_list(&current_task->process->blocked_ipc_calls_queue, &requester->list_member);
  current_task->servicing_syscall_requester = 0;
}
void syscall_unblock_ipc_call(union syscall_args* args) {
  if (args->arg1 || args->arg2 || args->arg3 || args->arg4) {
    puts("Reserved argument is set");
    terminate_current_task(&args->registers);
    return;
  }
  struct task* syscall_requester = 0;
  for (struct task* task = (struct task*) current_task->process->blocked_ipc_calls_queue.first; task; task = (struct task*) task->list_member.next) {
    if (!args->arg0 || task->process->pid == args->arg0) {
      syscall_requester = task;
      break;
    }
  }
  if (!syscall_requester) {
    puts("No IPC call to unblock with specified PID");
    terminate_current_task(&args->registers);
    return;
  }
  remove_linked_list(&current_task->process->blocked_ipc_calls_queue, &syscall_requester->list_member);
  struct task* syscall_handler = current_task->process->syscall_handler;
  if (!syscall_handler) {
    insert_linked_list(&current_task->process->syscall_queue, &syscall_requester->list_member);
    return;
  }
  current_task->process->syscall_handler = 0;
  syscall_handler->servicing_syscall_requester = syscall_requester;
  syscall_handler->blocked = 0;
  syscall_handler->registers.rdi = syscall_requester->registers.rdi & 255;
  syscall_handler->registers.rsi = syscall_requester->registers.rsi;
  syscall_handler->registers.rdx = syscall_requester->registers.rdx;
  syscall_handler->registers.rcx = syscall_requester->registers.rcx;
  syscall_handler->registers.r8 = syscall_requester->registers.r8;
  syscall_handler->registers.r9 = syscall_requester->registers.r9;
  schedule_task(syscall_handler, &args->registers);
}