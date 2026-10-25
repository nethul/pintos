# Design Document — Pintos Project 2: User Programs

**Course:** CS2042 / CS2043 — Operating Systems  
**Project:** User Programs (Lab 2)  
**Name / Student ID:** Nanayakkara N.S / 240449A  
**Date:** October 2026

---

## 1. Overview

Pintos already loads ELF executables, but user programs cannot pass arguments, safely interact with kernel memory, terminate cleanly, wait for children, or use files. This project adds:

1. Command-line argument passing.
2. Safe access to user memory from system calls.
3. System-call dispatch and implementation.
4. Process termination messages.
5. Process creation, execution, waiting, and exit-status propagation.
6. File-system system calls with per-process file descriptors.
7. Synchronization around the provided, unsynchronized file system.

The implementation is concentrated in `userprog/process.c`, `userprog/process.h`, `userprog/syscall.c`, `userprog/syscall.h`, and `userprog/exception.c`.

---

## 2. Argument Passing

### Requirement

`process_execute("grep foo bar")` must start `grep` with arguments `foo` and `bar`. Repeated spaces are treated as one delimiter. The command line is limited to a reasonable size, such as one 4 KiB page.

### Design

- Copy the command-line string into kernel memory or a temporary buffer before parsing it, because `process_execute()` receives a user/kernel string that must remain valid while the child starts.
- Tokenize the command line using `strtok_r()` from `lib/string.c`, using space as the delimiter.
- Use the first token as the executable name passed to `process_execute()`/`start_process()`.
- Pass the remaining tokens to the child through the child’s initial stack.

### Stack setup

In `setup_stack()`, arguments are pushed in the conventional 32-bit x86 order:

1. Push each argument string, including its terminating null byte.
2. Record the address of each string.
3. Apply word alignment so that the final stack pointer is 4-byte aligned.
4. Push a null `argv[argc]` sentinel.
5. Push the argument addresses in reverse order, so `argv[0]` is at the lowest address among them.
6. Push `argv`.
7. Push `argc`.
8. Push a fake return address (`0`).

The final `esp` points to `argc`; the user program startup code then treats the stack as a normal C `main(int argc, char *argv[])` invocation.

### Justification

- **Copying before parsing** prevents races or invalid-pointer problems if the caller’s buffer changes or is freed.
- **`strtok_r()`** is reentrant and already provided by Pintos, so it avoids introducing a custom parser.
- **Pushing strings first, then pointers** is necessary because the pointers must refer to final stack addresses.
- **A 4 KiB argument limit** keeps the initial stack within one page and avoids unbounded stack usage.

### Files changed

- `userprog/process.c`: command-line parsing and stack construction.
- `userprog/process.h`: any helper declarations, if used.

---

## 3. User Memory Access

### Requirement

System calls receive pointers created by user programs. These pointers may be null, unmapped, partially invalid, or point into kernel memory. Invalid accesses must terminate only the offending process, never panic or corrupt the kernel.

### Design

The implementation validates every user pointer before the kernel dereferences it.

For a pointer to be valid:

- It must be non-null.
- It must be below `PHYS_BASE`.
- Every byte in the object being accessed must be mapped in the current process’s page directory.

For a buffer of length `n`, validation checks the first and last byte and, where appropriate, every page boundary crossed by the buffer. This catches buffers that begin in valid memory but extend into an unmapped page.

Validation uses `pagedir_get_page()` from `userprog/pagedir.c` together with `is_user_vaddr()` from `threads/vaddr.h`.

### Justification

- **Validate-then-access** is simpler to reason about than relying on page faults, especially for avoiding resource leaks.
- It prevents kernel-mode dereferences of invalid addresses, which could otherwise cause a kernel panic.
- Checking page boundaries is necessary because pointer validity is not guaranteed merely because the first byte is mapped.

### Error behavior

If validation fails, the system call terminates the calling process with exit status `-1` and prints the required termination message. Any locks, file descriptors, or allocated memory held by the process are released during process cleanup.

### Files changed

- `userprog/syscall.c`: Implemented pointer and memory buffer validation helpers (`is_valid_buffer`, `is_valid_string`, and `validate_args`) that ensure all accessed user memory pages are below `PHYS_BASE` and mapped in the current thread's page directory (`pagedir`), terminating invalid processes via `sys_exit(-1)`.
- `userprog/exception.c`: Retained standard page fault exception handling through `kill()`, terminating offending user processes with exit code -1.

---

## 4. System-Call Infrastructure

### Requirement

Implement the system-call handler in `userprog/syscall.c` and support the system calls specified by the lab.

### Design

When a user program executes `int $0x30`, control enters `syscall_handler()`.

1. Read the system-call number from the user stack at `esp`.
2. Validate the stack pointer and each argument location.
3. Read arguments according to the calling convention: the first argument is at `esp + 1`, the second at `esp + 2`, and so on (32-bit words).
4. Dispatch to a handler using a `switch` statement on the system-call number.
5. Place the return value, if any, in `eax`.

The supported calls are:

| System call | Return value | Main validation |
|---|---:|---|
| `halt` | none | None; shuts down Pintos |
| `exit(status)` | none | None |
| `exec(cmd_line)` | `pid_t` | Valid user string |
| `wait(pid)` | `int` | Valid pid |
| `create(file, size)` | `bool` | Valid user string |
| `remove(file)` | `bool` | Valid user string |
| `open(file)` | `int` | Valid user string |
| `filesize(fd)` | `int` | Valid fd |
| `read(fd, buffer, size)` | `int` | Valid fd and writable user buffer |
| `write(fd, buffer, size)` | `int` | Valid fd and readable user buffer |
| `seek(fd, position)` | none | Valid fd |
| `tell(fd)` | `unsigned` | Valid fd |
| `close(fd)` | none | Valid fd |

### Justification

- A `switch` dispatcher is explicit, easy to extend for Projects 3 and 4, and avoids function-pointer complexity.
- Reading arguments from the interrupted user stack follows the Pintos user-system-call convention.
- Returning errors through `eax` preserves the interface expected by `lib/user/syscall.c`.

### Files changed

- `userprog/syscall.c`: Full implementation of all system calls with pointer validation, file descriptor lookups, and global filesystem lock synchronization.
- `userprog/syscall.h`: Declaration of `syscall_init(void)`.

---

## 5. Process Control

### 5.1 Process termination messages

Whenever a user process exits, whether through `exit()` or because of an exception, Pintos prints:

```text
<process name>: exit(<status>)
```

The process name is the full executable name passed to `process_execute()`, without arguments. This message is not printed for ordinary kernel threads or for `halt`.

### Justification

The grading scripts rely on this exact format. Extra debugging output is removed before submission because it can confuse automated tests.

### 5.2 `exec`

`exec(cmd_line)` creates a child process and returns its pid.

Design:

- Validate `cmd_line` pointer and ensure the string lies entirely in valid user virtual memory below `PHYS_BASE`.
- In `process_execute()`, allocate a `struct process_starter` in a single 4 KiB kernel page (`palloc_get_page(0)`) containing both a pointer to the child status record and a copy of `cmd_line`.
- Allocate `struct child_status` via `malloc()`. Initialize:
  - `tid = TID_ERROR`
  - `exit_status = -1`
  - `exited = false`, `waited = false`, `load_success = false`
  - `ref_count = 2` (1 reference owned by parent in `cur->children`, 1 reference owned by child in `cur->child_status`)
  - `load_sema` initialized to 0
  - `wait_sema` initialized to 0
- Add `cs` to the parent’s `children` list before calling `thread_create()`.
- Spawn the new thread executing `start_process()`. In `start_process()`:
  - Set `cur->child_status = starter->cs` and `cur->child_status->tid = cur->tid`.
  - Call `load(starter->cmdline, &if_.eip, &if_.esp)`.
  - Record `cur->child_status->load_success = success`.
  - Free the `starter` page (`palloc_free_page(starter)`).
  - Signal the parent: `sema_up(&cur->child_status->load_sema)`.
  - If `!success`, call `thread_exit()`.
- The parent blocks on `sema_down(&cs->load_sema)`.
- If loading failed (`!cs->load_success`), the parent removes `cs` from `cur->children`, drops its reference via `child_status_unref(cs)`, and returns `TID_ERROR` (`-1`). Otherwise, returns `tid`.

### Justification

- Allocating a dedicated `starter` block eliminates race conditions where the command line or child status might be read after the caller modifies or frees memory.
- The load semaphore guarantees that `exec()` never returns a PID for a process that fails to load its executable.
- Discarding the failed child status immediately prevents orphaned descriptors or status records from accumulating.

### 5.3 `wait`

`wait(pid)` waits for a direct child and returns its exit status.

Design:

- Each process maintains a `children` list of `struct child_status`.
- `process_wait()` searches exclusively within the calling thread's `cur->children` list.
- If `pid` is not found (i.e. not a direct child, or a grandchild, or a foreign process), or if `cs->waited == true`, return `-1` immediately.
- Mark `cs->waited = true`.
- Block on `sema_down(&cs->wait_sema)`. (If the child already terminated, the semaphore value is 1, so `sema_down` returns immediately without sleeping).
- Read `status = cs->exit_status`.
- Remove `cs` from `cur->children` and release the parent's reference via `child_status_unref(cs)`.
- Return `status`.

If the child process was terminated by an unhandled kernel exception rather than `exit()`, `exception.c` sets `cur->exit_status = -1`, ensuring `wait()` returns `-1`.

### Justification

- **Reference Counting (`ref_count = 2`):** Guarantees zero memory leaks regardless of termination ordering:
  - If parent waits on child: child decrements ref_count on exit (to 1); parent decrements ref_count in `process_wait()` (to 0) and frees `cs`.
  - If parent exits before child: parent frees all children in `cur->children` (dropping references to 1); child decrements ref_count on exit (to 0) and frees `cs`.
  - If parent never waits on child: child drops reference on exit (to 1); parent drops reference in `process_exit()` (to 0) and frees `cs`.
- **Direct Child Lookup:** Searching only `cur->children` strictly enforces the rule that processes cannot wait on grandchildren or unrelated processes.
- **`waited` Flag & Removal:** Prevents a parent from waiting on the same child more than once.

### 5.4 `exit`

`exit(status)` records the status in `cur->exit_status`, closes all per-process file descriptors, releases the executable lock, signals waiting parents, and terminates the thread.

Cleanup sequence in `process_exit()`:
1. Print process termination message: `<name>: exit(<status>)`.
2. Close all open file descriptors (fd 2..127) while acquiring `filesys_lock`.
3. If `exec_file` is set, call `file_allow_write(cur->exec_file)` and `file_close()`, releasing `filesys_lock`.
4. Release `filesys_lock` if still held (in case of unexpected termination during I/O).
5. Clean up all children in `cur->children` list by popping each element and calling `child_status_unref()`.
6. If `cur->child_status != NULL`, set `exit_status = cur->exit_status`, set `exited = true`, call `sema_up(&cs->wait_sema)`, and drop child reference via `child_status_unref()`.
7. Destroy the process's page directory (`pagedir_destroy()`) and switch back to base kernel page directory.

### 5.5 Modern Toolchain Page-Size Alignment Fix

When compiling 32-bit user binaries (`-m32`) using modern x86_64 toolchains (binutils `ld`), the default linker segment alignment defaults to 2 MiB (`max-page-size=0x200000`). This caused ELF binaries to contain ~72 pages of zero padding between headers and code, inflating test binaries from ~40 KiB to >340 KiB and exhausting Pintos's 4 MiB physical RAM pool during deep recursion (`multi-recurse`).

By adding `-Wl,-z,max-page-size=4096 -Wl,--build-id=none` to `LDFLAGS` in `src/Makefile.userprog`, user binaries are generated with standard 4 KiB page alignment, reducing process memory footprints to 5 pages and allowing full recursive process execution without memory exhaustion.

### Files changed

- `userprog/process.c`: Implementation of `process_starter`, `child_status_unref()`, `process_execute()`, `start_process()`, `process_wait()`, `process_exit()`, and stack initialization.
- `userprog/process.h`: Declarations of `process_execute()`, `process_wait()`, `process_exit()`, and `process_activate()`.
- `userprog/syscall.c`: Handlers for `SYS_EXEC`, `SYS_WAIT`, and `SYS_EXIT`.
- `threads/thread.h`: Added `struct child_status`, extended `struct thread` with `children` list, `child_status`, `exit_status`, `fd_table[128]`, and `exec_file`.
- `threads/thread.c`: Initialization of `children` list and userprog state in `init_thread()`.
- `Makefile.userprog`: Added `-Wl,-z,max-page-size=4096 -Wl,--build-id=none` to `LDFLAGS`.

---

## 6. File-System System Calls

### Requirement

Implement `create`, `remove`, `open`, `filesize`, `read`, `write`, `seek`, `tell`, and `close`, with independent per-process file descriptors.

### Design

Each process has its own file-descriptor table. File descriptors:

- Reserve 0 for standard input and 1 for standard output.
- Begin allocating from 2.
- Are not inherited by child processes.
- Are independent even when two descriptors refer to the same underlying file.
- Are closed automatically when the owning process exits.

A file-descriptor entry stores the `struct file *` returned by the file system and enough information to remove the entry safely.

For console I/O:

- `read(0, ...)` reads from the keyboard using `input_getc()`.
- `write(1, ...)` writes the buffer using `putbuf()`.
- Large console writes may be split into chunks so output from different processes is less likely to interleave.

### Justification

- Per-process descriptor tables match Unix-like semantics and prevent one process from closing another process’s file.
- Storing `struct file *` separately for each open call preserves independent file offsets.
- Using `putbuf()` for console output prevents character-by-character interleaving.
- Closing descriptors during process exit prevents file-handle leaks.

### Synchronization

The provided file system has no internal synchronization. Therefore, every file-system operation is protected by a global file-system lock.

The lock is acquired before calling functions such as `filesys_create()`, `filesys_remove()`, `filesys_open()`, `file_read()`, `file_write()`, `file_seek()`, `file_tell()`, `file_close()`, and the file operations used during executable loading. It is released after the operation completes, including on error paths.

### Justification

A global lock is coarse-grained but correct. Since the base file system is not thread-safe, finer-grained locking would require modifying `filesys/`, which the lab advises against for this project.

### Files changed

- `userprog/syscall.c`: Per-process file descriptor management (`allocate_fd`, `get_file`) and file-system system calls (`SYS_CREATE`, `SYS_REMOVE`, `SYS_OPEN`, `SYS_FILESIZE`, `SYS_READ`, `SYS_WRITE`, `SYS_SEEK`, `SYS_TELL`, `SYS_CLOSE`) implemented directly inside the `switch (syscall_nr)` statement in `syscall_handler()`, protected by the global `filesys_lock`.
- `userprog/process.c`: Closing all open file descriptors in `cur->fd_table` (fds 2..127) and restoring executable write access (`file_allow_write(cur->exec_file)`) in `process_exit()`.
- `threads/thread.h`: Added per-process file descriptor array `struct file *fd_table[128]` and running executable pointer `struct file *exec_file` to `struct thread`.
- `threads/thread.c`: Initializing `fd_table` entries to `NULL` and `exec_file` to `NULL` in `init_thread()`.

---

## 7. Synchronization and Safety

### Locks and semaphores used

| Mechanism | Purpose |
|---|---|
| Global file-system lock | Serializes all access to the unsynchronized base file system |
| Load semaphore | Makes parent `exec()` wait for child load success/failure |
| Per-child wait semaphore | Makes parent `wait()` sleep until child exits |
| Process/status-record synchronization | Protects exit status and waited-for state, if needed |

### Resource cleanup

On process termination:

- Close every open file descriptor.
- Free the file-descriptor table.
- Signal the parent’s wait semaphore.
- Free child-status records that are no longer needed.
- Ensure that the initial process’s termination prevents Pintos from powering off prematurely.

### Justification

Cleanup is performed in one termination path so that normal exits, failed loads, and exceptions behave consistently. This avoids leaked locks, files, memory, and child records.

---

## 8. Testing Strategy

Tests are run from `userprog/build` using:

```sh
make check
```

Before running the full suite, `$SIMULATOR` is set to `qemu` in `pintos/src/tests/Make.tests`, as required by the lab.

### Areas tested

- Argument passing: multiple arguments, repeated spaces, long command lines, and programs that inspect `argv`.
- User-memory safety: null pointers, kernel pointers, unmapped pointers, and buffers crossing page boundaries.
- Process control: normal exit, exception termination, parent/child waiting, repeated waits, invalid waits, and orphaned processes.
- File calls: create/open/read/write/seek/tell/close, invalid descriptors, and removing open files.
- Concurrency: multiple processes using the file system and console simultaneously.

### Test Results Summary

#### Milestone 1: Argument Passing (100% Pass)
- `tests/userprog/args-none` -> **PASS**
- `tests/userprog/args-single` -> **PASS**
- `tests/userprog/args-multiple` -> **PASS**
- `tests/userprog/args-many` -> **PASS**
- `tests/userprog/args-dbl-space` -> **PASS**

#### Milestone 2 & 3: Process Exit Messages & Safe User-Memory Access (100% Pass)
- `tests/userprog/sc-bad-sp` -> **PASS** (invalid %esp below code rejected)
- `tests/userprog/sc-bad-arg` -> **PASS** (kernel-space argument rejected)
- `tests/userprog/sc-boundary` -> **PASS** (syscall across boundary)
- `tests/userprog/sc-boundary-2` -> **PASS** (syscall across boundary 2)
- `tests/userprog/sc-boundary-3` -> **PASS** (partially invalid 4-byte syscall number rejected)
- `tests/userprog/bad-read` -> **PASS** (null pointer read terminates with -1)
- `tests/userprog/bad-write` -> **PASS** (unmapped write terminates with -1)
- `tests/userprog/bad-read2` -> **PASS** (kernel read terminates with -1)
- `tests/userprog/bad-write2` -> **PASS** (kernel write terminates with -1)
- `tests/userprog/bad-jump` -> **PASS** (jump to 0 terminates with -1)
- `tests/userprog/bad-jump2` -> **PASS** (jump to kernel terminates with -1)
- `tests/userprog/create-null` -> **PASS** (null filename rejected)
- `tests/userprog/create-bad-ptr` -> **PASS** (bad filename pointer rejected)
- `tests/userprog/open-null` -> **PASS** (null filename rejected)
- `tests/userprog/open-bad-ptr` -> **PASS** (bad filename pointer rejected)
- `tests/userprog/exec-bad-ptr` -> **PASS** (bad command-line pointer rejected)

#### Milestone 4 & 5: Basic & File-System System Calls (100% Pass)
- `tests/userprog/create-normal` -> **PASS**
- `tests/userprog/create-empty` -> **PASS**
- `tests/userprog/create-null` -> **PASS**
- `tests/userprog/create-bad-ptr` -> **PASS**
- `tests/userprog/create-long` -> **PASS**
- `tests/userprog/create-exists` -> **PASS**
- `tests/userprog/create-bound` -> **PASS**
- `tests/userprog/open-normal` -> **PASS**
- `tests/userprog/open-missing` -> **PASS**
- `tests/userprog/open-boundary` -> **PASS**
- `tests/userprog/open-empty` -> **PASS**
- `tests/userprog/open-null` -> **PASS**
- `tests/userprog/open-bad-ptr` -> **PASS**
- `tests/userprog/open-twice` -> **PASS**
- `tests/userprog/close-normal` -> **PASS**
- `tests/userprog/close-twice` -> **PASS**
- `tests/userprog/close-stdin` -> **PASS**
- `tests/userprog/close-stdout` -> **PASS**
- `tests/userprog/close-bad-fd` -> **PASS**
- `tests/userprog/read-normal` -> **PASS**
- `tests/userprog/read-bad-ptr` -> **PASS**
- `tests/userprog/read-boundary` -> **PASS**
- `tests/userprog/read-zero` -> **PASS**
- `tests/userprog/read-stdout` -> **PASS**
- `tests/userprog/read-bad-fd` -> **PASS**
- `tests/userprog/write-normal` -> **PASS**
- `tests/userprog/write-bad-ptr` -> **PASS**
- `tests/userprog/write-boundary` -> **PASS**
- `tests/userprog/write-zero` -> **PASS**
- `tests/userprog/write-stdin` -> **PASS**
- `tests/userprog/write-bad-fd` -> **PASS**
- `tests/userprog/rox-simple` -> **PASS** (read-only executable protection verified)
- `tests/filesys/base/*` -> **PASS** (`lg-create`, `lg-full`, `lg-random`, `lg-seq-block`, `lg-seq-random`, `sm-create`, `sm-full`, `sm-random`, `sm-seq-block`, `sm-seq-random`, `syn-remove`)

#### Milestone 6: Process Control, Parent-Child Synchronization & Stress (100% Pass)
- `tests/userprog/exec-once` -> **PASS** (child executes and completes)
- `tests/userprog/exec-arg` -> **PASS** (arguments parsed and verified in child)
- `tests/userprog/exec-bound` -> **PASS** (exec string near page boundary)
- `tests/userprog/exec-bound-2` -> **PASS** (exec string crossing page boundary)
- `tests/userprog/exec-bound-3` -> **PASS** (exec string spanning boundary)
- `tests/userprog/exec-multiple` -> **PASS** (multiple children spawned and waited)
- `tests/userprog/exec-missing` -> **PASS** (nonexistent executable returns -1)
- `tests/userprog/exec-bad-ptr` -> **PASS** (invalid executable pointer rejected)
- `tests/userprog/wait-simple` -> **PASS** (parent waits for child exit status)
- `tests/userprog/wait-twice` -> **PASS** (re-waiting on same child returns -1)
- `tests/userprog/wait-killed` -> **PASS** (kernel-terminated child returns -1)
- `tests/userprog/wait-bad-pid` -> **PASS** (waiting on invalid/non-child PID returns -1)
- `tests/userprog/multi-recurse` -> **PASS** (15 levels of recursive exec and wait)
- `tests/userprog/multi-child-fd` -> **PASS** (non-inheritance of file descriptors verified)
- `tests/userprog/rox-child` -> **PASS** (executable write denial across parent and child)
- `tests/userprog/rox-multichild` -> **PASS** (executable write denial with multiple children)
- `tests/userprog/no-vm/multi-oom` -> **PASS** (10 successive waves of process exhaustion and memory reclamation with zero memory leaks)
- `tests/filesys/base/syn-read` -> **PASS** (10 concurrent child readers synchronized)
- `tests/filesys/base/syn-write` -> **PASS** (concurrent child writers serialized)

#### Final Test Suite Execution (`make check`)
- **Total Tests:** 80
- **Passed:** 80
- **Failed:** 0
- **Pass Rate:** **100%**

Passing tests log:
```text
pass tests/userprog/args-none
pass tests/userprog/args-single
pass tests/userprog/args-multiple
pass tests/userprog/args-many
pass tests/userprog/args-dbl-space
pass tests/userprog/sc-bad-sp
pass tests/userprog/sc-bad-arg
pass tests/userprog/sc-boundary
pass tests/userprog/sc-boundary-2
pass tests/userprog/sc-boundary-3
pass tests/userprog/halt
pass tests/userprog/exit
pass tests/userprog/create-normal
pass tests/userprog/create-empty
pass tests/userprog/create-null
pass tests/userprog/create-bad-ptr
pass tests/userprog/create-long
pass tests/userprog/create-exists
pass tests/userprog/create-bound
pass tests/userprog/open-normal
pass tests/userprog/open-missing
pass tests/userprog/open-boundary
pass tests/userprog/open-empty
pass tests/userprog/open-null
pass tests/userprog/open-bad-ptr
pass tests/userprog/open-twice
pass tests/userprog/close-normal
pass tests/userprog/close-twice
pass tests/userprog/close-stdin
pass tests/userprog/close-stdout
pass tests/userprog/close-bad-fd
pass tests/userprog/read-normal
pass tests/userprog/read-bad-ptr
pass tests/userprog/read-boundary
pass tests/userprog/read-zero
pass tests/userprog/read-stdout
pass tests/userprog/read-bad-fd
pass tests/userprog/write-normal
pass tests/userprog/write-bad-ptr
pass tests/userprog/write-boundary
pass tests/userprog/write-zero
pass tests/userprog/write-stdin
pass tests/userprog/write-bad-fd
pass tests/userprog/exec-once
pass tests/userprog/exec-arg
pass tests/userprog/exec-bound
pass tests/userprog/exec-bound-2
pass tests/userprog/exec-bound-3
pass tests/userprog/exec-multiple
pass tests/userprog/exec-missing
pass tests/userprog/exec-bad-ptr
pass tests/userprog/wait-simple
pass tests/userprog/wait-twice
pass tests/userprog/wait-killed
pass tests/userprog/wait-bad-pid
pass tests/userprog/multi-recurse
pass tests/userprog/multi-child-fd
pass tests/userprog/rox-simple
pass tests/userprog/rox-child
pass tests/userprog/rox-multichild
pass tests/userprog/bad-read
pass tests/userprog/bad-write
pass tests/userprog/bad-read2
pass tests/userprog/bad-write2
pass tests/userprog/bad-jump
pass tests/userprog/bad-jump2
pass tests/userprog/no-vm/multi-oom
pass tests/filesys/base/lg-create
pass tests/filesys/base/lg-full
pass tests/filesys/base/lg-random
pass tests/filesys/base/lg-seq-block
pass tests/filesys/base/lg-seq-random
pass tests/filesys/base/sm-create
pass tests/filesys/base/sm-full
pass tests/filesys/base/sm-random
pass tests/filesys/base/sm-seq-block
pass tests/filesys/base/sm-seq-random
pass tests/filesys/base/syn-read
pass tests/filesys/base/syn-remove
pass tests/filesys/base/syn-write
All 80 tests passed.
```

---

## 9. References

I consulted the following official resources to understand the project requirements, x86 calling conventions, and system call specifications:

- Course laboratory specification: **User Programs — CS2042/CS2043 Operating Systems** (`User_Programs-Lab2_new.pdf`).
- Stanford University Pintos documentation and project specifications: <https://www.scs.stanford.edu/23wi-cs212/pintos/pintos_3.html>.
- System V ABI Intel386 Architecture Processor Supplement for the 32-bit x86 stack layout, alignment rules, and calling convention.
- Pintos source code and internal headers (`threads/vaddr.h`, `userprog/pagedir.h`, `filesys/filesys.h`, `filesys/file.h`, and `lib/string.h`).
