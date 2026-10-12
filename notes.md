# System Architecture & Systems Programming Notes
A comprehensive reference guide covering Git commands, POSIX Threads (`pthread`), Concurrency, Synchronization, Thread Pools, Load Balancing (L4 vs L7), and CPU Affinity.

---

## 1. Quick Reference: Useful Commands

### A. Downloading GitHub Repositories
```bash
# Full clone with git history
git clone https://github.com/owner/repository-name.git

# Shallow clone (latest snapshot only, faster, saves disk)
git clone --depth 1 https://github.com/owner/repository-name.git

# Clone using GitHub CLI
gh repo clone owner/repository-name

# Direct download as ZIP via curl / wget without git
curl -L -o repo.zip https://github.com/owner/repository-name/archive/refs/heads/main.zip
wget https://github.com/owner/repository-name/archive/refs/heads/main.zip -O repo.zip
```

### B. Compiling Multi-threaded C Programs
```bash
# Compiling with pthread and math libraries (-O2 optimization recommended)
gcc -O2 -o my_program my_program.c -pthread -lm

# Compiling with clang
clang -O2 -o my_program my_program.c -pthread -lm
```

---

## 2. Process (`fork()`) vs. Thread (`pthread`)

| Feature | Process (`fork()`) | Thread (`pthread`) |
| :--- | :--- | :--- |
| **Memory** | Completely isolated memory space (copy-on-write page tables). | **Shared memory** (heap, global variables, file descriptors). |
| **Creation Cost** | **Heavy (~1 to 10 ms)**. Involves OS kernel process table allocation. | **Lightweight (~10 to 50 ns)**. Reuses process space. |
| **Communication** | Requires IPC (Pipes, Unix Sockets, Shared Memory). | Direct via memory pointers & shared structs. |
| **Concurrency Scaling** | Creating 1,000 processes crashes the system or thrashes OS scheduler. | A fixed thread pool handles unlimited tasks via an in-memory queue. |

---

## 3. What Does a Thread Look Like in C?

Every POSIX thread function must have this exact signature:
```c
void* worker_function(void* arg) {
    // Thread body
    return NULL;
}
```

### Spawning and Waiting
```c
#include <pthread.h>

pthread_t thread;
int arg_data = 42;

// Spawns the thread
pthread_create(&thread, NULL, worker_function, &arg_data);

// Waits for the thread to exit
pthread_join(thread, NULL);
```

---

## 4. Concurrency Primitives: Mutex & Condition Variable

### A. `pthread_mutex_t` (The Mutual Exclusion Lock)
Prevents race conditions by ensuring only **one thread** touches shared data at any given instant.
* `pthread_mutex_lock(&lock)`: Grabs lock (blocks if held by another thread).
* `pthread_mutex_unlock(&lock)`: Releases the lock.

### B. `pthread_cond_t` (The Notification Bell)
Prevents CPU-burning busy-waiting (`while(empty) {}`). Puts threads into a deep zero-CPU sleep until signaled.

### C. The 3-in-1 "Magic" of `pthread_cond_wait(&cond, &mutex)`
When a worker thread calls `pthread_cond_wait`:
1. It **atomically releases the mutex** so other threads (producers) can access the queue.
2. It **puts the thread to sleep** in the OS kernel (0% CPU).
3. When woken by `pthread_cond_signal()` or `broadcast()`, it **automatically re-acquires the mutex** before returning!

### D. The Golden Rule: ALWAYS use `while`, NEVER `if`
```c
// ❌ WRONG: Vulnerable to spurious wakeups and race conditions
if (queue_is_empty()) {
    pthread_cond_wait(&cond, &mutex);
}

//  CORRECT: Guarantees condition still holds after wake-up
while (queue_is_empty()) {
    pthread_cond_wait(&cond, &mutex);
}
```
* **Why**: The OS can spuriously wake up threads without a signal, or another thread might claim the task first.

---

## 5. Thread Pool Architecture (Producer-Consumer)

Instead of spawning threads per request, pre-spawn $N$ threads (matching CPU core count) and feed them from a shared FIFO queue.

```c
typedef struct {
    void (*function)(void *arg);  // Code to execute
    void *arg;                    // Data passed to the function
} Task;

// Worker thread loop
void* worker_loop(void* arg) {
    while (1) {
        pthread_mutex_lock(&queue_lock);
        
        while (queue_count == 0 && !shutdown) {
            pthread_cond_wait(&task_available, &queue_lock);
        }
        
        if (shutdown && queue_count == 0) {
            pthread_mutex_unlock(&queue_lock);
            break;
        }
        
        Task task = queue_pop();
        pthread_mutex_unlock(&queue_lock);
        
        // Execute the task
        task.function(task.arg);
    }
    return NULL;
}
```

---

## 6. What is "Work"? (Defining Tasks)

A unit of work is an independent operation packaged into a C struct:
* **In a CPU Pool**: A function pointer + its arguments struct (`Task { func, arg }`).
* **In a Network Load Balancer**: A client socket descriptor (`int client_fd`).

### Can a C Thread Pool Run Non-C Work?
**Yes!** A C thread can execute anything:
* External scripts via `system("python3 script.py")` or `posix_spawn()`.
* CLI tools like `ffmpeg` for video transcoding.
* Compression via `zlib`, image rendering, or database calls.

---

## 7. Load Balancing: Layer 4 vs. Layer 7

| Feature | Layer 4 (Transport / TCP Proxy) | Layer 7 (Application / HTTP Proxy) |
| :--- | :--- | :--- |
| **What it inspects** | Raw bytes, IP address, Port number. | HTTP methods, URLs, Headers, Cookies. |
| **Routing capability** | Per connection (IP/Port). | Per request (e.g. `/api` vs `/static`). |
| **C Implementation** | Blind byte transfer (`read()` & `write()`). | String parsing & HTTP state machine. |
| **Supported Protocols**| Any TCP stream (HTTP, MySQL, SSH, Redis). | HTTP and HTTPS only. |

### Are CPU Task Dispatchers and Network Load Balancers the Same?
**Conceptually, 100% yes.** Both implement the Producer-Consumer / Dispatcher pattern:
* **CPU Pool**: Dispatches tasks in RAM to CPU Cores via function pointers.
* **Network Load Balancer**: Dispatches sockets over the network to Backend Servers via TCP.

---

## 8. CPU Affinity (Thread Pinning) & Hardware Realities

### What is CPU Affinity?
Permanently binding a specific thread to a specific physical CPU core so the OS scheduler does not move it.

### Why does the Linux Scheduler bounce threads by default?
When a thread yields/sleeps and wakes up, Linux checks if its previous core is busy. If Core 0 is temporarily executing another process, Linux eagerly schedules the thread on idle Core 2 rather than waiting. This is called **thread migration**.

### Does Thread Pinning prevent preemption?
**NO.** Pinning only restricts the *allowable cores* for the thread. The OS can still preempt (pause) the thread on its assigned core if system interrupts or higher-priority processes need CPU time.

### Why does Cache Locality matter if task data changes?
Even though each task has different inputs:
1. **Instruction Cache (I-Cache)**: The compiled machine code for the worker loop, network functions, and parsing stays resident in L1 instruction cache.
2. **Stack Memory Address Reuse**: The virtual memory addresses of the thread's stack frames are reused on every function return and call, keeping the stack pages hot in L1 data cache.
3. **Reused Buffers**: Pre-allocated read/write buffers (e.g. `char buf[4096]`) stay warm in cache across thousands of requests.
4. **Shared Data Structures**: Task queues, mutex memory, and backend tables remain hot in CPU cache.

### CPU Affinity C Implementation
```c
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdio.h>

void pin_thread_to_core(pthread_t thread, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    int rc = pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
        perror("pthread_setaffinity_np failed");
    }
}
```

---

## 9. L1 Cache Architecture & Preemption Mechanics

### A. Size of L1 Cache & Instruction Density
* Modern CPUs feature **separate L1 Instruction (L1i)** and **L1 Data (L1d)** caches per core.
* Typical size: **32 KB to 64 KB** for L1i, and **32 KB to 64 KB** for L1d.
* **Instruction Density Math**:
  * An average machine instruction is only **~3 to 4 bytes** (x86-64 variable 2–7B, ARM64 fixed 4B).
  * 100 lines of C code $\approx$ 300 to 500 machine instructions $\approx$ **~2 KB**.
  * A 32 KB L1i cache can hold $\approx \mathbf{8,192}$ instructions!
  * A 64 KB L1i cache can hold $\approx \mathbf{16,384}$ instructions!
  * Entire worker loops, mutex code, and socket logic (~4–8 KB) fit easily inside L1i cache.

### B. What Happens During Preemption (Context Switch)?
* **Code, Stack, and Heap**: These already reside in RAM (mapped into physical memory pages). They are never moved or copied during preemption.
* **CPU Registers (~512 bytes)**: The hardware registers (`RIP` instruction pointer, `RSP` stack pointer, general-purpose registers) are saved by the OS kernel into the thread's `task_struct` in RAM.
* **L1/L2 Hardware Cache**: The OS does **not** wipe the cache upon preemption. If the preemption is brief (e.g. 0.1ms interrupt), many of the thread's cache lines remain in L1 SRAM when the thread resumes on the same core.

---

## 10. Build Systems: Direct GCC vs Makefile vs CMake

| Approach | When to Use | Pros & Cons |
| :--- | :--- | :--- |
| **Direct `gcc`** | Quick tests with 1 file | Dead simple, but unmanageable once you have 3+ files. |
| **Simple `Makefile` (Recommended)** | Standard C systems projects (Redis, Nginx) | **Zero extra tools needed** (`make` is native), 15 lines of code, fast incremental compilation. |
| **CMake** | Cross-platform C++ with complex libraries | Good for Windows/MSVC + Xcode + Linux, but introduces boilerplate files and multi-step build folders. |

### Minimalist C Makefile Template
```makefile
CC = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
SRCS = src/main.c src/threadpool.c src/socket.c
TARGET = load_balancer

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)
```

---

## 11. Cross-Platform Compatibility: Linux vs. macOS

### A. What Works 100% Identically (POSIX Standard)
Because both Linux and macOS are Unix/POSIX compliant, the core networking and threading code is identical:
* **`pthread` API**: `pthread_create`, `pthread_join`, `pthread_mutex_t`, `pthread_cond_t`.
* **Sockets**: `socket()`, `bind()`, `listen()`, `accept()`, `connect()`, `send()`, `recv()`.
* **I/O Polling**: `poll()`, `select()`.

### B. The Difference: CPU Affinity (`pthread_setaffinity_np`)
* The `_np` suffix stands for **Non-Portable**.
* **Linux**: Allows hard pinning a thread to a specific core number.
* **macOS**: Does **not** support hard pinning (`pthread_setaffinity_np` is missing). Apple Silicon (M1/M2/M3/M4) uses heterogeneous cores (**P-cores** for performance, **E-cores** for efficiency). The Darwin kernel automatically places busy network threads onto P-cores and manages cache clusters.

### C. Cross-Platform C Preprocessor Pattern
```c
#include <pthread.h>
#include <stdio.h>

#ifdef __linux__
    #define _GNU_SOURCE
    #include <sched.h>
    void pin_thread_to_core(pthread_t thread, int core_id) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
    }
#elif defined(__APPLE__)
    void pin_thread_to_core(pthread_t thread, int core_id) {
        // macOS kernel manages P-core / E-core assignment automatically
        (void)thread;
        (void)core_id;
    }
#endif
```

---

## 12. Operating System Scheduling Realities

### A. The Myth of "100s of Background Processes"
When checking `ps aux` or System Monitor, a computer might list 200–400 processes.
* **99% of processes are in the `S` (Sleeping/Blocked) state**: Waiting for keyboard, mouse, network packets, or timer events. They use **0% CPU** and do not compete on runqueues.
* **Only ~1 to 5 threads** on the entire computer are actively in the **`R` (Runnable/Running) state** at any given millisecond.
* When your worker wakes from `pthread_cond_wait`, its low `vruntime` in Linux CFS gives it **immediate priority scheduling**.

### B. Multi-Core Runqueues & Work Stealing (OS Level)
* The OS scheduler **never leaves a CPU core idle** if there are runnable threads waiting.
* If Core 0 has 2 active worker threads and Core 1 has 0, Core 1's kernel scheduler **instantly steals** the waiting thread within microseconds.
* **With Affinity**: Assigned seating (threads never swap cores).
* **Without Affinity**: Musical chairs (all cores stay 100% busy in parallel, but threads may swap seats).

---

## 13. Process Switch vs. Thread Switch Mechanics

Inside our application, we are performing **Thread Switches**, which are ~10x faster than Process Switches.

| Step | Process Switch (`fork()`) | Thread Switch (`pthread`) |
| :--- | :--- | :--- |
| **Who is switching?** | Between distinct programs (e.g. Chrome $\rightarrow$ App) | Between our worker threads (Worker 1 $\rightarrow$ Worker 2) |
| **Memory Space** | Different virtual memory spaces | **Exact same memory space (Shared)** |
| **Page Table Pointer (`CR3`)** | **Swapped by kernel** | **Untouched** |
| **TLB Cache (Memory Translation)**| **Flushed / Invalidated** (leads to memory cache misses) | **Preserved 100% intact** |
| **What gets saved?** | Registers + Memory mappings + Process state | Only CPU registers (`RIP`, `RSP`, `RAX`, etc.) |
| **Latency** | **~1,500 to 5,000 ns (Slow)** | **~100 to 300 ns (Blazingly fast)** |

---

## 14. Application-Level Work Stealing Algorithm

When implementing work stealing inside an application runtime (e.g. Go scheduler, Rust Tokio, Java ForkJoinPool):

### Why Analyze WORKERS, Not CPU Cores?
1. **Speed**: Checking another worker's queue size in RAM takes **1 nanosecond**. Querying CPU core stats from the kernel takes **microseconds**.
2. **Accuracy**: CPU core usage might be caused by an unrelated background app. Checking a worker's queue directly answers: *"Does my application have pending work?"*

### The Deque (Double-Ended Queue) Trick
Each worker thread owns its own local queue:
* **The Owner Thread** pushes and pops from the **HEAD (LIFO)**:
  * Keeps recently used data cache-hot in L1 cache.
* **The Thief Thread** steals tasks from the **TAIL (FIFO)**:
  * Eliminates lock contention because the owner and thief operate at opposite ends of the memory array.

---

## 15. The Clean Division of Labor in Scheduling

You do **not** need to write scheduling logic that routes tasks to physical CPU cores.

```text
[Task] 
   │
   ▼ (Your Application's Job)
[Worker Thread]  ──► Assigned via Round Robin / In-Memory Queue
   │
   ▼ (OS Kernel's Job)
[Physical CPU Core] ──► Mapped via OS Scheduler (or statically pinned at startup)
```

1. **Your Code**: Decides which **Worker Thread** gets the task.
2. **The OS Kernel**: Decides which **Physical CPU Core** executes that worker.
3. If CPU Affinity is configured once at startup (`Worker 0 = Core 0`, `Worker 1 = Core 1`), assigning a task to `Worker 0` automatically routes it to `Core 0` with zero runtime math!

---

## 16. Semaphores (`sem_t`) vs. Condition Variables (`pthread_cond_t`)

Can a counting semaphore be used in a thread pool? **Yes, conceptually**, but `pthread_cond_t` is the industry standard for 3 critical reasons:

| Feature | Semaphore (`sem_t`) | Mutex + Cond Var (`pthread_cond_t`) |
| :--- | :--- | :--- |
| **Protects Queue Memory?** | No (still requires a separate mutex) | **Yes (via paired mutex)** |
| **Wake ALL Workers at Once?** | No (must loop `sem_post()` $N$ times) | **Yes (`pthread_cond_broadcast`)** |
| **Compound Boolean Checks?** | No (strictly an integer counter) | **Yes (`while(empty && !shutdown)`)** |
| **macOS Native Compatibility** | ❌ Broken (`sem_init` returns `ENOSYS`) | **100% Native on Linux & macOS** |

---

## 17. Deadlock Prevention: The 4 Golden Rules

Deadlocks occur when threads are permanently blocked waiting for resources held by one another. We prevent deadlocks using 4 strict architectural rules:

1. **The Single Mutex Rule**: Use only one mutex (`pool->lock`) for the queue. Circular wait (*Thread A waits for Lock 2 while Thread B waits for Lock 1*) is mathematically impossible with a single lock.
2. **Unlock BEFORE Running Tasks (Crucial!)**:
   * Worker grabs the lock, pops the task (takes ~10 nanoseconds), and **unlocks the mutex immediately**.
   * The task function `task.function(task.arg)` executes with **zero locks held**.
3. **Always Check Conditions in a `while` Loop**: Prevents missed wakeups and spurious kernel wakeups.
4. **Broadcast on Shutdown**: Use `pthread_cond_broadcast(&has_work)` during cleanup so no worker is left sleeping in kernel space forever.

---

## 18. Thrashing & The 4 Architectural Safeguards

**Thrashing** occurs when the system spends more time managing overhead than performing productive work.

| Thrashing Type | Root Cause | Our Architectural Safeguard |
| :--- | :--- | :--- |
| **CPU Context-Switch Thrashing** | Spawning too many threads fighting for CPU cores | **Fixed thread count bounded to physical cores** ($N_{\text{threads}} = N_{\text{cores}}$). |
| **L1/L2 Cache Churn Thrashing** | Threads bouncing between cores, flushing cache lines | **CPU Affinity (Thread Pinning)** binds workers to permanent cores. |
| **Hardware Bus / False Sharing** | Different cores modifying variables on the same 64-byte line | **64-Byte Cache-Line Alignment** (`__attribute__((aligned(64)))`) isolates worker memory. |
| **Lock Contention Thrashing** | Workers holding mutexes during execution | **Microsecond critical sections**: hold lock only to pop pointer, release before execution. |

---

## 19. Measuring "Load" in Least-Loaded Scheduling

### Why Avoid OS CPU % (`/proc/stat`)?
* **Slow**: Syscalls take ~100,000 nanoseconds.
* **Laggy**: Averages past 100ms–1000ms history; cannot predict immediate backlog.

### The Gold-Standard Metric: In-Flight Workload
Calculated directly in RAM in **~2 nanoseconds**:
$$\text{Load}(W_i) = \text{Tasks Queued}(W_i) + \text{Currently Executing}(W_i)$$

### The "Unknown Task Duration" Paradox (Halting Problem)
You cannot predict a task's exact runtime before execution. Systems handle this via:
1. **Pull-Based FIFO Queues**: Fastest workers automatically absorb more tasks without needing to predict weight.
2. **Runtime Feedback**: Learning historical average runtime for recurring task types.
3. **Work Stealing**: Dynamically stealing tasks from workers stuck on unexpectedly heavy jobs.

---

## 20. Centralized Pull Queue (Bank) vs. Distributed Work Stealing (Supermarket)

```mermaid
flowchart TD
    subgraph Model1 ["Bank Model: Centralized Pull Queue (Solution 1)"]
        CQ["1 Shared Queue (1 Mutex)"] --> W1["Worker 1"]
        CQ --> W2["Worker 2"]
        CQ --> W3["Worker 3"]
    end

    subgraph Model2 ["Supermarket Model: Work Stealing (Solution 3)"]
        Q1["Queue 1"] --> W4["Worker 1"]
        Q2["Queue 2"] --> W5["Worker 2"]
        W4 -.->|Steals from back| Q2
    end
```

* **Centralized Pull Queue (Our Architecture)**:
  * 1 shared queue in RAM. Workers pull tasks when free.
  * **Self-balancing**: A 10-second task ties up 1 worker while others finish 50 quick tasks.
  * Ideal for 2 to 16 CPU cores. Clean, bulletproof, zero wasted worker capacity.
* **Work Stealing (Advanced Multi-Core Runtimes)**:
  * Each worker has a private queue (Deque). Workers steal from other queues only when their own queue is empty.
  * Scales to 64–128+ cores (used by Go runtime, Rust Tokio, Java ForkJoinPool).

---

## 21. Scheduling Policy Trade-Offs: Round-Robin vs. Least-Loaded

* **Round-Robin (`SCHEDULE_ROUND_ROBIN`)**:
  * Rotates tasks strictly: Worker 0 $\rightarrow$ Worker 1 $\rightarrow$ Worker 2 $\rightarrow$ Worker 0.
  * **Vulnerable to Head-of-Line Blocking (Convoy Effect)**: If a 2-second task is assigned to Worker 0, a 0.01-second task assigned next to Worker 0 gets stuck waiting behind it even if Worker 1 is completely idle.
* **Least-Loaded (`SCHEDULE_LEAST_LOADED` / First-Available)**:
  * Routes new tasks to whichever worker currently has the lowest in-flight workload.
  * Light tasks immediately bypass busy workers and finish with zero latency.

---

## 22. Predictive Scheduling & Runtime Learning (EMA)

### A. The Blind Spot of Count-Based Least-Loaded
If Worker 1 has 1 light task (2 ms) and Worker 2 has 1 heavy task (2,000 ms), simple queue-count scheduling considers both equally loaded (`count = 1`). New tasks could mistakenly be sent to Worker 2.

### B. The 3 Core Steps of Predictive Scheduling
1. **Task Categorization**: Each task carries an integer `task_type` or identifier.
2. **Runtime Profiling & EMA Learning**: When tasks complete, record elapsed time and update an **Exponential Moving Average**:
   $$\text{PredictedTime} = (1 - \alpha) \times \text{OldPredicted} + \alpha \times \text{ActualDuration}$$
   * Smooths out outliers and naturally adapts if input sizes or CPU frequencies drift.
3. **Estimated Backlog Dispatching**:
   $$\text{Backlog}(W_i) = \sum_{t \in Q_i} \text{PredictedDuration}(t.\text{type}) + \text{EstimatedRemainingCurrent}(W_i)$$
   The dispatcher routes incoming tasks to whichever worker has the **lowest total predicted time in milliseconds**, rather than just the lowest count.

---

## 23. Stage 1 Implementation: Hardware & Portability Utilities (`include/common.h`)

This file provides the low-level foundation for our CPU Task Assigner.

### A. Dynamic Core Detection
* Implemented using `sysconf(_SC_NPROCESSORS_ONLN)`.
* Avoids hardcoding thread counts; automatically scales to match the exact hardware capacity of the host machine (detected **3 cores** on current system).

### B. High-Resolution Monotonic Timing (`get_time_ms`)
* Implemented with `clock_gettime(CLOCK_MONOTONIC, &ts)`.
* **Why Monotonic?** Unlike `gettimeofday()`, `CLOCK_MONOTONIC` measures raw hardware clock cycles and **never steps backward** if system time is adjusted via NTP. This provides microsecond accuracy essential for calculating Exponential Moving Averages (EMA) in predictive scheduling.
* Live test measured 50.00 ms requested sleep as **50.08 ms** actual elapsed time.

### C. 64-Byte Cache Line Alignment (`CACHE_ALIGNED`)
* Defined as:
  ```c
  #define CACHE_LINE_SIZE 64
  #define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))
  ```
* **Why it matters**: Modern x86_64 and ARM64 CPUs load data into cache in 64-byte chunks. If two workers share a 64-byte chunk, modifying one invalidates both caches (False Sharing).
* Live test verified `(uintptr_t)addr % 64 == 0`, guaranteeing zero cross-core cache invalidation.

### D. Cross-Platform CPU Pinning (`pin_thread_to_core`)
* **Linux implementation**:
  ```c
  cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core_id, &cpuset);
  pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
  ```
* **macOS implementation**: Uses `#elif defined(__APPLE__)` graceful fallback (no-op), allowing Darwin kernel to balance between Performance and Efficiency cores while keeping the codebase 100% portable.
* Live test verified successful pinning of `pthread_self()` to Core 0 with zero kernel errors.

---

## 24. Core Engine Implementation (`src/threadpool.c`)

### A. Per-Worker Architecture
* Each worker thread owns its own private ring-buffer queue (`head`, `tail`, `count`, `capacity`).
* **Zero Lock Contention**: Workers only lock their private mailbox; Worker 0 never blocks Worker 1 from taking tasks.
* Allocated via `aligned_alloc(CACHE_LINE_SIZE, ...)` to ensure each worker struct resides in its own dedicated hardware cache line.

### B. The Anti-Deadlock Worker Loop
1. **Sleep**: `pthread_cond_wait(&w->has_work, &w->lock)` while queue is empty and pool is alive.
2. **Pop**: Grab task from ring buffer, decrement `queue_count`, set `is_busy = 1`.
3. **UNLOCK FIRST**: Calls `pthread_mutex_unlock(&w->lock)` **before** running `task.function(task.arg)`.
   * Running tasks with zero locks held guarantees that circular lock-wait deadlocks are mathematically impossible.
4. **Time & Feedback**: Measures execution duration using `get_time_ms()` and calls `scheduler_record_completion()` to update EMA predictions.

### C. Graceful Teardown Flow (`threadpool_destroy`)
1. Set `is_shutdown = true`.
2. Ring `pthread_cond_broadcast(&w->has_work)` on every worker to wake sleeping threads.
3. Workers detect `is_shutdown && queue_count == 0` and break out of the infinite loop.
4. Main thread calls `pthread_join(w->thread, NULL)` on all workers, guaranteeing all in-flight tasks finish cleanly.
5. All mutexes and heap allocations are freed, leaving 0 memory leaks.

---

## 25. Multi-Policy Scheduler Implementation (`src/scheduler.c`)

### A. Policy 1: Round-Robin (`select_round_robin`)
* Uses a rotating cursor guarded by `stats_lock`: `(next++) % num_threads`.
* Simple, low overhead, but completely blind to task execution times.

### B. Policy 2: Least-Loaded (`select_least_loaded`)
* Calculates in-flight workload: `load = w->queue_count + w->is_busy`.
* Direct read from RAM in ~2 nanoseconds across all workers.
* Routes incoming tasks to whichever worker currently has the fewest total pending items.

### C. Policy 3: Predictive Scheduler with EMA (`select_predictive`)
* Estimates total remaining execution time in milliseconds:
  $$\text{Backlog}(W_i) = \sum_{q \in Q_i} \text{EMA}(q.\text{type}) + \left(0.5 \times \text{EMA}(\text{current\_type})\right)$$
* Dispatches to the worker that will become idle soonest, rather than just the worker with the lowest count.
* **Continuous Learning Loop**: Every completed task calls `scheduler_record_completion()`, smoothly adjusting predictions as system conditions change:
  $$\text{New Prediction} = (0.8 \times \text{Old}) + (0.2 \times \text{Actual Duration})$$

---

## 26. Verification Suite, Benchmarks & Sanitizer Hardening (`src/main.c`, `Makefile`)

### A. Test 1: Physical Core Pinning Verification
* Submits 6 tasks across 3 worker threads.
* Output logs show tasks executing interleaved across **Core 0, Core 1, and Core 2**:
  ```text
  [Quick Task #01] executed on Core 0
  [Quick Task #02] executed on Core 1
  [Quick Task #03] executed on Core 2
  ```
* Confirms thread affinity prevents kernel migration and distributes load evenly.

### B. Test 2: True Multi-Core Parallel Speedup
* Executed 6 heavy CPU-bound calculation tasks.
  * **1 Worker Thread (Single Core)**: **332.88 ms**
  * **3 Worker Threads (All Cores)**:  **170.94 ms**
  * **Parallel Speedup**: **~1.95x faster** true parallel speedup.

### C. Test 3: Asymmetric Load Policy Comparison
* Workload: 1 Heavy Task (takes ~250ms on Core 0) + 6 Quick Tasks (~30ms each).
  * **Round-Robin**: Quick tasks assigned to Core 0 suffer Head-of-Line Blocking and wait behind the heavy task.
  * **Least-Loaded & Predictive**: Dynamically detect Core 0's backlog and route all quick tasks around it to Core 1 and Core 2, completing earlier.

### D. Hardening with Compiler Sanitizers
* **ThreadSanitizer (`make tsan`)**: Validated with `-fsanitize=thread`. 
  * Caught initial unsynchronized read of `is_shutdown` and queue counts.
  * Fixed by guarding all status inspections with dedicated mutexes and per-worker flags.
  * Final TSan run: **0 data races, 0 deadlocks, clean pass**.
* **AddressSanitizer (`make asan`)**: Validated with `-fsanitize=address,undefined`.
  * Checked entire allocation lifecycle (`aligned_alloc`, `malloc`, `free`).
  * Final ASan run: **0 memory leaks, 0 buffer overflows, clean pass**.









---

## 27. HTTP Microservice as Real Workload (Option 1)

### Why Replace Synthetic Tasks with Real Work?
- Synthetic tasks (`usleep`, XOR loops) don't involve I/O or system calls, which is unrealistic.
- Real HTTP socket work gives us measurable, comparable, reproducible benchmarks.
- `ab` (ApacheBench) is an industry-standard tool for req/s and latency measurement.

### Files Added
| File | Purpose |
|------|---------|
| `include/server.h` | Port constant, backlog, `TASK_TYPE_HTTP`, and function declarations |
| `src/server.c` | `socket() → bind() → listen() → accept()` loop; on new client: `threadpool_add(pool, http_handle_request, client_fd, TASK_TYPE_HTTP)` |
| `include/http_handler.h` | Declaration of the worker task function |
| `src/http_handler.c` | Reads HTTP request, routes to `/`, `/ping`, `/status`, returns JSON |
| `src/main.c` | Starts pool (Least-Loaded), calls `server_create()` + `server_run()` |
| `src/baseline.c` | **Comparison binary**: naive `pthread_create()` per request, no pool, no affinity |

### Key Implementation Details
- `client_fd` is passed to the thread pool as `(void*)(intptr_t)client_fd` — portable integer-to-pointer cast.
- Worker unpacks it as `int client_fd = (int)(intptr_t)arg`.
- `SO_REUSEADDR` prevents "Address already in use" after restart.
- `SERVER_BACKLOG = 128` lets the kernel queue connections during busy bursts.
- Baseline uses `pthread_detach()` so no `pthread_join()` is needed per thread.
- If `threadpool_add()` fails (queue full), server sends HTTP 503 instead of dropping silently.

### Benchmark Results (5000 requests, 50 concurrent, ARM64 3-core Kali Linux)

| Metric | Our Task Assigner (Least-Loaded, pinned) | Baseline (pthread_create per req) |
|--------|------------------------------------------|----------------------------------|
| Requests/sec | **30,027 req/s** | 18,400 req/s |
| Latency p50 | **1 ms** | 2 ms |
| Latency p99 | **5 ms** | 6 ms |
| Worst case | **5 ms** | 9 ms |
| Failed requests | 0 | 0 |

**Our scheduler is ~63% faster with 2× lower median latency.**

### Why Our Scheduler Wins
1. **Zero thread spawn cost** — workers are pre-created; no `clone()` syscall per request.
2. **CPU affinity** — each worker is pinned to a core, so its L1 cache stays warm across requests.
3. **Bounded concurrency** — exactly 3 workers for 3 cores; baseline can create hundreds of threads simultaneously, causing OS scheduling pressure and TLB thrashing.
4. **Least-Loaded routing** — tasks go to the least busy worker, not randomly.

---

## 28. Architecture: How HTTP and Task Assigner Connect

```
Client (curl/ab)
    |
    | TCP connect(:8080)
    v
accept() in main thread
    |
    | threadpool_add(pool, http_handle_request, client_fd, TASK_TYPE_HTTP)
    v
Scheduler selects worker (Least-Loaded)
    |
    | worker_t dequeues task, unlocks, calls:
    v
http_handle_request(client_fd)
    |  recv() HTTP request
    |  snprintf() JSON body
    |  send() HTTP response
    |  close(client_fd)
    v
Worker free, picks next task
```
