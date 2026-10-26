		     +--------------------------+
		     |		CS 2043		|
		     | PROJECT 2: USER PROGRAMS	|
		     | 	   DESIGN DOCUMENT     	|
		     +--------------------------+

---- GROUP ----

>> Fill in the names and email addresses of your group members.

Nanayakkara N.S. <240449A>

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

All 80/80 test cases pass with a 100.0% score on make grade.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

1. Course laboratory specification: User Programs - CS2042/CS2043 Operating Systems (User_Programs-Lab2_new.pdf).
2. Stanford Pintos documentation & project specification: https://www.scs.stanford.edu/23wi-cs212/pintos/pintos_3.html.
3. System V Application Binary Interface (Intel386 Architecture Processor Supplement) for 32-bit x86 stack frame conventions.

			   ARGUMENT PASSING
			   ================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In userprog/process.c:

#define MAX_ARGS 64

struct process_starter {
  struct child_status *cs;
  char cmdline[PGSIZE - sizeof(struct child_status *)];
};

struct process_starter: Packs the child status pointer and raw command-line string into a single page passed safely to start_process().

---- ALGORITHMS ----

>> A2: Briefly describe how you implemented argument parsing.  How do
>> you arrange for the elements of argv[] to be in the right order?
>> How do you avoid overflowing the stack page?

1. Command-line parsing:
   In process_execute(), a copy of the command line is placed into starter->cmdline. The program name (the first token) is extracted using strtok_r() with space delimiters to name the new thread in thread_create().
   Inside start_process(), load() tokenizes the command line into distinct arguments using strtok_r(), saving each token pointer in a local array char *argv[MAX_ARGS] and counting argc.

2. Stack placement and ordering:
   In setup_stack(), the stack pointer sp begins at PHYS_BASE:
   - Argument strings are copied onto the stack in right-to-left order (argv[argc - 1] down to argv[0]). The virtual address of each string on the user stack is recorded in arg_addrs[].
   - The stack pointer is aligned to a 4-byte boundary: sp = (uint8_t *)((uintptr_t)sp & ~3).
   - A null pointer sentinel (argv[argc] = NULL) is pushed.
   - Pointers to the strings are pushed in reverse order (arg_addrs[argc - 1] down to arg_addrs[0]), which places argv[0] at the lowest address among them.
   - The address of argv[0] (the argv pointer) is pushed.
   - argc is pushed.
   - A dummy return address (0) is pushed.
   - *esp is set to sp.

3. Avoiding stack overflow:
   Pintos limits the initial user stack to 1 page (4 KiB). Before setting *esp = sp, setup_stack() verifies:
   if (sp < (uint8_t *)PHYS_BASE - PGSIZE)
   If the argument data exceeds 4 KiB, the function frees the allocated page, load() returns false, and process creation aborts.

---- RATIONALE ----

>> A3: Why does Pintos implement strtok_r() but not strtok()?

Standard strtok() stores its internal state in a hidden static global pointer. In an operating system kernel with multiple threads running concurrently, simultaneous calls to strtok() would overwrite each other's state and corrupt the strings. strtok_r() is reentrant and thread-safe because the caller explicitly passes a context pointer (char **save_ptr) to store the tokenizer state.

>> A4: In Pintos, the kernel separates commands into a executable name
>> and arguments.  In Unix-like systems, the shell does this
>> separation.  Identify at least two advantages of the Unix approach.

1. Early error handling and lower kernel overhead:
   The shell checks syntax, quotes, and wildcard expansion in user space. Invalid commands or non-existent files are caught immediately without making system calls, allocating kernel pages, or creating new threads.

2. Simpler and safer kernel code:
   The kernel execve() call takes a pre-split char *argv[] array instead of a raw command string. This keeps string parsing logic out of privileged kernel space, reducing the risk of kernel-mode buffer bugs or delimiter injection.

			     SYSTEM CALLS
			     ============

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In threads/thread.h:

struct child_status {
  tid_t tid;                  /* Child thread ID. */
  int exit_status;            /* Exit status of the child. */
  bool exited;                /* Has the child exited? */
  bool waited;                /* Has the parent already waited for it? */
  bool load_success;          /* Did the child load successfully? */
  struct semaphore load_sema; /* Synchronizes parent exec with child load. */
  struct semaphore wait_sema; /* Synchronizes parent wait with child exit. */
  struct lock ref_lock;       /* Synchronizes reference count changes. */
  struct list_elem elem;      /* Element in parent's children list. */
  int ref_count;              /* Reference count (parent and child). */
};

struct child_status: Shared status record between parent and child for load synchronization, exit status passing, wait coordination, and reference-counted memory cleanup.

In struct thread (threads/thread.h):

#ifdef USERPROG
  uint32_t *pagedir;                 /* Page directory. */
  int exit_status;                   /* Process termination exit status. */
  struct file *fd_table[128];        /* Per-process file descriptor table. */
  struct file *exec_file;            /* Running executable file (write denied). */
  struct list children;              /* List of direct child_status structures. */
  struct child_status *child_status; /* Shared status record with parent. */
#endif

- exit_status: Process exit code set on termination.
- fd_table[128]: Maps process-local file descriptors (2..127) to struct file pointers.
- exec_file: Open file pointer for the running binary, with write denied.
- children: List of child_status structures for this thread's direct children.
- child_status: Pointer to this process's own status record shared with its parent.

In userprog/syscall.h / userprog/syscall.c:

extern struct lock filesys_lock;

filesys_lock: Global lock that serializes all access to the unsynchronized base file system.

>> B2: Describe how file descriptors are associated with open files.
>> Are file descriptors unique within the entire OS or just within a
>> single process?

File descriptors are associated with open files through the array struct file *fd_table[128] stored inside each struct thread.
- Descriptors 0 and 1 are reserved for standard input and standard output.
- When a file is opened, allocate_fd() scans indices 2 through 127 for the first NULL slot and stores the struct file pointer returned by filesys_open().
- File descriptors are unique only within a single process, not across the whole operating system. Two independent processes can both have descriptor 2 open, pointing to different open files and maintaining their own file positions.

---- ALGORITHMS ----

>> B3: Describe your code for reading and writing user data from the
>> kernel.

Before reading or writing user data, all pointers and buffers are validated:
1. is_valid_buffer(buffer, size):
   - Checks that buffer is non-null (unless size is 0).
   - Checks that (uintptr_t)buffer + size does not wrap around (integer overflow).
   - Checks that start and end addresses are below PHYS_BASE via is_user_vaddr().
   - Loops through each page boundary crossed by the buffer and calls pagedir_get_page() to confirm every page is mapped in the process's page directory.
2. is_valid_string(str):
   - Confirms str is non-null and below PHYS_BASE.
   - Checks each byte address is mapped in pagedir until reaching the null terminator '\0'.
3. Reading/Writing:
   - For console read (fd 0), sys_read() reads characters with input_getc().
   - For console write (fd 1), sys_write() outputs text with putbuf().
   - For files (fd >= 2), the struct file pointer is looked up in fd_table. The function acquires filesys_lock, calls file_read() or file_write(), and releases filesys_lock.

>> B4: Suppose a system call causes a full page (4,096 bytes) of data
>> to be copied from user space into the kernel.  What is the least
>> and the greatest possible number of inspections of the page table
>> (e.g. calls to pagedir_get_page()) that might result?  What about
>> for a system call that only copies 2 bytes of data?  Is there room
>> for improvement in these numbers, and how much?

1. For a 4,096-byte copy:
   - Least inspections: 1 inspection. If the buffer is exactly page-aligned (starting at offset 0 of a page), the entire 4,096 bytes fall within that single page.
   - Greatest inspections: 2 inspections. If the buffer is not page-aligned (e.g. starts at offset 4), it crosses into the next page, so both pages must be inspected.

2. For a 2-byte copy:
   - Least inspections: 1 inspection. If both bytes lie within the same virtual page (offsets 0..4094).
   - Greatest inspections: 2 inspections. If the 2 bytes span across a page boundary (byte 0 at offset 4095 of page N, and byte 1 at offset 0 of page N+1).

3. Room for improvement:
   Yes. Instead of verifying each page table entry in software with pagedir_get_page() before copying, the kernel could access user memory directly using inline assembly instructions (get_user and put_user) and rely on the CPU MMU hardware. If an address is invalid, the hardware triggers a page fault, which page_fault() catches and handles by killing the process. This drops the software page table inspection overhead to 0 in the normal case.

>> B5: Briefly describe your implementation of the "wait" system call
>> and how it interacts with process termination.

1. When a parent calls wait(pid):
   - process_wait() searches cur->children for a child_status record matching pid.
   - If no matching child is found, or if cs->waited is already true, it returns -1 immediately.
   - It marks cs->waited = true and calls sema_down(&cs->wait_sema) to wait for the child.
2. When the child exits:
   - In process_exit(), the child sets its cur->child_status->exit_status to cur->exit_status and marks exited = true.
   - It unblocks the parent by calling sema_up(&cs->wait_sema).
   - It drops its reference to cs by calling child_status_unref(cs).
3. Parent resumption:
   - The parent wakes up from sema_down(), reads cs->exit_status, removes cs from cur->children, drops its own reference via child_status_unref(cs), and returns the exit status.

>> B6: Any access to user program memory at a user-specified address
>> can fail due to a bad pointer value.  Such accesses must cause the
>> process to be terminated.  System calls are fraught with such
>> accesses, e.g. a "write" system call requires reading the system
>> call number from the user stack, then each of the call's three
>> arguments, then an arbitrary amount of user memory, and any of
>> these can fail at any point.  This poses a design and
>> error-handling problem: how do you best avoid obscuring the primary
>> function of code in a morass of error-handling?  Furthermore, when
>> an error is detected, how do you ensure that all temporarily
>> allocated resources (locks, buffers, etc.) are freed?  In a few
>> paragraphs, describe the strategy or strategies you adopted for
>> managing these issues.  Give an example.

I handle this with two clean strategies: early validation at the system call boundary, and centralized resource cleanup in process_exit().

First, syscall_handler() checks arguments up front before any system call action or lock acquisition. When the handler is called, it verifies that f->esp points to valid user memory. It casts f->esp to uint32_t *args and reads args[0] (the syscall number). In each switch case, a single call to validate_args(args, argc) verifies all argument words on the user stack. String pointers and buffer addresses are validated with is_valid_string() and is_valid_buffer() before opening files or acquiring locks. If validation fails, sys_exit(-1) is called right away, avoiding partial operations or complex unwinding.

Second, resource reclamation is centralized entirely in process_exit(). Because every termination path (normal exit, syscall error via sys_exit(-1), or a kernel page fault) runs through thread_exit() and process_exit(), process_exit() serves as the single cleanup location:
- It closes all open file descriptors in cur->fd_table (indices 2..127).
- It re-allows writes to cur->exec_file and closes it.
- It checks if (lock_held_by_current_thread(&filesys_lock)) lock_release(&filesys_lock), ensuring the file system lock is never left locked.
- It unrefs all child status records and signals any waiting parent.

For example, if write(fd, bad_buffer, size) is called with an invalid pointer, sys_write() calls is_valid_buffer(bad_buffer, size) before acquiring filesys_lock. If the check fails, sys_exit(-1) terminates the process immediately without any locks held, so no cleanup rollbacks are needed.

---- SYNCHRONIZATION ----

>> B7: The "exec" system call returns -1 if loading the new executable
>> fails, so it cannot return before the new executable has completed
>> loading.  How does your code ensure this?  How is the load
>> success/failure status passed back to the thread that calls "exec"?

I synchronize this using the load_sema semaphore inside struct child_status:
1. process_execute() allocates a struct child_status with load_success initialized to false and sema_init(&cs->load_sema, 0).
2. It bundles cs and the command line into a starter struct and calls thread_create().
3. The parent immediately calls sema_down(&cs->load_sema) to wait for the child.
4. The child starts in start_process(), unpacks starter, attaches cur->child_status = starter->cs, and calls load().
5. The child sets cur->child_status->load_success = success, frees the starter page, and calls sema_up(&cur->child_status->load_sema). If loading failed, the child calls thread_exit().
6. The parent wakes up from sema_down(). It checks cs->load_success: if false, it removes cs from its children list, drops its reference via child_status_unref(cs), and returns TID_ERROR (-1); if true, it returns cs->tid.

>> B8: Consider parent process P with child process C.  How do you
>> ensure proper synchronization and avoid race conditions when P
>> calls wait(C) before C exits?  After C exits?  How do you ensure
>> that all resources are freed in each case?  How about when P
>> terminates without waiting, before C exits?  After C exits?  Are
>> there any special cases?

Each child status structure uses reference counting (ref_count initialized to 2, one reference for P and one for C) protected by ref_lock, along with wait_sema initialized to 0:

- Case 1: P calls wait(C) before C exits:
  P calls sema_down(&cs->wait_sema) and blocks. When C exits, C saves its exit code in cs->exit_status, marks cs->exited = true, calls sema_up(&cs->wait_sema), and calls child_status_unref(cs) (lowering ref_count from 2 to 1). P wakes up, reads cs->exit_status, removes cs from cur->children, calls child_status_unref(cs) (lowering ref_count from 1 to 0), and frees cs.

- Case 2: P calls wait(C) after C exits:
  C already exited, stored exit_status, signaled sema_up(&wait_sema) (semaphore value is 1), and dropped its reference (ref_count becomes 1). When P calls wait(C), sema_down(&cs->wait_sema) succeeds immediately without blocking. P reads exit_status, calls child_status_unref(cs) (ref_count becomes 0), and frees cs.

- Case 3: P terminates without waiting, before C exits:
  When P exits, process_exit() iterates through cur->children and calls child_status_unref() on every child record (ref_count drops from 2 to 1). When C exits later, C calls child_status_unref() (ref_count drops from 1 to 0) and frees cs.

- Case 4: P terminates without waiting, after C exits:
  C already dropped its reference when it exited (ref_count became 1). When P exits later, process_exit() drops P's reference (ref_count drops from 1 to 0) and frees cs.

Special cases handled:
- Calling wait(C) twice: cs->waited flag ensures any subsequent wait call returns -1 immediately.
- Waiting on non-children or grandchildren: P only searches its own direct cur->children list, returning -1 for any foreign PID.

---- RATIONALE ----

>> B9: Why did you choose to implement access to user memory from the
>> kernel in the way that you did?

I chose the validate-then-access verification approach (is_valid_buffer and is_valid_string) using pagedir_get_page() and is_user_vaddr().
Reasons:
1. Simpler control flow and debugging: checking user addresses explicitly before dereferencing them avoids having kernel code trigger unexpected page faults while in privileged mode.
2. Safer lock handling: because pointers and buffers are validated before acquiring filesys_lock, invalid pointers never risk leaving kernel locks held or file systems in an inconsistent state.
3. Portability: the validation logic is written entirely in portable C without requiring architecture-specific assembly page-fault recovery tables.

>> B10: What advantages or disadvantages can you see to your design
>> for file descriptors?

- Advantages:
  1. O(1) lookup speed: using a fixed array (cur->fd_table[fd]) gives instant index access without needing linked-list searches.
  2. No dynamic memory allocation: the table is embedded directly inside struct thread, avoiding malloc() calls during file open.
  3. Simple cleanup: closing open descriptors on exit is a straightforward linear loop over the 126 entries.
- Disadvantages:
  1. Static descriptor limit: each process is limited to at most 126 open files (MAX_FD = 128).
  2. Search overhead: allocate_fd() scans linearly for the lowest free slot, though with N <= 128 this overhead is negligible.

>> B11: The default tid_t to pid_t mapping is the identity mapping.
>> If you changed it, what advantages are there to your approach?

I kept the default identity mapping (pid_t == tid_t). In Pintos Project 2, every user process maps 1:1 to a single kernel thread. An identity mapping is ideal because:
1. It avoids the extra overhead of maintaining a separate process ID lookup table or hash map.
2. The thread allocator already assigns unique positive 32-bit integer IDs.
A non-identity mapping would only be needed if supporting multiple user threads within the same process (which Pintos Project 2 does not require).

			   SURVEY QUESTIONS
			   ================

>> In your opinion, was this assignment, or any one of the three problems
>> in it, too easy or too hard?  Did it take too long or too little time?

The assignment is well-balanced. Getting parent-child synchronization and pointer validation right across page boundaries took careful work.

>> Did you find that working on a particular part of the assignment gave
>> you greater insight into some aspect of OS design?

Setting up the stack according to the 32-bit x86 calling convention in setup_stack() and managing process lifetimes with reference-counted child structures gave a solid understanding of how operating systems manage user processes.

>> Is there some particular fact or hint we should give students in
>> future quarters to help them solve the problems?  Conversely, did you
>> find any of our guidance to be misleading?

Make sure to note that modern 64-bit toolchains default to 2 MiB page alignment when compiling 32-bit binaries, which inflates binary sizes and exhausts Pintos RAM unless -Wl,-z,max-page-size=4096 is added to LDFLAGS.
