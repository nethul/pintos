#include "userprog/syscall.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>

static void syscall_handler(struct intr_frame *);

void syscall_init(void) {
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Terminates the current user process with the given exit status. */
void sys_exit(int status) {
  struct thread *cur = thread_current();
  cur->exit_status = status;
  thread_exit();
}

/* Validates that a user memory buffer of size bytes is wholly valid:
   - buffer is non-null (unless size == 0)
   - start and end addresses are below PHYS_BASE
   - does not wrap around
   - every page crossed is mapped in the current process's page directory */
static bool is_valid_buffer(const void *buffer, size_t size) {
  if (size == 0)
    return true;
  if (buffer == NULL)
    return false;

  const uint8_t *start = (const uint8_t *)buffer;
  if ((uintptr_t)start + size < (uintptr_t)start)
    return false; /* Integer overflow / wrap-around */

  const uint8_t *end = start + size - 1;
  if (!is_user_vaddr(start) || !is_user_vaddr(end))
    return false;

  uint32_t *pd = thread_current()->pagedir;
  if (pd == NULL)
    return false;

  const uint8_t *page = (const uint8_t *)pg_round_down(start);
  while (page <= end) {
    if (pagedir_get_page(pd, page) == NULL)
      return false;
    if ((uintptr_t)page + PGSIZE < (uintptr_t)page)
      break;
    page += PGSIZE;
  }
  return true;
}

/* Validates that a null-terminated user string is valid:
   - str is non-null
   - all bytes up to and including the null terminator are below PHYS_BASE
     and mapped in the current process's page directory */
static bool is_valid_string(const char *str) {
  if (str == NULL || !is_user_vaddr(str))
    return false;

  uint32_t *pd = thread_current()->pagedir;
  if (pd == NULL)
    return false;

  while (true) {
    if (!is_user_vaddr(str) || pagedir_get_page(pd, str) == NULL)
      return false;
    if (*str == '\0')
      return true;
    str++;
  }
}

/* Validates that the user stack contains argc 32-bit arguments starting
   immediately above the syscall number (i.e. at esp + 4). */
static void validate_args(const void *esp, size_t argc) {
  if (!is_valid_buffer((const char *)esp + sizeof(int),
                       argc * sizeof(uint32_t)))
    sys_exit(-1);
}

static void syscall_handler(struct intr_frame *f) {
  /* 1. Ensure esp points to valid user memory for the syscall number. */
  if (!is_valid_buffer(f->esp, sizeof(int))) {
    sys_exit(-1);
  }

  int syscall_nr = *(int *)f->esp;

  switch (syscall_nr) {
  case SYS_HALT: {
    shutdown_power_off();
    break;
  }

  case SYS_EXIT: {
    validate_args(f->esp, 1);
    int status = *(int *)((char *)f->esp + 4);
    sys_exit(status);
    break;
  }

  case SYS_EXEC: {
    validate_args(f->esp, 1);
    const char *cmd_line = *(const char **)((char *)f->esp + 4);
    if (!is_valid_string(cmd_line))
      sys_exit(-1);
    /* Exec will be fully implemented in Milestone 6 */
    f->eax = -1;
    break;
  }

  case SYS_WAIT: {
    validate_args(f->esp, 1);
    /* Wait will be fully implemented in Milestone 6 */
    f->eax = -1;
    break;
  }

  case SYS_CREATE: {
    validate_args(f->esp, 2);
    const char *file = *(const char **)((char *)f->esp + 4);
    if (!is_valid_string(file))
      sys_exit(-1);
    f->eax = false;
    break;
  }

  case SYS_REMOVE: {
    validate_args(f->esp, 1);
    const char *file = *(const char **)((char *)f->esp + 4);
    if (!is_valid_string(file))
      sys_exit(-1);
    f->eax = false;
    break;
  }

  case SYS_OPEN: {
    validate_args(f->esp, 1);
    const char *file = *(const char **)((char *)f->esp + 4);
    if (!is_valid_string(file))
      sys_exit(-1);
    f->eax = -1;
    break;
  }

  case SYS_FILESIZE: {
    validate_args(f->esp, 1);
    f->eax = -1;
    break;
  }

  case SYS_READ: {
    validate_args(f->esp, 3);
    int fd = *(int *)((char *)f->esp + 4);
    void *buffer = *(void **)((char *)f->esp + 8);
    unsigned size = *(unsigned *)((char *)f->esp + 12);

    if (!is_valid_buffer(buffer, size))
      sys_exit(-1);

    if (fd == 0) {
      /* STDIN keyboard read */
      uint8_t *buf = (uint8_t *)buffer;
      unsigned i;
      for (i = 0; i < size; i++)
        buf[i] = input_getc();
      f->eax = size;
    } else {
      f->eax = -1;
    }
    break;
  }

  case SYS_WRITE: {
    validate_args(f->esp, 3);
    int fd = *(int *)((char *)f->esp + 4);
    const void *buffer = *(const void **)((char *)f->esp + 8);
    unsigned size = *(unsigned *)((char *)f->esp + 12);

    if (!is_valid_buffer(buffer, size))
      sys_exit(-1);

    if (fd == 1) {
      putbuf(buffer, size);
      f->eax = size;
    } else {
      f->eax = -1;
    }
    break;
  }

  case SYS_SEEK: {
    validate_args(f->esp, 2);
    break;
  }

  case SYS_TELL: {
    validate_args(f->esp, 1);
    f->eax = 0;
    break;
  }

  case SYS_CLOSE: {
    validate_args(f->esp, 1);
    break;
  }

  default:
    sys_exit(-1);
    break;
  }
}
