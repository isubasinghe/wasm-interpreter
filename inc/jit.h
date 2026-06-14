#pragma once

#include "ir.h"
#include "wasmdefs.h"
#include "xbyak/xbyak.h"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <stack>
#include <string>
#include <vector>

enum VRegKind {
  VRegSpilled,
  VRegPhys,
};

enum class RegState {
  Free,
  Used
};

struct PhysicalReg {
  Xbyak::Reg64 reg;
  RegState state;
};

struct VReg {
  VRegKind kind;
  Xbyak::Reg64 physreg;
  int64_t stack_offset;
};

struct VValue {
  VReg vreg;
  wasm_type_t ty;
};

enum class WasmControlFrameKind {
  Block,
  Loop,
  If
};

struct WasmControlFrame {
  WasmControlFrameKind kind;
  size_t label_index = 0;
  size_t start_pc = 0;
  size_t end_pc = 0;
  size_t else_pc = 0;
  size_t stack_height = 0;
  std::string label;
  std::string then_label;
  std::string else_label;
  std::string end_label;
  std::string branch_label;
  SigDecl sig;
};

struct WasmStackFrame {
  std::map<size_t, WasmControlFrame> pc_mappings;
  std::stack<WasmControlFrame> control_frames;
};

struct WasmJit: Xbyak::CodeGenerator {
  private:
    // r11 is reserved as a scratch register
    std::array<PhysicalReg, 8> registers {{
      {Xbyak::util::rax, RegState::Free},
      {Xbyak::util::rcx, RegState::Free},
      {Xbyak::util::rdx, RegState::Free},
      {Xbyak::util::rsi, RegState::Free},
      {Xbyak::util::rdi, RegState::Free},
      {Xbyak::util::r8, RegState::Free},
      {Xbyak::util::r9, RegState::Free},
      {Xbyak::util::r10, RegState::Free},
    }};
    std::stack<VValue> stack;
    WasmStackFrame frame;
    WasmModule *mod = nullptr;

    void mark_reg_free(Xbyak::Reg64 free_reg);

  public:
    WasmJit() = default;
    explicit WasmJit(WasmModule *mod);

    VValue consume_i32_reg();
    VReg produce_i32_reg();
    void emit_i32_const(int32_t value);
    void emit_add_op();
    void emit_if_block(
      const std::function<void()> &emit_then_branch,
      const std::function<void()> &emit_else_branch
    );
    void emit_loop_block(
      const std::function<void()> &emit_loop_body
    );

    void emit_br_if(
      const std::function<void()> &emit_done
    );
    void emit_br();
    void emit_br_table();
    void emit_return_i32();
    void preparse(std::vector<Instruction> instructions);
    const std::map<size_t, WasmControlFrame> &pc_mappings() const;
};
