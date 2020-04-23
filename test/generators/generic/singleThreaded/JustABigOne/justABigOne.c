// Copyright (c) 2020 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0
#include <malloc.h>

int main(int argc, char **argv, char **envp) {
  /*
   * Create only one allocation, and make that one sufficiently large that
   * it won't get carved out of a smaller region.  The reason that this
   * is possibly of interest is if we have a process in some language
   * environment that doesn't regularly use malloc but where there is
   * some shared library that may in certain cases use it only for
   * large allocations.
   */
  malloc(0x1000000);
  *((int *)(0)) = 92;
}
