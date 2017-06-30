; RUN: llvm-as %s -f -o %t.bc

; RUN: rm -rf %t.klee-out
; RUN: %klee -libc=uclibc -posix-runtime -output-dir=%t.klee-out -detect-infinite-loops -stop-after-n-instructions=10000 -allocate-determ -disable-opt %t.bc > %t.log 2>&1
; RUN: cat %t.log | FileCheck %s
; RUN: not test -f %t.klee-out/test000001.infty.err

; int main(int argc, char *argv[]) {
;   for (int i = 0; i < 2; i++)
;     for (int j = 0; j < 2; j++);
;   return 0;
; }

; CHECK-NOT: KLEE: ERROR: {{\(?[^:\)]*[\):]?}} infinite loop{{$}}

; regression test: this previously triggered an error at the start of the second
; iteration of the inner loop (see "false infinite loop"). At this position,
; %inc is assumed to be live although the basic block is entered from %for.cond
; instead of %for.inc

define internal i32 @main(i32 %argc, i8** %argv) {
entry:
  br label %for.cond

for.cond:                                         ; preds = %for.inc4, %entry
  %i.0 = phi i32 [ 0, %entry ], [ %inc5, %for.inc4 ]
  %cmp = icmp slt i32 %i.0, 2
  br i1 %cmp, label %for.cond1, label %for.end6

for.cond1:                                        ; preds = %for.inc, %for.cond
  %j.0 = phi i32 [ %inc, %for.inc ], [ 0, %for.cond ] ; false infinite loop
  %cmp2 = icmp slt i32 %j.0, 2
  br i1 %cmp2, label %for.inc, label %for.inc4

for.inc:                                          ; preds = %for.cond1
  %inc = add nsw i32 %j.0, 1
  br label %for.cond1

for.inc4:                                         ; preds = %for.cond1
  %inc5 = add nsw i32 %i.0, 1
  br label %for.cond

for.end6:                                         ; preds = %for.cond
  ret i32 0
}
