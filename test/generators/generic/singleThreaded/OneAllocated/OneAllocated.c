// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#include <malloc.h>
int main(int argc, const char**argv) {
   int *pI = (int *) (malloc(sizeof(int)));
   *pI = 92;
   *((int *) 0) = 92; // crash
   return 0;
}
