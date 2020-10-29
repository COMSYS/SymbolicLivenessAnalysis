// RUN: %clang %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %clang %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %clang %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %clang %s -emit-llvm -O3 -g -c -o %t-O3.bc

// TODO: check if difference between line 65 and 68 is significant or due to debug info

// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O0.klee-out -detect-infinite-loops -allocate-determ %t-O0.bc > %t-O0.log 2>&1
// RUN: test -f %t-O0.klee-out/test000001.infty.err
// RUN: cat %t-O0.log | FileCheck %s

// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O1.klee-out -detect-infinite-loops -allocate-determ %t-O1.bc > %t-O1.log 2>&1
// RUN: test -f %t-O0.klee-out/test000001.infty.err
// RUN: cat %t-O1.log | FileCheck %s

// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O2.klee-out -detect-infinite-loops -allocate-determ %t-O2.bc > %t-O2.log 2>&1
// RUN: test -f %t-O0.klee-out/test000001.infty.err
// RUN: cat %t-O2.log | FileCheck %s

// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t-O3.klee-out -detect-infinite-loops -allocate-determ %t-O3.bc > %t-O3.log 2>&1
// RUN: test -f %t-O0.klee-out/test000001.infty.err
// RUN: cat %t-O3.log | FileCheck %s

#include <stdio.h>
#include <stdlib.h>

#include <klee/klee.h>

struct node {
  struct node *next;
  int entry;
};

int main(int argc, char *argv[]) {
  unsigned x;
  klee_make_symbolic(&x, sizeof(x), "x");
  klee_assume(x < 10); // 10 paths, 9 of which are infinite

  struct node *prev = 0;
  struct node *head = 0;

  // create x new nodes
  for (unsigned i = 0; i < x; i++) {
    struct node *element = (struct node *) malloc(sizeof(struct node));

    // set some entry (in this case, its value is symbolic)
    element->entry = x - i;

    if (prev) // predecessor exists, create link
      prev->next = element;
    else // first element, use as head
      head = element;

    prev = element;
  }

  if (prev) // at least one node exists, create circular list
    prev->next = head;

  // CHECK: KLEE: ERROR: {{[^:]*}}/circular-list.c:{{[0-9]+}}: infinite loop{{$}}
  while (head) {
    printf("LIST ENTRY [%d]: %d\n", x, head->entry);
    head = head->next;
  }

  // CHECK: KLEE: done: completed paths = 10
  // CHECK: KLEE: done: generated tests = 1
  klee_silent_exit(0);
  return 0;
}
