
@0 = private unnamed_addr constant [35 x i8] c"Assertion failed at assert.cx:6:5\0A\00", align 1

declare i1 @b()

define i32 @main() {
  %1 = call i1 @b()
  %assert.condition = icmp eq i1 %1, false
  br i1 %assert.condition, label %assert.fail, label %assert.success

assert.fail:                                      ; preds = %0
  call void @_EN3std10assertFailEP4char(ptr @0)
  unreachable

assert.success:                                   ; preds = %0
  ret i32 0
}

declare void @_EN3std10assertFailEP4char(ptr)
