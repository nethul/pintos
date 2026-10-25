#include "userprog/syscall.h"
#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>

#define MAX_FD 128

/* Global lock to serialize all operations on the unsynchronized Pintos file system. */
struct lock filesys_lock;

static void syscall_handler(struct intr_frame *);

void syscall_init(void) {
  lock_init(&filesys_lock);
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
   immediately above the syscall number (i.e. at args + 1). */
static void validate_args(const uint32_t *args, size_t argc) {
  if (!is_valid_buffer(args + 1, argc * sizeof(uint32_t)))
    sys_exit(-1);
}

/* Allocates the lowest available file descriptor (>= 2) for the current thread. */
static int allocate_fd(struct file *file) {
  struct thread *cur = thread_current();
  int fd;
  for (fd = 2; fd < MAX_FD; fd++) {
    if (cur->fd_table[fd] == NULL) {
      cur->fd_table[fd] = file;
      return fd;
    }
  }
  return -1;
}

/* Retrieves the file pointer associated with fd in the current thread's table. */
static struct file *get_file(int fd) {
  if (fd < 2 || fd >= MAX_FD)
    return NULL;
  return thread_current()->fd_table[fd];
}

/* Terminates Pintos by shutting down the power. */
static void sys_halt(void) {
  shutdown_power_off();
}

/* Starts a child process executing the given command line. */
static tid_t sys_exec(const char *cmd_line) {
  if (!is_valid_string(cmd_line))
    sys_exit(-1);
  return process_execute(cmd_line);
}

/* Waits for child process with thread id pid to terminate. */
static int sys_wait(int pid) {
  return process_wait(pid);
}

/* Creates a new file with the specified name and initial size. */
static bool sys_create(const char *file, off_t initial_size) {
  if (!is_valid_string(file))
    sys_exit(-1);

  lock_acquire(&filesys_lock);
  bool success = filesys_create(file, initial_size);
  lock_release(&filesys_lock);
  return success;
}

/* Deletes the specified file from the filesystem. */
static bool sys_remove(const char *file) {
  if (!is_valid_string(file))
    sys_exit(-1);

  lock_acquire(&filesys_lock);
  bool success = filesys_remove(file);
  lock_release(&filesys_lock);
  return success;
}

/* Opens the specified file and assigns it a new per-process file descriptor. */
static int sys_open(const char *file) {
  if (!is_valid_string(file))
    sys_exit(-1);

  lock_acquire(&filesys_lock);
  struct file *opened = filesys_open(file);
  lock_release(&filesys_lock);

  if (opened == NULL)
    return -1;

  int fd = allocate_fd(opened);
  if (fd == -1) {
    lock_acquire(&filesys_lock);
    file_close(opened);
    lock_release(&filesys_lock);
    return -1;
  }
  return fd;
}

/* Returns the size, in bytes, of the open file referred to by fd. */
static int sys_filesize(int fd) {
  struct file *f_obj = get_file(fd);
  if (f_obj == NULL)
    return -1;

  lock_acquire(&filesys_lock);
  int len = file_length(f_obj);
  lock_release(&filesys_lock);
  return len;
}

/* Reads size bytes from fd into buffer. Returns number of bytes actually read. */
static int sys_read(int fd, void *buffer, unsigned size) {
  if (!is_valid_buffer(buffer, size))
    sys_exit(-1);

  if (fd == 0) {
    uint8_t *buf = (uint8_t *)buffer;
    unsigned i;
    for (i = 0; i < size; i++)
      buf[i] = input_getc();
    return size;
  }
  if (fd == 1)
    return -1;

  struct file *f_obj = get_file(fd);
  if (f_obj == NULL)
    return -1;

  lock_acquire(&filesys_lock);
  int bytes_read = file_read(f_obj, buffer, size);
  lock_release(&filesys_lock);
  return bytes_read;
}

/* Writes size bytes from buffer to fd. Returns number of bytes written. */
static int sys_write(int fd, const void *buffer, unsigned size) {
  if (!is_valid_buffer(buffer, size))
    sys_exit(-1);

  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }
  if (fd == 0)
    return -1;

  struct file *f_obj = get_file(fd);
  if (f_obj == NULL)
    return -1;

  lock_acquire(&filesys_lock);
  int bytes_written = file_write(f_obj, buffer, size);
  lock_release(&filesys_lock);
  return bytes_written;
}

/* Changes next byte position to be read/written in open file fd. */
static void sys_seek(int fd, unsigned position) {
  struct file *f_obj = get_file(fd);
  if (f_obj != NULL) {
    lock_acquire(&filesys_lock);
    file_seek(f_obj, position);
    lock_release(&filesys_lock);
  }
}

/* Returns position of next byte to be read or written in fd. */
static int sys_tell(int fd) {
  struct file *f_obj = get_file(fd);
  if (f_obj == NULL)
    return -1;

  lock_acquire(&filesys_lock);
  int pos = file_tell(f_obj);
  lock_release(&filesys_lock);
  return pos;
}

/* Closes file descriptor fd and frees slot in thread's fd table. */
static void sys_close(int fd) {
  if (fd >= 2 && fd < MAX_FD) {
    struct thread *cur = thread_current();
    struct file *f_obj = cur->fd_table[fd];
    if (f_obj != NULL) {
      cur->fd_table[fd] = NULL;
      lock_acquire(&filesys_lock);
      file_close(f_obj);
      lock_release(&filesys_lock);
    }
  }
}

static void syscall_handler(struct intr_frame *f) {
  /* Ensure stack pointer holds at least the syscall number */
  if (!is_valid_buffer(f->esp, sizeof(uint32_t)))
    sys_exit(-1);

  uint32_t *args = (uint32_t *)f->esp;
  int syscall_nr = args[0];

  switch (syscall_nr) {
  case SYS_HALT:
    sys_halt();
    break;

  case SYS_EXIT:
    validate_args(args, 1);
    sys_exit((int)args[1]);
    break;

  case SYS_EXEC:
    validate_args(args, 1);
    f->eax = sys_exec((const char *)args[1]);
    break;

  case SYS_WAIT:
    validate_args(args, 1);
    f->eax = sys_wait((int)args[1]);
    break;

  case SYS_CREATE:
    validate_args(args, 2);
    f->eax = sys_create((const char *)args[1], (off_t)args[2]);
    break;

  case SYS_REMOVE:
    validate_args(args, 1);
    f->eax = sys_remove((const char *)args[1]);
    break;

  case SYS_OPEN:
    validate_args(args, 1);
    f->eax = sys_open((const char *)args[1]);
    break;

  case SYS_FILESIZE:
    validate_args(args, 1);
    f->eax = sys_filesize((int)args[1]);
    break;

  case SYS_READ:
    validate_args(args, 3);
    f->eax = sys_read((int)args[1], (void *)args[2], (unsigned)args[3]);
    break;

  case SYS_WRITE:
    validate_args(args, 3);
    f->eax = sys_write((int)args[1], (const void *)args[2], (unsigned)args[3]);
    break;

  case SYS_SEEK:
    validate_args(args, 2);
    sys_seek((int)args[1], (unsigned)args[2]);
    break;

  case SYS_TELL:
    validate_args(args, 1);
    f->eax = sys_tell((int)args[1]);
    break;

  case SYS_CLOSE:
    validate_args(args, 1);
    sys_close((int)args[1]);
    break;

  default:
    sys_exit(-1);
    break;
  }
}
