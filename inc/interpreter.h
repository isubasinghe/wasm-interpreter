#pragma once
#include <stack>
#include <map>
#include "ir.h"

class Interpreter {
  private:
    std::stack<Immediate> scratch_pad;
    void push_func(FuncDecl f);
    void handle_call(uint32_t fn_idx);
  public:

    std::vector<FuncDecl> functions;
    Interpreter(WasmModule mod);
    ~Interpreter();
    std::vector<Instruction> parse_function(const FuncDecl& f);
    void run_function(int index, std::list<Immediate> args);
    void dump_stack();
};
