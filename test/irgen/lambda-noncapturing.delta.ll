
define void @_EN4main3fooE1aF3int3int_3int(i32 (i32, i32)* %a) {
  %1 = call i32 %a(i32 1, i32 2)
  ret void
}

define i32 @main() {
  call void @_EN4main3fooE1aF3int3int_3int(i32 (i32, i32)* @_EN4main9__lambda0E1a3int1b3int)
  ret i32 0
}

define i32 @_EN4main9__lambda0E1a3int1b3int(i32 %a, i32 %b) {
  %1 = add i32 %a, %b
  ret i32 %1
}
