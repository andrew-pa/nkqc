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
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>

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
		virtual bool equals(shared_ptr<type_id> o) = 0; // welcome to Java-land
		virtual ~type_id() {}
	};
	/*struct tuple_type : public type_id {
		vector<shared_ptr<type_id>> types;

		tuple_type(initializer_list<shared_ptr<type_id>> t) : types(t.begin(), t.end()) {}
		tuple_type(const vector<shared_ptr<type_id>>& t) : types(t) {}
		
		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) {
			return types.size() == 0 ? llvm::Type::getVoidTy() : llvm::StructType::create(types);
		}

	};*/
	struct unit_type : public type_id {
		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) override {
			return llvm::Type::getVoidTy(c);
		}
		virtual bool equals(shared_ptr<type_id> o) override {
			return dynamic_pointer_cast<unit_type>(o) != nullptr;
		}
	};
	struct integer_type : public type_id {
		uint8_t bitwidth;
		bool signed_;

		integer_type(bool s, uint8_t bw)
			: signed_(s), bitwidth(bw) {}

		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) {
			return llvm::Type::getIntNTy(c, bitwidth);
		}

		virtual bool equals(shared_ptr<type_id> o) override {
			auto p = dynamic_pointer_cast<integer_type>(o);
			return p != nullptr && p->bitwidth == bitwidth && p->signed_ == signed_;
		}
	};
	struct plain_type : public type_id {
		string name;
		plain_type(const string& n) : name(n) {}

		virtual llvm::Type* llvm_type(llvm::LLVMContext&) override {
			return nullptr;
		}
		virtual bool equals(shared_ptr<type_id> o) override {
			auto p = dynamic_pointer_cast<plain_type>(o);
			return p != nullptr && p->name == name;
		}
	};
	struct ptr_type : public type_id {
		shared_ptr<type_id> inner;
		ptr_type(shared_ptr<type_id> inner) : inner(inner) {}
		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) override {
			return inner->llvm_type(c)->getPointerTo();
		}

		virtual bool equals(shared_ptr<type_id> o) override {
			auto p = dynamic_pointer_cast<ptr_type>(o);
			return p != nullptr && inner->equals(p->inner);
		}
	};
	struct array_type : public type_id {
		size_t count;
		shared_ptr<type_id> element;
		array_type(size_t count, shared_ptr<type_id> e) : element(e), count(count) {}

		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) override {
			return llvm::ArrayType::get(element->llvm_type(c), count);
		}

		virtual bool equals(shared_ptr<type_id> o) override {
			auto p = dynamic_pointer_cast<array_type>(o);
			return p != nullptr && count == p->count && element->equals(p->element);
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
				case '(': {
					next_char(); assert(curr_char() == ')');
					next_char();
					return make_shared<unit_type>();
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
					next_ws();
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

			shared_ptr<type_id> parse_type(const string& s) {
				buf = s; idx = 0;
				return parse_type();
			}
		};
	}

	namespace codegen {
		struct code_generator {
			shared_ptr<llvm::Module> mod;

			typedef unordered_map<string, pair<llvm::Value*, shared_ptr<type_id>>> expr_context;
			struct expr_generator;
			struct expr_typer;

			struct function {
				virtual void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) = 0;
				virtual bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) = 0;
				virtual shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) = 0;
				//virtual shared_ptr<type_id> func_type() = 0;
				virtual ~function() {};
			};

			struct binary_llvm_op : public function {
				llvm::BinaryOperator::BinaryOps op;
				binary_llvm_op(llvm::BinaryOperator::BinaryOps op) : op(op) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override {
					g->s.push(llvm::BinaryOperator::Create(op, rcv, args[0], "", g->bb));
				}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					if (args.size() != 1) return false;
					auto rcv_int = dynamic_pointer_cast<integer_type>(rcv);
					auto arg_int = dynamic_pointer_cast<integer_type>(args[0]);
					return rcv_int != nullptr && arg_int != nullptr && rcv_int->bitwidth == arg_int->bitwidth && rcv_int->signed_ == arg_int->signed_;
				}

				shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
					if (!can_apply(rcv, args)) throw;
					return rcv;
				}
			};

			struct integer_cast_op : public function {
				integer_cast_op() {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t,const vector<shared_ptr<type_id>>& args_t) override {
					g->s.push(llvm::BinaryOperator::Create(op, rcv, args[0], "", g->bb));
				}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					if (args.size() != 1) return false;
					auto rcv_int = dynamic_pointer_cast<integer_type>(rcv);
					auto arg_int = dynamic_pointer_cast<integer_type>(args[0]);
					return rcv_int != nullptr && arg_int != nullptr;
				}

				shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
					if (!can_apply(rcv, args)) throw;
					return args[0];
				}
			};
			
			struct extern_fn : public function {
				llvm::Function* f;
				vector<pair<string, shared_ptr<type_id>>> args;
				shared_ptr<type_id> return_t;

				extern_fn(llvm::Function* f, vector<pair<string, shared_ptr<type_id>>> args, 
					shared_ptr<type_id> rt) : f(f), args(args), return_t(rt) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override {
					if (rcv != nullptr) throw;
					g->s.push(llvm::CallInst::Create(f, args, "", g->bb));
				}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& targs) override {
					if (rcv != nullptr) return false;
					if (targs.size() != args.size()) return false;
					for (int i = 0; i < args.size(); ++i) {
						if (!targs[i]->equals(args[i].second)) return false;
					}
					return true;
				}

				shared_ptr<type_id> return_type(code_generator* gen, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& targs) {
					if (!can_apply(rcv, targs)) throw;
					return return_t;
				}
			};

			struct global_fn : public function {
				llvm::Function* f;
				parser::fn_decl decl;

				global_fn(parser::fn_decl d, llvm::Function* f) : decl(d), f(f) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override {
					if (rcv != nullptr) throw;
					g->s.push(llvm::CallInst::Create(f, args, "", g->bb));
				}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					if (rcv != nullptr) return false;
					if (args.size() != decl.args.size()) return false;
					for (int i = 0; i < args.size(); ++i) {
						if (!args[i]->equals(decl.args[i].second)) return false;
					}
					return true;
				}

				shared_ptr<type_id> return_type(code_generator* gen, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
					if (!can_apply(rcv, args)) throw;
					expr_context fncx;
					expr_typer ty{ gen, &fncx };
					for (int i = 0; i < decl.args.size(); ++i) {
						ty.cx->insert_or_assign(decl.args[i].first, pair<llvm::Value*, shared_ptr<type_id>>{ nullptr, args[i] });
					}
					ty.visit(decl.body);
					auto v = ty.s.top(); ty.s.pop();
					return v;
				}
			};


			unordered_map<string, vector<shared_ptr<function>>> functions;

			code_generator(shared_ptr<llvm::Module> mod)
				: mod(mod) {
				functions["+"].push_back(make_shared<binary_llvm_op>(llvm::BinaryOperator::BinaryOps::Add));
				functions["*"].push_back(make_shared<binary_llvm_op>(llvm::BinaryOperator::BinaryOps::Mul));
				functions["-"].push_back(make_shared<binary_llvm_op>(llvm::BinaryOperator::BinaryOps::Sub));
				functions["/"].push_back(make_shared<binary_llvm_op>(llvm::BinaryOperator::BinaryOps::SDiv));
			}


			shared_ptr<function> lookup_function(const string& sel, shared_ptr<type_id> recv, const vector<shared_ptr<type_id>>& args) {
				auto fs = functions.find(sel);
				if (fs == functions.end()) return nullptr;
				for (auto& f : fs->second) {
					if (f->can_apply(recv, args)) return f;
				}
				return nullptr;
			}

			struct expr_typer : public ast::expr_visiter<> {
				stack<shared_ptr<type_id>> s;
				code_generator* gen;
				expr_context* cx;

				expr_typer(code_generator* g, expr_context* cx) : gen(g), cx(cx) {}

				void visit(const nkqc::ast::id_expr &x) override {
					s.push(cx->at(x.v).second);
				}
				void visit(const nkqc::ast::string_expr &x) override {
					s.push(make_shared<array_type>(x.v.size(), make_shared<integer_type>(false, 8)));
				}
				void visit(const nkqc::ast::number_expr &x) override {
					if (x.type == 'i') {
						s.push(make_shared<integer_type>(true, 32));
					}
					else throw;
				}
				virtual void visit(const nkqc::ast::block_expr &x) {
					x.body->visit(this);
				}
				virtual void visit(const nkqc::ast::symbol_expr &x) {
				}
				virtual void visit(const nkqc::ast::char_expr &x) {
				}
				virtual void visit(const nkqc::ast::array_expr &x) {
				}
				virtual void visit(const nkqc::ast::tag_expr &x) {
				}
				void visit(const nkqc::ast::seq_expr &x) override {
					x.first->visit(this);
					s.pop(); // discard type of first expression, evaluate it only for side effects
					x.second->visit(this);
				}
				void visit(const nkqc::ast::return_expr &x) override {
					x.val->visit(this);
				}
				virtual void visit(const nkqc::ast::unary_msgsnd &x) {
				}
				virtual void visit(const nkqc::ast::binary_msgsnd &x) {
					auto rcv = gen->type_of(x.rcv, cx), rhs = gen->type_of(x.rhs, cx);
					auto sf = gen->lookup_function(x.op, rcv, { rhs });
					if (sf != nullptr)
						s.push(sf->return_type(gen, rcv, { rhs }));
					else
						throw;
				}
				virtual void visit(const nkqc::ast::keyword_msgsnd &x) {
					auto glob = dynamic_pointer_cast<nkqc::ast::symbol_expr>(x.rcv);
					if (glob != nullptr && glob->v == "G")
						s.push(nullptr);
					else {
						x.rcv->visit(this);
					}
					auto rcv_t = s.top(); s.pop();
					vector<shared_ptr<type_id>> arg_t;
					for (const auto& arg : x.args) {
						arg->visit(this);
						arg_t.push_back(s.top()); s.pop();
					}
					auto f = gen->lookup_function(x.msgname, rcv_t, arg_t);
					if (f != nullptr) {
						s.push(f->return_type(gen, rcv_t, arg_t));
					}
					else throw;
				}
				virtual void visit(const nkqc::ast::cascade_msgsnd &x) {
				}
				virtual void visit(const nkqc::ast::assignment_expr &x) {
					x.val->visit(this);
					cx->insert_or_assign(x.name, pair<llvm::Value*, shared_ptr<type_id>>{ nullptr, s.top() });
					s.pop();
					s.push(make_shared<unit_type>());
				}
			};

			shared_ptr<type_id> type_of(shared_ptr<ast::expr> expr, expr_context* cx) {
				expr_typer t{ this, cx };
				expr->visit(&t);
				return t.s.top();
			}

			llvm::Type* type_of(shared_ptr<type_id> expr) {
				return expr->llvm_type(mod->getContext());
			}

			llvm::FunctionType* type_of(const nkqc::parser::fn_decl& fn, expr_context* cx) {
				vector<llvm::Type*> params;
				for (const auto& arg : fn.args) {
					params.push_back(type_of(arg.second));
				}
				return llvm::FunctionType::get(type_of(fn.body, cx)->llvm_type(mod->getContext()), params, false);
			}


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
					auto lhs_type = gen->type_of(x.rcv, cx), rhs_type = gen->type_of(x.rhs, cx);
					auto f = gen->lookup_function(x.op, lhs_type, { rhs_type });
					if (f != nullptr) {
						//	apply function
						x.rcv->visit(this);
						auto rcv = s.top(); s.pop();
						x.rhs->visit(this);
						auto rhs = s.top(); s.pop();
						f->apply(this, rcv, { rhs });
					} else {
						throw;
					}
				}
				virtual void visit(const nkqc::ast::keyword_msgsnd &x) override {
					auto glob = dynamic_pointer_cast<nkqc::ast::symbol_expr>(x.rcv);
					shared_ptr<type_id> rcv_t;
					if (glob != nullptr && glob->v == "G")
						s.push(nullptr);
					else {
						rcv_t = gen->type_of(x.rcv, cx);
						x.rcv->visit(this);
					}
					auto rcv = s.top(); s.pop();
					vector<llvm::Value*> args; vector<shared_ptr<type_id>> arg_t;
					for (const auto& arg : x.args) {
						arg->visit(this);
						args.push_back(s.top()); s.pop();
						arg_t.push_back(gen->type_of(arg,cx));
					}
					auto f = gen->lookup_function(x.msgname, rcv_t, arg_t);
					if (f != nullptr) {
						f->apply(this, rcv, args);
					}
					else throw;
				}
				virtual void visit(const nkqc::ast::cascade_msgsnd &x) override {
				}
				virtual void visit(const nkqc::ast::assignment_expr &x) override {
					x.val->visit(this);
					cx->insert_or_assign(x.name, pair<llvm::Value*, shared_ptr<type_id>>{ s.top(), gen->type_of(x.val, cx) });
					s.pop();
				}
			};

			void generate_expr(expr_context cx, shared_ptr<nkqc::ast::expr> expr, llvm::BasicBlock* block) {
				expr_generator xg{ this, block, &cx };
				expr->visit(&xg);
			}

			llvm::Function* define_function(const nkqc::parser::fn_decl& fn) {
				expr_context cx;
				for (const auto& arg : fn.args) {
					cx[arg.first] = pair<llvm::Value*, shared_ptr<type_id>>{ nullptr, arg.second };
				}
				auto cfn = dynamic_pointer_cast<ast::array_expr>(fn.body);
				if(cfn != nullptr) {
					vector<llvm::Type*> params;
					for (const auto& arg : fn.args) {
						params.push_back(type_of(arg.second));
					}
					parser::file_parser fp;
					auto ret_t = fp.parse_type(dynamic_pointer_cast<ast::tag_expr>(cfn->vs[1])->v);
					auto fn_t = llvm::FunctionType::get(ret_t->llvm_type(mod->getContext()), params, false);
					auto name = dynamic_pointer_cast<ast::symbol_expr>(cfn->vs[0])->v;
					auto F = //llvm::cast<llvm::Function>(
						//mod->getOrInsertFunction(, fn_t));
						llvm::Function::Create(fn_t, llvm::Function::ExternalLinkage, name, mod.get());
					functions[fn.selector].push_back(make_shared<extern_fn>(F, fn.args, ret_t));
				}
				else {
					auto F = llvm::cast<llvm::Function>(mod->getOrInsertFunction(fn.selector, type_of(fn, &cx)));
					auto entry_block = llvm::BasicBlock::Create(mod->getContext(), "entry", F);
					auto vals = F->arg_begin();
					for (const auto& arg : fn.args) {
						cx[arg.first].first = llvm::cast<llvm::Value>(&*vals);
						vals++;
					}
					generate_expr(cx, dynamic_pointer_cast<ast::block_expr>(fn.body)->body, entry_block);
					functions[fn.selector].push_back(make_shared<global_fn>(fn, F));
					return F;
				}
			}
		};
	}
}


int main(int argc, char* argv[]) {
	vector<string> args; for (int i = 1; i < argc; i++) args.push_back(argv[i]);

	putc(65, __acrt_iob_func(1));
	//fwrite("hello, world!", sizeof(char), sizeof(char) * 14, stdout);

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
		cg.define_function(f);
		cout << endl;
	});
	llvm::outs() << *mod << "\n";
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
