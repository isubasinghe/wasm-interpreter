(module
  (func (export "main") (param i32) (result i32)
    (if (result i32)
      (i32.eqz (local.get 0))
      (then
        (i32.const 10))
      (else
        (if (result i32)
          (i32.eq
            (local.get 0)
            (i32.const 1))
          (then
            (i32.const 20))
          (else
            (i32.const 30)))))
  )
)
