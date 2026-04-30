#pragma once
#include <iosfwd>
#include "ir.h"

std::ostream& operator<<(std::ostream& os, const wasm_limits_t& l);
std::ostream& operator<<(std::ostream& os, const SigDecl& sig);
std::ostream& operator<<(std::ostream& os, const FuncDecl& f);
std::ostream& operator<<(std::ostream& os, const GlobalDecl& g);
std::ostream& operator<<(std::ostream& os, const TableDecl& t);
std::ostream& operator<<(std::ostream& os, const MemoryDecl& m);
std::ostream& operator<<(std::ostream& os, const ElemDecl& e);
std::ostream& operator<<(std::ostream& os, const DataDecl& d);
std::ostream& operator<<(std::ostream& os, const ImportDecl& i);
std::ostream& operator<<(std::ostream& os, const ExportDecl& e);
std::ostream& operator<<(std::ostream& os, const CustomDecl& c);
std::ostream& operator<<(std::ostream& os, WasmModule& mod);

void dump_module(WasmModule &mod);
