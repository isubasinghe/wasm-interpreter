#include "jit.h"
#include "function_parser.h"
#include "ir.h"

#include <fstream>
#include <stdexcept>
#include <string>

using namespace Xbyak;

namespace {

constexpr size_t kStaticFrameSize = 512;
constexpr size_t kSlotSize = 8;

const std::array<Xbyak::Reg64, 6> kIntegerArgRegs {{
  Xbyak::util::rdi,
  Xbyak::util::rsi,
  Xbyak::util::rdx,
  Xbyak::util::rcx,
  Xbyak::util::r8,
  Xbyak::util::r9,
}};

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

WasmJit::WasmJit(WasmModule *mod): mod(mod) {
  if(mod != nullptr) {
    for(const auto &func: mod->Funcs()) {
      functions.push_back(func);
    }
  }
}

void WasmJit::mark_reg_free(Xbyak::Reg64 free_reg) {
  for(const auto &[_, local]: locals) {
    if(local.vreg.kind == VRegPhys && local.vreg.physreg == free_reg) {
      return;
    }
  }

  for(auto &reg: registers) {
    if(reg.reg == free_reg) {
      reg.state = RegState::Free;
    }
  }
}

void WasmJit::mark_reg_used(Xbyak::Reg64 used_reg) {
  for(auto &reg: registers) {
    if(reg.reg == used_reg) {
      reg.state = RegState::Used;
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

void WasmJit::emit_i32_add() {
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

WasmInstructionRange::const_iterator WasmInstructionRange::begin() const {
  if(instructions == nullptr) {
    throw std::runtime_error("instruction range has no instruction vector");
  }
  return instructions->begin() + static_cast<std::vector<Instruction>::difference_type>(start_pc);
}

WasmInstructionRange::const_iterator WasmInstructionRange::end() const {
  if(instructions == nullptr) {
    throw std::runtime_error("instruction range has no instruction vector");
  }
  return instructions->begin() + static_cast<std::vector<Instruction>::difference_type>(end_pc);
}

size_t WasmInstructionRange::size() const {
  return end_pc - start_pc;
}

bool WasmInstructionRange::empty() const {
  return size() == 0;
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

void WasmJit::emit_function_prologue(uint32_t fn_idx) {
  if(fn_idx >= functions.size()) {
    throw std::runtime_error("function index out of bounds: " + std::to_string(fn_idx));
  }

  locals.clear();
  while(!stack.empty()) {
    stack.pop();
  }
  for(auto &reg: registers) {
    reg.state = RegState::Free;
  }

  auto const name = "func_" + std::to_string(fn_idx);
  Xbyak::Label func_label;
  L(func_label);
  push(rbp);
  mov(rbp, rsp);
  sub(rsp, kStaticFrameSize);

  const FuncDecl &func = functions[fn_idx];
  uint32_t local_index = 0;

  if(func.sig != nullptr) {
    for(const auto param_type: func.sig->params) {
      VReg local_vreg {
        .kind = VRegSpilled,
        .physreg = Xbyak::util::rax,
        .stack_offset = 0,
      };

      if(local_index < kIntegerArgRegs.size()) {
        const auto reg = kIntegerArgRegs[local_index];
        local_vreg = VReg {
          .kind = VRegPhys,
          .physreg = reg,
          .stack_offset = 0,
        };
        mark_reg_used(reg);
      } else {
        local_vreg.stack_offset =
          static_cast<int64_t>(16 + (local_index - kIntegerArgRegs.size()) * kSlotSize);
      }

      locals.insert_or_assign(local_index, VValue {
        .vreg = local_vreg,
        .ty = param_type,
      });
      local_index++;
    }
  }

  size_t frame_slot = 0;
  for(const auto &group: func.pure_locals) {
    for(uint32_t i = 0; i < group.count; i++) {
      const size_t offset = (frame_slot + 1) * kSlotSize;
      if(offset > kStaticFrameSize) {
        throw std::runtime_error("jit static frame exhausted while mapping locals");
      }

      const auto stack_offset = -static_cast<int64_t>(offset);
      locals.insert_or_assign(local_index, VValue {
        .vreg = VReg {
          .kind = VRegSpilled,
          .physreg = Xbyak::util::rax,
          .stack_offset = stack_offset,
        },
        .ty = group.type,
      });
      mov(qword[rbp + stack_offset], 0);

      local_index++;
      frame_slot++;
    }
  }
}

void WasmJit::emit_function_epilogoue(uint32_t fn_idx) {
  mov(rsp, rbp);
  pop(rbp);
  ret();
}


void WasmJit::emit_function(uint32_t fn_idx) {
  if(fn_idx >= functions.size()) {
    throw std::runtime_error("function index out of bounds: " + std::to_string(fn_idx));
  }

  auto instructions = parse_function(&this->functions[fn_idx]);
  preparse(instructions);
  this->emit_function_prologue(fn_idx); 

  for(auto pc=0; pc < instructions.size();) {
    switch(instructions[pc].opcode) {
      case WASM_OP_LOCAL_GET:
        emit_local_get(static_cast<uint32_t>(instructions[pc].imm.unsigned_value));
        break;
      case WASM_OP_I32_ADD:
        emit_i32_add();
        break;
      default:
        throw;
    }
    pc++;
  }
  this->emit_function_epilogoue(fn_idx);
}

void WasmJit::write_binary(const std::filesystem::path &path) const {
  std::ofstream output(path, std::ios::binary);
  if(!output) {
    throw std::runtime_error("failed to open jit binary output: " + path.string());
  }

  const auto *code = getCode<const char *>();
  output.write(code, static_cast<std::streamsize>(getSize()));
  if(!output) {
    throw std::runtime_error("failed to write jit binary output: " + path.string());
  }
}

void WasmJit::emit_local_get() {
  throw std::runtime_error("emit_local_get requires a local index");
}

void WasmJit::emit_local_get(uint32_t local_idx) {
  for(const auto &[idx, local]: locals) {
    if(idx == local_idx) {
      stack.push(local);
      return;
    }
  }

  throw std::runtime_error("local index not mapped: " + std::to_string(local_idx));
}

void WasmJit::emit_local_set() {
}

const VecMap<uint32_t, VValue> &WasmJit::local_mappings() const {
  return locals;
}

const std::map<size_t, WasmControlFrame> &WasmJit::pc_mappings() const {
  return frame.pc_mappings;
}

const WasmControlFrame &WasmJit::control_frame_at(size_t pc) const {
  auto frame_it = frame.pc_mappings.find(pc);
  if(frame_it == frame.pc_mappings.end()) {
    throw std::runtime_error("control frame missing at pc " + std::to_string(pc));
  }
  return frame_it->second;
}

WasmInstructionRange WasmJit::instruction_range(
  const std::vector<Instruction> &instructions,
  size_t start_pc,
  size_t end_pc
) const {
  if(start_pc > end_pc) {
    throw std::runtime_error("instruction range start is after end");
  }
  if(end_pc > instructions.size()) {
    throw std::runtime_error("instruction range end is out of bounds");
  }
  return WasmInstructionRange {
    .instructions = &instructions,
    .start_pc = start_pc,
    .end_pc = end_pc,
  };
}

WasmIfBlockRanges WasmJit::if_block_ranges(
  const std::vector<Instruction> &instructions,
  const WasmControlFrame &control_frame
) const {
  if(control_frame.kind != WasmControlFrameKind::If) {
    throw std::runtime_error("if block ranges require an if control frame");
  }

  const size_t then_end_pc =
    control_frame.else_pc != 0 ? control_frame.else_pc : control_frame.end_pc;
  const size_t else_start_pc =
    control_frame.else_pc != 0 ? control_frame.else_pc + 1 : control_frame.end_pc;

  return WasmIfBlockRanges {
    .then_branch = instruction_range(instructions, control_frame.start_pc + 1, then_end_pc),
    .else_branch = instruction_range(instructions, else_start_pc, control_frame.end_pc),
  };
}

WasmBlockRanges WasmJit::block_ranges(
  const std::vector<Instruction> &instructions,
  const WasmControlFrame &control_frame
) const {
  if(control_frame.kind != WasmControlFrameKind::Block) {
    throw std::runtime_error("block ranges require a block control frame");
  }

  return WasmBlockRanges {
    .body = instruction_range(instructions, control_frame.start_pc + 1, control_frame.end_pc + 1),
  };
}

WasmLoopBlockRanges WasmJit::loop_block_ranges(
  const std::vector<Instruction> &instructions,
  const WasmControlFrame &control_frame
) const {
  if(control_frame.kind != WasmControlFrameKind::Loop) {
    throw std::runtime_error("loop block ranges require a loop control frame");
  }

  return WasmLoopBlockRanges {
    .body = instruction_range(instructions, control_frame.start_pc + 1, control_frame.end_pc + 1),
  };
}
