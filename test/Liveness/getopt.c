// RUN: %clang %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %clang %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %clang %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %clang %s -emit-llvm -O3 -g -c -o %t-O3.bc

// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O0.klee-out -detect-infinite-loops -max-instructions=10000 -allocate-determ %t-O0.bc -a -a > %t-O0.log 2>&1
// RUN: not test -f %t-O0.klee-out/test000001.infty.err
// RUN: cat %t-O0.log | FileCheck %s

// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O1.klee-out -detect-infinite-loops -max-instructions=10000 -allocate-determ %t-O1.bc -a -a > %t-O1.log 2>&1
// RUN: not test -f %t-O1.klee-out/test000001.infty.err
// RUN: cat %t-O1.log | FileCheck %s

// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O2.klee-out -detect-infinite-loops -max-instructions=10000 -allocate-determ %t-O2.bc -a -a > %t-O2.log 2>&1
// RUN: not test -f %t-O2.klee-out/test000001.infty.err
// RUN: cat %t-O2.log | FileCheck %s

// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O3.klee-out -detect-infinite-loops -max-instructions=10000 -allocate-determ %t-O3.bc -a -a > %t-O3.log 2>&1
// RUN: not test -f %t-O3.klee-out/test000001.infty.err
// RUN: cat %t-O3.log | FileCheck %s

// regression test: this triggered an infinite loop with -a -a when registering
// basic blocks on return from a function (in this case getopt_long)


#include <getopt.h>
#include <stdio.h>

static struct option const longopts[] = {
  {"long", no_argument, NULL, 'a'},
  {"longer", no_argument, NULL, 'b'},
  {NULL, 0, NULL, 0}
};

int main(int argc, char *argv[]) {
  while (1) {
    // CHECK-NOT: KLEE: ERROR: {{[^:]*}}/getopt.c:{{[0-9]+}}: infinite loop{{$}}
    int c = getopt_long(argc, argv, "+abc", longopts, NULL);
    printf("%d\n", c);
    if (c == -1) {
      break;
    }

    switch (c) {
      case 'a':
        printf("a\n");
        break;

      case 'b':
        printf("b\n");
        break;

      case 'c':
        printf("c\n");
        break;
    }
  }
}
