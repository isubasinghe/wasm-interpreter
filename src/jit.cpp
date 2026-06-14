#include "jit.h"
#include "ir.h"

#include <stdexcept>
#include <string>

using namespace Xbyak;

namespace {

SigDecl block_sig_from_immediate(const Immediate &imm, WasmModule *mod) {
  if(imm.kind != IMM_BLOCKT) {
    throw std::runtime_error("control frame immediate must be a blocktype");
  }

  SigDecl sig;
  switch(imm.blocktype_kind) {
    case WASM_BLOCKTYPE_EMPTY:
      return sig;
    case WASM_BLOCKTYPE_VALTYPE:
      sig.results.push_back(imm.type);
      return sig;
    case WASM_BLOCKTYPE_TYPEIDX:
      if(mod == nullptr) {
        throw std::runtime_error("jit preparse needs module context for typeidx blocktypes");
      }
      if(imm.unsigned_value >= mod->Sigs().size()) {
        throw std::runtime_error("block type index out of bounds: " + std::to_string(imm.unsigned_value));
      }
      return *mod->getSig(imm.unsigned_value);
  }

  throw std::runtime_error("invalid blocktype kind");
}

WasmControlFrameKind control_frame_kind_for_opcode(Opcode_t opcode) {
  switch(opcode) {
    case WASM_OP_BLOCK:
      return WasmControlFrameKind::Block;
    case WASM_OP_LOOP:
      return WasmControlFrameKind::Loop;
    case WASM_OP_IF:
      return WasmControlFrameKind::If;
  }
  throw std::runtime_error("opcode is not a control frame opener");
}

std::string label_prefix_for_kind(WasmControlFrameKind kind) {
  switch(kind) {
    case WasmControlFrameKind::Block:
      return "block";
    case WasmControlFrameKind::Loop:
      return "loop";
    case WasmControlFrameKind::If:
      return "if";
  }
  throw std::runtime_error("unknown control frame kind");
}

void assign_control_labels(WasmControlFrame &frame, size_t label_index) {
  frame.label_index = label_index;

  const auto prefix = label_prefix_for_kind(frame.kind);
  frame.label = prefix + "_" + std::to_string(label_index);

  switch(frame.kind) {
    case WasmControlFrameKind::Block:
      frame.end_label = frame.label;
      frame.branch_label = frame.end_label;
      break;
    case WasmControlFrameKind::Loop:
      frame.end_label = frame.label + "_end";
      frame.branch_label = frame.label;
      break;
    case WasmControlFrameKind::If:
      frame.then_label = frame.label + "_then";
      frame.else_label = frame.label + "_else";
      frame.end_label = frame.label + "_end";
      frame.branch_label = frame.end_label;
      break;
  }
}

size_t checked_sub_height(size_t height, size_t delta, const char *message) {
  if(height < delta) {
    throw std::runtime_error(message);
  }
  return height - delta;
}

void apply_simple_stack_effect(const Instruction &instr, size_t &stack_height) {
  switch(instr.opcode) {
    case WASM_OP_I32_CONST:
    case WASM_OP_I64_CONST:
    case WASM_OP_F32_CONST:
    case WASM_OP_F64_CONST:
      stack_height += 1;
      break;
    case WASM_OP_I32_ADD:
    case WASM_OP_I32_SUB:
    case WASM_OP_I32_EQ:
      stack_height = checked_sub_height(stack_height, 2, "binary op requires two stack values") + 1;
      break;
    case WASM_OP_I32_EQZ:
      stack_height = checked_sub_height(stack_height, 1, "i32.eqz requires one stack value") + 1;
      break;
    case WASM_OP_BR_IF:
      stack_height = checked_sub_height(stack_height, 1, "br_if requires a condition");
      break;
    default:
      break;
  }
}

} // namespace

WasmJit::WasmJit(WasmModule *mod): mod(mod) {}

void WasmJit::mark_reg_free(Xbyak::Reg64 free_reg) {
  for(auto &reg: registers) {
    if(reg.reg == free_reg) {
      reg.state = RegState::Free;
    }
  }
}

VValue WasmJit::consume_i32_reg() {
  if(this->stack.size() <= 0) {
    throw std::runtime_error("missing i32 operand");
  }
  auto val = this->stack.top();
  if(val.ty != WASM_TYPE_I32) {
    throw std::runtime_error("operand is not i32");
  }
  this->stack.pop();
  return val;
}

VReg WasmJit::produce_i32_reg() {
  for(auto &reg: registers) {
    if(reg.state == RegState::Free) {
      reg.state = RegState::Used;
      return VReg {
        .kind = VRegPhys,
        .physreg = reg.reg,
        .stack_offset = 0,
      };
    }
  }
  throw std::runtime_error("out of registers");
}

void WasmJit::emit_i32_const(int32_t value) {
  auto vreg = produce_i32_reg();
  mov(vreg.physreg.cvt32(), value);
  stack.push(VValue {
    .vreg = vreg,
    .ty = WASM_TYPE_I32,
  });
}

void WasmJit::emit_add_op() {
  auto rhs = consume_i32_reg();
  auto lhs = consume_i32_reg();
  if(lhs.vreg.kind != VRegKind::VRegPhys || rhs.vreg.kind != VRegKind::VRegPhys) {
    throw std::runtime_error("i32.add operands must be in registers");
  }
  add(lhs.vreg.physreg.cvt32(), rhs.vreg.physreg.cvt32());
  mark_reg_free(rhs.vreg.physreg);
  stack.push(VValue {
    .vreg = lhs.vreg,
    .ty = WASM_TYPE_I32,
  });
}

void WasmJit::emit_if_block(
  const std::function<void()> &emit_then_branch,
  const std::function<void()> &emit_else_branch
) {
  if(this->stack.size() <= 0) {
    throw std::runtime_error("missing if condition");
  }
  Xbyak::Label lelse;
  Xbyak::Label lend;
  VValue val = this->stack.top();
  this->stack.pop();

  if(val.vreg.kind != VRegKind::VRegPhys) {
    throw std::runtime_error("if condition must be in a register");
  }

  test(val.vreg.physreg.cvt32(), val.vreg.physreg.cvt32());
  mark_reg_free(val.vreg.physreg);
  jz(lelse);
  emit_then_branch();
  jmp(lend);
  L(lelse);
  emit_else_branch();
  L(lend);
}

void WasmJit::emit_loop_block(
     const std::function<void()> &emit_loop_body) {
}

void WasmJit::emit_br_if(
    const std::function<void()> &emit_done
) {

}

void WasmJit::emit_br() {
}

void WasmJit::emit_br_table() {
}

void WasmJit::preparse(std::vector<Instruction> instructions) {
  frame.pc_mappings.clear();
  while(!frame.control_frames.empty()) {
    frame.control_frames.pop();
  }

  size_t stack_height = 0;
  size_t next_label_index = 0;

  for(size_t pc = 0; pc < instructions.size(); pc++) {
    const auto &instr = instructions[pc];
    switch(instr.opcode) {
      case WASM_OP_BLOCK:
      case WASM_OP_LOOP:
      case WASM_OP_IF: {
        if(instr.opcode == WASM_OP_IF) {
          stack_height = checked_sub_height(stack_height, 1, "if requires an i32 condition");
        }

        WasmControlFrame control_frame;
        control_frame.kind = control_frame_kind_for_opcode(instr.opcode);
        assign_control_labels(control_frame, next_label_index++);
        control_frame.start_pc = pc;
        control_frame.sig = block_sig_from_immediate(instr.imm, mod);

        const auto param_count = control_frame.sig.params.size();
        control_frame.stack_height =
          checked_sub_height(stack_height, param_count, "control frame requires more params than stack height");

        frame.control_frames.push(control_frame);
        break;
      }
      case WASM_OP_ELSE: {
        if(frame.control_frames.empty()) {
          throw std::runtime_error("else without active control frame");
        }
        auto &control_frame = frame.control_frames.top();
        if(control_frame.kind != WasmControlFrameKind::If) {
          throw std::runtime_error("else requires active if frame");
        }
        control_frame.else_pc = pc;
        stack_height = control_frame.stack_height + control_frame.sig.params.size();
        break;
      }
      case WASM_OP_END: {
        if(pc == instructions.size() - 1) {
          break;
        }
        if(frame.control_frames.empty()) {
          throw std::runtime_error("end without active control frame");
        }

        auto control_frame = frame.control_frames.top();
        frame.control_frames.pop();
        control_frame.end_pc = pc;
        frame.pc_mappings[control_frame.start_pc] = control_frame;
        stack_height = control_frame.stack_height + control_frame.sig.results.size();
        break;
      }
      default:
        apply_simple_stack_effect(instr, stack_height);
        continue;
    }
  }

  if(!frame.control_frames.empty()) {
    throw std::runtime_error("unterminated control frame");
  }
}

void WasmJit::emit_return_i32() {
  auto result = consume_i32_reg();
  if(result.vreg.kind != VRegKind::VRegPhys) {
    throw std::runtime_error("return value must be in a register");
  }
  mov(eax, result.vreg.physreg.cvt32());
  ret();
}

const std::map<size_t, WasmControlFrame> &WasmJit::pc_mappings() const {
  return frame.pc_mappings;
}
