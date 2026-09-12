#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "jit.h"

#include <doctest/doctest.h>

namespace {

void add_function(WasmModule &mod, bytearr code, uint32_t pure_locals = 0,
                  uint32_t params = 1) {
  SigDecl sig;
  sig.params.assign(params, WASM_TYPE_I32);
  sig.results = {WASM_TYPE_I32};
  mod.Sigs().push_back(sig);
  FuncDecl func {};
  func.sig = &mod.Sigs().back();
  func.num_pure_locals = pure_locals;
  if(pure_locals != 0) {
    func.pure_locals.push_back({.count = pure_locals, .type = WASM_TYPE_I32});
  }
  func.code_bytes = std::move(code);
  mod.Funcs().push_back(func);
}

VValue reg_value(Xbyak::Reg64 reg, wasm_type_t type) {
  return {.vreg = {.kind = VRegPhys, .physreg = reg, .stack_offset = 0},
          .ty = type};
}

VValue slot_value(int64_t offset, wasm_type_t type) {
  return {.vreg = {.kind = VRegSpilled, .physreg = Xbyak::util::rax,
                  .stack_offset = offset}, .ty = type};
}

} // namespace

TEST_CASE("local.get preserves the value across local.set") {
  for(bool frame_local: {false, true}) {
    CAPTURE(frame_local);
    WasmModule mod;
    bytearr code;
    const byte index = frame_local ? 1 : 0;
    if(frame_local) {
      // Initialize the frame local from the argument.
      code = {0x20, 0x00, 0x21, index};
    }
    // local.get index; i32.const 7; local.set index; local.get index; i32.add; end
    code.insert(code.end(), {0x20, index, 0x41, 0x07, 0x21, index,
                             0x20, index, 0x6a, 0x0b});
    add_function(mod, code, frame_local ? 1 : 0);
    WasmJit jit(&mod);
    jit.emit_function(0);
    auto fn = jit.getCode<int32_t (*)(int32_t)>();
    CHECK(fn(3) == 10);
    CHECK(fn(-8) == -1);
  }
}

TEST_CASE("arithmetic on local.get leaves the local unchanged") {
  WasmModule mod;
  // local.get 0; i32.const 1; i32.add; local.get 0; i32.add; end
  add_function(mod, {0x20, 0x00, 0x41, 0x01, 0x6a, 0x20, 0x00, 0x6a, 0x0b});
  WasmJit jit(&mod);
  jit.emit_function(0);
  CHECK(jit.getCode<int32_t (*)(int32_t)>()(3) == 7);
}

TEST_CASE("local.set releases temporaries across repeated assignments") {
  WasmModule mod;
  bytearr code;
  for(byte value = 0; value < 32; value++) {
    code.insert(code.end(), {0x41, value, 0x21, 0x00});
  }
  code.insert(code.end(), {0x20, 0x00, 0x0b});
  add_function(mod, code);
  WasmJit jit(&mod);
  jit.emit_function(0);
  CHECK(jit.getCode<int32_t (*)(int32_t)>()(3) == 31);
}

TEST_CASE("local.get copies stack-passed arguments") {
  WasmModule mod;
  // local.get 6; i32.const 7; local.set 6; end
  add_function(mod, {0x20, 0x06, 0x41, 0x07, 0x21, 0x06, 0x0b}, 0, 7);
  WasmJit jit(&mod);
  jit.emit_function(0);
  auto fn = jit.getCode<int32_t (*)(int32_t, int32_t, int32_t, int32_t,
                                   int32_t, int32_t, int32_t)>();
  CHECK(fn(0, 0, 0, 0, 0, 0, 3) == 3);
}

TEST_CASE("emit_move copies both integer widths between all storage kinds") {
  for(auto type: {WASM_TYPE_I32, WASM_TYPE_I64}) {
    for(bool source_in_memory: {false, true}) {
      for(bool destination_in_memory: {false, true}) {
        CAPTURE(type);
        CAPTURE(source_in_memory);
        CAPTURE(destination_in_memory);
        WasmJit jit;
        jit.push(jit.rbp);
        jit.mov(jit.rbp, jit.rsp);
        jit.sub(jit.rsp, 16);
        jit.mov(jit.qword[jit.rbp - 8], jit.rdi);
        jit.mov(jit.qword[jit.rbp - 16], 0);
        auto source = source_in_memory ? slot_value(-8, type)
                                       : reg_value(jit.rdi, type);
        auto destination = destination_in_memory ? slot_value(-16, type)
                                                 : reg_value(jit.rax, type);
        jit.emit_move(destination, source);
        if(destination_in_memory) {
          jit.mov(jit.rax, jit.qword[jit.rbp - 16]);
        }
        jit.mov(jit.rsp, jit.rbp);
        jit.pop(jit.rbp);
        jit.ret();
        auto fn = jit.getCode<uint64_t (*)(uint64_t)>();
        constexpr uint64_t value = 0xfedcba9876543210ULL;
        CHECK(fn(value) == (type == WASM_TYPE_I32 ? uint64_t(uint32_t(value)) : value));
      }
    }
  }
}

TEST_CASE("local.set rejects invalid operands without consuming the stack") {
  WasmModule mod;
  add_function(mod, {});
  WasmJit jit(&mod);
  jit.emit_function_prologue(0);
  CHECK_THROWS_AS(jit.emit_local_set(0), std::runtime_error);
  jit.emit_i32_const(9);
  CHECK_THROWS_AS(jit.emit_local_set(1), std::runtime_error);
  CHECK(jit.consume_i32_reg().ty == WASM_TYPE_I32);
}

TEST_CASE("emit_move rejects mismatched and unsupported types") {
  WasmJit jit;
  CHECK_THROWS_AS(jit.emit_move(reg_value(jit.rax, WASM_TYPE_I32),
                                reg_value(jit.rcx, WASM_TYPE_I64)), std::runtime_error);
  CHECK_THROWS_AS(jit.emit_move(reg_value(jit.rax, WASM_TYPE_F32),
                                reg_value(jit.rcx, WASM_TYPE_F32)), std::runtime_error);
  CHECK(jit.getSize() == 0);
}
