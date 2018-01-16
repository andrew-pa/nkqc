#include "parser.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <functional>
#include <stack>

#include <llvm/IR/Type.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/ADT/APInt.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

/*

struct Counter | {x i32} |

fn {Counter} increment [
	x := x + 1
]
fn {Counter} setTo: {v i32} [
	x := v
]

fn print: {v i8*} times: {n i32} [
	n repeat: [
		! print: v
	]
]

fn mul: {x f32} and: {y f32} add: {z f32} [
	sum := x + y.
	^ sum * z
]

fn square: {x i32} [
	^ x * x
]

fn main [
	^ 1 + ! square: 2
]

*/

/*
<decl> := <fndecl> | <structdecl>
<type> := ('u'|'i'|'f')<bitwidth> | '*'<type> | '['<number>']'<type> | <name>
<var_decl> := '{' <name> <type> '}'
<fn_sel_decl> := (<sel_part> <var_decl>?)
<fndecl> := 'fn' <fn_sel_decl> <expr:block> | 'fn' <name> <expr:block> | 'fn' <type> <fn_sel_decl> <expr:block>
<structdecl> := 'struct' <name> '|' <var_decl>+ '|'
*/

/*
	- mapping of typename -> type
	- struct initializer method
	- static functions
	- methods
*/

template<typename T, typename E>
struct result {
	char type;
	union {
		T value;
		E error;
	};

	result(const T& v) : type(0), value(v) {}
	result(const E& e) : type(1), error(e) {}
};

#include "types.h"

#include "llvm_codegen.h"

int main(int argc, char* argv[]) {
	vector<string> args; for (int i = 1; i < argc; i++) args.push_back(argv[i]);

	llvm::LLVMContext ctx;
	auto mod = make_shared<llvm::Module>(args[0], ctx);
	try {

		string s;
		ifstream input_file(args[0]);
		while (input_file) {
			string line; getline(input_file, line);
			s += line + "\n";
		}
		auto p = nkqc::parser::file_parser{};
		auto cg = nkqc::codegen::code_generator{ mod };

		p.parse_all(s, [&](const nkqc::parser::fn_decl& f) {
			cout << f.selector << " -> ";
			f.body->print(cout);
			cout << endl;
			cg.define_function(f);
		}, [&](const string& name, shared_ptr<nkqc::type_id> structure) {
			cg.define_type(name, structure);
		});
		llvm::outs() << *mod << "\n";
	} catch (const nkqc::parser::parse_error& e) {
		cout << "error parsing at line " << e.line << ", column " << e.col << ": " << e.what() << endl;
		return 1;
	} catch (const nkqc::codegen::internal_codegen_error& e) {
		cout << "internal error: " << e.what() << endl;
		return 1;
	} catch (const nkqc::codegen::type_mismatch_error& e) {
		cout << "error: type mismatch types: ";
		e.a->print(cout);
		cout << ", ";
		e.b->print(cout);
		cout << "; " << e.what() << endl;
		return 1;
	} catch (const nkqc::codegen::no_such_function_error& e) {
		cout << "error: no such function " << e.selector << endl;
		if (e.reciever != nullptr) {
			cout << "\twith receiver: ";
			e.reciever->print(cout);
			cout << endl;
		}
		if (e.arguments.size() > 0) {
			cout << "\tand arguments: ";
			for (const auto& t : e.arguments) {
				t->print(cout);
				cout << " ";
			}
			cout << endl;
		}
		cout << "\textra: " << e.what() << endl;
		return 1;
	}
	//getchar();

	llvm::InitializeAllTargetInfos();
	llvm::InitializeAllTargets();
	llvm::InitializeAllTargetMCs();
	llvm::InitializeAllAsmParsers();
	llvm::InitializeAllAsmPrinters();

	auto targ_trip = llvm::sys::getDefaultTargetTriple();
	cout << "target triple: " << targ_trip << endl;
	mod->setTargetTriple(targ_trip);
	string err;
	auto targ = llvm::TargetRegistry::lookupTarget(targ_trip, err);
	auto mach = targ->createTargetMachine(targ_trip, "generic", "", llvm::TargetOptions{}, llvm::Optional<llvm::Reloc::Model>{});
	mod->setDataLayout(mach->createDataLayout());
	error_code ec;
	llvm::raw_fd_ostream d(args[0] + ".o", ec, llvm::sys::fs::OpenFlags{});
	llvm::legacy::PassManager pass;
	mach->addPassesToEmitFile(pass, d, llvm::TargetMachine::CGFT_ObjectFile);
	pass.run(*mod.get());
	d.flush();
}
