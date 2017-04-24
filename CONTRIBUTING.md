# Contributing to `chap`

These guidelines describe how to contribute to `chap`. These are just
guidelines, not rules. Use your best judgement, and feel free to propose changes
to this document in a pull request.

#### Table Of Contents

1. [Prerequisites](#prerequisites)
1. [Building](#building)
1. [Testing](#testing)
1. [Formatting Commits](#formatting-commits)

### CLA

If you wish to contribute code and you have not signed our contributor license
agreement (CLA). Our bot will update the issue when you open a pull request. For
any questions about the CLA process, please refer to our
[FAQ](https://cla.vmware.com/faq).

### Prerequisites

* __Linux__ - Linux is the only supported platform for building and running
  `chap`. The primary testing distribution is [Ubuntu](www.ubuntu.com) LTS.

* __C++11__ - Currently supported compilers are:
  * __GCC 4.8__
  * __GCC 5__ (on Ubuntu 16.04 LTS)
  * __GCC 6__

* __CMake__ - We require CMake 3.5.

* __clang-format__ - We use clang-format's 'Google' style to format code.

### Building

```bash
git clone https://github.com/vmware/chap.git
mkdir build-chap
cd build-chap
cmake ../chap
make
./chap
Usage: chap [-t] <file>

-t means to just do truncation check then stop
   0 exit code means no truncation was found

Supported file types include the following:

64-bit little-endian ELF core file
32-bit little-endian ELF core file
```

### Testing

We use CTest to run tests.
```bash
cd build-chap
ctest
Test project /dbc/pa-dbc1110/mzeren/build-chap
    Start 1: expectedOutput/ELF32/LibcMalloc/OneAllocated
1/1 Test #1: expectedOutput/ELF32/LibcMalloc/OneAllocated ...   Passed    0.14 sec

100% tests passed, 0 tests failed out of 1

Total Test time (real) =   0.14 sec
```

### Pull Request Checklist

Before publishing a pull request, please:

1. Review open and closed issues to look for duplicates.
1. Add tests to test your change.
1. Run the full test suite.
1. Use clang-format to format just your changed lines.
   The [git-clang-format](https://github.com/llvm-mirror/clang/blob/master/tools/clang-format/git-clang-format)
   script may be helpful.

### Formatting Commit Messages

When formatting commit messages please:
1. Separate subject from body with a blank line.
1. Prefix subject with a one word category such as `doc:`, `build:`, `tests`,
   `core:`, etc.
3. Capitalize the subject line.
2. Try to limit the subject line to 50 characters.
4. Do not end the subject line with a period.
5. Use the imperative mood in the subject line.
6. Wrap the body at 72 characters.
7. Use the body to explain what and why vs. how.

For more background and discussion on these rules please read:
http://chris.beams.io/posts/git-commit/
