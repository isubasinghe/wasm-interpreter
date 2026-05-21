#pragma once
#include <stack>
#include <map>
#include <memory>
#include <vector>
#include "ir.h"

namespace spdlog {
class logger;
}

enum class StepResult {
  Continue,
  Return,
  FunctionEnd
};


enum class ControlFrameKind {
  Block,
  Loop,
  If
};

struct ControlFrame {
  ControlFrameKind kind;
  size_t start_pc;
  size_t end_pc = 0;
  size_t else_pc = 0;
  size_t stack_height = 0;
  SigDecl sig;
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
    std::stack<StackFrame, std::vector<StackFrame>> frames;
  public:
    StackFrame &frame();
    size_t &pc();
    std::map<size_t, ControlFrame> &pc_mappings();
    std::vector<Immediate> enter_function(SigDecl *decl);
    ControlHandler();
    ~ControlHandler();
    friend class Interpreter;
};

class Interpreter {
  private:
    ControlHandler handler;
    std::shared_ptr<spdlog::logger> logger;

    WasmModule mod;
    std::vector<TableDecl> tables;
    std::map<uint32_t, std::map<uint32_t, bool>> mapped;
    std::vector<FuncDecl> functions;
    void push_func(FuncDecl f);
    void add_table(TableDecl t);
    void handle_call(uint32_t fn_idx);
    void handle_indirect_call(Immediate imm);
    void handle_local_get(Immediate imm, const std::vector<Immediate> &args);
    void handle_local_set(Immediate imm, std::vector<Immediate> &args);
    void run_function(FuncDecl *f, std::vector<Immediate> args);
    void handle_f64_add();
    void handle_block(Immediate imm);
    void handle_end();
    bool handle_br_if(Immediate imm);
    bool handle_br(Immediate imm);
    void handle_return(FuncDecl *f);
    void exit_function(FuncDecl *f);
    void handle_if_block(Immediate imm);
    void handle_else_block(Immediate imm);
    void handle_loop_block(Immediate imm);
    void handle_i32_eqz();
    void handle_i32_eq();
    void handle_i32_add();
    void handle_i32_sub();
    void enter_control_frame(ControlFrameKind kind, Immediate imm);
    StepResult run_step(FuncDecl *f, const std::vector<Instruction> &instructions, std::vector<Immediate> &args);
    std::stack<Immediate> &stack();
    size_t &pc();
  public:

    Interpreter(WasmModule mod, bool file_logging = false);
    ~Interpreter();
    std::vector<Instruction> parse_function(const FuncDecl *f);
    void run_function(int index, std::vector<Immediate> args);
    void run_main();
    void dump_stack();
};
