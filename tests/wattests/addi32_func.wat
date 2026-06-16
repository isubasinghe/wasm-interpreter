(module
  (func $addi32 (export "addi32") (param $lhs i32) (param $rhs i32) (result i32)
    (i32.add (local.get $lhs) (local.get $rhs)))

  (func (export "main") (result i32)
    (call $addi32 (i32.const 40) (i32.const 2)))
)
