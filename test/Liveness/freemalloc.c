// XFAIL: *

// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %llvmgcc %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %llvmgcc %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %llvmgcc %s -emit-llvm -O3 -g -c -o %t-O3.bc

// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O0.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 -allocate-determ %t-O0.bc > %t-O0.log 2>&1
// RUN: test -f %t-O0.klee-out/test000001.infty.err
// RUN: cat %t-O0.log | FileCheck %s

// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O1.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 -allocate-determ %t-O1.bc > %t-O1.log 2>&1
// RUN: test -f %t-O1.klee-out/test000001.infty.err
// RUN: cat %t-O1.log | FileCheck %s

// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O2.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 -allocate-determ %t-O2.bc > %t-O2.log 2>&1
// RUN: test -f %t-O2.klee-out/test000001.infty.err
// RUN: cat %t-O2.log | FileCheck %s

// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O3.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 -allocate-determ %t-O3.bc > %t-O3.log 2>&1
// RUN: test -f %t-O3.klee-out/test000001.infty.err
// RUN: cat %t-O3.log | FileCheck %s

#include <stdlib.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  char *str = malloc(10 * sizeof(char));

  // CHECK: KLEE: ERROR: {{[^:]*}}/freemalloc.c:{{[0-9]+}}: infinite loop{{$}}
  while(1) {
    for(int i = 0; i < 2; i++) {
      str[i] = ('A' + i);
    }
    str[2] = '\0';
    printf("%s\n", str);
    free(str);
    str = 0;
    str = malloc(10 * sizeof(char));
  }
}
