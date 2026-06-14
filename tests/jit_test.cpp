#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "jit.h"

#include <doctest/doctest.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct JitTestOutput {
  int result;
  std::vector<uint8_t> code;
};

JitTestOutput run_jit(WasmJit &jit) {
  const uint8_t *code = jit.getCode<const uint8_t *>();
  std::vector<uint8_t> code_bytes(code, code + jit.getSize());

  auto f = jit.getCode<int (*)()>();
  return JitTestOutput {
    .result = f(),
    .code = code_bytes,
  };
}

JitTestOutput compile_add(int32_t lhs, int32_t rhs) {
  WasmJit jit;
  jit.emit_i32_const(lhs);
  jit.emit_i32_const(rhs);
  jit.emit_add_op();
  jit.emit_return_i32();
  return run_jit(jit);
}

JitTestOutput compile_if(int32_t condition, int32_t then_value, int32_t else_value) {
  WasmJit jit;
  jit.emit_i32_const(condition);
  jit.emit_if_block(
    [&]() {
      jit.mov(Xbyak::util::eax, then_value);
    },
    [&]() {
      jit.mov(Xbyak::util::eax, else_value);
    }
  );
  jit.ret();
  return run_jit(jit);
}

Instruction instruction(Opcode_t opcode) {
  Instruction instr;
  instr.opcode = opcode;
  return instr;
}

Instruction block_instruction(Opcode_t opcode, wasm_blocktype_kind_t kind = WASM_BLOCKTYPE_EMPTY) {
  Instruction instr;
  instr.opcode = opcode;
  instr.imm.kind = IMM_BLOCKT;
  instr.imm.blocktype_kind = kind;
  return instr;
}

Instruction result_block_instruction(Opcode_t opcode, wasm_type_t result_type) {
  Instruction instr = block_instruction(opcode, WASM_BLOCKTYPE_VALTYPE);
  instr.imm.type = result_type;
  return instr;
}

Instruction typeidx_block_instruction(Opcode_t opcode, uint32_t typeidx) {
  Instruction instr = block_instruction(opcode, WASM_BLOCKTYPE_TYPEIDX);
  instr.imm.unsigned_value = typeidx;
  return instr;
}

std::string write_temp_code_file(const std::vector<uint8_t> &code) {
  auto path = std::filesystem::temp_directory_path() / "wasm_jit_test_XXXXXX";
  std::string path_string = path.string();
  int fd = mkstemp(path_string.data());
  if(fd < 0) {
    throw std::runtime_error("mkstemp failed");
  }

  const uint8_t *data = code.data();
  size_t remaining = code.size();
  while(remaining > 0) {
    ssize_t written = write(fd, data, remaining);
    if(written < 0) {
      close(fd);
      std::filesystem::remove(path_string);
      throw std::runtime_error("failed to write temporary code file");
    }
    data += written;
    remaining -= written;
  }

  close(fd);
  return path_string;
}

std::string disassemble(const std::vector<uint8_t> &code) {
  auto path = write_temp_code_file(code);
  std::string command = "objdump -D -b binary -m i386:x86-64 " + path + " 2>&1";

  FILE *pipe = popen(command.c_str(), "r");
  if(pipe == nullptr) {
    std::filesystem::remove(path);
    throw std::runtime_error("failed to run objdump");
  }

  std::string output;
  char buffer[256];
  while(fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    output += buffer;
  }

  int status = pclose(pipe);
  std::filesystem::remove(path);
  if(status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    throw std::runtime_error("objdump failed:\n" + output);
  }

  return output;
}

} // namespace

TEST_CASE("jit i32.add returns expected values") {
  CHECK(compile_add(40, 2).result == 42);
  CHECK(compile_add(-5, 8).result == 3);
  CHECK(compile_add(0x7fffffff, 1).result == -2147483647 - 1);
}

TEST_CASE("jit i32.add generated code has a basic shape") {
  auto output = compile_add(40, 2);

  REQUIRE(!output.code.empty());
  CHECK(output.code.back() == 0xc3);
}

TEST_CASE("jit i32.add disassembly contains add and ret") {
  auto output = compile_add(40, 2);
  auto asm_text = disassemble(output.code);

  CAPTURE(asm_text);
  CHECK(asm_text.find("add") != std::string::npos);
  CHECK(asm_text.find("ret") != std::string::npos);
}

TEST_CASE("jit if block emits then branch for nonzero condition") {
  CHECK(compile_if(1, 11, 22).result == 11);
  CHECK(compile_if(-1, 11, 22).result == 11);
}

TEST_CASE("jit if block emits else branch for zero condition") {
  CHECK(compile_if(0, 11, 22).result == 22);
}

TEST_CASE("jit preparse records block frame stack height") {
  WasmJit jit;
  jit.preparse({
    instruction(WASM_OP_I32_CONST),
    result_block_instruction(WASM_OP_BLOCK, WASM_TYPE_I32),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  });

  const auto &mappings = jit.pc_mappings();
  REQUIRE(mappings.size() == 1);
  REQUIRE(mappings.contains(1));

  const auto &frame = mappings.at(1);
  CHECK(frame.kind == WasmControlFrameKind::Block);
  CHECK(frame.label_index == 0);
  CHECK(frame.label == "block_0");
  CHECK(frame.end_label == "block_0");
  CHECK(frame.branch_label == "block_0");
  CHECK(frame.start_pc == 1);
  CHECK(frame.end_pc == 3);
  CHECK(frame.stack_height == 1);
  CHECK(frame.sig.results.size() == 1);
  CHECK(frame.sig.results.front() == WASM_TYPE_I32);
}

TEST_CASE("jit preparse records if else and stack height after condition") {
  WasmJit jit;
  jit.preparse({
    instruction(WASM_OP_I32_CONST),
    result_block_instruction(WASM_OP_IF, WASM_TYPE_I32),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_ELSE),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  });

  const auto &mappings = jit.pc_mappings();
  REQUIRE(mappings.size() == 1);
  REQUIRE(mappings.contains(1));

  const auto &frame = mappings.at(1);
  CHECK(frame.kind == WasmControlFrameKind::If);
  CHECK(frame.label_index == 0);
  CHECK(frame.label == "if_0");
  CHECK(frame.then_label == "if_0_then");
  CHECK(frame.else_label == "if_0_else");
  CHECK(frame.end_label == "if_0_end");
  CHECK(frame.branch_label == "if_0_end");
  CHECK(frame.start_pc == 1);
  CHECK(frame.else_pc == 3);
  CHECK(frame.end_pc == 5);
  CHECK(frame.stack_height == 0);
  CHECK(frame.sig.results.size() == 1);
  CHECK(frame.sig.results.front() == WASM_TYPE_I32);
}

TEST_CASE("jit preparse resolves typeidx blocktypes from module signatures") {
  WasmModule mod;
  SigDecl sig;
  sig.params.push_back(WASM_TYPE_I32);
  sig.results.push_back(WASM_TYPE_I64);
  mod.Sigs().push_back(sig);

  WasmJit jit(&mod);
  jit.preparse({
    instruction(WASM_OP_I32_CONST),
    typeidx_block_instruction(WASM_OP_BLOCK, 0),
    instruction(WASM_OP_I64_CONST),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  });

  const auto &mappings = jit.pc_mappings();
  REQUIRE(mappings.size() == 1);
  REQUIRE(mappings.contains(1));

  const auto &frame = mappings.at(1);
  CHECK(frame.stack_height == 0);
  CHECK(frame.sig.params.size() == 1);
  CHECK(frame.sig.params.front() == WASM_TYPE_I32);
  CHECK(frame.sig.results.size() == 1);
  CHECK(frame.sig.results.front() == WASM_TYPE_I64);
}

TEST_CASE("jit preparse assigns unique labels to nested control frames") {
  WasmJit jit;
  jit.preparse({
    block_instruction(WASM_OP_BLOCK),
    block_instruction(WASM_OP_LOOP),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  });

  const auto &mappings = jit.pc_mappings();
  REQUIRE(mappings.size() == 2);
  REQUIRE(mappings.contains(0));
  REQUIRE(mappings.contains(1));

  const auto &block = mappings.at(0);
  const auto &loop = mappings.at(1);

  CHECK(block.kind == WasmControlFrameKind::Block);
  CHECK(block.label_index == 0);
  CHECK(block.label == "block_0");
  CHECK(block.branch_label == "block_0");

  CHECK(loop.kind == WasmControlFrameKind::Loop);
  CHECK(loop.label_index == 1);
  CHECK(loop.label == "loop_1");
  CHECK(loop.end_label == "loop_1_end");
  CHECK(loop.branch_label == "loop_1");
}
