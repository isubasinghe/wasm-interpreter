#pragma once

#include <sys/syscall.h>
#include <cstdint>
#include <cstddef>
#include <iomanip>
#include <list>
#include <string>
#include <algorithm>
#include <memory>
#include <ostream>

#include "common.h"
#include "wasmdefs.h"

typedef std::list<wasm_type_t> typelist;

/* Utility Functions */
const char* wasm_type_string(wasm_type_t type);
const char* wasm_section_name(byte sec_code);
const char* wasm_kind_string(wasm_kind_t kind);

class WasmModule;

/* Intermediate components */
struct wasm_limits_t {
  uint32_t initial;
  uint32_t max;
  unsigned has_max : 1;
  unsigned flag : 1;
};

struct wasm_localcse_t {
  uint32_t count;
  wasm_type_t type;
};
typedef std::list<wasm_localcse_t> wasm_localcsv_t;


struct FuncDecl;

struct SubsecBytes {
  byte id;
  bytearr bytes;
};
struct DebugNameAssoc {
  FuncDecl* func;
  std::string name;
};
struct DebugNameDecl {
  // Everything except function subsection
  std::list <SubsecBytes> subsections;
  // Function subsection: Id 1
  std::list <DebugNameAssoc> func_assoc;
};

/* Section Field Declarations */
struct CustomDecl {
  std::string name;
  bytearr bytes;
  /* Only populated for 'name' section */
  DebugNameDecl debug;
};


struct SigDecl {
  typelist params;
  typelist results;

  bool operator==(const SigDecl &sig) {
    bool parameq = std::equal(this->params.begin(), this->params.end(),
            sig.params.begin(), sig.params.end());
    bool resulteq = std::equal(this->results.begin(), this->results.end(),
            sig.results.begin(), sig.results.end());
    return parameq && resulteq;
  }
  bool operator!=(const SigDecl &sig) {
    bool parameq = std::equal(this->params.begin(), this->params.end(),
            sig.params.begin(), sig.params.end());
    bool resulteq = std::equal(this->results.begin(), this->results.end(),
            sig.results.begin(), sig.results.end());
    return !(parameq && resulteq);
  }
};


struct FuncDecl {
  /* From func/import section */
  SigDecl* sig;
  /* From code section */
  wasm_localcsv_t pure_locals;
  uint32_t num_pure_locals;
  /* Code */ 
  bytearr code_bytes;
};


struct MemoryDecl {
  wasm_limits_t limits;
};


struct TableDecl {
  wasm_type_t reftype;
  wasm_limits_t limits;
};


struct GlobalDecl {
  wasm_type_t type;
  unsigned is_mutable : 1;
  bytearr init_expr_bytes;
};


struct DataDecl {
  uint32_t flag;
  Opcode_t opcode_offset;
  uint32_t mem_offset;
  MemoryDecl *mem;
  bytearr bytes;
};


struct ElemDecl {
  uint32_t flag;
  Opcode_t opcode_offset;
  uint32_t table_offset;
  std::list <FuncDecl*> func_indices;
};


union Descriptor {
  FuncDecl* func;
  GlobalDecl* global;
  TableDecl* table;
  MemoryDecl* mem;
};

struct ImportInfo {
  std::string mod_name;
  std::string member_name;
};
struct ImportDecl {
  std::string mod_name;
  std::string member_name;
  wasm_kind_t kind;
  Descriptor desc;
};


struct ExportDecl {
  std::string name;
  wasm_kind_t kind;
  Descriptor desc;
};


struct ImportSet {
  std::list <ImportDecl> list;
  uint32_t num_funcs = 0;
  uint32_t num_tables = 0;
  uint32_t num_mems = 0;
  uint32_t num_globals = 0;

  inline uint32_t get_num_imports () {
    return num_funcs + num_tables + num_mems + num_globals;
  }
};


/* Section */
class WasmModule {

  private:
    uint32_t magic = 0;
    uint32_t version = 0;

    std::list <CustomDecl>  customs;
    std::list <SigDecl>     sigs;
    ImportSet               imports;
    /* Func space */
    std::deque <FuncDecl>    funcs;
    /* Table space */
    std::list <TableDecl>   tables;
    /* Mem space */
    std::list <MemoryDecl>  mems;
    /* Global space */
    std::deque <GlobalDecl>  globals;
    std::list <ExportDecl>  exports;
    std::list <ElemDecl>    elems;
    std::list <DataDecl>    datas;

    /* Start section */
    FuncDecl* start_fn = nullptr;

    /* Datacount section */ 
    int has_datacount = 0;
    /* For decode validation only, don't use otherwise */
    int num_datas_datacount = 0;

    /* Custom name section debug reference */
    std::list <DebugNameAssoc> *fn_names_debug = nullptr;

    /* Decode functions */
    #define DECODE_DECL(sec,...)  \
      void decode_##sec##_section (buffer_t &buf, uint32_t len);
    DECODE_DECL(type);
    DECODE_DECL(import);
    DECODE_DECL(function);
    DECODE_DECL(table);
    DECODE_DECL(memory);
    DECODE_DECL(global);
    DECODE_DECL(export);
    DECODE_DECL(start);
    DECODE_DECL(element);
    DECODE_DECL(code);
    DECODE_DECL(data);
    DECODE_DECL(datacount);
    DECODE_DECL(custom);

    void swap(WasmModule &other) noexcept;

  public:
    WasmModule () = default;
    /* Copies own their section data and redirect internal pointers into the copy. */
    WasmModule (const WasmModule &mod);
    ~WasmModule() = default;

    WasmModule& operator=(const WasmModule &mod);

    /* Field Accessors */
    inline SigDecl* getSig(uint32_t idx)        { return GET_LIST_ELEM(this->sigs, idx); }
    inline FuncDecl* getFunc(uint32_t idx)      { return GET_DEQUE_ELEM(this->funcs, idx); }
    inline GlobalDecl* getGlobal(uint32_t idx)  { return GET_DEQUE_ELEM(this->globals, idx); }
    inline TableDecl* getTable(uint32_t idx)    { return GET_LIST_ELEM(this->tables, idx); }
    inline MemoryDecl* getMemory(uint32_t idx)  { return GET_LIST_ELEM(this->mems, idx); }
    inline DataDecl* getData(uint32_t idx)      { return GET_LIST_ELEM(this->datas, idx); }
    inline ImportDecl* getImport(uint32_t idx)  { return GET_LIST_ELEM(this->imports.list, idx); }

    /* Index Accessors */
    inline uint32_t getSigIdx(SigDecl *sig)           const { return GET_LIST_IDX(this->sigs, sig); }
    inline uint32_t getFuncIdx(FuncDecl *func)        const { return GET_DEQUE_IDX(this->funcs, func); }
    inline uint32_t getGlobalIdx(GlobalDecl *global)  const { return GET_DEQUE_IDX(this->globals, global); }
    inline uint32_t getTableIdx(TableDecl *table)     const { return GET_LIST_IDX(this->tables, table); }
    inline uint32_t getMemoryIdx(MemoryDecl *mem)     const { return GET_LIST_IDX(this->mems, mem); }
    inline uint32_t getDataIdx(DataDecl *data)        const { return GET_LIST_IDX(this->datas, data); }
    inline uint32_t getImportIdx(ImportDecl *import)  const { return GET_LIST_IDX(this->imports.list, import); }

    /* Import accessors */
    inline bool isImport(FuncDecl *func)      { return getFuncIdx(func)     < this->imports.num_funcs; }
    inline bool isImport(GlobalDecl *global)  { return getGlobalIdx(global) < this->imports.num_globals; }
    inline bool isImport(TableDecl *table)    { return getTableIdx(table)   < this->imports.num_tables; }
    inline bool isImport(MemoryDecl *mem)     { return getMemoryIdx(mem)    < this->imports.num_mems; }

    /* Section Accessors */
    inline std::deque <FuncDecl> &Funcs() { return this->funcs; }
    inline std::deque <GlobalDecl> &Globals() { return this->globals; }
    inline std::list  <ExportDecl> &Exports() { return this->exports; }
    inline std::list  <SigDecl>    &Sigs()    { return this->sigs; }
    inline ImportSet               &Imports() { return this->imports; }
    inline std::list  <TableDecl>  &Tables()  { return this->tables; }
    inline std::list  <MemoryDecl> &Mems()    { return this->mems; }
    inline std::list  <ElemDecl>   &Elems()   { return this->elems; }
    inline std::list  <DataDecl>   &Datas()   { return this->datas; }
    inline std::list  <CustomDecl> &Customs() { return this->customs; }

    inline FuncDecl* get_start_fn() { return this->start_fn; }
    inline uint32_t get_num_customs() { return this->customs.size(); }

    /* Decode wasm file from buffer */
    void decode_buffer (buffer_t &buf);

};



struct MemArg {
  uint32_t align = 0;
  uint32_t offset = 0;
};

struct BrTable {
  std::vector<uint32_t> labels;
  uint32_t default_label = 0;
};

struct Immediate {
  Immediate() = default;

  explicit Immediate(int32_t value)
      : kind(IMM_I32), signed_value(value), type(WASM_TYPE_I32) {}

  explicit Immediate(int64_t value)
      : kind(IMM_I64), signed_value(value), type(WASM_TYPE_I64) {}

  explicit Immediate(float value)
      : kind(IMM_F32), f32(value), type(WASM_TYPE_F32) {}

  explicit Immediate(double value)
      : kind(IMM_F64), f64(value), type(WASM_TYPE_F64) {}

  explicit Immediate(const std::array<byte, 16>& value)
      : kind(IMM_V128), type(WASM_TYPE_V128), bytes(value) {}

  explicit Immediate(wasm_type_t ref_type)
      : kind(IMM_REFNULLT), type(ref_type) {}

  Immediate(wasm_type_t ref_type, uint64_t ref_value)
      : kind(IMM_REFNULLT), unsigned_value(ref_value), type(ref_type) {}

  opcode_imm_type kind = IMM_NONE;

  int64_t signed_value = 0;
  uint64_t unsigned_value = 0;
  uint64_t unsigned_value2 = 0;

  float f32 = 0.0f;
  double f64 = 0.0;
  wasm_type_t type = WASM_TYPE_I32;
  wasm_blocktype_kind_t blocktype_kind = WASM_BLOCKTYPE_EMPTY;
  MemArg memarg;
  uint8_t laneidx = 0;
  std::array<byte, 16> bytes = {};
  BrTable br_table;
  std::vector<wasm_type_t> types;
};

inline std::ostream& operator<<(std::ostream& os, const Immediate& imm) {
  switch (imm.kind) {
    case IMM_NONE:
      return os << "<none>";
    case IMM_BLOCKT:
      switch (imm.blocktype_kind) {
        case WASM_BLOCKTYPE_EMPTY:
          return os << "blocktype empty";
        case WASM_BLOCKTYPE_VALTYPE:
          return os << "blocktype " << imm.type;
        case WASM_BLOCKTYPE_TYPEIDX:
          return os << "blocktype typeidx " << imm.unsigned_value;
      }
      return os << "blocktype ?";
    case IMM_LABEL:
      return os << "label " << imm.unsigned_value;
    case IMM_FUNC:
      return os << "func " << imm.unsigned_value;
    case IMM_LOCAL:
      return os << "local " << imm.unsigned_value;
    case IMM_GLOBAL:
      return os << "global " << imm.unsigned_value;
    case IMM_TABLE:
      return os << "table " << imm.unsigned_value;
    case IMM_MEMORY:
      return os << "memory " << imm.unsigned_value;
    case IMM_TAG:
      return os << "tag " << imm.unsigned_value;
    case IMM_DATA:
      return os << "data " << imm.unsigned_value;
    case IMM_LABELS:
      os << "labels [";
      for (size_t i = 0; i < imm.br_table.labels.size(); i++) {
        if (i) os << ' ';
        os << imm.br_table.labels[i];
      }
      return os << "] default " << imm.br_table.default_label;
    case IMM_SIG_TABLE:
      return os << "type " << imm.unsigned_value
                << " table " << imm.unsigned_value2;
    case IMM_MEMARG:
      return os << "offset=" << imm.memarg.offset
                << " align=" << (1ull << imm.memarg.align);
    case IMM_I32:
      return os << "i32 " << static_cast<int32_t>(imm.signed_value);
    case IMM_I64:
      return os << "i64 " << imm.signed_value;
    case IMM_F32:
      return os << "f32 " << imm.f32;
    case IMM_F64:
      return os << "f64 " << imm.f64;
    case IMM_REFNULLT:
      return os << "ref.null " << imm.type;
    case IMM_VALTS:
      os << "types [";
      for (size_t i = 0; i < imm.types.size(); i++) {
        if (i) os << ' ';
        os << imm.types[i];
      }
      return os << ']';
    case IMM_DATA_MEMORY:
      return os << "data " << imm.unsigned_value
                << " memory " << imm.unsigned_value2;
    case IMM_DATA_TABLE:
      return os << "data " << imm.unsigned_value
                << " table " << imm.unsigned_value2;
    case IMM_MEMORYCP:
      return os << "memory " << imm.unsigned_value
                << " memory " << imm.unsigned_value2;
    case IMM_TABLECP:
      return os << "table " << imm.unsigned_value
                << " table " << imm.unsigned_value2;
    case IMM_V128: {
      os << "v128 0x";
      std::ios_base::fmtflags flags = os.flags();
      char fill = os.fill();
      for (byte b : imm.bytes) {
        os << std::hex << std::setw(2) << std::setfill('0')
           << static_cast<unsigned>(b);
      }
      os.flags(flags);
      os.fill(fill);
      return os;
    }
    case IMM_LANEIDX:
      return os << "lane " << static_cast<unsigned>(imm.laneidx);
    case IMM_LANEIDX16:
      os << "lanes [";
      for (size_t i = 0; i < imm.bytes.size(); i++) {
        if (i) os << ' ';
        os << static_cast<unsigned>(imm.bytes[i]);
      }
      return os << ']';
    case IMM_MEMARG_LANEIDX:
      return os << "offset=" << imm.memarg.offset
                << " align=" << (1ull << imm.memarg.align)
                << " lane=" << static_cast<unsigned>(imm.laneidx);
  }

  return os << "immediate(" << static_cast<int>(imm.kind) << ')';
}

struct Instruction {
  Opcode_t opcode = 0;
  size_t offset = 0;
  Immediate imm;
};
