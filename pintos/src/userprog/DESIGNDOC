		     +--------------------------+
		     |		CS 2043		|
		     | PROJECT 2: USER PROGRAMS	|
		     | 	   DESIGN DOCUMENT     	|
		     +--------------------------+

---- GROUP ----

>> Fill in the names and email addresses of your group members.

Nanayakkara N.S. <240449A@uom.lk>

---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the
>> TAs, or extra credit, please give them here.

All 80/80 test cases pass with a 100.0% score on `make grade`.

>> Please cite any offline or online sources you consulted while
>> preparing your submission, other than the Pintos documentation, course
>> text, lecture notes, and course staff.

1. Course laboratory specification: User Programs — CS2042/CS2043 Operating Systems (User_Programs-Lab2_new.pdf).
2. Stanford Pintos documentation & project specification: https://www.scs.stanford.edu/23wi-cs212/pintos/pintos_3.html.
3. System V Application Binary Interface (Intel386 Architecture Processor Supplement) for 32-bit x86 stack frame conventions.

			   ARGUMENT PASSING
			   ================

---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In `userprog/process.c`:
```c
#define MAX_ARGS 64

struct process_starter {
  struct child_status *cs;
  char cmdline[PGSIZE - sizeof(struct child_status *)];
};
```
`struct process_starter`: Encapsulates the child status pointer and raw command-line string into a single allocated page passed safely to `start_process()`.

---- ALGORITHMS ----

>> A2: Briefly describe how you implemented argument parsing.  How do
>> you arrange for the elements of argv[] to be in the right order?
>> How do you avoid overflowing the stack page?

1. Command-line parsing:
   In `process_execute()`, a copy of the command-line string is placed into `starter->cmdline`. The program name (first token) is extracted using `strtok_r()` with space delimiters to name the thread in `thread_create()`.
   Inside `start_process()`, `load()` tokenizes the command-line into distinct arguments using `strtok_r()`, storing string pointers in a local array `char *argv[MAX_ARGS]` and counting `argc`.

2. Stack placement & ordering:
   In `setup_stack()`, the stack pointer begins at `PHYS_BASE`:
   - Argument strings are copied onto the stack in right-to-left order (`argv[argc - 1]` down to `argv[0]`). Their resulting user-virtual addresses are stored in a temporary array `arg_addrs[]`.
   - The stack pointer is rounded down to a 4-byte boundary for word alignment: `sp = (uint8_t *)((uintptr_t)sp & ~3)`.
   - A null pointer sentinel (`argv[argc] = NULL`) is pushed.
   - Pointers to the argument strings are pushed in reverse order (`arg_addrs[argc - 1]` down to `arg_addrs[0]`). This places `argv[0]` at the lowest memory address in the array.
   - The address of `argv[0]` (i.e. `argv` itself) is pushed.
   - `argc` is pushed.
   - A fake return address (dummy `NULL` pointer) is pushed.
   - `*esp` is assigned to `sp`.

3. Avoiding stack overflow:
   Pintos restricts the initial user stack to a single page (4 KiB). Before setting `*esp = sp`, `setup_stack()` checks:
   `if (sp < (uint8_t *)PHYS_BASE - PGSIZE)`
   If the total argument data exceeds one page, the function frees the allocated stack page, fails `load()`, and aborts process creation.

---- RATIONALE ----

>> A3: Why does Pintos implement strtok_r() but not strtok()?

Standard `strtok()` stores its internal parsing state in a hidden static global pointer. In a multi-threaded operating system kernel where multiple threads may parse strings concurrently, calls to `strtok()` would corrupt each other's parsing state. `strtok_r()` is reentrant and thread-safe because the caller explicitly provides a context pointer (`char **save_ptr`) to store the tokenizer state.

>> A4: In Pintos, the kernel separates commands into a executable name
>> and arguments.  In Unix-like systems, the shell does this
>> separation.  Identify at least two advantages of the Unix approach.

1. User-Level Fault Isolation & Performance:
   Shell-side parsing handles command validation, syntax checking, quotes, and wildcard expansion entirely in user space. Malformed commands or non-existent files are rejected before invoking expensive kernel context switches, memory allocation, and thread creation.

2. Minimal Kernel Attack Surface:
   The kernel `execve()` system call accepts a pre-tokenized `char *argv[]` array rather than a raw command-line string. This eliminates complex string parsing from privileged ring 0, reducing kernel vulnerability to buffer overflows or delimiter injection attacks.

			     SYSTEM CALLS
			     ============

---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct' or
>> `struct' member, global or static variable, `typedef', or
>> enumeration.  Identify the purpose of each in 25 words or less.

In `threads/thread.h`:
```c
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
```
`struct child_status`: Shared tracking record between parent and child for load synchronization, exit status propagation, waiting, and ref-counted lifetime management.

In `struct thread` (`threads/thread.h`):
```c
#ifdef USERPROG
  uint32_t *pagedir;                 /* Page directory. */
  int exit_status;                   /* Process termination exit status. */
  struct file *fd_table[128];        /* Per-process file descriptor table. */
  struct file *exec_file;            /* Running executable file (write denied). */
  struct list children;              /* List of direct child_status structures. */
  struct child_status *child_status; /* Shared status record with parent. */
#endif
```
- `exit_status`: Exit code recorded by `sys_exit()` or kernel exception.
- `fd_table[128]`: Maps process-local file descriptors (2..127) to open `struct file` pointers.
- `exec_file`: Open file pointer for the running executable, held with write denied.
- `children`: List of `struct child_status` records for this thread's direct children.
- `child_status`: Pointer to this thread's own `child_status` record held by its parent.

In `userprog/syscall.h` / `userprog/syscall.c`:
```c
extern struct lock filesys_lock;
```
`filesys_lock`: Global mutual exclusion lock serializing all operations across the thread-unsafe Pintos base file system.

>> B2: Describe how file descriptors are associated with open files.
>> Are file descriptors unique within the entire OS or just within a
>> single process?

File descriptors are associated with open files via the thread's private `fd_table` array (`struct file *fd_table[128]`). 
- Descriptors 0 and 1 are reserved for standard input and standard output, respectively.
- When `sys_open()` opens a file, `allocate_fd()` iterates from index 2 to 127 and assigns the first `NULL` entry to the returned `struct file *`.
- File descriptors are unique **only within a single process**, not across the entire OS. Two separate processes can have descriptor 2 open concurrently, each indexing their own independent file objects and file offsets.

---- ALGORITHMS ----

>> B3: Describe your code for reading and writing user data from the
>> kernel.

User memory validation is performed prior to any dereference:
1. `is_valid_buffer(buffer, size)`:
   - Verifies `buffer` is non-NULL (when `size > 0`).
   - Ensures `(uintptr_t)buffer + size` does not wrap around (integer overflow check).
   - Confirms start and end addresses are below `PHYS_BASE` via `is_user_vaddr()`.
   - Iterates through every virtual page boundary between `pg_round_down(buffer)` and `buffer + size - 1`, calling `pagedir_get_page(thread_current()->pagedir, page)` to ensure each page is mapped in user memory.
2. `is_valid_string(str)`:
   - Ensures `str` is non-NULL and below `PHYS_BASE`.
   - Traverses byte-by-byte verifying each byte is below `PHYS_BASE` and mapped in `pagedir` until the terminating `'\0'` is encountered.
3. Reading/Writing:
   - Console: For fd 0, `sys_read()` reads keystrokes using `input_getc()`. For fd 1, `sys_write()` writes through `putbuf()`.
   - Files: For fd >= 2, the file pointer is retrieved from `fd_table`. The operation acquires `filesys_lock`, invokes `file_read()` or `file_write()`, and releases `filesys_lock`.

>> B4: Suppose a system call causes a full page (4,096 bytes) of data
>> to be copied from user space into the kernel.  What is the least
>> and the greatest possible number of inspections of the page table
>> (e.g. calls to pagedir_get_page()) that might result?  What about
>> for a system call that only copies 2 bytes of data?  Is there room
>> for improvement in these numbers, and how much?

1. For a full 4,096-byte copy:
   - Least possible inspections: 1 inspection. This occurs if the buffer is exactly page-aligned (e.g. starting at offset 0 of a virtual page), so the entire 4,096 bytes fit within that single page.
   - Greatest possible inspections: 2 inspections. If the buffer is not page-aligned (e.g. starts at offset 4), it straddles two consecutive virtual pages, requiring inspection of both pages.

2. For a 2-byte copy:
   - Least possible inspections: 1 inspection. If both bytes reside in the same page (offsets 0..4094).
   - Greatest possible inspections: 2 inspections. If the 2 bytes span across a 4 KiB page boundary (byte 0 at offset 4095 of page N, and byte 1 at offset 0 of page N+1).

3. Room for improvement:
   Yes, significantly. Instead of inspecting the software page tables via `pagedir_get_page()` prior to every transfer, the kernel can access user memory directly using inline assembly (`get_user` / `put_user`) and rely on the CPU's MMU hardware. If an address is invalid, the MMU triggers a page fault, which `page_fault()` catches and handles by terminating the process. This eliminates software page table lookups entirely (reducing overhead to 0 software inspections in the common path).

>> B5: Briefly describe your implementation of the "wait" system call
>> and how it interacts with process termination.

1. When parent calls `sys_wait(pid)`:
   - `process_wait()` searches `cur->children` for a `child_status` record matching `pid`.
   - If not found (not a direct child) or if `cs->waited` is already true, it returns -1 immediately.
   - It sets `cs->waited = true` and blocks by calling `sema_down(&cs->wait_sema)`.
2. When the child process terminates:
   - In `process_exit()`, the child sets its `cur->child_status->exit_status = cur->exit_status` and marks `exited = true`.
   - It unblocks the parent via `sema_up(&cs->wait_sema)`.
   - It decrements its reference to `cs` via `child_status_unref(cs)` and cleans up its own resources.
3. Waking up:
   - The parent wakes from `sema_down()`, reads `cs->exit_status`, removes `cs` from `cur->children`, drops its own reference via `child_status_unref(cs)`, and returns the status.

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

We separate error handling into two distinct architectural tiers: proactive early validation at the boundary, and centralized cleanup in `process_exit()`.

First, `syscall_handler()` avoids deep nested checks by validating all input arguments up front before any system call logic or lock acquisition begins. When `syscall_handler()` is entered, it verifies that `f->esp` points to valid memory. It casts `f->esp` to `uint32_t *args` and reads `args[0]` (the syscall number). In each switch case, a single call to `validate_args(args, argc)` validates all stack argument words in one contiguous check. String arguments and I/O buffers are validated using `is_valid_string()` or `is_valid_buffer()` before any kernel lock or resource is acquired. If validation fails, `sys_exit(-1)` is called immediately without messy partial rollbacks.

Second, resource reclamation is centralized entirely in `process_exit()`. Because every termination path (normal exit, syscall validation failure via `sys_exit(-1)`, or unhandled exception in `exception.c`) must execute `thread_exit()` -> `process_exit()`, `process_exit()` acts as the single cleanup barrier:
- It closes all open file descriptors in `fd_table` (fds 2..127).
- It re-allows writes to `cur->exec_file` and closes it.
- It checks `if (lock_held_by_current_thread(&filesys_lock)) lock_release(&filesys_lock);`, guaranteeing the global file system lock is never left orphaned.
- It unrefs all child status records and unblocks any waiting parent.

Example: If `write(fd, bad_buffer, size)` is invoked with an unmapped buffer, `sys_write()` validates `bad_buffer` with `is_valid_buffer()` before calling `lock_acquire(&filesys_lock)`. The failure triggers `sys_exit(-1)` immediately, ensuring zero acquired locks or allocated descriptors need unwinding.

---- SYNCHRONIZATION ----

>> B7: The "exec" system call returns -1 if loading the new executable
>> fails, so it cannot return before the new executable has completed
>> loading.  How does your code ensure this?  How is the load
>> success/failure status passed back to the thread that calls "exec"?

Synchronization is handled by `load_sema` inside `struct child_status`:
1. `process_execute()` allocates a `struct child_status` initialized with `load_success = false` and `sema_init(&cs->load_sema, 0)`.
2. It packs `cs` and the command line into `starter` and calls `thread_create()`.
3. The parent immediately blocks on `sema_down(&cs->load_sema)`.
4. The child thread begins in `start_process()`, unpacks `starter`, sets `cur->child_status = starter->cs`, and executes `load()`.
5. The child sets `cur->child_status->load_success = success`, frees `starter`, and calls `sema_up(&cur->child_status->load_sema)`. If loading failed, the child terminates with `thread_exit()`.
6. The parent wakes up from `sema_down()`. It inspects `cs->load_success`: if false, it removes `cs` from `children`, drops its reference via `child_status_unref(cs)`, and returns `TID_ERROR` (-1); if true, it returns `cs->tid`.

>> B8: Consider parent process P with child process C.  How do you
>> ensure proper synchronization and avoid race conditions when P
>> calls wait(C) before C exits?  After C exits?  How do you ensure
>> that all resources are freed in each case?  How about when P
>> terminates without waiting, before C exits?  After C exits?  Are
>> there any special cases?

Each child status record uses reference counting (`ref_count = 2`, one for parent and one for child) protected by `ref_lock`, along with `wait_sema` initialized to 0:

- Case 1: P calls `wait(C)` before C exits:
  P calls `sema_down(&cs->wait_sema)` and sleeps. When C exits, C writes its exit code to `cs->exit_status`, marks `cs->exited = true`, calls `sema_up(&cs->wait_sema)`, and calls `child_status_unref(cs)` (lowering `ref_count` from 2 to 1). P wakes up, reads `cs->exit_status`, removes `cs` from `cur->children`, calls `child_status_unref(cs)` (lowering `ref_count` from 1 to 0), and frees `cs`.

- Case 2: P calls `wait(C)` after C exits:
  C already exited, stored `exit_status`, called `sema_up(&wait_sema)` (semaphore value becomes 1), and dropped its reference (`ref_count` becomes 1). When P calls `wait(C)`, `sema_down(&cs->wait_sema)` succeeds immediately without blocking. P reads `exit_status`, calls `child_status_unref(cs)` (`ref_count` becomes 0), and frees `cs`.

- Case 3: P terminates without waiting, before C exits:
  When P exits, `process_exit()` iterates through `cur->children` and calls `child_status_unref()` on all child status records (`ref_count` drops from 2 to 1). When C later terminates, C calls `child_status_unref()` (`ref_count` drops from 1 to 0) and frees `cs`.

- Case 4: P terminates without waiting, after C exits:
  C already dropped its reference upon exit (`ref_count` = 1). When P exits, `process_exit()` drops P's reference (`ref_count` drops from 1 to 0) and frees `cs`.

Special cases handled:
- Repeated `wait(C)` calls: `cs->waited` flag ensures subsequent calls immediately return -1.
- Waiting on non-children / grandchildren: P strictly searches its own `cur->children` list, rejecting foreign PIDs with -1.

---- RATIONALE ----

>> B9: Why did you choose to implement access to user memory from the
>> kernel in the way that you did?

We selected the validate-then-access verification approach (`is_valid_buffer` and `is_valid_string`) using `pagedir_get_page()` and `is_user_vaddr()`. 
Reasons:
1. Robustness & Predictable Control Flow: Explicit verification before dereference guarantees that invalid user pointers never trigger kernel-mode page faults.
2. Clean Lock Invariants: Because validation occurs before acquiring `filesys_lock` or allocating resources, error exits never risk leaking locks or leaving shared data structures partially modified.
3. Maintainability: The logic is written entirely in portable C without architecture-dependent inline assembly page fault fixup tables.

>> B10: What advantages or disadvantages can you see to your design
>> for file descriptors?

- Advantages:
  1. O(1) Access Time: Fixed-size direct indexing (`cur->fd_table[fd]`) provides immediate access without list traversals.
  2. Zero Dynamic Heap Allocation: The table is embedded directly within `struct thread`, eliminating `malloc()` overhead during `open()`.
  3. Simple Cleanup: Closing all open files at process exit is a simple linear iteration over 126 array entries.
- Disadvantages:
  1. Static Descriptor Limit: The process can open at most 126 concurrent files (`MAX_FD = 128`).
  2. Fragmentation Search: Descriptor allocation performs a linear scan for the lowest free slot, though with $N \le 128$ the CPU cost is negligible.

>> B11: The default tid_t to pid_t mapping is the identity mapping.
>> If you changed it, what advantages are there to your approach?

We retained the identity mapping (`pid_t == tid_t`). In Pintos Project 2, each user process consists of exactly one kernel thread. An identity mapping is optimal because:
1. It eliminates the overhead and complexity of maintaining a global process table or pid-to-thread hash map.
2. Thread IDs already provide unique 32-bit positive integers managed by the thread allocator.
A non-identity mapping would only be advantageous in Project 3/4 if supporting multithreaded processes (where multiple threads share a single process ID), which Pintos does not require.

			   SURVEY QUESTIONS
			   ================

>> In your opinion, was this assignment, or any one of the three problems
>> in it, too easy or too hard?  Did it take too long or too little time?

The assignment is well-balanced. Handling process synchronization and pointer safety across page boundaries required careful attention to detail.

>> Did you find that working on a particular part of the assignment gave
>> you greater insight into some aspect of OS design?

Implementing the System V calling convention in `setup_stack()` and synchronizing parent-child process lifetimes via reference-counted status blocks gave deep insight into real-world OS process management.

>> Is there some particular fact or hint we should give students in
>> future quarters to help them solve the problems?  Conversely, did you
>> find any of our guidance to be misleading?

Emphasize early that modern 64-bit host toolchains (`ld`) default to 2 MiB page alignment when generating 32-bit user binaries (`-m32`), which can exhaust Pintos RAM during recursion unless `-Wl,-z,max-page-size=4096` is specified.
