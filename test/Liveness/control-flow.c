// RUN: %clang %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %clang %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %clang %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %clang %s -emit-llvm -O3 -g -c -o %t-O3.bc

// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O0.klee-out -detect-infinite-loops -emit-all-errors -max-instructions=100000 -allocate-determ %t-O0.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O0.klee-out/test000001.infty.err
// RUN: test -f %t-O0.klee-out/test000002.infty.err
// RUN: test -f %t-O0.klee-out/test000003.infty.err
// RUN: test -f %t-O0.klee-out/test000004.infty.err
// RUN: test -f %t-O0.klee-out/test000005.infty.err

// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O1.klee-out -detect-infinite-loops -emit-all-errors -max-instructions=100000 -allocate-determ %t-O1.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O1.klee-out/test000001.infty.err
// RUN: test -f %t-O1.klee-out/test000002.infty.err
// RUN: test -f %t-O1.klee-out/test000003.infty.err
// RUN: test -f %t-O1.klee-out/test000004.infty.err
// RUN: test -f %t-O1.klee-out/test000005.infty.err

// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O2.klee-out -detect-infinite-loops -emit-all-errors -max-instructions=100000 -allocate-determ %t-O2.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O2.klee-out/test000001.infty.err
// RUN: test -f %t-O2.klee-out/test000002.infty.err
// RUN: test -f %t-O2.klee-out/test000003.infty.err
// RUN: test -f %t-O2.klee-out/test000004.infty.err
// RUN: test -f %t-O2.klee-out/test000005.infty.err

// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O3.klee-out -detect-infinite-loops -emit-all-errors -max-instructions=100000 -allocate-determ %t-O3.bc 2>&1 | FileCheck %s
// RUN: test -f %t-O3.klee-out/test000001.infty.err
// RUN: test -f %t-O3.klee-out/test000002.infty.err
// RUN: test -f %t-O3.klee-out/test000003.infty.err
// RUN: test -f %t-O3.klee-out/test000004.infty.err
// RUN: test -f %t-O3.klee-out/test000005.infty.err

#include <klee/klee.h>

// x = 1: simple while true (continue)                  prints "aa..."
// x = 2: goto loop                                     prints "xbb..."
// x = 3: simple while true                             prints "ccc..."
// x = 4: leave while loop, then same as x = 1          prints "dxxdaa..."
// x = 5: leave while loop, then same as x = 2          prints "dxxeee..."

// CHECK: KLEE: ERROR: {{[^:]*}}/control-flow.c:{{[0-9]+}}: infinite loop{{$}}

int main(int argc, char *argv[]) {
  int x;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x > 0);

  int y = 1;
  char c = 'x';

  while (y) {
    switch (x) {
      case 1:
        printf("a");
        continue;
      case 2:
        goto label;
      case 3:
        c = 'c';
        break;
      default:
        printf("d");
        y = 0;
        break;
    }

    printf("%c", c);

label:
    printf("%c", c);

    if (x == 2) {
      if (c == 'x') {
        c = 'b';
      }
      goto label;
    }
  }

  if (x == 4) {
    x = 1;
    y = 1;
    c = 'd';
    goto label;
  } else if (x == 5) {
    x = 2;
    c = 'e';
    goto label;
  }

  // do not output any test cases for terminating runs
  klee_silent_exit(0);
}
