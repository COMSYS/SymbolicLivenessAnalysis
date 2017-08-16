// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %llvmgcc %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %llvmgcc %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %llvmgcc %s -emit-llvm -O3 -g -c -o %t-O3.bc

// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -output-dir=%t-O0.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 -allocate-determ %t-O0.bc > %t-O0.log 2>&1
// RUN: not test -f %t-O0.klee-out/test000001.infty.err
// RUN: cat %t-O0.log | FileCheck %s

// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -output-dir=%t-O1.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 -allocate-determ %t-O1.bc > %t-O1.log 2>&1
// RUN: not test -f %t-O1.klee-out/test000001.infty.err
// RUN: cat %t-O1.log | FileCheck %s

// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -output-dir=%t-O2.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 -allocate-determ %t-O2.bc > %t-O2.log 2>&1
// RUN: not test -f %t-O2.klee-out/test000001.infty.err
// RUN: cat %t-O2.log | FileCheck %s

// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -output-dir=%t-O3.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 -allocate-determ %t-O3.bc > %t-O3.log 2>&1
// RUN: not test -f %t-O3.klee-out/test000001.infty.err
// RUN: cat %t-O3.log | FileCheck %s

// regression test: we previously falsely detected an infinite loop in this code
// because after returning from plusone, all changes made to allocas were
// discarded (even though, the alloca's scope is not left in this case).

__attribute__((noinline))
void plusone(int *value) {
  if (*value < 3) {
    *value = *value + 1;
  } else {
    *value = 0;
  }
}

int main(int argc, char *argv[]) {
  int s1 = 1;

  while (1) {
    plusone(&s1);
    // CHECK-NOT: KLEE: ERROR: {{[^:]*}}/passed-alloca-finite.c:{{[0-9]+}}: infinite loop{{$}}
    if (s1 == 0) break;
  }

  return 0;
}
