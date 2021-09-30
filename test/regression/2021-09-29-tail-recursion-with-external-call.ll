; RUN: klee -detect-infinite-loops -exit-on-error %s 2>&1 | FileCheck %s
;
; Derived from the following program:
; ---
; #include <stddef.h>
;
; size_t __ctype_get_mb_cur_max(void);
;
; int gcd(int x, int y) {
;   if (y == 0) {
;     return x;
;   } else {
;     __ctype_get_mb_cur_max(); // external call
;     return gcd(y, x % y);
;   }
; }
;
; int main(void) {
;   return gcd(2, 4);
; }
; ---
; This program does not have an infinite loop, but is detected to contain one.
; The problem is that after the external call leads to clearing of the trace, there is no stack frame on the trace.
; This leads to a NOP when returning from the recursive call to gcd (into another instance of gcd).
; Thus, fingerprints from the higher stack frame now appear to be of the current stack frame.
; The basic block that finally returns from gcd will have the same fingerprint as both return the same number.

define i32 @gcd(i32 %x, i32 %y) {
entry:
  %cmp = icmp eq i32 %y, 0
  br i1 %cmp, label %return, label %if.else

if.else:                                          ; preds = %entry
  %call = call i64 @__ctype_get_mb_cur_max()
  %rem = srem i32 %x, %y
  %call1 = call i32 @gcd(i32 %y, i32 %rem)
  br label %return

return:                                           ; preds = %entry, %if.else
  %retval.0 = phi i32 [ %call1, %if.else ], [ %x, %entry ]
; CHECK-NOT: ERROR: {{.*}} infinite loop
  ret i32 %retval.0
}

declare i64 @__ctype_get_mb_cur_max()

define i32 @main() {
entry:
  %call = call i32 @gcd(i32 2, i32 4)
  ret i32 %call
}
