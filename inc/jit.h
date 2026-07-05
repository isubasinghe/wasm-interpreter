#pragma once

#include "ir.h"
#include "wasmdefs.h"
#include "vecmap.h"
#include "xbyak/xbyak.h"

#include <array>
#include <cstdint>
#include <filesystem>
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
  std::vector<WasmControlFrame> control_frames;
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
    VecMap<uint32_t, VValue> locals;
    std::vector<FuncDecl> functions;
    std::stack<VValue> stack;
    WasmStackFrame frame;
    WasmModule *mod = nullptr;
    const SigDecl *current_sig = nullptr;
    std::string current_epilogue_label;

    void mark_reg_free(Xbyak::Reg64 free_reg);
    void mark_reg_used(Xbyak::Reg64 used_reg);
    void emit_return_values(const SigDecl *sig);
    size_t pc = 0;
    size_t instruction_size = 0;
    std::vector<Instruction> instructions;

  public:
    WasmJit() = default;
    explicit WasmJit(WasmModule *mod);

    VValue consume_i32_reg();
    VReg produce_i32_reg();
    void emit_i32_const(int32_t value);
    void emit_i32_add();
    void emit_if_block();
    void emit_loop_block();
    void emit_br_if();
    void emit_br();
    void emit_br_table();
    void emit_return_i32();
    void emit_return();
    void emit_local_set();
    void emit_local_get();
    void emit_local_get(uint32_t local_idx);
    bool emit_instr();
    void emit_block();
    void emit_until(const std::function<bool(const Instruction &instr)> fn);
    void emit_function_prologue(uint32_t fn_idx);
    void emit_function_epilogoue(uint32_t fn_idx);
    void emit_function(uint32_t fn_idx);
    void write_binary(const std::filesystem::path &path) const;
    void preparse(std::vector<Instruction> instructions);
    const VecMap<uint32_t, VValue> &local_mappings() const;
    const std::map<size_t, WasmControlFrame> &pc_mappings() const;
    const WasmControlFrame &control_frame_at(size_t pc) const;
};
