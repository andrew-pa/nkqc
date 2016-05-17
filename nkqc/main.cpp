#include "parser.h"
#include <iostream>
#include <fstream>
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

int main(int argc, char* argv[]) {
	vector<string> args(argv[0], argv[argc]);
	
	string input_file_contents;
	{
		ifstream input_file(args[0]);
		while (input_file) {
			string line; getline(input_file, line);
			input_file_contents += line + "\n";
		}
	}
	nkqc::parser::preprocess(input_file_contents);

	nkqc::parser::class_parser cp{ input_file_contents };
	nkqc::vm::codegen::context cx;
	nkqc::vm::codegen::expr_emitter emx(&cx);
	while (cp.more()) {
		auto cls = cp.parse();
		vector<nkqc::vm::string_id_t> iv; 
		for (const auto& ivn : cls->instance_vars) iv.push_back(cx.add_string(ivn));
		map<nkqc::vm::string_id_t, nkqc::vm::stmethod> mth;
		map<nkqc::vm::string_id_t, nkqc::vm::stmethod> clsmth;
		for (const auto& md : cls->methods) {
			nkqc::vm::codegen::local_context lc{ cx.find_string(cls->name) };
			for (const auto& a : md.args) lc.alloc_local(a, 0);
			for (const auto& l : md.local_vars) lc.alloc_local(l, 0);
			emx.visit(lc, md.body);
			auto m = nkqc::vm::stmethod(md.args.size(), lc.code);
			if (md.mod == nkqc::ast::top_level::method_decl::modifier::static_)
				clsmth[cx.add_string(md.sel)] = m;
			else
				mth[cx.add_string(md.sel)] = m;
		}
		cx.classes.push_back(nkqc::vm::stclass(
			cx.add_string(cls->super_name+"Class"),
			cx.add_string(cls->name+"Class"),
			{}, clsmth));
		cx.classes.push_back(nkqc::vm::stclass(
			cx.add_string(cls->super_name),
			cx.add_string(cls->name),
			iv, mth));
	}



	/*{
		nkqc::parser::expr_parser xp;
	auto res = xp.parse(
		R"(
		true & false not & (nil isNil) ifFalse: [self halt].
		y := self size + super size.
		#($a #a "a" 1 1.0) do: [ :each | Transcript show: (each class name); show: ' '].
		^ x < y
	)");
	res->print(cout);}*/
	/*nkqc::vm::codegen::context cx;

	cx.classes.push_back(nkqc::vm::stclass(0, cx.add_string("Class"), {
		cx.add_string("name"), cx.add_string("super_name"), 
		cx.add_string("instance_variables"),
		cx.add_string("methods")
	}, {}));
	cx.classes.push_back(nkqc::vm::stclass(0, cx.add_string("Object"), {}, {}));
	cx.classes.push_back(nkqc::vm::stclass(cx.add_string("Object"), cx.add_string("Method"), {
		cx.add_string("sel"), cx.add_string("num_args"), cx.add_string("code_idx")
	}, {}));
	cx.classes.push_back(nkqc::vm::stclass(cx.add_string("Object"), cx.add_string("Array"), {
		cx.add_string("size")
	}, {}));
	cx.classes.push_back(nkqc::vm::stclass(cx.add_string("Object"), cx.add_string("SmallInteger"), {}, {
		{ cx.add_string("+"), nkqc::vm::stmethod(1, nkqc::vm::codegen::assemble(cx, R"(ldlc 0;ldlc 1;math +)")) }
	}));

	nkqc::parser::expr_parser xp; nkqc::vm::codegen::expr_emitter xm;
	xm.visit(cx, nkqc::vm::codegen::local_context{ 0 }, xp.parse(R"(1 + 2)"));
	
	//auto instr = nkqc::vm::codegen::assemble(cx, R"(push 0;crobj)");

	nkqc::vm::image img{ cx.classes, cx.strings };

	nkqc::vm::interpreter::vmcore vc(img);
	vc.run(cx.code);*/
}