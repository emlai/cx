
%"A<int>" = type { i32* }

define i32 @main() {
  %a = alloca %"A<int>"
  call void @_ENM26imported_generic_type_init1AI3intE4initE(%"A<int>"* %a)
  ret i32 0
}

define void @_ENM26imported_generic_type_init1AI3intE4initE(%"A<int>"* %this) {
  %a = getelementptr inbounds %"A<int>", %"A<int>"* %this, i32 0, i32 0
  store i32* null, i32** %a
  ret void
}
