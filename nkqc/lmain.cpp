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
#include <llvm/ADT/APInt.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_ostream.h>

/*

struct Counter | {x i32} |

fn Counter increment [
	x := x + 1
]
fn Counter setTo: {v i32} [
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
	- error handling in parser
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

namespace nkqc {
	struct type_id {
		virtual llvm::Type* llvm_type(llvm::LLVMContext&) = 0;
		virtual ~type_id() {}
	};
	struct integer_type : public type_id {
		uint8_t bitwidth;
		bool signed_;

		integer_type(bool s, uint8_t bw)
			: signed_(s), bitwidth(bw) {}

		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) {
			return llvm::Type::getIntNTy(c, bitwidth);
		}
	};
	struct plain_type : public type_id {
		string name;
		plain_type(const string& n) : name(n) {}

		virtual llvm::Type* llvm_type(llvm::LLVMContext&) override {
			return nullptr;
		}
	};
	struct ptr_type : public type_id {
		shared_ptr<type_id> inner;
		ptr_type(shared_ptr<type_id> inner) : inner(inner) {}
		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) override {
			return inner->llvm_type(c)->getPointerTo();
		}
	};
	struct array_type : public type_id {
		size_t count;
		shared_ptr<type_id> element;
		array_type(size_t count, shared_ptr<type_id> e) : element(e), count(count) {}

		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) override {
			return llvm::ArrayType::get(element->llvm_type(c), count);
		}
	};

	namespace parser {
		struct parse_error {
			size_t line, col;
			size_t reason;
		};

		struct decl {
			virtual ~decl() {}
		};

		struct fn_decl {
			shared_ptr<type_id> receiver;
			string selector;
			vector<pair<string, shared_ptr<type_id>>> args;
			shared_ptr<nkqc::ast::expr> body;

			fn_decl(const string& sel, vector<pair<string, shared_ptr<type_id>>> args, shared_ptr<nkqc::ast::expr> body)
				: selector(sel), args(args), body(body) {}
			fn_decl(shared_ptr<type_id> rev, const string& sel, vector<pair<string, shared_ptr<type_id>>> args, shared_ptr<nkqc::ast::expr> body)
				: receiver(rev), selector(sel), args(args), body(body) {}
		};

		struct file_parser : public nkqc::parser::expr_parser {
			shared_ptr<type_id> parse_type() {
				switch (curr_char()) {
				case '*':
					next_char();
					return make_shared<ptr_type>(parse_type());
				case '[': {
					next_char();
					string numv;
					do {
						numv += curr_char();
						next_char();
					} while (more_token() && isdigit(curr_char()));
					assert(curr_char() == ']'); next_char();
					return make_shared<array_type>(atoll(numv.c_str()), parse_type());
				}
				case 'u':
				case 'i': {
					char type = curr_char();
					next_char();
					string numv;
					do {
						numv += curr_char();
						next_char();
					} while (more_token() && isdigit(curr_char()));
					return make_shared<integer_type>(type == 'i', atoi(numv.c_str()));
				}
				default:
					return make_shared<plain_type>(get_token());
				}
			}

			tuple<string, vector<pair<string, shared_ptr<type_id>>>> parse_sel() {
				string sel; vector<pair<string, shared_ptr<type_id>>> args;
				string t = peek_token(true);
				assert(t.size() > 0);
				if (t[t.size() - 1] == ':') {
					while (more_token()) {
						t = get_token(true);
						assert(t[t.size() - 1] == ':');
						sel += t;
						next_ws();
						assert(curr_char() == '{'); next_char();
						next_ws();
						auto n = get_token();
						next_ws();
						args.push_back({ n, parse_type() });
						next_ws();
						assert(curr_char() == '}'); next_char();
						next_ws();
					}
				}
				else {
					sel = get_token();
				}
				return { sel, args };
			}

			void parse_all(const string& s, function<void(const fn_decl&)> FN) {
				buf = s; idx = 0;
				while (more()) {
					auto t = get_token();
					next_ws();
					if (t == "fn") {
						//type_id pot_recv = parse_type();
						string sel; vector<pair<string,shared_ptr<type_id>>> args;
						tie(sel, args) = parse_sel();
						next_ws();
						FN(fn_decl(sel, args, _parse(false, false, false)));
					}
				}
				return;
			}
		};
	}

	namespace codegen {
		struct code_generator {
			shared_ptr<llvm::Module> mod;

			code_generator(shared_ptr<llvm::Module> mod)
				: mod(mod) {}

			llvm::Type* type_of(shared_ptr<ast::expr> expr) {
				return llvm::Type::getInt32Ty(mod->getContext());
			}

			llvm::Type* type_of(shared_ptr<type_id> expr) {
				return expr->llvm_type(mod->getContext());
			}

			llvm::FunctionType* type_of(const nkqc::parser::fn_decl& fn) {
				vector<llvm::Type*> params;
				for (const auto& arg : fn.args) {
					params.push_back(type_of(arg.second));
				}
				return llvm::FunctionType::get(type_of(fn.body), params, false);
			}

			typedef unordered_map<string, pair<llvm::Value*, shared_ptr<type_id>>> expr_context;

			struct expr_generator : public ast::expr_visiter<> {
				code_generator* gen;
				llvm::BasicBlock* bb;
				expr_context* cx;
				stack<llvm::Value*> s;

				expr_generator(code_generator* gen, llvm::BasicBlock* bb, expr_context* cx)
					: gen(gen), bb(bb), cx(cx) {}

				virtual void visit(const nkqc::ast::id_expr &x) override {
					s.push(cx->at(x.v).first);
				}
				virtual void visit(const nkqc::ast::string_expr &x) override {
				}
				virtual void visit(const nkqc::ast::number_expr &x) override {
					s.push(llvm::ConstantInt::get(llvm::Type::getInt32Ty(gen->mod->getContext()), x.iv));
				}
				virtual void visit(const nkqc::ast::block_expr &x) override {
				}
				virtual void visit(const nkqc::ast::symbol_expr &x) override {
				}
				virtual void visit(const nkqc::ast::char_expr &x) override {
				}
				virtual void visit(const nkqc::ast::array_expr &x) override {
				}
				virtual void visit(const nkqc::ast::tag_expr &x) override {
					cout << "tag " << x.v << endl;
				}
				virtual void visit(const nkqc::ast::seq_expr &x) override {
					x.first->visit(this);
					x.second->visit(this);
				}
				virtual void visit(const nkqc::ast::return_expr &x) override {
					x.val->visit(this);
					auto v = s.top(); s.pop();
					s.push(llvm::ReturnInst::Create(gen->mod->getContext(), v, bb));
				}
				virtual void visit(const nkqc::ast::unary_msgsnd &x) override {
				}
				virtual void visit(const nkqc::ast::binary_msgsnd &x) override {
				}
				virtual void visit(const nkqc::ast::keyword_msgsnd &x) override {
				}
				virtual void visit(const nkqc::ast::cascade_msgsnd &x) override {
				}
				virtual void visit(const nkqc::ast::assignment_expr &x) override {
				}
			};

			void generate_expr(expr_context cx, shared_ptr<nkqc::ast::expr> expr, llvm::BasicBlock* block) {
				expr_generator xg{ this, block, &cx };
				expr->visit(&xg);
			}

			llvm::Function* generate_function(const nkqc::parser::fn_decl& fn) {
				auto F = llvm::cast<llvm::Function>(mod->getOrInsertFunction(fn.selector, type_of(fn)));
				auto entry_block = llvm::BasicBlock::Create(mod->getContext(), "entry", F);
				expr_context cx;
				auto vals = F->arg_begin();
				for (const auto& arg : fn.args) {
					cx[arg.first] = pair<llvm::Value*, shared_ptr<type_id>>{ llvm::cast<llvm::Value>(&*vals), arg.second };
					vals++;
				}
				generate_expr(cx, dynamic_pointer_cast<ast::block_expr>(fn.body)->body, entry_block);
				return F;
			}
		};
	}
}


int main(int argc, char* argv[]) {
	vector<string> args; for (int i = 1; i < argc; i++) args.push_back(argv[i]);

	string s;
	ifstream input_file(args[0]);
	while (input_file) {
		string line; getline(input_file, line);
		s += line + "\n";
	}

	auto p = nkqc::parser::file_parser{};
	llvm::LLVMContext ctx;
	auto mod = make_shared<llvm::Module>(args[0], ctx);
	auto cg = nkqc::codegen::code_generator{mod};

	p.parse_all(s, [&] (const nkqc::parser::fn_decl& f) {
		cout << f.selector << " -> ";
		f.body->print(cout);
		cout << endl;
		cg.generate_function(f);
		llvm::outs() << *mod << "\n";
		getchar();
	});
}