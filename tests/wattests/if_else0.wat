(module
  (func (export "main") (param i32) (result i32)
    (if (result i32)
      (i32.eqz (local.get 0))
      (then
        (i32.const 10))
      (else
        (i32.const 20)))
  )
)
