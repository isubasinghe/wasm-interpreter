#pragma once

#include "ir.h"

#include <cstddef>
#include <vector>

Opcode_t read_opcode(const bytearr& code, size_t& pos);
void parse_immediate(Immediate& imm, const bytearr& code, size_t& pos);
std::vector<Instruction> parse_function(const FuncDecl *f);
