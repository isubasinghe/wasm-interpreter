#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include "ir.h"

#include <doctest/doctest.h>

namespace {

WasmModule make_module() {
  WasmModule mod;
  mod.Sigs().push_back({});
  mod.Sigs().push_back({{WASM_TYPE_I32}, {WASM_TYPE_I32}});
  FuncDecl imported {};
  imported.sig = mod.getSig(0);
  mod.Funcs().push_back(imported);
  FuncDecl start = imported;
  start.code_bytes = {0x0b};
  mod.Funcs().push_back(start);
  FuncDecl entry {};
  entry.sig = mod.getSig(1);
  entry.pure_locals = {{2, WASM_TYPE_I32}};
  entry.num_pure_locals = 2;
  entry.code_bytes = {0x20, 0x00, 0x0b};
  mod.Funcs().push_back(entry);
  mod.Tables().push_back({.reftype = WASM_TYPE_FUNCREF, .limits = {.initial = 3}});
  mod.Mems().push_back({.limits = {.initial = 1}});
  mod.Globals().push_back({.type = WASM_TYPE_I32, .is_mutable = 0});

  auto &imports = mod.Imports();
  imports.num_funcs = imports.num_tables = imports.num_mems = imports.num_globals = 1;
  imports.list = {
    {"env", "f", KIND_FUNC, {.func = mod.getFunc(0)}},
    {"env", "t", KIND_TABLE, {.table = mod.getTable(0)}},
    {"env", "m", KIND_MEMORY, {.mem = mod.getMemory(0)}},
    {"env", "g", KIND_GLOBAL, {.global = mod.getGlobal(0)}},
  };
  mod.Exports() = {
    {"entry", KIND_FUNC, {.func = mod.getFunc(2)}},
    {"table", KIND_TABLE, {.table = mod.getTable(0)}},
    {"memory", KIND_MEMORY, {.mem = mod.getMemory(0)}},
    {"global", KIND_GLOBAL, {.global = mod.getGlobal(0)}},
  };
  mod.Elems().push_back({.flag = 0, .opcode_offset = WASM_OP_I32_CONST,
                        .table_offset = 0,
                        .func_indices = {mod.getFunc(0), mod.getFunc(2), mod.getFunc(2)}});
  mod.Datas().push_back({.flag = 0, .opcode_offset = WASM_OP_I32_CONST,
                        .mem_offset = 0, .mem = mod.getMemory(0), .bytes = {1, 2, 3}});
  mod.Datas().push_back({.flag = 1, .mem = nullptr, .bytes = {4, 5}});
  mod.Customs().push_back({.name = "metadata", .bytes = {6, 7}});

  // Set the private start-function and debug-name references through the decoder.
  const bytearr sections = {
    0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00,
    0x08, 0x01, 0x01, // start function 1
    0x00, 0x0f, 0x04, 'n', 'a', 'm', 'e',
    0x01, 0x08, 0x01, 0x02, 0x05, 'e', 'n', 't', 'r', 'y',
  };
  buffer_t buffer {sections.data(), sections.data(), sections.data() + sections.size()};
  mod.decode_buffer(buffer);
  mod.Customs().back().debug.subsections.push_back({.id = 0, .bytes = {8, 9}});
  return mod;
}

void check_owned_links(WasmModule &mod) {
  REQUIRE(mod.Funcs().size() == 3);
  REQUIRE(mod.Sigs().size() == 2);
  CHECK(mod.getFunc(0)->sig == mod.getSig(0));
  CHECK(mod.getFunc(1)->sig == mod.getSig(0));
  CHECK(mod.getFunc(2)->sig == mod.getSig(1));
  CHECK(mod.get_start_fn() == mod.getFunc(1));
  CHECK(mod.getFunc(2)->sig->params.front() == WASM_TYPE_I32);
  CHECK(mod.getFunc(2)->code_bytes == bytearr{0x20, 0x00, 0x0b});
  CHECK(mod.getFunc(2)->pure_locals.front().count == 2);

  REQUIRE(mod.Imports().list.size() == 4);
  auto import = mod.Imports().list.begin();
  CHECK((import++)->desc.func == mod.getFunc(0));
  CHECK((import++)->desc.table == mod.getTable(0));
  CHECK((import++)->desc.mem == mod.getMemory(0));
  CHECK(import->desc.global == mod.getGlobal(0));
  CHECK(mod.Imports().get_num_imports() == 4);

  REQUIRE(mod.Exports().size() == 4);
  auto exp = mod.Exports().begin();
  CHECK((exp++)->desc.func == mod.getFunc(2));
  CHECK((exp++)->desc.table == mod.getTable(0));
  CHECK((exp++)->desc.mem == mod.getMemory(0));
  CHECK(exp->desc.global == mod.getGlobal(0));

  REQUIRE(mod.Elems().size() == 1);
  auto func = mod.Elems().front().func_indices.begin();
  CHECK(*func++ == mod.getFunc(0));
  CHECK(*func++ == mod.getFunc(2));
  CHECK(*func == mod.getFunc(2));
  REQUIRE(mod.Datas().size() == 2);
  CHECK(mod.Datas().front().mem == mod.getMemory(0));
  CHECK(mod.Datas().front().bytes == bytearr{1, 2, 3});
  CHECK(mod.Datas().back().mem == nullptr);
  CHECK(mod.Datas().back().bytes == bytearr{4, 5});
  REQUIRE(mod.Customs().size() == 2);
  CHECK(mod.Customs().front().bytes == bytearr{6, 7});
  auto &debug = mod.Customs().back().debug;
  REQUIRE(debug.func_assoc.size() == 1);
  CHECK(debug.func_assoc.front().func == mod.getFunc(2));
  CHECK(debug.func_assoc.front().name == "entry");
  CHECK(debug.subsections.front().bytes == bytearr{8, 9});
}

} // namespace

TEST_CASE("module copies survive destruction of their source") {
  std::unique_ptr<WasmModule> copy;
  {
    WasmModule source = make_module();
    SUBCASE("copy constructor") {
      copy = std::make_unique<WasmModule>(source);
    }
    SUBCASE("assignment over an existing module") {
      copy = std::make_unique<WasmModule>(make_module());
      *copy = source;
    }
    CHECK(copy->getFunc(2)->sig != source.getFunc(2)->sig);
    CHECK(copy->get_start_fn() != source.get_start_fn());
  }
  check_owned_links(*copy);
  // Copying the copy also exercises the cached debug-name reference.
  WasmModule second(*copy);
  copy.reset();
  check_owned_links(second);
}

TEST_CASE("module copies own independent nested data") {
  WasmModule source = make_module();
  WasmModule copy(source);
  copy.getSig(1)->params.front() = WASM_TYPE_I64;
  copy.getFunc(2)->code_bytes.front() = 0x01;
  copy.getFunc(2)->pure_locals.front().count = 10;
  copy.getTable(0)->limits.initial = 20;
  copy.getMemory(0)->limits.initial = 30;
  copy.getGlobal(0)->type = WASM_TYPE_I64;
  copy.Datas().front().bytes.front() = 40;
  copy.Customs().front().bytes.front() = 50;
  copy.Customs().back().debug.func_assoc.front().name = "changed";
  copy.Customs().back().debug.subsections.front().bytes.front() = 60;
  check_owned_links(source);
  CHECK(source.getTable(0)->limits.initial == 3);
  CHECK(source.getMemory(0)->limits.initial == 1);
  CHECK(source.getGlobal(0)->type == WASM_TYPE_I32);
}

TEST_CASE("module self-assignment preserves all references") {
  WasmModule mod = make_module();
  const WasmModule &alias = mod;
  mod = alias;
  check_owned_links(mod);
}

TEST_CASE("empty modules and null references can be copied and assigned") {
  WasmModule empty;
  WasmModule copy(empty);
  CHECK(copy.get_start_fn() == nullptr);
  CHECK(copy.Imports().get_num_imports() == 0);
  copy = make_module();
  copy = empty;
  CHECK(copy.Funcs().empty());
  CHECK(copy.Customs().empty());
  WasmModule second(copy);
  CHECK(second.get_start_fn() == nullptr);
  FuncDecl func {};
  empty.Funcs().push_back(func);
  copy = empty;
  CHECK(copy.getFunc(0)->sig == nullptr);
}

TEST_CASE("foreign references fail assignment without changing the destination") {
  WasmModule destination = make_module();
  WasmModule source = make_module();
  SigDecl foreign;
  source.getFunc(2)->sig = &foreign;
  CHECK_THROWS_AS(destination = source, WasmValidationError);
  check_owned_links(destination);
}
