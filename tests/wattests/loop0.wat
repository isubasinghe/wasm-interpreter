(module
  (func (export "main") (result i32)
    (local $i i32)
    (local $sum i32)

    (local.set $i (i32.const 5))
    (local.set $sum (i32.const 0))

    (block $done
      (loop $loop
        (br_if $done (i32.eqz (local.get $i)))
        (local.set $sum
          (i32.add (local.get $sum) (local.get $i)))
        (local.set $i
          (i32.sub (local.get $i) (i32.const 1)))
        (br $loop)))

    (local.get $sum)
  )
)
