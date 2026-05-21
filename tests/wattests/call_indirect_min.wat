(module
  (type $return_i32 (func (result i32)))
  (table 1 funcref)
  (elem (i32.const 0) $answer)

  (func $answer (type $return_i32)
    (i32.const 42))

  (func (export "main") (result i32)
    (i32.const 0)
    (call_indirect (type $return_i32)))
)
