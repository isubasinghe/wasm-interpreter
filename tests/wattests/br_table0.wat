(module
  ;; Expected results:
  ;;   main(0) -> 10
  ;;   main(1) -> 20
  ;;   main(2) -> 30
  ;;   main(anything else) -> 99
  ;;
  ;; This is equivalent to:
  ;;   switch (x) {
  ;;     case 0: return 10;
  ;;     case 1: return 20;
  ;;     case 2: return 30;
  ;;     default: return 99;
  ;;   }
  (func (export "main") (param $x i32) (result i32)
    (block $default
      (block $case2
        (block $case1
          (block $case0
            (local.get $x)
            (br_table $case0 $case1 $case2 $default))

          ;; case 0
          (i32.const 10)
          (return))

        ;; case 1
        (i32.const 20)
        (return))

      ;; case 2
      (i32.const 30)
      (return))

    ;; default
    (i32.const 99))
)
