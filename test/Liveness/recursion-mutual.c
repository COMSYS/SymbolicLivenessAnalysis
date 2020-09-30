// RUN: %clang %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %clang %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %clang %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %clang %s -emit-llvm -O3 -g -c -o %t-O3.bc

// ---  cannot be detected with -O0 (alloca)
// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -output-dir=%t-O0.klee-out -detect-infinite-loops -allocate-determ -max-instructions=1000 %t-O0.bc 2>&1 | not FileCheck %s
// RUN: not test -f %t-O0.klee-out/test000001.infty.err

// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -output-dir=%t-O1.klee-out -detect-infinite-loops -allocate-determ -max-instructions=10000 %t-O1.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O1.klee-out/test000001.infty.err

// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -output-dir=%t-O2.klee-out -detect-infinite-loops -allocate-determ -max-instructions=10000 %t-O2.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O2.klee-out/test000001.infty.err

// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -output-dir=%t-O3.klee-out -detect-infinite-loops -allocate-determ -max-instructions=10000 %t-O3.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O3.klee-out/test000001.infty.err

int is_odd(unsigned int n);

// returns 1 if n is even, 0 otherwise
int is_even(unsigned int n) {
  if (n != 0)
    n = is_odd(n + 1); // BUG: should be minus instead of plus
  else
    n = 0; // recursion ends here, number is even

  // operation added to prevent tail call optimization
  return 1 - n;
}

// returns 1 if n is odd, 0 otherwise
int is_odd(unsigned int n) {
  if (n != 0)
    n = is_even(n - 1);
  else
    n = 0; // recursion ends here, number is odd

  // operation added to prevent tail call optimization
  return 1 - n;
}

int main(void) {
  // CHECK: KLEE: ERROR: {{[^:]*}}/recursion-mutual.c:{{[0-9]+}}: infinite loop{{$}}
  return is_even(42);
}
