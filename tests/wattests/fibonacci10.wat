(module
  (func (export "main") (result i32)
    (local $n i32)
    (local $a i32)
    (local $b i32)
    (local $tmp i32)

    (local.set $n (i32.const 10))
    (local.set $a (i32.const 0))
    (local.set $b (i32.const 1))

    (block $done
      (loop $loop
        (br_if $done (i32.eqz (local.get $n)))

        (local.set $tmp (local.get $b))
        (local.set $b
          (i32.add (local.get $a) (local.get $b)))
        (local.set $a (local.get $tmp))
        (local.set $n
          (i32.sub (local.get $n) (i32.const 1)))

        (br $loop)))

    (local.get $a)
  )
)
