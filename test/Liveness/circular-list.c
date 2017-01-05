// RUN: %llvmgcc %s -emit-llvm -O0 -g -c -o %t-O0.bc
// RUN: %llvmgcc %s -emit-llvm -O1 -g -c -o %t-O1.bc
// RUN: %llvmgcc %s -emit-llvm -O2 -g -c -o %t-O2.bc
// RUN: %llvmgcc %s -emit-llvm -O3 -g -c -o %t-O3.bc

// RUN: rm -rf %t-O0.klee-out
// RUN: %klee -output-dir=%t-O0.klee-out %t-O0.bc 2>&1 | FileCheck %s
// RUN: rm -rf %t-O1.klee-out
// RUN: %klee -output-dir=%t-O1.klee-out %t-O1.bc 2>&1 | FileCheck %s
// RUN: rm -rf %t-O2.klee-out
// RUN: %klee -output-dir=%t-O2.klee-out %t-O2.bc 2>&1 | FileCheck %s
// RUN: rm -rf %t-O3.klee-out
// RUN: %klee -output-dir=%t-O3.klee-out %t-O3.bc 2>&1 | FileCheck %s

#include <klee/klee.h>

struct node {
  struct node *next;
  int entry;
};

int main(void) {
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

  // CHECK: infinite loop
  while (head)
    head = head->next;

  return 0;
}
