# Instructions for Antigravity — Pintos Project 2: User Programs

## Role and goal

You are helping implement **Pintos Project 2: User Programs** for CS2042/CS2043. Work incrementally in the existing Pintos source tree, preserving the provided interfaces and test expectations.

The objective is to implement:

1. Command-line argument passing.
2. Safe user-memory access for system calls.
3. System-call dispatch and required system calls.
4. Process termination messages.
5. `exec`, `wait`, and exit-status propagation.
6. File-related system calls and per-process file descriptors.
7. Synchronization around the provided unsynchronized file system.

Do **not** modify files under `filesys/` unless explicitly necessary. The lab recommends treating the base file system as a critical section instead. Do not edit or weaken test cases. Do not add debug output to the final submission.

Primary files to modify:

- `userprog/process.c`
- `userprog/process.h`
- `userprog/syscall.c`
- `userprog/syscall.h`
- `userprog/exception.c`, only if using the page-fault-based user-memory approach
- `threads/thread.c` and `threads/thread.h`, only if process/file-descriptor state is added to `struct thread`

Before changing code, read:

- `userprog/process.c`
- `userprog/syscall.c`
- `userprog/pagedir.c`
- `threads/vaddr.h`
- `threads/thread.h`
- `filesys/file.h`
- `filesys/filesys.h`
- `lib/string.c`
- `lib/user/syscall.h`
- `lib/syscall-nr.h`
- `userprog/DESIGNDOC.md`

## Ground rules

- Implement one milestone at a time.
- After each milestone, build and run the relevant tests.
- Keep functions small and explain non-obvious synchronization or cleanup logic with concise comments.
- Never dereference a user pointer without validating it or using a fault-safe access mechanism.
- Always release locks and free allocated memory on error paths.
- Ensure that only the offending user process terminates on invalid input; Pintos itself must never panic or crash.
- Use QEMU for tests, as required by the lab.
- Do not copy code from public repositories or classmates. Use references only to understand interfaces and concepts.

Build and test from `userprog/build`:

```sh
make
make check
```

To run one test:

```sh
make tests/userprog/args-none.result
```

Set QEMU in `src/tests/Make.tests` before testing:

```make
TESTCMD += qemu
```

---

## Milestone 0 — Establish a baseline

1. Confirm the repository builds cleanly:

   ```sh
   cd pintos/userprog/build
   make
   ```

2. Run the existing test suite:

   ```sh
   make check
   ```

3. Record the initial passing/failing tests. Most Project 2 tests are expected to fail initially.

4. Create a Git branch:

   ```sh
   git checkout -b project2-userprog
   ```

5. Copy `DESIGNDOC.md` into:

   ```text
   pintos/src/userprog/DESIGNDOC
   ```

6. Do not implement everything at once. Start with argument passing and basic process exit behavior.

**Expected outcome:** clean build and a recorded baseline test summary.

---

## Milestone 1 — Argument passing

### Goal

Make this work:

```text
process_execute("grep foo bar")
```

The child process must receive:

- `argc = 3`
- `argv[0] = "grep"`
- `argv[1] = "foo"`
- `argv[2] = "bar"`
- `argv[3] = NULL`

### Tasks

1. In `process_execute()`:
   - Make a private copy of the command-line string before parsing it.
   - Tokenize the copied string using `strtok_r()` with space as the delimiter.
   - Treat the first token as the executable filename.
   - Preserve the remaining tokens for the child process.
   - Handle repeated spaces correctly.
   - Decide and enforce a reasonable command-line limit, such as arguments fitting within one 4 KiB page.

2. Pass the parsed arguments to `start_process()` through a suitable heap-allocated structure or by copying the command line into kernel memory.

3. In `setup_stack()`:
   - Push argument strings onto the user stack, including null terminators.
   - Save the user-stack address of each argument string.
   - Align the stack pointer to a 4-byte boundary.
   - Push `argv[argc] = NULL`.
   - Push pointers to the argument strings in reverse order.
   - Push `argv`.
   - Push `argc`.
   - Push a fake return address of `0`.
   - Set the initial user stack pointer to `argc`.

4. Ensure that the child receives the executable name only, not the full command line, as its process name.

### Tests to target

Run these after implementation:

```sh
make tests/userprog/args-none.result
make tests/userprog/args-single.result
make tests/userprog/args-multiple.result
make tests/userprog/args-many.result
make tests/userprog/args-dbl-space.result
```

### Acceptance criteria

- Arguments appear correctly in user programs.
- Repeated spaces behave like one delimiter.
- No kernel panic occurs for long but valid argument lists.
- The process name printed on exit excludes arguments.

---

## Milestone 2 — Process exit messages

### Goal

Print the required termination message for every user process:

```text
<process name>: exit(<status>)
```

### Tasks

1. Locate where a user process terminates.
2. Print the message using the exact format:

   ```c
   printf("%s: exit(%d)\n", process_name, exit_status);
   ```

3. Ensure the process name is the executable name passed to `process_execute()`, with command-line arguments removed.
4. Do not print this message for:
   - Kernel threads that are not user processes.
   - The `halt` system call.
5. Remove all extra debugging prints before final submission.

### Acceptance criteria

- Normal `exit(status)` prints the message.
- A process killed by an exception also prints the message.
- No additional custom messages interfere with automated tests.

---

## Milestone 3 — Safe user-memory access

### Goal

Prevent invalid user pointers from crashing Pintos.

### Tasks

1. Add helper functions in `userprog/syscall.c` to validate user memory.
2. A valid user pointer must:
   - Not be `NULL`.
   - Be below `PHYS_BASE`.
   - Refer to memory mapped in the current process’s page directory.
3. For buffers, validate:
   - The first byte.
   - The last byte.
   - Every page boundary crossed by the buffer.
4. Use `is_user_vaddr()` from `threads/vaddr.h` and `pagedir_get_page()` from `userprog/pagedir.c`.
5. If validation fails, terminate the calling process with exit status `-1`.
6. Ensure that any lock acquired or memory allocated before detecting the invalid pointer is released.

### Design choice

Use the **validate-before-dereference** approach:

- It is easier to reason about resource cleanup.
- It avoids modifying `page_fault()` unless necessary.
- It directly satisfies the requirement that invalid pointers terminate only the offending process.

### Tests to target

```sh
make tests/userprog/sc-bad-sp.result
make tests/userprog/sc-bad-arg.result
make tests/userprog/bad-read.result
make tests/userprog/bad-write.result
make tests/userprog/bad-read2.result
make tests/userprog/bad-write2.result
make tests/userprog/bad-jump.result
make tests/userprog/bad-jump2.result
```

### Acceptance criteria

- Invalid pointers cause process termination, not a kernel panic.
- Partially valid buffers crossing a page boundary are rejected.
- No file descriptors, locks, or memory leak after an invalid access.

---

## Milestone 4 — System-call dispatch

### Goal

Replace the skeleton handler with a dispatcher that reads the syscall number and arguments from the user stack.

### Tasks

1. In `syscall_handler()`:
   - Validate the user stack pointer.
   - Read the system-call number from `esp`.
   - Validate each argument address before reading it.
   - Read arguments using the x86 32-bit convention:
     - First argument: `esp + 4`
     - Second argument: `esp + 8`
     - Third argument: `esp + 12`
   - Dispatch using a `switch` statement.
   - Store return values in `eax`.

2. Implement a central helper for terminating the current process on invalid arguments.

3. Keep the dispatcher extensible for Project 3 and Project 4 system calls.

### System calls to support

Implement these in this order:

1. `halt`
2. `exit`
3. `exec`
4. `wait`
5. `create`
6. `remove`
7. `open`
8. `filesize`
9. `read`
10. `write`
11. `seek`
12. `tell`
13. `close`

### Acceptance criteria

- The syscall number and arguments are read safely.
- Each syscall routes to the correct implementation.
- Invalid syscall arguments terminate only the calling process.
- Return values are placed correctly in `eax`.

---

## Milestone 5 — Basic system calls

### Goal

Implement the simpler system calls before process control.

### Tasks

1. `halt`
   - Call `shutdown_power_off()`.
   - Do not print an exit message for `halt`.

2. `exit(status)`
   - Record the exit status.
   - Close all file descriptors.
   - Print the required termination message.
   - Wake any parent waiting for this process.
   - Terminate the current thread.

3. `create(file, initial_size)`
   - Validate the filename pointer.
   - Acquire the global file-system lock.
   - Call `filesys_create()`.
   - Release the lock.
   - Return `true` on success and `false` otherwise.

4. `remove(file)`
   - Validate the filename pointer.
   - Acquire the global file-system lock.
   - Call `filesys_remove()`.
   - Release the lock.
   - Return the result.
   - Do not close descriptors merely because the underlying file was removed.

5. `open(file)`
   - Validate the filename pointer.
   - Acquire the global file-system lock.
   - Call `filesys_open()`.
   - Release the lock.
   - If opening fails, return `-1`.
   - Otherwise allocate the next available file descriptor starting at 2.
   - Store the returned `struct file *` in the process’s descriptor table.

6. `filesize(fd)`
   - Validate `fd`.
   - Acquire the file-system lock.
   - Call `file_length()`.
   - Release the lock.
   - Return the size, or `-1` for an invalid descriptor.

7. `seek(fd, position)`
   - Validate `fd`.
   - Acquire the file-system lock.
   - Call `file_seek()`.
   - Release the lock.

8. `tell(fd)`
   - Validate `fd`.
   - Acquire the file-system lock.
   - Call `file_tell()`.
   - Release the lock.
   - Return the file position.

9. `close(fd)`
   - Validate `fd`.
   - Acquire the file-system lock.
   - Call `file_close()`.
   - Release the lock.
   - Remove the descriptor from the process table.
   - Reject repeated closes of the same descriptor.

### Console behavior

- `read(0, buffer, size)` reads from the keyboard with `input_getc()`.
- `write(1, buffer, size)` writes using `putbuf()`.
- Write console output in one `putbuf()` call when reasonably possible, or split only large buffers.
- Do not allow user programs to close or write through descriptor 0 or 1 except as specified.

### Tests to target

```sh
make tests/userprog/create-normal.result
make tests/userprog/create-empty.result
make tests/userprog/create-bad-ptr.result
make tests/userprog/open-normal.result
make tests/userprog/open-bad-ptr.result
make tests/userprog/close-twice.result
make tests/userprog/read-normal.result
make tests/userprog/write-normal.result
make tests/userprog/seek-tell.result
```

### Acceptance criteria

- File operations work through per-process descriptors.
- Invalid descriptors and invalid user buffers are handled safely.
- File-system calls are serialized.
- Process exit closes all remaining descriptors.

---

## Milestone 6 — Process control: `exec`, `wait`, and exit status

This is the most involved milestone. Implement it only after argument passing, exit messages, and basic syscalls work.

### Goal

Implement correct parent-child process relationships and exit-status retrieval.

### Data design

Add a child-process status structure containing at least:

- Child process/thread identifier.
- Exit status.
- Whether the child has exited.
- Whether the child was successfully loaded.
- Whether the parent has already waited for it.
- A semaphore used by the parent to wait for child termination.

Add to each process:

- A list of direct children.
- A pointer to its own status record.
- A pointer to its parent’s status record or enough information to notify the parent.

### `exec` design

1. Validate the user command-line pointer.
2. Copy or otherwise secure the command line before creating the child.
3. Call `process_execute()`.
4. Block the parent until the child reports whether its executable loaded successfully.
5. If loading failed, return `-1`.
6. Otherwise return the child’s pid.

Use a semaphore or condition variable so that the parent cannot return before load success or failure is known.

### `wait` design

1. Search only the caller’s direct-child list.
2. Return `-1` immediately if:
   - The pid is not a direct child.
   - The child has already been waited for.
3. If the child has not exited, wait on its semaphore.
4. After the child exits:
   - Retrieve its exit status.
   - Return `-1` if the child was killed by the kernel rather than exiting normally.
   - Mark the child as waited for.
   - Free the child status record.
5. Ensure that a parent can wait for children in any order.
6. Ensure that children can exit before their parent waits.
7. Ensure that resources are freed even if the parent never waits.

### Process cleanup

When any user process terminates:

1. Close all open file descriptors.
2. Free the descriptor table.
3. Record its exit status.
4. Mark whether termination was normal or caused by a kernel exception.
5. Signal the parent’s wait semaphore.
6. Remove itself from its parent’s child list when safe.
7. Free child records only after they are no longer needed by the parent.
8. Ensure the initial process’s termination prevents Pintos from exiting prematurely.

### Tests to target

```sh
make tests/userprog/exec-missing.result
make tests/userprog/exec-bad-ptr.result
make tests/userprog/wait-simple.result
make tests/userprog/wait-twice.result
make tests/userprog/wait-killed.result
make tests/userprog/wait-bad-pid.result
make tests/userprog/wait-orphan.result
make tests/userprog/multi-recurse.result
make tests/userprog/multi-child-fd.result
```

### Acceptance criteria

- `exec` returns `-1` only when the child cannot load or run.
- `wait` returns the child’s exit status exactly once.
- Repeated `wait` on the same child returns `-1`.
- A parent cannot wait for a grandchild.
- Kernel-killed children cause `wait` to return `-1`.
- No process resources leak, including when parents never wait.

---

## Milestone 7 — File-system synchronization

### Goal

Prevent concurrent user processes from corrupting the provided file system.

### Tasks

1. Add one global lock for all file-system operations.
2. Protect every call into:
   - `filesys_create()`
   - `filesys_remove()`
   - `filesys_open()`
   - `file_read()`
   - `file_write()`
   - `file_seek()`
   - `file_tell()`
   - `file_length()`
   - `file_close()`
3. Also protect executable loading because it accesses the file system.
4. Ensure that the lock is released on every return path.
5. Avoid holding the lock while performing unrelated work or blocking indefinitely.

### Tests to target

```sh
make tests/userprog/multi-child-fd.result
make tests/userprog/rox-multichild.result
make tests/filesys/base/syn-read.result
make tests/filesys/base/syn-write.result
```

### Acceptance criteria

- Concurrent file operations do not panic or corrupt state.
- Executable loading remains safe when multiple processes start simultaneously.
- No deadlock occurs.

---

## Milestone 8 — Robustness pass

### Goal

Make Pintos “bulletproof” against malicious or malformed user programs.

### Checklist

Verify each of the following:

- [ ] Null pointers are rejected.
- [ ] Kernel-space pointers are rejected.
- [ ] Unmapped pointers are rejected.
- [ ] Buffers crossing page boundaries are handled safely.
- [ ] Invalid file descriptors are rejected.
- [ ] Closing descriptor 0 or 1 is handled according to the specification.
- [ ] Repeated `close(fd)` does not double-free.
- [ ] `wait()` cannot be called twice successfully on the same child.
- [ ] A parent cannot wait for a non-child or grandchild.
- [ ] Orphaned children are cleaned up.
- [ ] `exec()` synchronizes with child loading.
- [ ] Process exit closes all descriptors.
- [ ] File-system operations are serialized.
- [ ] No extra debug output remains.
- [ ] The exit message format is exact.
- [ ] `make check` passes the Project 2 tests.

### Stress tests

Run the complete suite repeatedly:

```sh
make check
```

Also run individual tests that previously failed and confirm that their output matches expectations.

---

## Milestone 9 — Final documentation and submission

1. Update `pintos/src/userprog/DESIGNDOC` so every section matches the final implementation:
   - Actual data structures.
   - Actual synchronization primitives.
   - Actual cleanup strategy.
   - Actual design tradeoffs.
   - Test results.

2. Remove all temporary debugging code and print statements.

3. Run:

   ```sh
   make check
   ```

4. Capture screenshots of:
   - The full `make check` summary.
   - Any especially relevant passing test groups.

5. Verify the submission contains:
   - Updated `pintos/src/userprog/DESIGNDOC`.
   - Complete modified Pintos source code.
   - Screenshots of the test summary.

6. Do not include:
   - Test modifications.
   - Peer code.
   - Copied online implementations.
   - Debugging output.

---

## Suggested implementation order

Implement in this exact order:

1. Argument passing.
2. Process exit messages.
3. User-memory validation.
4. System-call dispatcher.
5. `halt`, `exit`, `create`, `remove`, `open`, `filesize`, `seek`, `tell`, `close`.
6. `read` and `write`.
7. `exec` and load synchronization.
8. `wait` and child-status tracking.
9. Global file-system lock.
10. Robustness cleanup and final testing.

---

## Common pitfalls to avoid

- Parsing the command line after it may have become invalid.
- Forgetting to align the user stack before pushing `argv`.
- Pushing argument pointers before pushing the strings they point to.
- Validating only the first byte of a buffer.
- Allowing a user pointer into kernel memory.
- Returning from `exec()` before knowing whether the child loaded.
- Letting a parent wait for a grandchild.
- Allowing repeated `wait()` calls on one child.
- Failing to close descriptors when a process exits.
- Calling file-system functions without holding the global lock.
- Leaving debug print statements in the final submission.

---

## Reference policy

Use these references for understanding only:

- Course lab specification: `User_Programs-Lab2_new.pdf`.
- Stanford Pintos Project 2 documentation: <https://www.scs.stanford.edu/23wi-cs212/pintos/pintos_3.html>.
- Stanford Pintos manual: <https://www.scs.stanford.edu/09wi-cs140/pintos/pintos.pdf>.
- Public design-document examples:
  - <https://ycruan.github.io/files/162_project2_design/>
  - <https://github.com/bklxxjjz/pintos/blob/master/doc/project2.md>
  - <https://pkuflyingpig.gitbook.io/pintos/project-description/lab2-user-programs/your-tasks>

Do not copy implementation code from these sources or from other students. The final code and design document must be your own work.
