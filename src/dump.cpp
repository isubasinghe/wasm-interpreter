#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "dump.h"
#include "errors.h"
#include "ir.h"

namespace {

uint64_t read_uleb(const bytearr& code, size_t& pos) {
  uint64_t result = 0;
  unsigned shift = 0;
  while (true) {
    if (pos >= code.size()) throw WasmTruncatedInputError("truncated uleb");
    byte b = code[pos++];
    result |= uint64_t(b & 0x7f) << shift;
    if ((b & 0x80) == 0) break;
    shift += 7;
  }
  return result;
}

int64_t read_sleb(const bytearr& code, size_t& pos) {
  int64_t result = 0;
  unsigned shift = 0;
  byte b;
  do {
    if (pos >= code.size()) throw WasmTruncatedInputError("truncated sleb");
    b = code[pos++];
    result |= int64_t(b & 0x7f) << shift;
    shift += 7;
  } while (b & 0x80);
  if (shift < 64 && (b & 0x40))
    result |= -(int64_t(1) << shift);
  return result;
}

template <typename T>
T read_raw(const bytearr& code, size_t& pos) {
  if (pos + sizeof(T) > code.size()) throw WasmTruncatedInputError("truncated raw");
  T v;
  std::memcpy(&v, &code[pos], sizeof(T));
  pos += sizeof(T);
  return v;
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

void emit_blocktype(std::ostream& os, const bytearr& code, size_t& pos) {
  if (pos >= code.size()) throw WasmTruncatedInputError("truncated blocktype");

  byte b = code[pos];
  if (b == 0x40) {
    pos++;
    os << " empty";
    return;
  }

  if (is_valtype_byte(b)) {
    pos++;
    os << ' ' << static_cast<wasm_type_t>(b);
    return;
  }

  int64_t typeidx = read_sleb(code, pos);
  if (typeidx < 0) {
    throw WasmInvalidImmediateError("blocktype type index must be non-negative");
  }
  os << " (type " << typeidx << ')';
}

const char* kind_name(wasm_kind_t k) {
  switch (k) {
    case KIND_FUNC:   return "func";
    case KIND_TABLE:  return "table";
    case KIND_MEMORY: return "memory";
    case KIND_GLOBAL: return "global";
  }
  return "?";
}

void emit_indent(std::ostream& os, int n) {
  for (int i = 0; i < n; i++) os << "  ";
}

void emit_immediate(std::ostream& os, opcode_imm_type kind,
                    const bytearr& code, size_t& pos) {
  switch (kind) {
    case IMM_NONE:
      break;
    case IMM_BLOCKT:
      emit_blocktype(os, code, pos);
      break;
    case IMM_LABEL:
    case IMM_FUNC:
    case IMM_LOCAL:
    case IMM_GLOBAL:
    case IMM_TABLE:
    case IMM_MEMORY:
    case IMM_TAG:
    case IMM_DATA:
      os << ' ' << read_uleb(code, pos);
      break;
    case IMM_LABELS: {
      uint64_t n = read_uleb(code, pos);
      for (uint64_t i = 0; i < n; i++) os << ' ' << read_uleb(code, pos);
      os << " (default " << read_uleb(code, pos) << ')';
      break;
    }
    case IMM_SIG_TABLE: {
      uint64_t typeidx = read_uleb(code, pos);
      uint64_t tabidx  = read_uleb(code, pos);
      os << " (type " << typeidx << ") (table " << tabidx << ')';
      break;
    }
    case IMM_MEMARG: {
      uint64_t align  = read_uleb(code, pos);
      uint64_t offset = read_uleb(code, pos);
      os << " offset=" << offset << " align=" << (1ull << align);
      break;
    }
    case IMM_I32:
      os << ' ' << static_cast<int32_t>(read_sleb(code, pos));
      break;
    case IMM_I64:
      os << ' ' << read_sleb(code, pos);
      break;
    case IMM_F32: {
      uint32_t bits = read_raw<uint32_t>(code, pos);
      float v;
      std::memcpy(&v, &bits, sizeof(v));
      os << ' ' << v;
      break;
    }
    case IMM_F64: {
      uint64_t bits = read_raw<uint64_t>(code, pos);
      double v;
      std::memcpy(&v, &bits, sizeof(v));
      os << ' ' << v;
      break;
    }
    case IMM_REFNULLT:
      os << ' ' << static_cast<wasm_type_t>(code[pos++]);
      break;
    case IMM_VALTS: {
      uint64_t n = read_uleb(code, pos);
      os << " [";
      for (uint64_t i = 0; i < n; i++) {
        if (i) os << ' ';
        os << static_cast<wasm_type_t>(code[pos++]);
      }
      os << ']';
      break;
    }
    case IMM_DATA_MEMORY:
    case IMM_DATA_TABLE:
    case IMM_MEMORYCP:
    case IMM_TABLECP: {
      uint64_t a = read_uleb(code, pos);
      uint64_t b = read_uleb(code, pos);
      os << ' ' << a << ' ' << b;
      break;
    }
    case IMM_V128: {
      os << " 0x";
      for (int i = 0; i < 16; i++) {
        os << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(code[pos++]);
      }
      os << std::dec << std::setfill(' ');
      break;
    }
    case IMM_LANEIDX:
      os << ' ' << static_cast<unsigned>(code[pos++]);
      break;
    case IMM_LANEIDX16:
      os << " [";
      for (int i = 0; i < 16; i++) {
        if (i) os << ' ';
        os << static_cast<unsigned>(code[pos++]);
      }
      os << ']';
      break;
    case IMM_MEMARG_LANEIDX: {
      uint64_t align  = read_uleb(code, pos);
      uint64_t offset = read_uleb(code, pos);
      os << " offset=" << offset << " align=" << (1ull << align)
         << " lane=" << static_cast<unsigned>(code[pos++]);
      break;
    }
  }
}

void disassemble(std::ostream& os, const bytearr& code, int base_indent) {
  size_t pos = 0;
  int indent = base_indent;
  while (pos < code.size()) {
    byte b = code[pos++];
    uint32_t opcode = b;
    if (is_prefix_byte(b)) {
      uint64_t sub = read_uleb(code, pos);
      if (sub > 0xFF) {
        emit_indent(os, indent);
        os << "(;unknown 0x" << std::hex << opcode
           << std::setw(2) << std::setfill('0') << sub
           << std::dec << std::setfill(' ') << ";)\n";
        continue;
      }
      opcode = (b << 8) | static_cast<uint32_t>(sub);
    }

    if (opcode == WASM_OP_END || opcode == WASM_OP_ELSE) {
      if (indent > base_indent) indent--;
    }

    const opcode_entry_t& e = opcode_table[OP_IDX(opcode)];
    emit_indent(os, indent);
    if (e.mnemonic == nullptr) {
      os << "(;unknown 0x" << std::hex << opcode
         << std::dec << ";)\n";
      continue;
    }
    os << e.mnemonic;
    emit_immediate(os, e.imm_type, code, pos);
    os << '\n';

    if (opcode == WASM_OP_BLOCK || opcode == WASM_OP_LOOP ||
        opcode == WASM_OP_IF    || opcode == WASM_OP_ELSE) {
      indent++;
    }
  }
}

}  // namespace

std::ostream& operator<<(std::ostream& os, const wasm_limits_t& l) {
  os << l.initial;
  if (l.has_max) os << ' ' << l.max;
  return os;
}

std::ostream& operator<<(std::ostream& os, const SigDecl& sig) {
  bool wrote = false;
  if (!sig.params.empty()) {
    os << "(param";
    for (auto t : sig.params) os << ' ' << t;
    os << ')';
    wrote = true;
  }
  if (!sig.results.empty()) {
    if (wrote) os << ' ';
    os << "(result";
    for (auto t : sig.results) os << ' ' << t;
    os << ')';
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const FuncDecl& f) {
  os << *f.sig;
  os << "\n    (locals";
  for (auto const& l : f.pure_locals) {
    for (uint32_t i = 0; i < l.count; i++) os << ' ' << l.type;
  }
  os << ")\n";
  disassemble(os, f.code_bytes, 2);
  return os;
}

std::ostream& operator<<(std::ostream& os, const GlobalDecl& g) {
  os << '(' << (g.is_mutable ? "mut " : "") << g.type << ") ;; "
     << g.init_expr_bytes.size() << " init bytes";
  return os;
}

std::ostream& operator<<(std::ostream& os, const TableDecl& t) {
  os << t.limits << ' ' << t.reftype;
  return os;
}

std::ostream& operator<<(std::ostream& os, const MemoryDecl& m) {
  os << m.limits;
  return os;
}

std::ostream& operator<<(std::ostream& os, const ElemDecl& e) {
  os << "flag=" << e.flag
     << " table_off=" << e.table_offset
     << " funcs=" << e.func_indices.size();
  return os;
}

std::ostream& operator<<(std::ostream& os, const DataDecl& d) {
  os << "flag=" << d.flag
     << " mem_off=" << d.mem_offset
     << " bytes=" << d.bytes.size();
  return os;
}

std::ostream& operator<<(std::ostream& os, const ImportDecl& i) {
  os << '"' << i.mod_name << "\" \"" << i.member_name << "\" ("
     << kind_name(i.kind) << ')';
  return os;
}

std::ostream& operator<<(std::ostream& os, const ExportDecl& e) {
  os << '"' << e.name << "\" (" << kind_name(e.kind) << ')';
  return os;
}

std::ostream& operator<<(std::ostream& os, const CustomDecl& c) {
  os << '"' << c.name << "\" (" << c.bytes.size() << " bytes)";
  return os;
}

std::ostream& operator<<(std::ostream& os, WasmModule& mod) {
  os << "(module\n";

  os << "  ;; types (" << mod.Sigs().size() << ")\n";
  uint32_t i = 0;
  for (auto const& s : mod.Sigs()) {
    os << "  (type (;" << i++ << ";) " << s << ")\n";
  }

  auto& imps = mod.Imports();
  os << "\n  ;; imports (" << imps.list.size() << ")\n";
  i = 0;
  for (auto const& im : imps.list) {
    os << "  (import (;" << i++ << ";) \"" << im.mod_name << "\" \""
       << im.member_name << "\" (" << kind_name(im.kind) << ' ';
    switch (im.kind) {
      case KIND_FUNC:   os << mod.getFuncIdx(im.desc.func); break;
      case KIND_TABLE:  os << mod.getTableIdx(im.desc.table); break;
      case KIND_MEMORY: os << mod.getMemoryIdx(im.desc.mem); break;
      case KIND_GLOBAL: os << mod.getGlobalIdx(im.desc.global); break;
    }
    os << "))\n";
  }

  os << "\n  ;; functions (" << mod.Funcs().size() << ")\n";
  i = 0;
  for (auto& f : mod.Funcs()) {
    os << "  (func (;" << i++ << ";) " << f << "  )\n";
  }

  os << "\n  ;; tables (" << mod.Tables().size() << ")\n";
  i = 0;
  for (auto const& t : mod.Tables()) {
    os << "  (table (;" << i++ << ";) " << t << ")\n";
  }

  os << "\n  ;; memories (" << mod.Mems().size() << ")\n";
  i = 0;
  for (auto const& m : mod.Mems()) {
    os << "  (memory (;" << i++ << ";) " << m << ")\n";
  }

  os << "\n  ;; globals (" << mod.Globals().size() << ")\n";
  i = 0;
  for (auto const& g : mod.Globals()) {
    os << "  (global (;" << i++ << ";) " << g << ")\n";
  }

  os << "\n  ;; exports (" << mod.Exports().size() << ")\n";
  for (auto const& e : mod.Exports()) {
    os << "  (export \"" << e.name << "\" (" << kind_name(e.kind) << ' ';
    switch (e.kind) {
      case KIND_FUNC:   os << mod.getFuncIdx(e.desc.func); break;
      case KIND_TABLE:  os << mod.getTableIdx(e.desc.table); break;
      case KIND_MEMORY: os << mod.getMemoryIdx(e.desc.mem); break;
      case KIND_GLOBAL: os << mod.getGlobalIdx(e.desc.global); break;
    }
    os << "))\n";
  }

  os << "\n  ;; elements (" << mod.Elems().size() << ")\n";
  i = 0;
  for (auto const& el : mod.Elems()) {
    os << "  (elem (;" << i++ << ";) " << el << ")\n";
  }

  os << "\n  ;; data (" << mod.Datas().size() << ")\n";
  i = 0;
  for (auto const& d : mod.Datas()) {
    os << "  (data (;" << i++ << ";) " << d << ")\n";
  }

  os << "\n  ;; start: ";
  if (mod.get_start_fn()) {
    os << "func " << mod.getFuncIdx(mod.get_start_fn()) << '\n';
  } else {
    os << "(none)\n";
  }

  os << "\n  ;; customs (" << mod.Customs().size() << ")\n";
  for (auto const& c : mod.Customs()) {
    os << "  (custom " << c << ")\n";
  }

  os << ")\n";
  return os;
}

void dump_module(WasmModule &mod) {
  std::cout << mod;
}
