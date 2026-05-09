#include <stack>
#include <array>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "errors.h"
#include "interpreter.h"
#include "ir.h"

namespace {

uint8_t read_byte(const bytearr& code, size_t& pos) {
  if (pos >= code.size()) {
    throw WasmTruncatedInputError("truncated bytecode");
  }
  return code[pos++];
}

uint64_t read_uleb(const bytearr& code, size_t& pos) {
  uint64_t result = 0;
  unsigned shift = 0;

  while (true) {
    byte b = read_byte(code, pos);
    result |= uint64_t(b & 0x7f) << shift;

    if ((b & 0x80) == 0) {
      return result;
    }

    shift += 7;
    if (shift >= 64) {
      throw WasmInvalidImmediateError("uleb immediate is too large");
    }
  }
}

int64_t read_sleb(const bytearr& code, size_t& pos) {
  int64_t result = 0;
  unsigned shift = 0;
  byte b = 0;

  do {
    b = read_byte(code, pos);
    result |= int64_t(b & 0x7f) << shift;
    shift += 7;
  } while ((b & 0x80) != 0 && shift < 64);

  if ((b & 0x80) != 0) {
    throw WasmInvalidImmediateError("sleb immediate is too large");
  }

  if (shift < 64 && (b & 0x40)) {
    result |= -(int64_t(1) << shift);
  }

  return result;
}

template <typename T>
T read_raw(const bytearr& code, size_t& pos) {
  if (pos + sizeof(T) > code.size()) {
    throw WasmTruncatedInputError("truncated raw immediate");
  }

  T value;
  std::memcpy(&value, &code[pos], sizeof(T));
  pos += sizeof(T);
  return value;
}

bool is_prefix_byte(byte b) {
  return b == WASM_EXT1_GCREF || b == WASM_EXT1_FC ||
         b == WASM_EXT1_SIMD  || b == WASM_EXT1_THREADS;
}

bool is_valtype_byte(byte b) {
  switch (static_cast<wasm_type_t>(b)) {
    case WASM_TYPE_I32:
    case WASM_TYPE_I64:
    case WASM_TYPE_F32:
    case WASM_TYPE_F64:
    case WASM_TYPE_V128:
    case WASM_TYPE_FUNCREF:
    case WASM_TYPE_EXTERNREF:
      return true;
    case WASM_TYPE_FUNC:
      return false;
  }
  return false;
}

Opcode_t read_opcode(const bytearr& code, size_t& pos) {
  byte b = read_byte(code, pos);
  Opcode_t opcode = b;

  if (is_prefix_byte(b)) {
    uint64_t subopcode = read_uleb(code, pos);
    if (subopcode > 0xff) {
      throw WasmUnsupportedOpcodeError((Opcode_t(b) << 8) | Opcode_t(subopcode));
    }
    opcode = (Opcode_t(b) << 8) | Opcode_t(subopcode);
  }

  if (opcode_table[opcode].mnemonic == nullptr) {
    throw WasmUnknownOpcodeError(opcode);
  }

  return opcode;
}

void parse_blocktype(Immediate& imm, const bytearr& code, size_t& pos) {
  if (pos >= code.size()) {
    throw WasmTruncatedInputError("truncated blocktype");
  }

  byte b = code[pos];
  if (b == 0x40) {
    pos++;
    imm.blocktype_kind = WASM_BLOCKTYPE_EMPTY;
    return;
  }

  if (is_valtype_byte(b)) {
    pos++;
    imm.blocktype_kind = WASM_BLOCKTYPE_VALTYPE;
    imm.type = static_cast<wasm_type_t>(b);
    return;
  }

  int64_t typeidx = read_sleb(code, pos);
  if (typeidx < 0) {
    throw WasmInvalidImmediateError("blocktype type index must be non-negative");
  }
  imm.blocktype_kind = WASM_BLOCKTYPE_TYPEIDX;
  imm.unsigned_value = static_cast<uint64_t>(typeidx);
}

void parse_immediate(Immediate& imm, const bytearr& code, size_t& pos) {
  switch (imm.kind) {
    case IMM_NONE:
      break;
    case IMM_BLOCKT:
      parse_blocktype(imm, code, pos);
      break;
    case IMM_LABEL:
    case IMM_FUNC:
    case IMM_LOCAL:
    case IMM_GLOBAL:
    case IMM_TABLE:
    case IMM_MEMORY:
    case IMM_TAG:
    case IMM_DATA:
      imm.unsigned_value = read_uleb(code, pos);
      break;
    case IMM_LABELS: {
      uint64_t num_labels = read_uleb(code, pos);
      imm.br_table.labels.reserve(num_labels);
      for (uint64_t i = 0; i < num_labels; i++) {
        imm.br_table.labels.push_back(read_uleb(code, pos));
      }
      imm.br_table.default_label = read_uleb(code, pos);
      break;
    }
    case IMM_SIG_TABLE:
      imm.unsigned_value = read_uleb(code, pos);
      imm.unsigned_value2 = read_uleb(code, pos);
      break;
    case IMM_MEMARG:
      imm.memarg.align = read_uleb(code, pos);
      imm.memarg.offset = read_uleb(code, pos);
      break;
    case IMM_I32:
      imm.signed_value = static_cast<int32_t>(read_sleb(code, pos));
      break;
    case IMM_I64:
      imm.signed_value = read_sleb(code, pos);
      break;
    case IMM_F32: {
      uint32_t bits = read_raw<uint32_t>(code, pos);
      std::memcpy(&imm.f32, &bits, sizeof(imm.f32));
      break;
    }
    case IMM_F64: {
      uint64_t bits = read_raw<uint64_t>(code, pos);
      std::memcpy(&imm.f64, &bits, sizeof(imm.f64));
      break;
    }
    case IMM_REFNULLT:
      imm.type = static_cast<wasm_type_t>(read_byte(code, pos));
      break;
    case IMM_VALTS: {
      uint64_t num_types = read_uleb(code, pos);
      imm.types.reserve(num_types);
      for (uint64_t i = 0; i < num_types; i++) {
        imm.types.push_back(static_cast<wasm_type_t>(read_byte(code, pos)));
      }
      break;
    }
    case IMM_DATA_MEMORY:
    case IMM_DATA_TABLE:
    case IMM_MEMORYCP:
    case IMM_TABLECP:
      imm.unsigned_value = read_uleb(code, pos);
      imm.unsigned_value2 = read_uleb(code, pos);
      break;
    case IMM_V128:
      for (byte& b : imm.bytes) {
        b = read_byte(code, pos);
      }
      break;
    case IMM_LANEIDX:
      imm.laneidx = read_byte(code, pos);
      break;
    case IMM_LANEIDX16:
      for (byte& b : imm.bytes) {
        b = read_byte(code, pos);
      }
      break;
    case IMM_MEMARG_LANEIDX:
      imm.memarg.align = read_uleb(code, pos);
      imm.memarg.offset = read_uleb(code, pos);
      imm.laneidx = read_byte(code, pos);
      break;
  }
}

}  // namespace


Interpreter::Interpreter(WasmModule mod) {
  this->mod = mod;
  for(auto const &f: mod.Funcs()) {
    this->push_func(f);
  }
  
  for(auto const &d: mod.Datas()) {
  }

  for(auto const &c: mod.Customs()) {
  }

  for(auto const &s: mod.Sigs()) {
  }

  for(auto const &e: mod.Elems()) {
  }

  for(auto const &g: mod.Globals()) {
  }

  for(auto const &t: mod.Tables()) {
    this->add_table(t);
  }

  for(auto const &d: mod.Datas()) {
  }
}

Interpreter::~Interpreter() {

}

void Interpreter::add_table(TableDecl t) {
  this->tables.push_back(t);
}

void Interpreter::push_func(FuncDecl f) {
  this->functions.push_back(f);
}

std::vector<Instruction> Interpreter::parse_function(const FuncDecl *f) {
  const bytearr& code = f->code_bytes;
  std::vector<Instruction> instructions;

  size_t pos = 0;
  while (pos < code.size()) {
    Instruction inst;
    inst.offset = pos;
    inst.opcode = read_opcode(code, pos);
    inst.imm.kind = opcode_table[inst.opcode].imm_type;
    parse_immediate(inst.imm, code, pos);
    instructions.push_back(inst);
  }

  return instructions;
}

void Interpreter::handle_call(uint32_t fn_idx) {
  if(static_cast<size_t>(fn_idx) >= this->functions.size()) {
    throw WasmInvalidIndexError("function index out of bounds: " + std::to_string(fn_idx));
  }

  FuncDecl &function = this->functions[fn_idx];
  std::vector<Immediate> args = this->handler.enter_function(function.sig);
  this->run_function(&function, args);
}

void Interpreter::handle_indirect_call(Immediate imm) {
  if(imm.kind != IMM_SIG_TABLE) {
    throw WasmInvalidImmediateError("call_indirect immediate must be a signature/table pair");
  }

  if(!this->mapped.contains(imm.unsigned_value)) {
    throw WasmInvalidIndexError("call_indirect type index is unmapped: " +
                                std::to_string(imm.unsigned_value));
  }
  auto map = this->mapped[imm.unsigned_value];
  if(map[imm.unsigned_value2] == false) {
    throw WasmInvalidIndexError("call_indirect table index is unmapped: " +
                                std::to_string(imm.unsigned_value2));
  }
  throw WasmUnsupportedOpcodeError(WASM_OP_CALL_INDIRECT);
}

static Immediate similar_cast(Immediate imm, Immediate other) {
  if(imm.type == other.type) {
    return other;
  }
  std::cerr << imm.type << " != " << other.type << std::endl;
  throw WasmTypeMismatchTrap("operand type mismatch");
}

void Interpreter::handle_local_get(Immediate imm, std::vector<Immediate> args) {
  if(imm.kind != IMM_LOCAL) {
    throw WasmInvalidImmediateError("local.get immediate must be a local index");
  }
  if(imm.unsigned_value >= args.size()) {
    throw WasmInvalidIndexError("local index out of bounds: " +
                                std::to_string(imm.unsigned_value));
  }
  this->stack().push(args[imm.unsigned_value]);
}

void Interpreter::handle_f64_add() {
  if(this->stack().size() < 2) {
    throw WasmStackUnderflowTrap("f64.add requires two operands on the stack");
  }
  Immediate first = this->stack().top();
  this->stack().pop();
  Immediate second = this->stack().top();
  this->stack().pop();
  if(first.type != WASM_TYPE_F64) {
    throw WasmTypeMismatchTrap("f64.add first operand must be f64");
  }
  if(second.type != WASM_TYPE_F64) {
    throw WasmTypeMismatchTrap("f64.add second operand must be f64");
  }
  Immediate imm = Immediate(first.f64 + second.f64);
  this->stack().push(imm);
}

void Interpreter::handle_block(Immediate imm) {
  ControlFrameKind kind;
  switch(imm.blocktype_kind) {
    case WASM_BLOCKTYPE_EMPTY:
      kind = ControlFrameKind::Block;
      break;
    case WASM_BLOCKTYPE_TYPEIDX:
      break;
    case WASM_BLOCKTYPE_VALTYPE:
      break;
    default:
      throw std::exception();
  }
  // this->handler.block_frame();
}

inline std::map<size_t, ControlFrame> preparse(std::vector<Instruction> instructions) {
  std::map<size_t, ControlFrame> pc_mappings;
  std::stack<ControlFrame> control_frames;

  for(auto pc = 0; pc < instructions.size(); pc++) {
    ControlFrame frame;
    switch(instructions[pc].opcode) {
      case WASM_OP_BLOCK:
        frame.kind = ControlFrameKind::Block;
        frame.start_pc = pc;
        control_frames.push(frame);
        break;
      case WASM_OP_IF:
        frame.kind = ControlFrameKind::If;
        frame.start_pc = pc;
        control_frames.push(frame);
        break;
      case WASM_OP_LOOP:
        frame.kind = ControlFrameKind::Loop;
        frame.start_pc = pc;
        control_frames.push(frame);
        break;
      case WASM_OP_ELSE:
        if(control_frames.size() <= 0) {
          throw WasmError("control frames is at zero but expected an if frame");
        }
        if(control_frames.top().kind != ControlFrameKind::If) {
          throw WasmError("inside else block so expected If control frame at top of stack");
        }
        control_frames.top().else_pc = pc;
        break;
      case WASM_OP_END:
        if(instructions.size() -1 != pc) {
          if(control_frames.size() <= 0) {
            throw WasmError("control frames is at zero");
          }
          ControlFrame finished_frame = control_frames.top();
          control_frames.pop();
          finished_frame.end_pc = pc;
          pc_mappings[finished_frame.start_pc] = finished_frame;
        }
        break;
      default:
        continue;
    }
  }

  return pc_mappings;
}

void Interpreter::run_function(FuncDecl *f, std::vector<Immediate> args) {

  std::vector<Instruction> instructions = this->parse_function(f);
  preparse(instructions);
  return;

  for(auto const &instr: instructions) {
    switch(instr.opcode) {
      case WASM_OP_CALL:
        this->handle_call(instr.imm.signed_value);
        break;
      case WASM_OP_I32_CONST:
        this->stack().push(instr.imm);
        break;
      case WASM_EOF:
        break;
      case WASM_OP_CALL_INDIRECT:
        this->handle_indirect_call(instr.imm);
        break;
      case WASM_OP_LOCAL_GET:
        this->handle_local_get(instr.imm, args);
        break;
      case WASM_OP_F64_ADD:
        this->handle_f64_add();
        break;
      case WASM_OP_BLOCK:
        this->handle_block(instr.imm);
        break;
      default:
        std::printf("%0x not supported\n", instr.opcode);
    }
  }
}

void Interpreter::run_function(int index, const std::vector<Immediate> args) {

  if(index < 0 || static_cast<size_t>(index) >= this->functions.size()) {
    return;
  }
  this->run_function(&this->functions[index], args);
}

void Interpreter::run_main() {
  FuncDecl *f;
  for(auto &exp: mod.Exports()) {
    if(exp.name == "main" && exp.kind == KIND_FUNC) {
      f = exp.desc.func;
      break;
    }
  }
  if(f == nullptr) {
    return;
  }
  std::vector<Immediate> params;
  for(auto const &p: f->sig->params) {
    switch(p) {
      case WASM_TYPE_I32:
        params.push_back(Immediate(static_cast<int32_t>(41)));
        break;
      case WASM_TYPE_I64:
        params.push_back(Immediate(static_cast<int64_t>(42)));
        break;
      case WASM_TYPE_F32:
        params.push_back(Immediate(static_cast<float>(0.41)));
        break;
      case WASM_TYPE_F64:
        params.push_back(Immediate(static_cast<double>(0.82)));
        break;
      default:
        std::cout << "unsupported type" << std::endl;
        return;
    }
  }
  this->run_function(f, params);

}

void Interpreter::dump_stack() {
  std::cout << "DUMPING IMMEDIATE" << std::endl;
  while(!this->stack().empty()) {
    auto val = this->stack().top();
    std::cout << val << std::endl;
    this->stack().pop();
  }
}

std::stack<Immediate> &Interpreter::stack() {
  return this->handler.frame().operand_stack();
}

size_t &::Interpreter::pc() {
  return this->handler.frame().operand_pc();
}

StackFrame::StackFrame() {

}

StackFrame::~StackFrame() {

}

std::stack<Immediate> &StackFrame::operand_stack() {
  return this->stack;
}

size_t &StackFrame::operand_pc() {
  return this->pc;
}

ControlHandler::ControlHandler() {
  this->frames.push(StackFrame());
}

ControlHandler::~ControlHandler() {

}

StackFrame &ControlHandler::frame() {
  if(this->frames.empty()) {
    throw WasmStackUnderflowTrap("no active stack frame");
  }
  return this->frames.top();
}

void ControlHandler::block_frame(ControlFrameKind kind) {
}

void ControlHandler::if_frame() {

}

void ControlHandler::loop_frame() {

}

std::vector<Immediate> ControlHandler::enter_function(SigDecl *decl) {
  bool has_args = (decl != nullptr && (decl->params.size() > 0));

  if(this->frames.size() <= 0 && has_args) {
    throw WasmError("the function signature expects args the stack frame size is zero, there is nothing to peek at");
  }

  StackFrame &caller_frame = this->frames.top();

  if(decl->params.size() > caller_frame.stack.size()) {
    throw WasmError("params were not received in callee's stack");
  }

  StackFrame callee_frame = StackFrame();
  std::vector<Immediate> args;

  if(decl != nullptr) { 
    for(auto const &p: decl->params) {
      Immediate imm = caller_frame.stack.top();
      caller_frame.stack.pop();
      if(imm.type != p) {
        throw WasmTypeMismatchTrap("expected type x got type y");
      }
      args.push_back(imm);
    }
  }
  this->frames.push(callee_frame);
  return args;
}
