// Copyright (c) 2017 Broadcom. All Rights Reserved.
// The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
// SPDX-License-Identifier: GPL-2.0

#include <malloc.h>
int main(int argc, const char**argv) {
   int *pI = (int *) (malloc(sizeof(int)));
   *pI = 92;
   *((int *) 0) = 92; // crash
   return 0;
}
