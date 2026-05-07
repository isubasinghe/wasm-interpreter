#pragma once
#include <stack>
#include <map>
#include "ir.h"

enum class ControlFrameKind {
  Block,
  Loop,
  If
};

struct ControlFrame {
  ControlFrameKind kind;
  size_t start_pc;
  size_t end_pc;
  size_t else_pc;
};

class StackFrame {
};

class ControlHandler {
  private:
    size_t pc;
  public:
    void block_frame();
    void if_frame();
    void loop_frame();
    void enter_function();

    ControlHandler();
    ~ControlHandler();
};

class Interpreter {
  private:
    ControlHandler handler;

    WasmModule mod;
    std::stack<Immediate> scratch_pad;
    std::vector<TableDecl> tables;
    std::map<uint32_t, std::map<uint32_t, bool>> mapped;
    std::vector<FuncDecl> functions;
    void push_func(FuncDecl f);
    void add_table(TableDecl t);
    void handle_call(uint32_t fn_idx);
    void handle_indirect_call(Immediate imm);
    void handle_local_get(Immediate imm, std::vector<Immediate> args);
    void run_function(FuncDecl *f, std::vector<Immediate> args);
    void handle_f64_add();
    void handle_block(Immediate imm);
  public:

    Interpreter(WasmModule mod);
    ~Interpreter();
    std::vector<Instruction> parse_function(const FuncDecl *f);
    void run_function(int index, std::vector<Immediate> args);
    void run_main();
    void dump_stack();
};
