#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "jit.h"
#include "vecmap.h"

#include <doctest/doctest.h>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <fstream>
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

FuncDecl make_test_func(SigDecl *sig, wasm_localcsv_t pure_locals = {}) {
  FuncDecl func;
  func.sig = sig;
  func.pure_locals = pure_locals;
  func.num_pure_locals = 0;
  for(const auto &group: pure_locals) {
    func.num_pure_locals += group.count;
  }
  return func;
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

std::vector<uint8_t> read_binary_file(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if(!input) {
    throw std::runtime_error("failed to open binary file");
  }
  return std::vector<uint8_t>(
    std::istreambuf_iterator<char>(input),
    std::istreambuf_iterator<char>()
  );
}

std::vector<Opcode_t> opcodes_in(const WasmInstructionRange &range) {
  std::vector<Opcode_t> opcodes;
  for(const auto &instr: range) {
    opcodes.push_back(instr.opcode);
  }
  return opcodes;
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

TEST_CASE("jit writes generated binary code to a file") {
  WasmJit jit;
  jit.emit_i32_const(40);
  jit.emit_i32_const(2);
  jit.emit_add_op();
  jit.emit_return_i32();

  const uint8_t *code = jit.getCode<const uint8_t *>();
  std::vector<uint8_t> expected(code, code + jit.getSize());

  auto path = std::filesystem::temp_directory_path() / "wasm_jit_binary_test_XXXXXX";
  std::string path_string = path.string();
  int fd = mkstemp(path_string.data());
  REQUIRE(fd >= 0);
  close(fd);

  jit.write_binary(path_string);
  auto actual = read_binary_file(path_string);
  std::filesystem::remove(path_string);

  CHECK(actual == expected);
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

TEST_CASE("jit returns a preparsed control frame by program counter") {
  WasmJit jit;
  jit.preparse({
    block_instruction(WASM_OP_BLOCK),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  });

  const auto &frame = jit.control_frame_at(0);
  CHECK(frame.kind == WasmControlFrameKind::Block);
  CHECK(frame.start_pc == 0);
  CHECK(frame.end_pc == 1);

  CHECK_THROWS_AS(jit.control_frame_at(1), std::runtime_error);
}

TEST_CASE("jit instruction ranges expose half-open absolute pc bounds") {
  WasmJit jit;
  std::vector<Instruction> instructions {
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_I32_ADD),
    instruction(WASM_OP_END),
  };

  auto range = jit.instruction_range(instructions, 1, 3);

  CHECK(range.start_pc == 1);
  CHECK(range.end_pc == 3);
  CHECK(range.size() == 2);
  CHECK(!range.empty());
  CHECK(opcodes_in(range) == std::vector<Opcode_t> {
    WASM_OP_I32_CONST,
    WASM_OP_I32_ADD,
  });
}

TEST_CASE("jit if block ranges split then and else bodies") {
  WasmJit jit;
  std::vector<Instruction> instructions {
    instruction(WASM_OP_I32_CONST),
    result_block_instruction(WASM_OP_IF, WASM_TYPE_I32),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_ELSE),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_I32_ADD),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  };
  jit.preparse(instructions);

  auto ranges = jit.if_block_ranges(instructions, jit.control_frame_at(1));

  CHECK(ranges.then_branch.start_pc == 2);
  CHECK(ranges.then_branch.end_pc == 3);
  CHECK(opcodes_in(ranges.then_branch) == std::vector<Opcode_t> {
    WASM_OP_I32_CONST,
  });

  CHECK(ranges.else_branch.start_pc == 4);
  CHECK(ranges.else_branch.end_pc == 7);
  CHECK(opcodes_in(ranges.else_branch) == std::vector<Opcode_t> {
    WASM_OP_I32_CONST,
    WASM_OP_I32_CONST,
    WASM_OP_I32_ADD,
  });
}

TEST_CASE("jit if block ranges use an empty else body when there is no else") {
  WasmJit jit;
  std::vector<Instruction> instructions {
    instruction(WASM_OP_I32_CONST),
    result_block_instruction(WASM_OP_IF, WASM_TYPE_I32),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  };
  jit.preparse(instructions);

  auto ranges = jit.if_block_ranges(instructions, jit.control_frame_at(1));

  CHECK(ranges.then_branch.start_pc == 2);
  CHECK(ranges.then_branch.end_pc == 3);
  CHECK(ranges.else_branch.start_pc == 3);
  CHECK(ranges.else_branch.end_pc == 3);
  CHECK(ranges.else_branch.empty());
}

TEST_CASE("jit block ranges expose the block body and matching end") {
  WasmJit jit;
  std::vector<Instruction> instructions {
    block_instruction(WASM_OP_BLOCK),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_I32_ADD),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  };
  jit.preparse(instructions);

  auto ranges = jit.block_ranges(instructions, jit.control_frame_at(0));

  CHECK(ranges.body.start_pc == 1);
  CHECK(ranges.body.end_pc == 5);
  CHECK(opcodes_in(ranges.body) == std::vector<Opcode_t> {
    WASM_OP_I32_CONST,
    WASM_OP_I32_CONST,
    WASM_OP_I32_ADD,
    WASM_OP_END,
  });
}

TEST_CASE("jit loop block ranges expose the loop body and matching end") {
  WasmJit jit;
  std::vector<Instruction> instructions {
    block_instruction(WASM_OP_LOOP),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_BR_IF),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  };
  jit.preparse(instructions);

  auto ranges = jit.loop_block_ranges(instructions, jit.control_frame_at(0));

  CHECK(ranges.body.start_pc == 1);
  CHECK(ranges.body.end_pc == 4);
  CHECK(opcodes_in(ranges.body) == std::vector<Opcode_t> {
    WASM_OP_I32_CONST,
    WASM_OP_BR_IF,
    WASM_OP_END,
  });
}

TEST_CASE("jit block ranges include nested control frames as normal instructions") {
  WasmJit jit;
  std::vector<Instruction> instructions {
    block_instruction(WASM_OP_BLOCK),
    block_instruction(WASM_OP_LOOP),
    instruction(WASM_OP_END),
    instruction(WASM_OP_I32_CONST),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  };
  jit.preparse(instructions);

  auto block_ranges = jit.block_ranges(instructions, jit.control_frame_at(0));
  auto loop_ranges = jit.loop_block_ranges(instructions, jit.control_frame_at(1));

  CHECK(block_ranges.body.start_pc == 1);
  CHECK(block_ranges.body.end_pc == 5);
  CHECK(opcodes_in(block_ranges.body) == std::vector<Opcode_t> {
    WASM_OP_LOOP,
    WASM_OP_END,
    WASM_OP_I32_CONST,
    WASM_OP_END,
  });

  CHECK(loop_ranges.body.start_pc == 2);
  CHECK(loop_ranges.body.end_pc == 3);
  CHECK(opcodes_in(loop_ranges.body) == std::vector<Opcode_t> {
    WASM_OP_END,
  });
}

TEST_CASE("jit control frame range helpers require matching frame kinds") {
  WasmJit jit;
  std::vector<Instruction> instructions {
    block_instruction(WASM_OP_BLOCK),
    block_instruction(WASM_OP_LOOP),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
    instruction(WASM_OP_END),
  };
  jit.preparse(instructions);

  const auto &block_frame = jit.control_frame_at(0);
  const auto &loop_frame = jit.control_frame_at(1);

  CHECK_THROWS_AS(jit.loop_block_ranges(instructions, block_frame), std::runtime_error);
  CHECK_THROWS_AS(jit.block_ranges(instructions, loop_frame), std::runtime_error);
  CHECK_THROWS_AS(jit.if_block_ranges(instructions, block_frame), std::runtime_error);
}

TEST_CASE("vecmap supports small map lookup and iteration") {
  VecMap<int, std::string> map;
  auto [first_it, first_inserted] = map.insert({2, "two"});
  CHECK(first_inserted);
  CHECK(first_it->second == "two");

  auto [second_it, second_inserted] = map.insert({1, "one"});
  CHECK(second_inserted);
  CHECK(second_it->second == "one");
  CHECK(map.size() == 2);
  CHECK(map.contains(1));
  CHECK(!map.contains(3));
  CHECK(map.at(2) == "two");

  std::vector<int> keys;
  for(const auto &[key, value]: map) {
    keys.push_back(key);
  }
  CHECK(keys == std::vector<int> {2, 1});
}

TEST_CASE("vecmap updates existing keys without growing") {
  VecMap<int, std::string> map {{1, "one"}, {2, "two"}};

  auto [it, inserted] = map.insert_or_assign(1, "uno");

  CHECK(!inserted);
  CHECK(it->first == 1);
  CHECK(it->second == "uno");
  CHECK(map.size() == 2);
  CHECK(map.at(1) == "uno");
}

TEST_CASE("vecmap operator index inserts default values") {
  VecMap<int, int> map;

  map[4] += 7;
  map[4] += 5;

  CHECK(map.size() == 1);
  CHECK(map.at(4) == 12);
}

TEST_CASE("vecmap erase and missing key behavior") {
  VecMap<int, std::string> map {{1, "one"}, {2, "two"}};

  CHECK(map.erase(1));
  CHECK(!map.erase(3));
  CHECK(!map.contains(1));
  CHECK(map.contains(2));
  CHECK(map.size() == 1);
  CHECK_THROWS_AS(map.at(1), std::out_of_range);
}

TEST_CASE("jit prologue maps signature params to abi registers") {
  WasmModule mod;
  SigDecl sig;
  sig.params = {
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
  };
  mod.Sigs().push_back(sig);
  mod.Funcs().push_back(make_test_func(&mod.Sigs().back()));

  WasmJit jit(&mod);
  jit.emit_function_prologue(0);

  const auto &locals = jit.local_mappings();
  REQUIRE(locals.size() == 6);
  CHECK(locals.at(0).vreg.kind == VRegPhys);
  CHECK(locals.at(0).vreg.physreg == Xbyak::util::rdi);
  CHECK(locals.at(1).vreg.physreg == Xbyak::util::rsi);
  CHECK(locals.at(2).vreg.physreg == Xbyak::util::rdx);
  CHECK(locals.at(3).vreg.physreg == Xbyak::util::rcx);
  CHECK(locals.at(4).vreg.physreg == Xbyak::util::r8);
  CHECK(locals.at(5).vreg.physreg == Xbyak::util::r9);
}

TEST_CASE("jit prologue maps stack-passed params to positive rbp offsets") {
  WasmModule mod;
  SigDecl sig;
  sig.params = {
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
  };
  mod.Sigs().push_back(sig);
  mod.Funcs().push_back(make_test_func(&mod.Sigs().back()));

  WasmJit jit(&mod);
  jit.emit_function_prologue(0);

  const auto &locals = jit.local_mappings();
  REQUIRE(locals.size() == 8);
  CHECK(locals.at(6).vreg.kind == VRegSpilled);
  CHECK(locals.at(6).vreg.stack_offset == 16);
  CHECK(locals.at(7).vreg.kind == VRegSpilled);
  CHECK(locals.at(7).vreg.stack_offset == 24);
}

TEST_CASE("jit prologue maps pure locals to static frame slots") {
  WasmModule mod;
  SigDecl sig;
  sig.params = {WASM_TYPE_I32};
  mod.Sigs().push_back(sig);
  wasm_localcsv_t pure_locals {
    wasm_localcse_t {
      .count = 2,
      .type = WASM_TYPE_I32,
    },
  };
  mod.Funcs().push_back(make_test_func(&mod.Sigs().back(), pure_locals));

  WasmJit jit(&mod);
  jit.emit_function_prologue(0);

  const auto &locals = jit.local_mappings();
  REQUIRE(locals.size() == 3);
  CHECK(locals.at(0).vreg.kind == VRegPhys);
  CHECK(locals.at(0).vreg.physreg == Xbyak::util::rdi);
  CHECK(locals.at(1).vreg.kind == VRegSpilled);
  CHECK(locals.at(1).vreg.stack_offset == -8);
  CHECK(locals.at(1).ty == WASM_TYPE_I32);
  CHECK(locals.at(2).vreg.kind == VRegSpilled);
  CHECK(locals.at(2).vreg.stack_offset == -16);
  CHECK(locals.at(2).ty == WASM_TYPE_I32);
}

TEST_CASE("jit does not free registers that are mapped to locals") {
  WasmModule mod;
  SigDecl sig;
  sig.params = {WASM_TYPE_I32};
  mod.Sigs().push_back(sig);
  mod.Funcs().push_back(make_test_func(&mod.Sigs().back()));

  WasmJit jit(&mod);
  jit.emit_function_prologue(0);

  jit.emit_i32_const(1);
  jit.emit_if_block(
    [&]() {},
    [&]() {}
  );

  auto next = jit.produce_i32_reg();
  CHECK(next.physreg != Xbyak::util::rdi);
}

TEST_CASE("jit local.get pushes register-backed local values") {
  WasmModule mod;
  SigDecl sig;
  sig.params = {WASM_TYPE_I32};
  mod.Sigs().push_back(sig);
  mod.Funcs().push_back(make_test_func(&mod.Sigs().back()));

  WasmJit jit(&mod);
  jit.emit_function_prologue(0);
  jit.emit_local_get(0);

  auto value = jit.consume_i32_reg();
  CHECK(value.vreg.kind == VRegPhys);
  CHECK(value.vreg.physreg == Xbyak::util::rdi);
  CHECK(value.ty == WASM_TYPE_I32);
}

TEST_CASE("jit local.get pushes stack-passed parameter locations") {
  WasmModule mod;
  SigDecl sig;
  sig.params = {
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
    WASM_TYPE_I32,
  };
  mod.Sigs().push_back(sig);
  mod.Funcs().push_back(make_test_func(&mod.Sigs().back()));

  WasmJit jit(&mod);
  jit.emit_function_prologue(0);
  jit.emit_local_get(6);

  auto value = jit.consume_i32_reg();
  CHECK(value.vreg.kind == VRegSpilled);
  CHECK(value.vreg.stack_offset == 16);
  CHECK(value.ty == WASM_TYPE_I32);
}

TEST_CASE("jit local.get pushes pure local frame slots") {
  WasmModule mod;
  SigDecl sig;
  sig.params = {WASM_TYPE_I32};
  mod.Sigs().push_back(sig);
  wasm_localcsv_t pure_locals {
    wasm_localcse_t {
      .count = 1,
      .type = WASM_TYPE_I32,
    },
  };
  mod.Funcs().push_back(make_test_func(&mod.Sigs().back(), pure_locals));

  WasmJit jit(&mod);
  jit.emit_function_prologue(0);
  jit.emit_local_get(1);

  auto value = jit.consume_i32_reg();
  CHECK(value.vreg.kind == VRegSpilled);
  CHECK(value.vreg.stack_offset == -8);
  CHECK(value.ty == WASM_TYPE_I32);
}

TEST_CASE("jit local.get throws for unmapped locals") {
  WasmModule mod;
  SigDecl sig;
  mod.Sigs().push_back(sig);
  mod.Funcs().push_back(make_test_func(&mod.Sigs().back()));

  WasmJit jit(&mod);
  jit.emit_function_prologue(0);

  CHECK_THROWS_AS(jit.emit_local_get(0), std::runtime_error);
}
