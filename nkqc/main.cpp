#include "parser.h"
#include <iostream>
using namespace std;

#include "vm.h"
#include "vm_codegen.h"
#include "vm_interpreter.h"

//TODO: cmake build system
//TODO: finalize class/method defn syntax, file syntax
//TODO: make compiler frontend
//		make it so that it has a instant interp mode
//TODO: make VM frontend
//TODO: documention
//TODO: std library

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

	cx.classes.push_back(nkqc::vm::stclass(0, cx.add_string("Class"), 0, {}));
	cx.classes.push_back(nkqc::vm::stclass(0, cx.add_string("Object"), 0, {}));
	cx.classes.push_back(nkqc::vm::stclass(cx.add_string("Object"), cx.add_string("Method"), 0, {}));
	cx.classes.push_back(nkqc::vm::stclass(cx.add_string("Object"), cx.add_string("SmallInteger"), 0, {
		{ cx.add_string("+"), nkqc::vm::stmethod(1, nkqc::vm::codegen::assemble(cx, R"(ldlc 0;ldlc 1;math +)")) }
	}));

	nkqc::parser::expr_parser xp; nkqc::vm::codegen::expr_emitter xm;
	xm.visit(cx, nkqc::vm::codegen::local_context{ 0 }, xp.parse(R"(1 + 2)"));
	
	//auto instr = nkqc::vm::codegen::assemble(cx, R"(push 0;crobj)");

	nkqc::vm::image img{ cx.classes, cx.strings };

	nkqc::vm::interpreter::vmcore vc(img);
	vc.run(cx.code);
}