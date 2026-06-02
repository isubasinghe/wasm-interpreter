#include <stack>
#include <map>
#include <memory>
#include <vector>

#include "validator.h"


Validator::Validator() {
}



void Validator::validate_func(FuncDecl *f, const std::vector<Instruction> &instructions) {
  for(auto pc = 0; pc < instructions.size(); pc++) {
  }
  
}

Validator::~Validator() {
}
