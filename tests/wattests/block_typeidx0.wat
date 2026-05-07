(module
  (type $block_i32_to_i32 (func (param i32) (result i32)))
  (func (export "main") (result i32)
    (i32.const 42)
    (block (type $block_i32_to_i32)
    )
  )
)
