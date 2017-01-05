// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %llvmgcc %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %llvmgcc %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %llvmgcc %s -emit-llvm -O3 -g -c -o %t-O3.bc

// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -output-dir=%t-O0.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 %t-O0.bc 2>&1 | not FileCheck %s
// RUN: not test -f %t-O0.klee-out/test000001.infty.err
// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -output-dir=%t-O1.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 %t-O1.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O1.klee-out/test000001.infty.err
// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -output-dir=%t-O2.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 %t-O2.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O2.klee-out/test000001.infty.err
// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -output-dir=%t-O3.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 %t-O3.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O3.klee-out/test000001.infty.err

// -O0 is not expected to work because of allocas in is_even and is_odd

int is_odd(int n);

int is_even(int n) {
  if (n != 0)
    n = is_odd(n + 1); // bug: should be minus instead of plus
  else
    n = 1;

  // multiplication added to complicate tail recursion
  return n * -1;
}

int is_odd(int n) {
  if (n != 0)
    n = is_even(n - 1);
  else
    n = 0;

  // multiplication added to complicate tail recursion
  return n * -1;
}

int main(void) {
  // CHECK: infinite loop
  return is_even(42);
}
