#pragma once

#include <format>
#include <sstream>
#include <stack>
#include <string>

#include "ir.h"

namespace wasm_format {

inline std::string to_string(const Immediate& imm) {
  std::ostringstream os;
  os << imm;
  return os.str();
}

inline std::string to_string(const Instruction& instr) {
  std::ostringstream os;
  os << instr.offset << ": ";

  const opcode_entry_t& entry = opcode_table[OP_IDX(instr.opcode)];
  if (entry.mnemonic != nullptr) {
    os << entry.mnemonic;
  } else {
    os << "opcode 0x" << std::hex << instr.opcode << std::dec;
  }

  if (instr.imm.kind != IMM_NONE) {
    os << ' ' << instr.imm;
  }

  return os.str();
}

template <typename Container>
std::string to_string(std::stack<Immediate, Container> stack) {
  std::ostringstream os;
  os << '[';
  bool first = true;
  while (!stack.empty()) {
    if (!first) {
      os << ", ";
    }
    first = false;
    os << stack.top();
    stack.pop();
  }
  os << ']';
  return os.str();
}

}

template <>
struct std::formatter<Immediate> : std::formatter<std::string> {
  auto format(const Immediate& imm, format_context& ctx) const {
    return std::formatter<std::string>::format(wasm_format::to_string(imm), ctx);
  }
};

template <>
struct std::formatter<Instruction> : std::formatter<std::string> {
  auto format(const Instruction& instr, format_context& ctx) const {
    return std::formatter<std::string>::format(wasm_format::to_string(instr), ctx);
  }
};

template <typename Container>
struct std::formatter<std::stack<Immediate, Container>> : std::formatter<std::string> {
  auto format(const std::stack<Immediate, Container>& stack, format_context& ctx) const {
    return std::formatter<std::string>::format(wasm_format::to_string(stack), ctx);
  }
};
