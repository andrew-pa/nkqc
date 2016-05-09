#include "parser.h"
#include <iostream>
using namespace std;

#include "vm.h"
#include "vm_codegen.h"
#include "vm_interpreter.h"

int main() {
	/*nkqc::parser::expr_parser xp;
	auto res = xp.parse(
	R"(
		true & false not & (nil isNil) ifFalse: [self halt].
		y := self size + super size.
		#($a #a "a" 1 1.0) do: [ :each | Transcript show: (each class name); show: ' '].
		^ x < y
	)");
	res->print(cout);*/

	nkqc::vm::codegen::context cx;

	cx.classes.push_back(nkqc::vm::stclass(0, cx.add_string("Object"), {}));
	cx.classes.push_back(nkqc::vm::stclass(0, cx.add_string("SmallInteger"), {
		{ cx.add_string("+"), nkqc::vm::stmethod(1, nkqc::vm::codegen::assemble(cx, R"(ldlc 0;ldlc 1;math +)")) }
	}));

	nkqc::parser::expr_parser xp; nkqc::vm::codegen::expr_emitter xm;
	xm.visit(cx, nkqc::vm::codegen::local_context{ 0 }, xp.parse(R"(1 + 2)"));
	
	//auto instr = nkqc::vm::codegen::assemble(cx, R"(push 0;crobj)");

	nkqc::vm::interpreter::vmcore vc;
	vc.strings = cx.strings;
	vc.classes = cx.classes;
	vc.small_integer_class_id = vc.find_class_by_name(vc.find_string("SmallInteger"));
	vc.run(cx.code);
}