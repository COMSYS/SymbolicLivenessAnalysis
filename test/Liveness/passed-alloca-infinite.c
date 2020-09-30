// XFAIL: *

// This test fails because clang, instead of calling alloca only after execution
// reached the if-then branch, places the alloca instruction at the beginning of
// the entry basic block of allocator(). Thus, a new allocation is made in each
// iteration of the recursive loop, polluting the fingerprint.

// RUN: %clang %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %clang %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %clang %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %clang %s -emit-llvm -O3 -g -c -o %t-O3.bc

// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -output-dir=%t-O0.klee-out -detect-infinite-loops -max-instructions=10000 -allocate-determ %t-O0.bc > %t-O0.log 2>&1
// RUN: test -f %t-O0.klee-out/test000001.infty.err
// RUN: cat %t-O0.log | FileCheck %s

// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -output-dir=%t-O1.klee-out -detect-infinite-loops -max-instructions=10000 -allocate-determ %t-O1.bc > %t-O1.log 2>&1
// RUN: test -f %t-O1.klee-out/test000001.infty.err
// RUN: cat %t-O1.log | FileCheck %s

// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -output-dir=%t-O2.klee-out -detect-infinite-loops -max-instructions=10000 -allocate-determ %t-O2.bc > %t-O2.log 2>&1
// RUN: test -f %t-O2.klee-out/test000001.infty.err
// RUN: cat %t-O2.log | FileCheck %s

// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -output-dir=%t-O3.klee-out -detect-infinite-loops -max-instructions=10000 -allocate-determ %t-O3.bc > %t-O3.log 2>&1
// RUN: test -f %t-O3.klee-out/test000001.infty.err
// RUN: cat %t-O3.log | FileCheck %s

__attribute__((noinline))
void allocator(int *x) {
  *x = *x - 1;
  if (*x == 0) {
    int y = 5; // allocation
    // CHECK: KLEE: ERROR: {{[^:]*}}/passed-alloca-infinite.c:{{[0-9]+}}: infinite loop{{$}}
    allocator(&y);
  } else {
    *x = *x + 1;
    allocator(x);
  }
}

int main() {
  int a = 1;
  allocator(&a);
}
