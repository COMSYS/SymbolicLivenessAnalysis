// RUN: %clang %s -emit-llvm -O0 -g -c -o %t.bc

// RUN: rm -rf %t.klee-out
// RUN: %klee -exit-on-error -libc=uclibc -posix-runtime -output-dir=%t.klee-out -detect-infinite-loops -allocate-determ %t.bc 2>&1 | FileCheck %s

void klee_warning(const char *);

int foo() {
  // CHECK: KLEE: WARNING: foo: nothing to see here
  klee_warning("nothing to see here");
  return 1;
}

int main() {
  // CHECK-NOT: KLEE: ERROR: {{[^:]*}}/nothing.c:{{[0-9]+}}: infinite loop{{$}}

  foo();
  return 0;
}
