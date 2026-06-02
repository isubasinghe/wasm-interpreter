#include "jit.h"
#include "xbyak/xbyak.h"

using namespace Xbyak;

struct Add : Xbyak::CodeGenerator {
	Add()
	{
    mov(rax, rdi);
    add(rax, rsi);
    ret();
	}
};

void x() {
  Add c;
  auto f = c.getCode<int (*)()>();
  f();
}
