# OS-Level CPU Task Assigner & Thread Pool in C

A high-performance, multithreaded CPU task dispatcher and thread pool written in C using POSIX Threads (`pthread`) and Linux CPU Affinity.

## Features
- **Fixed Thread Pool**: Pre-spawns worker threads matching CPU cores.
- **CPU Affinity Pinning**: Pins each worker thread to a dedicated physical CPU core (`pthread_setaffinity_np`), eliminating thread migration and maximizing L1/L2 cache locality.
- **Thread-Safe Task Queue**: Synchronized using `pthread_mutex_t` and `pthread_cond_t` (Producer-Consumer pattern).
- **Cross-Platform Compatibility**: Automatically detects and adapts between Linux and macOS.

## Architecture
```
[Task Submission] 
       │
       ▼ (Application Queue)
[Thread Pool Queue]
       │
 ┌─────┼─────┐
 ▼     ▼     ▼
Core0 Core1 Core2  (Pinned Workers)
```

## Documentation & Notes
See [`notes.md`](./notes.md) for detailed architecture documentation, L1 cache sizing math, process vs thread switches, and scheduling mechanics.
