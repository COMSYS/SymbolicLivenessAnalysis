// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %llvmgcc %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %llvmgcc %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %llvmgcc %s -emit-llvm -O3 -g -c -o %t-O3.bc

// ---  with allocate-determ: not detected
// ---  without allocate-determ: detected after 3 iterations?
// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O0.klee-out -detect-infinite-loops -stop-after-n-instructions=100000 -allocate-determ %t-O0.bc 2>&1 | not FileCheck %s

// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O1.klee-out -detect-infinite-loops -stop-after-n-instructions=100000 -allocate-determ %t-O1.bc > %t-O1.log 2>&1
// RUN: test -f %t-O1.klee-out/test000001.infty.err
// RUN: cat %t-O1.log | FileCheck %s
// RUN: bash -c 'cat %t-O1.log | grep AB | wc -l | { read linecount; test $linecount == 1; }'

// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O2.klee-out -detect-infinite-loops -stop-after-n-instructions=100000 -allocate-determ %t-O2.bc > %t-O2.log 2>&1
// RUN: test -f %t-O2.klee-out/test000001.infty.err
// RUN: cat %t-O2.log | FileCheck %s
// RUN: bash -c 'cat %t-O2.log | grep AB | wc -l | { read linecount; test $linecount == 1; }'

// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O3.klee-out -detect-infinite-loops -stop-after-n-instructions=100000 -allocate-determ %t-O3.bc > %t-O3.log 2>&1
// RUN: test -f %t-O3.klee-out/test000001.infty.err
// RUN: cat %t-O3.log | FileCheck %s
// RUN: bash -c 'cat %t-O3.log | grep AB | wc -l | { read linecount; test $linecount == 1; }'

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
  char *test = "AB";

  // CHECK: KLEE: ERROR: {{[^:]*}}/memcpy.c:{{[0-9]+}}: infinite loop{{$}}
  while(1) {
    char *str = malloc(10 * sizeof(str));
    int len = strlen(test);
    memcpy(str, test, len);
    str[len] = '\0';
    printf("%s\n", str);
    free(str);
  }
}
