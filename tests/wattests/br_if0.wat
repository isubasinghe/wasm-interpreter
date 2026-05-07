(module
  (func (export "main") (param i32) (result i32)
    (block $done
      (br_if $done (local.get 0))
      (return (i32.const 7)))
    (i32.const 42)
  )
)
