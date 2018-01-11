#pragma once
#include "parser.h"
#include "types.h"
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <functional>
#include <stack>
#include <list>

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

namespace nkqc {
	namespace codegen {
		struct internal_codegen_error : public runtime_error {
			internal_codegen_error(const string& m) : runtime_error(m) {}
		};
		struct no_such_function_error : public runtime_error {
			string selector;
			shared_ptr<type_id> reciever;
			vector<shared_ptr<type_id>> arguments;

			no_such_function_error(const string& m, const string& sel, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& arg)
				: runtime_error(m), selector(sel), reciever(rcv), arguments(arg) {}
		};

		struct code_generator : public typing_context {
			shared_ptr<llvm::Module> mod;

			//typedef unordered_map<string, pair<llvm::Value*, shared_ptr<type_id>>> expr_context;
			struct expr_context {
				typedef unordered_map<string, pair<llvm::Value*, shared_ptr<type_id>>> scope;
				list<scope> scopes;

				expr_context() : scopes{{}} {}

				class iterator {
					list<scope>::iterator cur_scope;
					list<scope>::iterator end_scope;
					scope::iterator cur_val;
				public:
					iterator(list<scope>::iterator cur, list<scope>::iterator end, scope::iterator curv)
						: cur_scope(cur), end_scope(end), cur_val(curv) {}
					bool operator==(const iterator& i) {
						return cur_scope == i.cur_scope && cur_val == i.cur_val;
					}
					bool operator!=(const iterator& i) { return !(*this == i); }
					void operator++() {
						if (cur_scope == end_scope) return;
						if (cur_val == cur_scope->end()) {
							cur_scope++;
							if (cur_scope == end_scope) return;
							cur_val = cur_scope->begin();
						} else cur_val++;
					}
					scope::value_type& operator *() { return *cur_val; }
					scope::value_type* operator ->() { return &*cur_val; }
					const scope::value_type& operator *() const { return *cur_val; }
					const scope::value_type& operator ->() const { return *cur_val; }
				};

				void insert_or_assign(const string& name, shared_ptr<type_id> type, llvm::Value* value = nullptr) {
					auto place = find(name);
					if (place != end()) {
						place->second = { value,type };
					}
					else {
						scopes.front()[name] = { value,type };
					}
				}
				pair<llvm::Value*, shared_ptr<type_id>>& at(const string& name) {
					return find(name)->second;
				}
				pair<llvm::Value*, shared_ptr<type_id>>& operator[](const string& name) {
					return scopes.front()[name];
				}
				iterator begin() { return iterator(scopes.begin(), scopes.end(), scopes.front().begin()); }
				iterator end() { return iterator(scopes.end(), scopes.end(), scopes.front().end()); }
				iterator find(const string& name) {
					for (auto scp = scopes.begin(); scp != scopes.end(); ++scp) {
						auto pv = scp->find(name);
						if (pv != scp->end()) return iterator(scp, scopes.end(), pv);
					}
					return end();
				}

				void push_scope() { scopes.push_front({}); }
				void pop_scope() { scopes.pop_front(); }
			};

			

			struct expr_generator;
			struct expr_typer;

			struct function {
				virtual void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) = 0;
				virtual bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) = 0;
				virtual shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) = 0;
				//virtual shared_ptr<type_id> func_type() = 0;
				virtual ~function() {};
			};

			struct type_record {
				shared_ptr<type_id> type;
				unordered_map<string, vector<shared_ptr<function>>> static_functions;
			};
			unordered_map<string, type_record> types;
			virtual shared_ptr<type_id> type_for_name(const string & name) const override {
				return types.at(name).type;
			}

			struct binary_llvm_op : public function {
				llvm::BinaryOperator::BinaryOps op;
				binary_llvm_op(llvm::BinaryOperator::BinaryOps op) : op(op) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override;

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override;

				shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args);
			};

			struct numeric_comp_op : public function {
				llvm::CmpInst::Predicate pred;
				bool floating;

				numeric_comp_op(llvm::CmpInst::Predicate pred, bool floating) : pred(pred), floating(floating) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t);

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override;

				shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args);
			};

			struct cast_op : public function {
				cast_op() {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override;

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override;

				shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args);
			};

			struct extern_fn : public function {
				llvm::Function* f;
				vector<pair<string, shared_ptr<type_id>>> args;
				shared_ptr<type_id> return_t;

				extern_fn(llvm::Function* f, vector<pair<string, shared_ptr<type_id>>> args,
					shared_ptr<type_id> rt) : f(f), args(args), return_t(rt) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override;

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& targs) override;

				shared_ptr<type_id> return_type(code_generator* gen, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& targs);
			};

			struct llvm_function : public function {
				llvm::Function* f;
				parser::fn_decl decl;

				llvm_function(const parser::fn_decl& d, llvm::Function* f) : decl(d), f(f) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override;

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override;

				shared_ptr<type_id> return_type(code_generator* gen, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override;
			};

			struct global_fn : public llvm_function {
				global_fn(const parser::fn_decl& d, llvm::Function* f) : llvm_function(d,f) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override {
					if (rcv != nullptr) throw internal_codegen_error("tried to apply a global function with a non-null reciever");
					llvm_function::apply(g, rcv, args, rcv_t, args_t);
				}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					return rcv == nullptr && llvm_function::can_apply(rcv, args);
				}

			};

			struct struct_initializer : public function {
				shared_ptr<struct_type> type;

				struct_initializer(shared_ptr<struct_type> t) :type(t) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override;

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override;
				

				shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args);
			};

			struct static_fn : public llvm_function {
				static_fn(const parser::fn_decl& d, llvm::Function* f) : llvm_function(d, f) {}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					return rcv->equals(this->decl.receiver) && llvm_function::can_apply(rcv, args);
				}
			};

			struct method : public llvm_function {

				method(const parser::fn_decl& d, llvm::Function* f) : llvm_function(d,f) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override;

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override;

				shared_ptr<type_id> return_type(code_generator* gen, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override;
			};

			unordered_map<string, vector<shared_ptr<function>>> functions;

			code_generator(shared_ptr<llvm::Module> mod);

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

				void visit(const nkqc::ast::id_expr &x) override;
				void visit(const nkqc::ast::string_expr &x) override;
				void visit(const nkqc::ast::number_expr &x) override;
				virtual void visit(const nkqc::ast::block_expr &x);
				virtual void visit(const nkqc::ast::symbol_expr &x);
				virtual void visit(const nkqc::ast::char_expr &x);
				virtual void visit(const nkqc::ast::array_expr &x);
				virtual void visit(const nkqc::ast::tag_expr &x);
				void visit(const nkqc::ast::seq_expr &x) override;
				void visit(const nkqc::ast::return_expr &x) override;
				virtual void visit(const nkqc::ast::unary_msgsnd &x);
				virtual void visit(const nkqc::ast::binary_msgsnd &x);
				virtual void visit(const nkqc::ast::keyword_msgsnd &x);
				virtual void visit(const nkqc::ast::cascade_msgsnd &x);
				virtual void visit(const nkqc::ast::assignment_expr &x);
			};
			shared_ptr<type_id> type_of(shared_ptr<ast::expr> expr, expr_context* cx) {
				expr_typer t{ this, cx };
				expr->visit(&t);
				return t.s.top()->resolve(this);
			}

			llvm::Type* type_of(shared_ptr<type_id> expr) {
				return expr->resolve(this)->llvm_type(mod->getContext());
			}

			struct expr_generator : public ast::expr_visiter<> {
				code_generator* gen;
				llvm::BasicBlock* bb;
				llvm::IRBuilder<> irb;
				expr_context* cx;
				stack<llvm::Value*> s;

				expr_generator(code_generator* gen, llvm::BasicBlock* bb, expr_context* cx)
					: gen(gen), bb(bb), cx(cx), irb(bb) {}

				void allocate() {
					auto a = irb.CreateAlloca(s.top()->getType(), nullptr, "var");
					irb.CreateStore(s.top(), a);
					s.pop();
					s.push(a);
				}

				virtual void visit(const nkqc::ast::id_expr &x) override;
				virtual void visit(const nkqc::ast::string_expr &x) override;
				virtual void visit(const nkqc::ast::number_expr &x) override;
				virtual void visit(const nkqc::ast::block_expr &x) override;
				virtual void visit(const nkqc::ast::symbol_expr &x) override;
				virtual void visit(const nkqc::ast::char_expr &x) override;
				virtual void visit(const nkqc::ast::array_expr &x) override;
				virtual void visit(const nkqc::ast::tag_expr &x) override;
				virtual void visit(const nkqc::ast::seq_expr &x) override;
				virtual void visit(const nkqc::ast::return_expr &x) override;
				virtual void visit(const nkqc::ast::unary_msgsnd &x) override;
				virtual void visit(const nkqc::ast::binary_msgsnd &x) override;
				virtual void visit(const nkqc::ast::keyword_msgsnd &x) override;
				virtual void visit(const nkqc::ast::cascade_msgsnd &x) override;
				virtual void visit(const nkqc::ast::assignment_expr &x) override;
			};
			void generate_expr(expr_context cx, shared_ptr<nkqc::ast::expr> expr, llvm::BasicBlock* block) {
				expr_generator xg{ this, block, &cx };
				expr->visit(&xg);
			}

			llvm::Function* define_function(nkqc::parser::fn_decl fn);

			void define_type(const string& name, shared_ptr<type_id> type);
		};
	}
}
