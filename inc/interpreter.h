#pragma once
#include <stack>
#include <map>
#include <deque>
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
  size_t stack_height = 0;
  SigDecl *sig = nullptr;
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
    std::map<size_t, ControlFrame> pc_mappings;
    std::stack<ControlFrame> control_frames;
  public:
    StackFrame();
    ~StackFrame();
    std::stack<Immediate> &operand_stack();
    size_t &operand_pc();
    std::map<size_t, ControlFrame> &operand_pc_mappings();
    friend class ControlHandler;
    friend class Interpreter;
};

class ControlHandler {
  private:
    std::deque<StackFrame> frames;
  public:
    StackFrame &frame();
    size_t &pc();
    std::map<size_t, ControlFrame> &pc_mappings();
    void block_frame(ControlFrameKind kind);
    void if_frame();
    void loop_frame();
    std::vector<Immediate> enter_function(SigDecl *decl);

    ControlHandler();
    ~ControlHandler();
    friend class Interpreter;
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
    void handle_end();
    bool handle_br_if(Immediate imm);
    void handle_return(FuncDecl *f);
    void exit_function(FuncDecl *f);
    void handle_if_block(Immediate imm);
    void handle_else_block(Immediate imm);
    void handle_i32_eqz();
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
