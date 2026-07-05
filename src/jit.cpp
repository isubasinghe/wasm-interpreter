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

const WasmControlFrame &control_frame_at_depth(
  const std::vector<WasmControlFrame> &control_frames,
  uint64_t depth
) {
  if(depth >= control_frames.size()) {
    throw std::runtime_error("branch label depth exceeds active control frames");
  }

  return control_frames[control_frames.size() - 1 - static_cast<size_t>(depth)];
}

void apply_simple_stack_effect(const Instruction &instr, size_t &stack_height) {
  switch(instr.opcode) {
    case WASM_OP_I32_CONST:
    case WASM_OP_I64_CONST:
    case WASM_OP_F32_CONST:
    case WASM_OP_F64_CONST:
    case WASM_OP_LOCAL_GET:
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




void WasmJit::preparse(std::vector<Instruction> instructions) {
  frame.pc_mappings.clear();
  frame.control_frames.clear();

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

        frame.control_frames.push_back(control_frame);
        break;
      }
      case WASM_OP_ELSE: {
        if(frame.control_frames.empty()) {
          throw std::runtime_error("else without active control frame");
        }
        auto &control_frame = frame.control_frames.back();
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

        auto control_frame = frame.control_frames.back();
        frame.control_frames.pop_back();
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

void WasmJit::emit_until(const std::function<bool(const Instruction &instr)> until) {
  bool cont = true;
  while(cont) {
    if(this->pc < this->instruction_size) {
      if(until(this->instructions[this->pc])) {
        return;
      }
      cont = emit_instr();
    }else {
      cont = false;
    }
  }
}

void WasmJit::emit_block() {
  const auto &control_frame = control_frame_at(this->pc);
  this->frame.control_frames.push_back(control_frame);
  this->pc++;

  emit_until([this, end_pc = control_frame.end_pc](const Instruction &){
      return this->pc == end_pc;
  });

  L(control_frame.end_label);
  this->frame.control_frames.pop_back();
}

void WasmJit::emit_if_block() {
  throw std::runtime_error("emit if block is not implemented");
}

void WasmJit::emit_br_if() {
  Instruction &instr = this->instructions[this->pc];
  if(instr.imm.kind != IMM_LABEL) {
    throw std::runtime_error("br_if immediate must be a label depth");
  }

  const auto &target_frame = control_frame_at_depth(this->frame.control_frames, instr.imm.unsigned_value);
  auto condition = consume_i32_reg();

  if(condition.vreg.kind == VRegKind::VRegPhys) {
    test(condition.vreg.physreg.cvt32(), condition.vreg.physreg.cvt32());
    mark_reg_free(condition.vreg.physreg);
  } else {
    cmp(dword[rbp + condition.vreg.stack_offset], 0);
  }
  jnz(target_frame.branch_label);
}

bool WasmJit::emit_instr() {
  bool reached_end = this->pc >= this->instruction_size;
  if(reached_end) {
    return false;
  }

  Instruction &instr = this->instructions[this->pc];
  switch(instr.opcode) {
    case WASM_OP_LOCAL_GET:
      emit_local_get(instr.imm.unsigned_value);
      break;
    case WASM_OP_I32_CONST:
      emit_i32_const(instr.imm.signed_value);
      break;
    case WASM_OP_I32_ADD:
      emit_i32_add();
      break;
    case WASM_OP_BLOCK:
      emit_block();
      break;
    case WASM_OP_BR_IF:
      emit_br_if();
      break;
    default:
      throw std::runtime_error("unsupported jit opcode");
  }
  this->pc++;
  return true;
}

void WasmJit::emit_function(uint32_t fn_idx) {
  if(fn_idx >= functions.size()) {
    throw std::runtime_error("function index out of bounds: " + std::to_string(fn_idx));
  }

  auto instructions = parse_function(&this->functions[fn_idx]);
  preparse(instructions);
  this->emit_function_prologue(fn_idx);
  this->pc = 0;
  this->instruction_size = instructions.size();
  this->instructions = instructions;

  emit_until([this, end_pc = this->instruction_size](const Instruction &instr) { 
      return this->pc == end_pc;
  });

  const auto *sig = functions[fn_idx].sig;
  if(sig != nullptr && !sig->results.empty()) {
    if(sig->results.size() != 1 || sig->results.front() != WASM_TYPE_I32) {
      throw std::runtime_error("jit function results are only supported for single i32 values");
    }
    auto result = consume_i32_reg();
    if(result.vreg.kind != VRegKind::VRegPhys) {
      throw std::runtime_error("return value must be in a register");
    }
    mov(eax, result.vreg.physreg.cvt32());
    mark_reg_free(result.vreg.physreg);
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
