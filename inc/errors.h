#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

class WasmError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class WasmDecodeError : public WasmError {
 public:
  using WasmError::WasmError;
};

class WasmParseError : public WasmError {
 public:
  using WasmError::WasmError;
};

class WasmValidationError : public WasmError {
 public:
  using WasmError::WasmError;
};

class WasmUnsupportedError : public WasmError {
 public:
  using WasmError::WasmError;
};

class WasmRuntimeTrap : public WasmError {
 public:
  using WasmError::WasmError;
};

class WasmUnknownOpcodeError : public WasmDecodeError {
 public:
  explicit WasmUnknownOpcodeError(uint32_t opcode)
      : WasmDecodeError("unknown opcode: " + std::to_string(opcode)) {}
};

class WasmUnsupportedOpcodeError : public WasmUnsupportedError {
 public:
  explicit WasmUnsupportedOpcodeError(uint32_t opcode)
      : WasmUnsupportedError("unsupported opcode: " + std::to_string(opcode)) {}
};

class WasmInvalidImmediateError : public WasmDecodeError {
 public:
  using WasmDecodeError::WasmDecodeError;
};

class WasmTruncatedInputError : public WasmDecodeError {
 public:
  using WasmDecodeError::WasmDecodeError;
};

class WasmInvalidIndexError : public WasmValidationError {
 public:
  using WasmValidationError::WasmValidationError;
};

class WasmTypeMismatchTrap : public WasmRuntimeTrap {
 public:
  using WasmRuntimeTrap::WasmRuntimeTrap;
};

class WasmStackUnderflowTrap : public WasmRuntimeTrap {
 public:
  using WasmRuntimeTrap::WasmRuntimeTrap;
};

class WasmOutOfBoundsTrap : public WasmRuntimeTrap {
 public:
  using WasmRuntimeTrap::WasmRuntimeTrap;
};

class WasmNullReferenceTrap : public WasmRuntimeTrap {
 public:
  using WasmRuntimeTrap::WasmRuntimeTrap;
};
