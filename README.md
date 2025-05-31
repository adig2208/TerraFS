# TerraFS — A Minimal Read-Write Filesystem in Userspace

**TerraFS** is a lightweight, Unix-like filesystem implemented in C using [FUSE](https://github.com/libfuse/libfuse). It emulates core file system operations over a virtual disk image, making it ideal for learning and experimenting with filesystem internals in a safe, user-space environment.

## Features

- Create, read, write, and delete files and directories
- Custom metadata management using inodes and a simulated block device
- File operations: chmod, utime, rename, truncate
- Python utilities for disk image generation and inspection
- Unit tested using the `libcheck` framework

## Project Structure

```text
project/
├── homework.c         # Core implementation of FUSE callbacks
├── fs5600.h           # Filesystem data structures and constants
├── hw3fuse.c          # FUSE setup and boilerplate
├── misc.c             # Helper functions
├── unittest-1.c       # Unit tests - basic operations
├── unittest-2.c       # Unit tests - extended operations
├── gen-disk.py        # Python script to generate disk image
├── read-img.py        # Read/print disk image contents
├── diskfmt.py         # Shared disk format module
├── disk1.in           # Disk image layout input
├── disk2.in           # Alternate disk layout input
└── Makefile           # Build system
```
