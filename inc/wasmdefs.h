#pragma once

#include "wasmops.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
#include "errors.h"
#endif

#define WASM_MAGIC 0x6d736100u
#define WASM_VERSION 1

#define WASM_PAGE_SIZE 65536

/* Other common defines/expressions */
#define INIT_EXPR(type, val) ({  \
  bytedeque bdeq; \
  int ty = WASM_OP_##type;  \
  bdeq.push_back(ty); \
  switch (ty) { \
    case WASM_OP_I32_CONST: WR_I32(val); break; \
    case WASM_OP_I64_CONST: WR_I64(val); break;  \
    case WASM_OP_F32_CONST: WR_U32_RAW(val); break;  \
    case WASM_OP_F64_CONST: WR_U64_RAW(val); break; \
    default: throw WasmParseError("invalid initializer expression type"); \
  } \
  bdeq.push_back(WASM_OP_END);  \
  bytearr (bdeq.begin(), bdeq.end()); \
})

/* Section constants */
typedef enum {
  WASM_SECT_CUSTOM = 0,
  WASM_SECT_TYPE,
  WASM_SECT_IMPORT,
  WASM_SECT_FUNCTION,
  WASM_SECT_TABLE,
  WASM_SECT_MEMORY,
  WASM_SECT_GLOBAL,
  WASM_SECT_EXPORT,
  WASM_SECT_START,
  WASM_SECT_ELEMENT,
  WASM_SECT_CODE,
  WASM_SECT_DATA,
  WASM_SECT_DATACOUNT
} wasm_section_t;


/* Import/export desc kind decoding */
#define WASM_DESC_FUNC 0x00
#define WASM_DESC_TABLE 0x01
#define WASM_DESC_MEM 0x02
#define WASM_DESC_GLOBAL 0x03

/* Type decoding */
typedef enum {
  WASM_TYPE_I32 = 0x7F, 
  WASM_TYPE_I64 = 0x7E,
  WASM_TYPE_F32 = 0x7D,
  WASM_TYPE_F64 = 0x7C, 
  WASM_TYPE_V128 = 0x7B,
  WASM_TYPE_FUNCREF = 0x70,
  WASM_TYPE_EXTERNREF = 0x6F,
  WASM_TYPE_FUNC = 0x60
} wasm_type_t;

typedef enum {
  WASM_BLOCKTYPE_EMPTY = 0,
  WASM_BLOCKTYPE_VALTYPE,
  WASM_BLOCKTYPE_TYPEIDX
} wasm_blocktype_kind_t;


#ifdef __cplusplus
#include <ostream>
inline std::ostream& operator<<(std::ostream& os, wasm_type_t t) {
  switch(t) {
    case WASM_TYPE_I32: return os << "i32";
    case WASM_TYPE_I64: return os << "i64";
    case WASM_TYPE_F32: return os << "f32";
    case WASM_TYPE_F64: return os << "f64";
    case WASM_TYPE_V128: return os << "v128";
    case WASM_TYPE_FUNCREF: return os << "funcref";
    case WASM_TYPE_EXTERNREF: return os << "externref";
    case WASM_TYPE_FUNC: return os << "func";
  }
  return os << "wasm_type_t(" << static_cast<int>(t) << ")";
}
#endif

/* Kind decoding */
typedef enum {
  KIND_FUNC = WASM_DESC_FUNC, 
  KIND_TABLE = WASM_DESC_TABLE, 
  KIND_MEMORY = WASM_DESC_MEM,
  KIND_GLOBAL = WASM_DESC_GLOBAL
} wasm_kind_t;

/* Common types */
typedef union V128_t {
  uint64_t v[2];
} V128_t;

typedef uint32_t Opcode_t;
typedef uint8_t Laneidx_t;
typedef struct {
  Laneidx_t l[16];
} Laneidx16_t;

static inline bool isReftype(wasm_type_t type) { 
  return (type == WASM_TYPE_EXTERNREF) || (type == WASM_TYPE_FUNCREF);
}


/* Opcode immediate types */
typedef enum {
  IMM_NONE = 0,
  IMM_BLOCKT,
  IMM_LABEL,
  IMM_LABELS,
  IMM_FUNC,
  IMM_SIG_TABLE,
  IMM_LOCAL,
  IMM_GLOBAL,
  IMM_TABLE,
  IMM_MEMARG,
  IMM_I32,
  IMM_F64,
  IMM_MEMORY,
  IMM_TAG,
  IMM_I64,
  IMM_F32,
  IMM_REFNULLT,
  IMM_VALTS,
  // Extension immediates
  IMM_DATA_MEMORY,
  IMM_DATA,
  IMM_MEMORYCP,
  IMM_DATA_TABLE,
  IMM_TABLECP,
  // SIMD
  IMM_V128,
  IMM_LANEIDX,
  IMM_LANEIDX16,
  IMM_MEMARG_LANEIDX
} opcode_imm_type;

/* Information associated with each opcode */
typedef struct {
  const char*       mnemonic;
  opcode_imm_type   imm_type;
  int               invalid;
} opcode_entry_t;

/* Dense remap of WASM opcodes. The raw opcode values in wasmops.h encode the
 * prefix and sub-opcode as a packed byte sequence (e.g. SIMD 3-byte form
 * 0xFDxx01), which would size the table at ~16M entries (~254 MB). OP_IDX
 * collapses those values into a 1280-entry layout:
 *   0x000-0x0FF : single-byte opcodes
 *   0x100-0x1FF : 0xFB-prefix (reserved)
 *   0x200-0x2FF : 0xFC-prefix
 *   0x300-0x3FF : 0xFD-prefix (SIMD; both 2-byte sub<128 and 3-byte sub>=128)
 *   0x400-0x4FF : 0xFE-prefix
 *
 * For SIMD, the 2-byte runtime form uses the actual sub-opcode in the low
 * byte, and the 3-byte header form encodes the same sub-opcode in its middle
 * byte (the LEB continuation byte has the high bit set, so the byte equals
 * the sub-opcode value). Both map to the same dense slot. */
#define OPCODE_TABLE_SIZE 0x500u

#define OP_IDX(op) \
  ( (uint32_t)(op) < 0x100u ? (uint32_t)(op) : \
    ((uint32_t)(op) >> 16) == 0u ? \
      (0x100u + ((((uint32_t)(op) >> 8) & 0xFFu) - 0xFBu) * 0x100u + ((uint32_t)(op) & 0xFFu)) : \
    ((uint32_t)(op) >> 16) == 0xFDu && ((uint32_t)(op) & 0xFFu) == 0x01u ? \
      (0x100u + (0xFDu - 0xFBu) * 0x100u + (((uint32_t)(op) >> 8) & 0xFFu)) : \
    0xFFu )

#ifdef __cplusplus
extern "C" {
#endif
extern opcode_entry_t opcode_table[OPCODE_TABLE_SIZE];
extern bool legal_opcodes_table[OPCODE_TABLE_SIZE];
#ifdef __cplusplus
}
#endif
