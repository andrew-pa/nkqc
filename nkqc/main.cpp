#include "parser.h"
#include <iostream>
#include <fstream>
using namespace std;

#include "vm.h"
#include "vm_codegen.h"
#include "vm_interpreter.h"

//TODO: cmake build system
//TODO: finalize class/method defn syntax, file syntax & document
//TODO: make compiler frontend
//		make it so that it has a instant interp mode
//TODO: make VM frontend
//TODO: documention
//TODO: std library

nkqc::vm::codegen::context compile_file(const string& contents) {
	auto input_file_contents = nkqc::parser::preprocess(contents);

	nkqc::parser::class_parser cp{ input_file_contents };
	nkqc::vm::codegen::context cx;
	nkqc::vm::codegen::expr_emitter emx(&cx);
	while (cp.more()) {
		auto cls = cp.parse();
		vector<nkqc::vm::string_id_t> iv;
		for (const auto& ivn : cls->instance_vars) iv.push_back(cx.add_string(ivn));
		map<nkqc::vm::string_id_t, nkqc::vm::stmethod> mth;
		map<nkqc::vm::string_id_t, nkqc::vm::stmethod> clsmth;
		auto cc = cx.classes.size();
		cx.classes.push_back(nkqc::vm::stclass(
			cls->super_name == "nil" ? -1 : cx.add_string(cls->super_name + "Class"),
			cx.add_string(cls->name + "Class"),
			{}, {}, nkqc::vm::stclass::flags::meta_class));
		auto c = cx.classes.size();
		cx.classes.push_back(nkqc::vm::stclass(
			cls->super_name == "nil" ? -1 : cx.add_string(cls->super_name),
			cx.add_string(cls->name),
			iv, {}));
		for (const auto& md : cls->methods) {
			nkqc::vm::codegen::local_context lc{ cx.add_string(cls->name) };
			for (const auto& a : md.args) lc.alloc_local(a, 0);
			for (const auto& l : md.local_vars) lc.alloc_local(l, 0);
			emx.visit(lc, md.body);
			auto m = nkqc::vm::stmethod(
				md.mod == nkqc::ast::top_level::method_decl::modifier::varadic ? -1 : md.args.size(), lc.code);
			if (md.mod == nkqc::ast::top_level::method_decl::modifier::static_)
				clsmth[cx.add_string(md.sel)] = m;
			else
				mth[cx.add_string(md.sel)] = m;
		}
		cx.classes[cc].methods = clsmth;
		cx.classes[c].methods = mth; //silly hax to ensure that the class is there while the methods are codegen'd 
	}
	return cx;
}

string read_file(const string& path) {
	string s;
	ifstream input_file(path);
	while (input_file) {
		string line; getline(input_file, line);
		s += line + "\n";
	}
	return s;
}


int main(int argc, char* argv[]) {
	vector<string> args; for (int i = 1; i < argc; i++) args.push_back(argv[i]);

	nkqc::vm::codegen::context cx =
		compile_file(read_file(args[0]));

	for (auto& c : cx.classes) {
		if (c.name == cx.find_string("ObjectClass")) {
			c.super = cx.find_string("Class");
		}
	}

	/*for (const auto& c : cx.classes) {
		cout << (c.name > 0 ? cx.strings[c.name] : "nil") << " : " << (c.super > 0 ? cx.strings[c.super] : "nil") << endl;
	}*/

	nkqc::vm::image img{ cx.classes, cx.strings, cx.blocks };

	nkqc::vm::interpreter::vmcore vc(img);
	auto instr = nkqc::vm::codegen::assemble(cx, R"(clsnmd !Program;sndmsg !run)");
	vc.run(instr);

	if(!vc.stk.empty()) cout << "run returned " << vc.stk.top().intval << endl;

	getchar();
	return 0;
	

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
