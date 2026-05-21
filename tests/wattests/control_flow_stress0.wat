(module
  ;; Expected final result: 176.
  ;;
  ;; This intentionally avoids br_table because the current interpreter parses
  ;; but does not execute it yet.
  (type $i32_to_i32 (func (param i32) (result i32)))

  (func (export "main") (result i32)
    (local $i i32)
    (local $sum i32)
    (local $tmp i32)

    (local.set $sum (i32.const 0))

    ;; br_if not-taken in a no-result block.
    (block $not_taken
      (br_if $not_taken (i32.const 0))
      (local.set $sum (i32.add (local.get $sum) (i32.const 0))))

    ;; Block result, with br_if taken to the block end.
    (local.set $tmp
      (block $pick (result i32)
        (br_if $pick (i32.const 7) (i32.const 1))
        (local.set $tmp)
        (i32.const 900)))
    (local.set $sum (i32.add (local.get $sum) (local.get $tmp)))

    ;; True arm should skip else.
    (local.set $tmp
      (if (result i32)
        (i32.const 1)
        (then (i32.const 11))
        (else (i32.const 901))))
    (local.set $sum (i32.add (local.get $sum) (local.get $tmp)))

    ;; False arm should jump to else, and nested false arm should do the same.
    (local.set $tmp
      (if (result i32)
        (i32.const 0)
        (then (i32.const 902))
        (else
          (if (result i32)
            (i32.const 0)
            (then (i32.const 903))
            (else (i32.const 13))))))
    (local.set $sum (i32.add (local.get $sum) (local.get $tmp)))

    ;; Loop continue to label 0, not-taken br_if inside the loop, and break to
    ;; the surrounding block. Only the i == 2 path should add 100.
    (local.set $i (i32.const 5))
    (block $done
      (loop $loop
        (br_if $done (i32.eqz (local.get $i)))
        (local.set $i (i32.sub (local.get $i) (i32.const 1)))
        (br_if $loop
          (i32.eqz
            (i32.eq (local.get $i) (i32.const 2))))
        (local.set $sum (i32.add (local.get $sum) (i32.const 100)))
        (br $loop)))

    ;; Branch out through multiple active frames from inside an if. The branch
    ;; value must survive unwinding the if and middle block to become the outer
    ;; block result.
    (local.set $tmp
      (block $outer (result i32)
        (block $middle
          (if
            (i32.const 1)
            (then
              (i32.const 23)
              (br $outer)
              (i32.const 904)
              (local.set $sum))
            (else
              (i32.const 905)
              (local.set $sum))))
        (i32.const 906)))
    (local.set $sum (i32.add (local.get $sum) (local.get $tmp)))

    ;; The else marker must jump over the else body after a true then branch.
    (if
      (i32.const 1)
      (then
        (local.set $sum (i32.add (local.get $sum) (i32.const 5))))
      (else
        (i32.const 907)
        (return)))

    ;; An if-without-else false condition should skip directly to end.
    (if
      (i32.const 0)
      (then
        (i32.const 908)
        (return)))

    ;; Type-indexed block with one parameter/result. A branch to the block end
    ;; must preserve the parameter as the result.
    (local.set $tmp
      (i32.const 17)
      (block (type $i32_to_i32)
        (br_if 0 (i32.const 1))
        (local.set $tmp)
        (i32.const 909)))
    (local.set $sum (i32.add (local.get $sum) (local.get $tmp)))

    ;; Return from inside a block after a not-taken br_if. Code after return
    ;; should never run.
    (block $ret
      (br_if $ret (i32.const 0))
      (local.get $sum)
      (return)
      (i32.const 910)
      (local.set $sum))

    (i32.const -1))
)
