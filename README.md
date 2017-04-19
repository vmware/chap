# `chap`

`chap` analyzes un-instrumented ELF core files for leaks, memory growth, and
corruption. It is sufficiently reliable that it can be use in automation to
catch leaks before they are committed. As an interactive tool, it helps explain
memory growth, can identify some forms of corruption, and  supplements a
debugger by giving the status of various memory locations.

## Motivation

Traditionally, memory analysis for C and C++ requires instrumentation. However,
if an incident occurs using code that was not instrumented it may not be
practical to reproduce the problem. For example, it may have been due to a rare
execution path or resources required for the reproduction setup may not be
available. Instrumentation may also distort timing enough that it is not
practical to run on a regular basis, or it may be incomplete and report false
leaks.

## Quick Start

`chap` is distributed as source, so first build it (on Linux only):

```
$ git clone https://github.com/vmware/chap.git
$ mkdir build-chap
$ cd build-chap
$ cmake ../chap
$ make
$ ./chap
Usage: chap [-t] <file>

-t means to just do truncation check then stop
   0 exit code means no truncation was found

Supported file types include the following:

64-bit little-endian ELF core file
32-bit little-endian ELF core file
```

If that doesn't work out of the box, see [CONTRIBUTING.md](CONTRIBUTING.md) for
pre-requisites and other details.

Once built, here's a trivial example of an interactive session:

```
$ echo "int main() { new int; new int; *(int *)0 = 1; return 0; }" | g++ -xc++ -
$ ulimit -c unlimited
$ ./a.out
Segmentation fault (core dumped)
$ ./chap `ls -t core.* | head -1`
> summarize leaked
Unsigned allocations have 1 instances taking 0x18(24) bytes.
   Unsigned allocations of size 0x18 have 1 instances taking 0x18(24) bytes.
1 allocations use 0x18 (24) bytes.
> enumerate allocations /size 18
13f5010
13f5030
> explain 13f5010
Address 13f5010 is at offset 0x0 in a used allocation at 13f5010 of size 0x18
This allocation appears to be leaked.
This allocation appears to be unreferenced.
> explain 13f5030
Address 13f5030 is at offset 0x0 in a used allocation at 13f5030 of size 0x18
This allocation appears to be anchored.
Allocation at 13f5030 appears to be directly anchored from at least one register.
Register rcx for thread 1 references 13f5030
>
```

For more information on how to use `chap`, please see the built-in help and the
[USERGUIDE.md](USERGUIDE.md).

## Community

The best way to communicate with the maintainers is via the
[GitHub issue tracker](https://github.com/vmware/chap/issues).

## Contributing

We welcome contributions from the community. Please see
[CONTRIBUTING.md](CONTRIBUTING.md) for details.

If you wish to contribute code and you have not signed our contributor license
agreement (CLA). Our bot will update the issue when you open a pull request. For
any questions about the CLA process, please refer to our
[FAQ](https://cla.vmware.com/faq).

## License

`chap` is available under the GNU GENERAL PUBLIC LICENSE Version 2. Please see
[LICENSE.md](LICENSE.md).
