// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %llvmgcc %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %llvmgcc %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %llvmgcc %s -emit-llvm -O3 -g -c -o %t-O3.bc

// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -output-dir=%t-O0.klee-out -detect-infinite-loops -stop-after-n-instructions=1000 %t-O0.bc 2>&1 | FileCheck %s
// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -output-dir=%t-O1.klee-out -detect-infinite-loops -stop-after-n-instructions=1000 %t-O1.bc 2>&1 | FileCheck %s
// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -output-dir=%t-O2.klee-out -detect-infinite-loops -stop-after-n-instructions=1000 %t-O2.bc 2>&1 | FileCheck %s
// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -output-dir=%t-O3.klee-out -detect-infinite-loops -stop-after-n-instructions=1000 %t-O3.bc 2>&1 | FileCheck %s

#include <inttypes.h>
#include <klee/klee.h>

int main(int argc, char **argv) {
  int array[3];
  klee_make_symbolic(&array, sizeof(array), "array");
  int x;
  klee_make_symbolic(&x, sizeof(x), "x");
  array[0] = 0;
  array[1] = 1;
  array[2] = 2;
  if(x >= 0 && x <= 2) {
    // CHECK: infinite loop
    while(array[x] == 1) {
      array[x + 1] = 0;
      array[x - 1] = 1;
    }
  }
}
