#pragma once
#include <stack>
#include <map>
#include "ir.h"

class Interpreter {
  private:
    std::stack<long long> scratch_pad;
    void push_func(FuncDecl f);
  public:

    std::vector<FuncDecl> functions;
    Interpreter(WasmModule mod);
    ~Interpreter();
    std::vector<Instruction> parse_function(const FuncDecl& f);
    void run_function(int index);
};
