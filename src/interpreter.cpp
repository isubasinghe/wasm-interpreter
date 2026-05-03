#include <stack>
#include <array>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "interpreter.h"
#include "ir.h"

namespace {

uint8_t read_byte(const bytearr& code, size_t& pos) {
  if (pos >= code.size()) {
    throw std::runtime_error("truncated bytecode");
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
      throw std::runtime_error("uleb immediate is too large");
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
    throw std::runtime_error("sleb immediate is too large");
  }

  if (shift < 64 && (b & 0x40)) {
    result |= -(int64_t(1) << shift);
  }

  return result;
}

template <typename T>
T read_raw(const bytearr& code, size_t& pos) {
  if (pos + sizeof(T) > code.size()) {
    throw std::runtime_error("truncated raw immediate");
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

Opcode_t read_opcode(const bytearr& code, size_t& pos) {
  byte b = read_byte(code, pos);
  Opcode_t opcode = b;

  if (is_prefix_byte(b)) {
    uint64_t subopcode = read_uleb(code, pos);
    if (subopcode > 0xff) {
      throw std::runtime_error("unsupported extended opcode");
    }
    opcode = (Opcode_t(b) << 8) | Opcode_t(subopcode);
  }

  if (opcode_table[opcode].mnemonic == nullptr) {
    throw std::runtime_error("unknown opcode");
  }

  return opcode;
}

void parse_immediate(Immediate& imm, const bytearr& code, size_t& pos) {
  switch (imm.kind) {
    case IMM_NONE:
      break;
    case IMM_BLOCKT:
      imm.signed_value = read_sleb(code, pos);
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
  for(auto const &f: mod.Funcs()) {
    this->push_func(f);
  }  
}

Interpreter::~Interpreter() {

}

void Interpreter::push_func(FuncDecl f) {
  this->functions.push_back(f);
}

std::vector<Instruction> Interpreter::parse_function(const FuncDecl& f) {
  const bytearr& code = f.code_bytes;
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
    std::cout << "fn_idx >= fn.size()\n";
    throw std::exception();
    return;
  }

  auto function = this->functions[fn_idx];
  if(function.sig->params.size() > this->scratch_pad.size()) {
    std::cout << "param.size() isn't equal\n";
    throw std::exception();
    return;
  }

  std::list<Immediate> l = std::list<Immediate>();
  for(int i=0; i < function.sig->params.size(); i++) {
    Immediate im = this->scratch_pad.top();
    l.push_back(im);
  }
  this->run_function(fn_idx, l);
}


void Interpreter::run_function(int index, const std::list<Immediate> args) {
  if(index < 0 || static_cast<size_t>(index) >= this->functions.size()) {
    return;
  }

  std::vector<Instruction> instructions = this->parse_function(this->functions[index]);

  for(auto const &instr: instructions) {
    switch(instr.opcode) {
      case WASM_OP_CALL:
        this->handle_call(instr.imm.signed_value);
        break;
      case WASM_OP_I32_CONST:
        this->scratch_pad.push(instr.imm);
        break;
      case WASM_EOF:
        break;
      default:
        std::printf("%0x not supported\n", instr.opcode);
    }
  }
  
}

void Interpreter::dump_stack() {
  while(!this->scratch_pad.empty()) {
    auto val = this->scratch_pad.top();
    std::cout << val << std::endl;
    this->scratch_pad.pop();
  }
}
