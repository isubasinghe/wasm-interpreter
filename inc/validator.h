#pragma once
#include "ir.h"
#include "wasmdefs.h"

struct BasicBlock {
  size_t stack_height;
  std::map<wasm_type_t, size_t> type_mappings;
  struct BasicBlock *children;
  int num_children;

  void add_count(wasm_type_t ty) {
    if(this->type_mappings.find(ty) == this->type_mappings.end()) {
      this->type_mappings[ty] = 0;
    }
    this->type_mappings[ty] = this->type_mappings[ty] + 1;
  }
};

#include "wasmdefs.h"
#include "ir.h"
class Validator {
  private:
    void validate_func(FuncDecl *f, const std::vector<Instruction> &instruction);
  public:
    Validator();
    ~Validator();
};
