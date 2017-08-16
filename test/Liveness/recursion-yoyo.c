// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %llvmgcc %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %llvmgcc %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %llvmgcc %s -emit-llvm -O3 -g -c -o %t-O3.bc

// ---  cannot be detected with -O0 (alloca)
// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O0.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 %t-O0.bc 2>&1 | not FileCheck %s
// RUN: not test -f %t-O0.klee-out/test000001.infty.err

// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O1.klee-out -detect-infinite-loops -stop-after-n-instructions=100000 %t-O1.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O1.klee-out/test000001.infty.err

// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O2.klee-out -detect-infinite-loops -stop-after-n-instructions=100000 %t-O2.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O2.klee-out/test000001.infty.err

// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O3.klee-out -detect-infinite-loops -stop-after-n-instructions=100000 %t-O3.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O3.klee-out/test000001.infty.err

#include <stdio.h>

void yoYo(int x, int y) {
  printf("x: %d, y: %d\n", x, y);
  if(x > 0) {
    return yoYo(--x, ++y);
  } else {
    return yoYo(y, x);
  }
}

/* output:
 * x: 5, y: 0
 * x: 4, y: 1
 * x: 3, y: 2
 * x: 2, y: 3
 * x: 1, y: 4
 * x: 0, y: 5
 * x: 5, y: 0
 * x: 4, y: 1
 */

int main(int argc, char *argv[]) {
  // CHECK: infinite loop
  yoYo(5, 0);
}
