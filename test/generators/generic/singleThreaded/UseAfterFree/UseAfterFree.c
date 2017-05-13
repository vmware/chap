// Copyright (c) 2017 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: GPL-2.0

#include <malloc.h>
#define NUM_BUFFERS 400
int main(int argc, const char **argv) {
  void **buffers[NUM_BUFFERS];
  for (size_t i = 0; i < NUM_BUFFERS; i++) {
    buffers[i] = (void **)malloc((i + 1) * sizeof(void *));
    buffers[i][0] = buffers + i;
  }
  // Free every other buffer.  This avoids any coallescing.
  for (size_t i = 0; i < NUM_BUFFERS; i += 2) {
    free(buffers[i]);
  }
  // Use a few of the free ones
  buffers[0][0] = argv;
  buffers[10][0] = (void *)(0x0123456789abcdef);
  buffers[50][0] = buffers;
  buffers[100][0] = buffers + 1;
  *((int *)0) = 92;  // crash
  return 0;
}
