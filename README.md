# High-Performance CPU Task Assigner & Thread Pool in C

A production-grade, low-latency CPU task scheduler, thread pool, and HTTP microservice written in C using POSIX Threads (`pthread`), Linux CPU Affinity, and cache-line aligned data structures.

---

## Key Features

- **Pre-Spawned Thread Pool**: Eliminates on-demand thread creation costs (`clone()` system calls and 8MB virtual memory stack allocation per task).
- **Per-Worker Lockless Queuing**: Each worker owns a private ring buffer, eliminating global lock contention between worker threads.
- **Cache-Line Alignment (`alignas(64)`)**: Prevents false sharing across CPU cores under the MESI cache coherence protocol.
- **Multiple Scheduling Policies**:
  - **Round-Robin (`SCHEDULE_ROUND_ROBIN`)**: Blind cyclic task distribution.
  - **Least-Loaded (`SCHEDULE_LEAST_LOADED`)**: Dynamic inspection of worker in-flight load (`queue_count + is_busy`) in ~2ns from RAM.
  - **Predictive EMA (`SCHEDULE_PREDICTIVE`)**: Exponential Moving Average runtime estimation minimizing total completion latency.
- **Hardware Thread Pinning**: Binds worker threads to specific physical CPU cores using `pthread_setaffinity_np` for maximum L1/L2 cache locality.
- **Embedded HTTP/1.1 Microservice**: Serves live network requests with routing (`/`, `/ping`, `/status`) and CPU core tracking.
- **Zero Memory Leaks & Zero Data Races**: Hardened with **AddressSanitizer (ASan)** and **ThreadSanitizer (TSan)**.

---

## Architecture Overview

```
                      Client Requests (curl / ab / browser)
                                       │
                                       ▼ TCP Port 8080
                           ┌────────────────────────┐
                           │   Main Accept Loop     │
                           │   (src/server.c)       │
                           └───────────┬────────────┘
                                       │ threadpool_add(client_fd)
                                       ▼
                     ┌──────────────────────────────────┐
                     │   Task Assigner / Scheduler      │
                     │   (Round-Robin / Least-Loaded /  │
                     │    Predictive EMA)               │
                     └───────┬──────────┬──────────┬────┘
                             │          │          │
             ┌───────────────┘          │          └───────────────┐
             ▼                          ▼                          ▼
   ┌───────────────────┐      ┌───────────────────┐      ┌───────────────────┐
   │ Worker 0 (Core 0) │      │ Worker 1 (Core 1) │      │ Worker 2 (Core 2) │
   │ 64B Cache-Aligned │      │ 64B Cache-Aligned │      │ 64B Cache-Aligned │
   │ Private Queue     │      │ Private Queue     │      │ Private Queue     │
   └─────────┬─────────┘      └─────────┬─────────┘      └─────────┬─────────┘
             │                          │                          │
             ▼                          ▼                          ▼
      [HTTP Handler]             [HTTP Handler]             [HTTP Handler]
      recv() -> JSON -> send()   recv() -> JSON -> send()   recv() -> JSON -> send()
```

---

## Benchmark Results (ARM64 Kali Linux · 3 Physical Cores)

### 1. HTTP Server: Task Assigner vs. Native Linux Scheduler Baseline
Workload: 10,000 HTTP requests (`/status`), evaluated across three concurrency levels using ApacheBench (`ab`):

| Concurrency | Metric | Our Task Assigner (Pinned Pool) | Linux Baseline (On-demand Threads) | Performance Gain |
|---|---|---|---|---|
| **$c=10$ (Low)** | Throughput<br>Mean latency<br>p99 latency<br>Worst case | **37,152 req/s**<br>**0.269 ms**<br>**1 ms**<br>**3 ms** | 23,048 req/s<br>0.434 ms<br>2 ms<br>**80 ms** | **+61.2% req/s**<br>**1.6x faster**<br>**2x lower tail**<br>**26.6x better** |
| **$c=50$ (Medium)** | Throughput<br>Mean latency<br>p99 latency<br>Worst case | **40,998 req/s**<br>**1.220 ms**<br>**2 ms**<br>**6 ms** | 31,902 req/s<br>1.567 ms<br>5 ms<br>7 ms | **+28.5% req/s**<br>**1.3x faster**<br>**2.5x lower tail**<br>**Better** |
| **$c=100$ (High)** | Throughput<br>Mean latency<br>p99 latency<br>Worst case | **39,849 req/s**<br>**2.509 ms**<br>9 ms<br>11 ms | 32,272 req/s<br>3.099 ms<br>**7 ms**<br>**10 ms** | **+23.5% req/s**<br>**1.2x faster**<br>Baseline (more threads)<br>Baseline |

> **Key Finding: Thread Spawn Jitter**  
> Under sudden bursts at low concurrency, on-demand `pthread_create()` suffered an **80 ms worst-case latency** due to `clone()` syscall overhead and cold stack allocation. Our pre-warmed thread pool had a maximum latency of only **3 ms**.

---

### 2. The CPU Pinning Factor: Pinned vs. Unpinned Pool

| Concurrency | Pinned Pool (`load_balancer`) | Unpinned Pool (`load_balancer_unpinned`) |
|---|---|---|
| **$c=10$** | 36,360 req/s | **41,888 req/s** |
| **$c=50$** | 38,354 req/s | **47,082 req/s** |
| **$c=100$** | 37,861 req/s | **47,879 req/s** |

> **Architectural Insight**:  
> For **CPU-bound pure compute**, strict hardware pinning wins by preserving L1/L2 cache locality.  
> However, for **network socket I/O on loopback**, the Linux kernel network stack (`ksoftirqd`) and the client (`ab`) share the same 3 physical cores. An **unpinned pool** gives the Linux scheduler flexibility to interleave network interrupts and worker processing without core contention.

---

## Building and Running

### Prerequisites
- GCC or Clang
- POSIX threads (`libpthread`)
- ApacheBench (`apache2-utils` or `httpd-tools`) for benchmarking

### Build Commands
```bash
# Build optimized binaries (main server, unpinned pool, and baseline)
make all

# Build with AddressSanitizer (detects memory leaks & buffer overflows)
make asan

# Build with ThreadSanitizer (detects concurrency data races & deadlocks)
make tsan

# Clean build artifacts
make clean
```

### Running the Servers

```bash
# 1. Run our primary Task Assigner HTTP Server (Port 8080)
./load_balancer

# 2. Run the Unpinned Pool HTTP Server (Port 8080)
./load_balancer_unpinned

# 3. Run the Native Linux Baseline Server (Port 8081)
./baseline
```

### Benchmarking with ApacheBench

```bash
# Test throughput and latency under 50 concurrent clients
ab -n 10000 -c 50 http://localhost:8080/status

# Test baseline server
ab -n 10000 -c 50 http://localhost:8081/status
```

---

## Repository Structure

```
├── include/
│   ├── common.h               # Time utilities, CPU count, CPU affinity, CACHE_ALIGNED
│   ├── threadpool.h           # Public API (policies enum, task_t, threadpool_t)
│   ├── threadpool_internal.h  # Private worker_t, scheduler_state_t, pool structs
│   ├── scheduler.h            # Internal scheduler routing interface
│   ├── server.h               # TCP listening socket & accept loop interface
│   └── http_handler.h         # HTTP request/response handler declarations
├── src/
│   ├── threadpool.c           # Core engine: private ring buffers, worker loop, shutdown
│   ├── scheduler.c            # Policies: Round-Robin, Least-Loaded, Predictive EMA
│   ├── server.c               # TCP socket creation, SO_REUSEADDR, accept loop
│   ├── http_handler.c         # HTTP parser, JSON response generator, core_id reporting
│   ├── main.c                 # Main server entry point (Least-Loaded pool on port 8080)
│   └── baseline.c             # Linux scheduler baseline (spawns pthread_create per req)
├── Makefile                   # Optimized release, ASan, and TSan build targets
├── README.md                  # Project overview, architecture, and benchmark results
└── notes.md                   # In-depth systems engineering and operating systems guide
```

---

## Detailed Notes & Systems Concepts
For comprehensive documentation on operating system concepts, mutex vs condition variables, cache line sizing, false sharing, Linux CFS scheduler vs application schedulers, and memory sanitizers, read [`notes.md`](./notes.md).
