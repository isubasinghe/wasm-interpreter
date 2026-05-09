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
  size_t end_pc = 0;
  size_t else_pc;
};

inline std::ostream& operator<<(std::ostream& os, const ControlFrame& frame) {
  switch(frame.kind) {
    case ControlFrameKind::Block:
      std::cout << "control block {\n\t" 
        << frame.start_pc << "\n\t"
        << frame.end_pc << "\n}";
      break;
    case ControlFrameKind::Loop:
      std::cout << "control loop {\n\t"
        << frame.start_pc << "\n\t"
        << frame.end_pc << "\n}";
      break;
    case ControlFrameKind::If:
      std::cout << "control if {\n\t"
        << frame.start_pc << "\n\t";
      if(frame.else_pc != 0) {
          std::cout << frame.else_pc << "\n\t";
      }
      std::cout << frame.end_pc << "\n}";
      break;
  }
  return os;
}

class StackFrame {
  private:
    size_t pc;
    std::stack<Immediate> stack;
  public:
    StackFrame();
    ~StackFrame();
    std::stack<Immediate> &operand_stack();
    size_t &operand_pc();
    friend class ControlHandler;
};

class ControlHandler {
  private:
    std::stack<StackFrame> frames;
  public:
    StackFrame &frame();
    void block_frame(ControlFrameKind kind);
    void if_frame();
    void loop_frame();
    std::vector<Immediate> enter_function(SigDecl *decl);

    ControlHandler();
    ~ControlHandler();
};

class Interpreter {
  private:
    ControlHandler handler;

    WasmModule mod;
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
    std::stack<Immediate> &stack();
    size_t &pc();
  public:

    Interpreter(WasmModule mod);
    ~Interpreter();
    std::vector<Instruction> parse_function(const FuncDecl *f);
    void run_function(int index, std::vector<Immediate> args);
    void run_main();
    void dump_stack();
};
