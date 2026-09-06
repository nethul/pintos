# Pintos Project 1: Threads — Your Field Guide

> Based on the JHU CS318 Fall 2020 Pintos lab spec (Project 1: Threads).
> This is not a solution set. It's a map. You will still have to walk
> the terrain yourself — but you won't be walking blind.

---

## 0. Read this first: how to use this guide

The official spec throws a lot at you at once: alarm clocks, priority
scheduling, priority donation, a whole 4.4BSD-style scheduler with
fixed-point math. It *feels* like four unrelated things. It isn't —
they all boil down to one question, asked in increasingly hard forms:

> **"Given a set of threads, which one should run right now — and how
> do I make that decision cheaply, correctly, and without ever busy-waiting?"**

Every exercise in this project is a variation on that question. Once
you see that thread, the project stops being four separate mountains
and becomes one mountain with four camps on the way up.

I'm going to walk you through each camp. For each one you'll get:

1. **The intuition** — what's actually going on, in plain English.
2. **The data you need** — what fields/structures you'll add, and why.
3. **A guided derivation** — I'll ask you the questions you should be
   asking yourself, and sketch the shape of the answer, but leave the
   actual C statements for you to write.
4. **Pitfalls** — the bugs that eat 90% of people's time on this lab.
5. **How to sanity-check yourself** before you even run the test suite.

Keep `threads/thread.c`, `threads/thread.h`, `threads/synch.c`,
`threads/synch.h`, and `devices/timer.c` open in your editor as you
read. Nothing here will make sense in the abstract — you need the
real source in front of you.

---

## 1. Orient yourself before touching any code

Before writing a single line, spend 30–45 minutes doing this — it will
save you hours later:

- [ ] Read `struct thread` in `threads/thread.h` top to bottom. Note
      every existing field and what it's for.
- [ ] Find `ready_list` in `thread.c`. This is the list of
      `THREAD_READY` threads. Note how threads get added to it
      (`thread_unblock`, `thread_yield`) and removed from it
      (`next_thread_to_run`).
- [ ] Find `next_thread_to_run()`. This is *the* scheduling decision
      point. Right now it almost certainly just pops the front of
      `ready_list` (FIFO / round robin). This function is the thing
      you're really here to change the behavior of — even if you do it
      indirectly by controlling insertion order.
- [ ] Read `sema_down`, `sema_up`, `lock_acquire`, `lock_release`,
      `cond_wait`, `cond_signal` in `synch.c`. Notice that each
      semaphore keeps its own `waiters` list — separate from
      `ready_list`.
- [ ] Read `timer_sleep()` and `timer_interrupt()` in `devices/timer.c`.

**Key mental model:** at any moment, a thread is in exactly one of
four states, and in exactly one of (at most) two lists:

| State | Meaning | Which list holds it |
|---|---|---|
| `THREAD_RUNNING` | on the CPU right now | none (it's *the* current thread) |
| `THREAD_READY` | could run, waiting its turn | `ready_list` |
| `THREAD_BLOCKED` | waiting on something (lock, sema, timer, cond var) | some *other* list — the sema's `waiters`, or a sleep list you'll build |
| `THREAD_DYING` | about to be reaped | none, transient |

Almost everything you build in this project is: **"maintain one of
these lists in priority order, or scan it for the max, so the highest
priority thread is always the one picked next."**

---

## 2. Camp 1 — The Alarm Clock (`timer_sleep`)

### Intuition

Right now, `timer_sleep(ticks)` does this (conceptually):

```
start = current_tick
while (current_tick - start < ticks)
    thread_yield();   // "busy wait" — keep giving up the CPU, check again
```

This *works*, but it's wasteful: the sleeping thread is repeatedly put
back on the ready list and repeatedly scheduled, just to check "am I
done yet?" and go back to sleep. That's CPU cycles spent doing nothing
useful, over and over, `TIMER_FREQ` times a second, for every sleeping
thread.

What you actually want: the thread should get **out of the scheduler's
way entirely** — leave `THREAD_READY` and go `THREAD_BLOCKED` — and
something else (the timer interrupt) should be responsible for waking
it up at the right time.

### The data you need

Ask yourself: *"If I'm the timer interrupt handler, ticking 100 times
a second, and I need to know which sleeping threads to wake up right
now — what do I need to have kept track of?"*

You need, for each sleeping thread, **the tick at which it should wake
up**. And you need a place to find *all* sleeping threads quickly,
without scanning every thread in the system on every single tick
(remember: the timer interrupt fires 100 times/sec and must be fast —
see §A.4.3 on external interrupt handlers needing to "complete as
quickly as they can").

This suggests:

- A **new list**, say `sleeping_list`, of threads currently sleeping —
  separate from `ready_list`.
- A **new field** in `struct thread` (or tracked alongside), e.g.
  `int64_t wakeup_tick`, recording when this particular thread should
  be woken.

You'll need a `struct list_elem` to link into `sleeping_list` — you
can reuse the existing `elem` field in `struct thread` for this,
*since a thread can't simultaneously be on `ready_list` and
`sleeping_list`* (same trick the docs mention: `elem` does "double
duty" for `ready_list` and a semaphore's `waiters`).

### Guided derivation

Now derive `timer_sleep()` yourself. Ask:

1. **What should happen when a thread calls `timer_sleep(x)`?**
   It should compute its wakeup tick (`current_tick + x`), record
   itself somewhere the timer interrupt can find it, and stop being
   schedulable. What Pintos primitive makes a thread stop being
   schedulable? (Hint: you saw it in §1 — `thread_block()`.)

2. **Is there a race here?** Between "check what tick it is now" and
   "actually get blocked," could a timer interrupt sneak in and change
   things underneath you? What tool from `A.3.1 Disabling Interrupts`
   would you reach for to make "compute wakeup tick, insert into
   sleeping list, block" effectively atomic? *(This is exactly the
   kind of case the spec calls out: coordinating data shared between a
   kernel thread and an interrupt handler.)*

3. **What should the timer interrupt handler do, every tick?**
   It needs to check `sleeping_list` for any thread whose
   `wakeup_tick` has arrived, and for each such thread call the
   function that reverses `thread_block()` — moving it back to
   `THREAD_READY`. What's that function called? You saw it in §1.

4. **Careful:** the timer interrupt runs in *interrupt context*. Which
   functions are you forbidden from calling there (re-read
   `A.4.3 External Interrupt Handling`)? Make sure whatever you call
   to wake a thread up is interrupt-context-safe. (`thread_unblock()`
   is fine; things that might sleep or yield are not.)

5. **Efficiency detail (not required, but good practice):** if you
   keep `sleeping_list` *sorted* by `wakeup_tick`, the interrupt
   handler only has to look at the front of the list and can stop as
   soon as it finds a thread that isn't ready yet — instead of
   scanning every sleeping thread on every tick. `list.c` has
   `list_insert_ordered()` for exactly this.

### Pitfalls

- **Don't** call `thread_yield()` anywhere in your new implementation
  — that's the busy-wait you're eliminating.
- **Don't** allocate a `struct list` per sleep call — one global
  sleeping list is fine.
- Remember `timer_sleep(0)` or negative ticks should just return
  immediately (check the original code for this guard, and keep it).
- If you disable interrupts around the block, make sure you restore
  the *previous* level afterward, not just blindly turn them back on —
  use `intr_set_level(old_level)` with the value `intr_disable()`
  returned.

### Self-check

Before running tests: trace through, on paper, three threads calling
`timer_sleep(10)`, `timer_sleep(20)`, `timer_sleep(5)` in that order.
Walk through 25 timer ticks by hand. At which tick does each thread
get unblocked? Does your code produce that?

---

## 3. Camp 2 — Priority Scheduling

### Intuition

Right now Pintos is priority-blind: `next_thread_to_run()` just takes
whichever thread has been waiting longest (round robin via a plain
list). You need the scheduler — and every place that wakes threads up
or adds them to a waiting list — to always prefer the **highest
priority** thread.

There are three separate places priority needs to be respected, and
it helps to treat them as three sub-problems:

1. **The ready list.** When choosing what to run next, always pick the
   highest-priority ready thread.
2. **Preemption.** When a *new* thread becomes ready (created, or
   unblocked) with higher priority than the currently running thread,
   the running thread must **immediately** yield — not wait for the
   next timer tick.
3. **Waiting lists for locks/semaphores/condition variables.** When a
   resource frees up and multiple threads are waiting for it, the
   *highest priority* waiter should be the one woken, not just the
   first one in line (FIFO).

### The data you need

You already have `int priority` in `struct thread`. For basic
priority scheduling (ignoring donation for a moment), that's enough
data — the work is entirely about *how you use* the existing
`priority` field when inserting into lists / choosing the max.

### Guided derivation

**3a. Ordering the ready list (or picking the max)**

Two valid strategies — pick one:

- **Keep `ready_list` sorted** by priority (highest first) using
  `list_insert_ordered()` with a comparison function you write. Then
  `next_thread_to_run()` just pops the front — barely changes.
- **Keep `ready_list` unsorted**, but change `next_thread_to_run()` to
  scan and remove the max-priority element.

Either is fine and the test suite doesn't care which. Sorted insertion
is usually a little easier to reason about since `next_thread_to_run`
stays almost untouched. Whichever you choose, write your comparison
function once — you'll reuse the *same idea* (compare by priority) in
several other places below, so make it something you can call or
adapt everywhere threads are compared.

Ask yourself: *"Every place in the current code that does
`list_push_back(&ready_list, ...)` — should that become an
ordered insert instead?"* Find them. There are a couple.

**3b. Preemption on thread creation and unblock**

Find `thread_create()`. After the new thread is made ready, ask:
*does the newly-created thread have higher priority than
`thread_current()`? If so, what should happen?* You need to yield —
but be careful *where*. If you're inside a section with interrupts
disabled, or inside `thread_create` itself before setup is complete,
calling `thread_yield()` at the wrong moment can be unsafe. Find a
safe point after the thread is fully set up and unblocked.

Do the same check anywhere a thread gets unblocked and could
suddenly outrank the currently running thread (`thread_unblock`, or
wherever `sema_up` wakes someone). *Should `sema_up` itself trigger a
yield, or is it enough that the woken thread simply out-competes for
the CPU the next time `next_thread_to_run` is called?* Think about
this one carefully — re-read the FAQ item *"Can a thread added to the
ready list preempt the processor?"* The answer there is your answer.

**3c. Priority-ordered waiting on semaphores / locks / condition variables**

Look at `struct semaphore`'s `waiters` list, populated in
`sema_down()`, drained in `sema_up()`. Right now `sema_up()` almost
certainly does `list_pop_front()` — FIFO order. You need it to wake
the *highest priority* waiter instead.

You have the same two choices as 3a: insert into `waiters` in
priority order, or scan for the max when popping. Reuse whichever
approach you picked before, for consistency.

**Condition variables are trickier.** Look at `struct condition`'s
list — it's a list of *semaphores* (one per waiter), not of threads
directly (check `cond_wait()` — each waiter creates its own
one-shot semaphore and puts a `struct semaphore_elem` on `cond->waiters`).
So "find the highest priority waiter" means: for each
`semaphore_elem` in the condition variable's waiter list, find the
priority of the thread waiting on *its* semaphore, then pick the max
of those. You'll write a comparison function that reaches one level
deeper than the lock/sema case. This is a very common stumbling point
— budget extra time for `cond_signal()`.

**3d. `thread_set_priority()` / `thread_get_priority()`**

These have skeletons waiting for you. `thread_set_priority` changes
the current thread's priority; if that makes it no longer the highest
priority thread around, it must yield — same pattern as 3b. Hold off
on making this fully correct with donation in mind — see below.

### Pitfalls

- Forgetting to re-sort/re-check the ready list when a *ready* (not
  running) thread's priority changes (this matters more once donation
  enters the picture — see Camp 3).
- Triggering a yield from inside an interrupt handler incorrectly —
  use `intr_yield_on_return()` in that context, not a direct
  `thread_yield()` call, if you find yourself needing to preempt from
  interrupt context (e.g., the timer interrupt waking up a
  higher-priority sleeper).
- Comparing priorities with the wrong direction. Remember: **higher
  number = higher priority** here (0 lowest, 63 highest) — the
  opposite convention from, e.g., Unix `nice`.

### Self-check

On paper: threads L (priority 10), M (priority 20), H (priority 30)
are all created in that order, all just loop forever without
blocking. After all three exist, which one is running? If L calls
`sema_down` on a semaphore initialized to 0, and later some code calls
`sema_up` on it while M and H are also waiting on it, which of M/H
wakes up?

---

## 4. Camp 2, continued — Priority Donation

This is the hardest part of the project. Take it slow.

### Intuition

The problem, in the concrete terms from the spec: thread **H** (high)
needs a lock held by thread **L** (low). Thread **M** (medium) is
runnable and doesn't need the lock. Without donation:

```
H blocks waiting for the lock (held by L).
M is ready and outranks L, so M keeps getting the CPU.
L never runs, never finishes with the lock, never releases it.
H waits forever, even though it outranks M.
```

This is called **priority inversion** — effectively, M is running at
H's expense even though H should win. The fix: while H waits on a
lock owned by L, **temporarily boost L's priority to H's**. Now L
outranks M, gets the CPU, finishes quickly, releases the lock — and H
can proceed. Once L releases, its priority drops back down.

**Nested donation:** if L itself is blocked waiting on a lock held by
some other thread, the boost has to propagate through — that's the
"chain" the spec describes with H → M → L.

### The data you need

Think this through like a detective, not a code-typist. To donate and
later *revoke* a donation correctly, a thread needs to remember:

1. **Its own "true" priority** — the one it would have absent any
   donations, i.e. what was last set via `thread_create()` or
   `thread_set_priority()`. Call it something like `base_priority`.
   The *existing* `priority` field will now represent the **effective**
   (possibly-donated) priority — the one the scheduler actually uses.

2. **What lock (if any) it's currently waiting to acquire**, so that
   if *its own* priority increases later (someone else donates to it),
   you know where to propagate that further. Something like
   `struct lock *waiting_on`.

3. **What locks it currently holds**, and — for each lock — **who's
   waiting on it**, so that when it releases a lock, it can recompute
   its own effective priority from whatever donations remain (from
   *other* locks it still holds) plus its base priority. This
   typically means giving `struct lock` a way to know its waiters
   (you already have `semaphore.waiters` inside the lock's semaphore!)
   and giving `struct thread` a list of locks it holds, e.g.
   `struct list locks_held`.

Sketch it out yourself before coding:

```
struct thread {
    ...
    int base_priority;          /* priority sans donation */
    struct lock *waiting_on;    /* lock this thread is blocked on, or NULL */
    struct list  locks_held;    /* locks currently owned by this thread */
    ...
};
```

(You don't have to use these exact names or exactly this shape — but
you do need to be able to answer "what's my true priority?" and
"who should I check when I release a lock?")

### Guided derivation

Walk through the three moments where donation logic must fire, and
for each, ask yourself the specific question:

**4a. When a thread tries to acquire a lock that's already held**
(inside `lock_acquire()`, before it blocks):

> *"If I'm about to block on this lock, and my priority is higher
> than the current holder's, shouldn't I push my priority onto them
> right now, before I go to sleep? And if the holder is itself
> waiting on another lock, shouldn't that boost propagate up the
> chain?"*

This is where you write a small recursive-or-looping "donate up the
chain" routine: from the current thread, follow `waiting_on` links
(this lock's holder, that holder's `waiting_on` lock, its holder, …),
bumping each thread's `priority` to the max of what it has and what's
being donated, and stopping when you hit a thread that's not waiting
on anything, or (to be safe against bugs) after some depth limit —
the spec explicitly allows you to cap nested donation depth (8 is
suggested).

**4b. When a lock is released** (inside `lock_release()`):

> *"I (the releasing thread) may have been running at an inflated,
> donated priority because of the very lock I'm about to give up.
> Once I give it up, what should my priority become?"*

Answer: recompute from `base_priority` and whatever donations remain
from *other* locks you still hold (check `locks_held` — for each
remaining held lock, look at who's waiting on it and take the max of
their priorities, if any). This is why you need `locks_held` — a
single "revert to base" is wrong if you're holding two locks and only
released one of them.

After recomputing, remember §3b: if your priority just dropped, you
might no longer be the highest priority thread — yield.

**4c. When `thread_set_priority()` is called:**

> *"If I call this while I'm currently *boosting* someone else (i.e.
> my base priority is lower than my effective priority because I
> donated... wait, no — donation flows the other way. Re-read: if
> *I* am the donee, currently running at a donated priority higher
> than what I'm about to set, should the new lower value take effect
> immediately, or only once the donation is released?"*

Re-read the FAQ answer to *"How does `thread_set_priority()` affect a
thread receiving donations?"* — it tells you exactly what's supposed
to happen: it sets the *base* priority; the thread's effective
priority is the max of base and the highest active donation; the
lower value only becomes visible once donations are released. Encode
that logic directly.

**4d. `thread_get_priority()`** simply needs to return the *effective*
(current, possibly-donated) priority — which, if you did 4a/4b
correctly, is just whatever's currently in `priority`.

### Pitfalls (this is where most people lose the most time)

- **Forgetting nested donation.** Test with three threads and two
  locks explicitly (H waits on lock1 held by M, M waits on lock2 held
  by L) before trusting your implementation with two threads.
- **Forgetting multiple donations to the same thread.** If two
  different higher-priority threads are both waiting on locks held by
  the same low-priority thread, that thread's priority should be the
  *max* of all donors — not overwritten by whichever donated last.
- **Losing track after partial release.** A thread holding two locks
  that releases one must not simply drop back to `base_priority`; it
  may still be entitled to a donation from the other lock's waiters.
- **Recomputing at the wrong time** — donation state can change while
  a thread is *blocked*, not just while it's ready or running (see
  FAQ: "Can a thread's priority change while it is blocked?" — yes).
  Your donation-propagation code must not assume the target thread is
  running.
- **Only implementing donation for locks.** The spec is explicit: you
  need donation for locks only, not semaphores/condition variables in
  general — don't over-build.

### Self-check

Re-derive, on paper, the classic H/M/L scenario with actual numbers
(say H=30, M=20, L=10) and a *nested* variant with two locks. At each
step, write down every thread's `base_priority` and effective
`priority`. This is tedious but it is exactly what the
`priority-donate-*` tests are checking mechanically — if you can't do
it by hand, the code won't do it right either.

---

## 5. Camp 3 — The Advanced Scheduler (MLFQS / "4.4BSD scheduler")

### Intuition

Forget donation entirely here — the advanced scheduler doesn't use it.
Instead of threads controlling their own priority, the **kernel
recomputes every thread's priority automatically**, based on how much
CPU time it's recently consumed and how "nice" it has asked to be to
others. This automatically favors I/O-bound / interactive threads
(low recent CPU usage → higher priority) over CPU-hogging threads,
without anyone having to manually set priorities.

Three quantities drive everything:

- **`nice`** — a per-thread knob, directly settable, in `[-20, 20]`.
  Higher `nice` → "I'm being nicer, give others more CPU" → lower
  priority for me.
- **`recent_cpu`** — a per-thread *estimate* of how much CPU time
  it's used lately, decaying over time (an exponentially weighted
  moving average — recent ticks count more than old ones).
- **`load_avg`** — one single *system-wide* number estimating how many
  threads have wanted the CPU recently (like Unix's load average).

And the formulas (straight from the spec, memorize the shapes not the
constants):

```
priority    = PRI_MAX - (recent_cpu / 4) - (nice * 2)
recent_cpu  = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice
load_avg    = (59/60)*load_avg + (1/60)*ready_threads
```

### Step 0: fixed-point arithmetic (do this first, in isolation)

`recent_cpu` and `load_avg` are *not integers* — but the Pintos kernel
has no floating point. So before touching the scheduler at all,
build a tiny, self-contained fixed-point arithmetic module (the
reference solution puts this in a new header, e.g.
`threads/fixed-point.h`, and it's ~100 lines). Do this as a standalone
exercise you can unit-test in your head with a calculator, *before*
wiring it into the scheduler — debugging fixed-point math at the same
time as scheduler logic is miserable.

The representation: pick 17.14 format — a 32-bit signed int where the
low 14 bits are the fractional part. So the integer `n` represents
the real value `n / 2^14`. Define `f = 1 << 14` and derive, yourself,
these operations (the spec gives you the target formulas — implement
them as small `static inline` functions with clear names, e.g.
`fp_from_int`, `fp_to_int_round`, `fp_add`, `fp_mul`, `fp_div`, ...):

| Operation | Formula (x, y fixed-point; n integer; f = 2^14) |
|---|---|
| int → fixed | `n * f` |
| fixed → int, truncate | `x / f` |
| fixed → int, round to nearest | `(x + f/2) / f` (x ≥ 0), `(x - f/2) / f` (x < 0) |
| x + y | `x + y` |
| x − y | `x - y` |
| x + n | `x + n*f` |
| x × y | `((int64_t) x) * y / f` |
| x × n | `x * n` |
| x ÷ y | `((int64_t) x) * f / y` |
| x ÷ n | `x / n` |

**Ask yourself why** the multiply and divide need the 64-bit cast —
work through the overflow example in the spec (64 in fixed-point,
squared) until it actually clicks, don't just copy the formula. This
will save you from subtly-wrong overflow bugs later that only show up
with certain thread counts.

### The data you need

- `int nice` and (fixed-point) `recent_cpu` fields on `struct thread`.
- A global (fixed-point) `load_avg`.
- A global boolean, `thread_mlfqs` — already declared per the spec —
  that tells your code whether to run this scheduler at all instead of
  the priority-donation one from Camps 1–2. **Every piece of donation
  logic you wrote should be skipped when `thread_mlfqs` is true**, and
  vice versa.

### Guided derivation

Ask yourself, for each trigger below, "what exactly needs
recalculating, and when?" — get the *timing* right; the tests are very
strict about this (the spec repeatedly stresses recomputation must
happen *before* any ordinary thread runs with the new tick count
visible, and must happen at *exactly* `timer_ticks() % TIMER_FREQ ==
0`, not "close enough"):

1. **Every timer tick:** the *running* thread's `recent_cpu`
   increments by 1 (fixed-point `+1`). Skip this if the running
   thread is the idle thread.
2. **Every 4th tick:** recompute *every* thread's `priority` from the
   formula (not just the running one — walk the list of all threads;
   `thread_foreach()` from §A.2.2 is built for exactly this).
3. **Once per second (`timer_ticks() % TIMER_FREQ == 0`):** recompute
   `load_avg` (system-wide, once), then recompute `recent_cpu` for
   *every* thread using the new `load_avg`. Order matters: compute
   the new `load_avg` first, then feed it into everyone's
   `recent_cpu` update.
4. **On `thread_create()`:** new thread's `recent_cpu` starts at the
   *parent's* current value (not 0) except the very first thread,
   which starts at 0; `nice` similarly inherits from the parent,
   defaulting to 0 for the initial thread.
5. **`thread_set_nice()`:** sets `nice`, recomputes *this* thread's
   priority immediately from the formula, and — familiar pattern by
   now — yields if it's no longer top priority.
6. **`thread_get_recent_cpu()` / `thread_get_load_avg()`:** these
   don't return the raw fixed-point value — the spec wants "100 times
   the value, rounded to nearest." That's a fixed-point multiply by
   100 (or by-integer op) followed by your round-to-nearest conversion
   back to a plain `int`.

**Where does all this run?** Steps 1–3 belong in the timer interrupt
handler (`timer_interrupt()` in `devices/timer.c`) or functions it
calls — re-read the warning in §A.4.3: this code runs in *interrupt
context, with interrupts disabled*, so it must be fast, must not call
anything that sleeps, and must not call `malloc`. Recomputing 64
priorities for a modest thread count is fine; just don't do anything
extravagant there. The FAQ's advanced-scheduler debugging tip is
worth re-reading here: if your timer interrupt handler does too much
work, you'll perturb the very measurements you're trying to take.

7. **Scheduling itself:** once every thread's `priority` field is
   correctly maintained, do you need *any new logic* in
   `next_thread_to_run()` at all — or does your Camp 2 "highest
   priority in ready_list" logic already do the right thing here for
   free? (This is a good sign if the answer is "no new logic needed" —
   it means your Camp 2 abstraction was well-designed.)

### Pitfalls

- Doing the `load_avg` and `recent_cpu` recalculation on the wrong
  tick, or in the wrong order relative to each other.
- Forgetting to skip donation-related code paths when `thread_mlfqs`
  is set (or vice versa) — the two scheduling policies must not
  interact; the FAQ explicitly says they're never tested together, but
  your code still needs to not crash or misbehave if it accidentally
  runs both.
- Letting `recent_cpu` get clamped to 0 when `nice` is negative — the
  spec explicitly says **do not** clamp negative `recent_cpu`.
- Recomputing priority for only the current thread instead of *all*
  threads every 4 ticks.
- Off-by-one on "how many ticks have passed" checks — use the modulo
  check exactly as specified (`timer_ticks() % TIMER_FREQ == 0`,
  `timer_ticks() % 4 == 0`), don't approximate with counters that can
  drift.

### Self-check

By hand (a spreadsheet helps): simulate 3 threads, all `nice = 0`,
over the first ~10 ticks, assuming all three are always ready to run
and none of them ever blocks. Compute `recent_cpu` and `priority` for
each at each tick, and `load_avg` at the 1-second mark (tick 100, or
scale down your mental model). Does thread rotation match what you'd
expect from "always run whoever has lowest recent CPU usage"?

---

## 6. Suggested order of attack

The official spec's own advice (worth following):

1. **In parallel, first:** the alarm clock (Camp 1) and *basic*
   priority scheduling on thread creation (Camp 2, just the "insert
   into ready list by priority + preempt on creation" part) — these
   don't depend on each other.
2. **Also early:** the fixed-point arithmetic module (Camp 3, Step 0)
   — it's self-contained and worth having ready.
3. **Then:** finish full priority scheduling — locks/semaphores/condvars
   waking the highest-priority waiter (Camp 2, §3c).
4. **Then, in either order:** priority donation (Camp 2, §4) or the
   full advanced scheduler (Camp 3) — they don't depend on each other,
   and the spec suggests getting the (non-donation part of the)
   priority scheduler solid before starting the advanced scheduler,
   since MLFQS reuses the "highest priority in ready list" machinery.

---

## 7. Before you run the test suite

Run through this checklist — it catches the majority of "why is this
failing and I don't know why" moments:

- [ ] No `thread_yield()`-in-a-loop anywhere (busy waiting is explicitly
      disallowed).
- [ ] Every place you disable interrupts, you restore the *previous*
      level, not a hardcoded "on."
- [ ] Every new list you introduce (`sleeping_list`, per-lock/thread
      bookkeeping) is initialized once, early, not lazily and not more
      than once.
- [ ] You didn't let `struct thread` grow so large it threatens the
      ~4 kB stack budget (the reference solution's additions to
      `thread.h` total well under 100 lines — if you're adding huge
      arrays, rethink).
- [ ] `thread_mlfqs` gates the *entire* advanced-scheduler code path,
      and your donation code doesn't run when it's set.
- [ ] You handled both "who wakes up" (choosing the max) *and*
      "does the current thread need to yield right now" (preemption)
      at every mutation point — it's easy to do one and forget the
      other.

Then: build in the `threads` directory, and read failing test output
literally — Pintos test failures usually print the exact expected vs.
actual ordering of events, which tells you precisely which invariant
you broke.

---

## 8. What to do when you're stuck

If you trace through a scenario by hand (as suggested in every
Self-check above) and your code's logic doesn't match your trace —
that's your bug, found without ever booting the kernel. If you trace
through and it *does* match, but the test still fails, add targeted
`printf()`s at the specific transition points (thread blocked, thread
unblocked, priority changed, donation applied) and compare against
the test's documented expected behavior — remember each test file has
a comment at the top explaining exactly what it expects (the FAQ
tells you this directly for the advanced scheduler, and it's true of
the other test suites too).

Good luck — this is genuinely one of the best "aha, *that's* how an
OS scheduler works" exercises out there. Once priority donation
clicks, you'll never look at a mutex the same way again.
