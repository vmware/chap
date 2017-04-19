# `chap` User's Guide

This manual describes how to use `chap` which is a tool that looks at process images, identfies dynamically allocated memory and how it is used.

### Where to Run `chap`.
At present this has only been tested on Linux, with the `chap` binary built for 64bit x86-64.

### Supported process image file formats.
At the time of this writing, the only process image file formats supported by `chap` are little-endian 32 bit ELF cores and little-endian 64 bit ELF cores, both of which are expected to be complete.  Run `chap` without any arguments to get a current list of supported process image file formats.

### Supported memory allocators.
At present the only memory allocator for which `chap` will be able to find allocations in the process image is the version of malloc used by glibc on Linux.
