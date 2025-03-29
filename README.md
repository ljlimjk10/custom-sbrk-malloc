# sbrk-mem-allocator

A simple custom memory allocator written in C++, using `sbrk()` `mmap()` and free list algo for learning.

---

- Manual memory management using `sbrk()` for small memory allocations
- Manual memory management using `mmap()` for larger memory allocation to free the mapped physical pages and reduce memory fragmentation
- Free list-based allocation
- Block splitting for efficient reuse
- Coalescing of adjacent free blocks

