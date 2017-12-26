#pragma once
#include "parser.h"
#include "types.h"
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

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override {
					g->s.push(g->irb.CreateBinOp(op, rcv, args[0]));
				}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					if (args.size() != 1) return false;
					auto rcv_int = dynamic_pointer_cast<integer_type>(rcv);
					auto arg_int = dynamic_pointer_cast<integer_type>(args[0]);
					return rcv_int != nullptr && arg_int != nullptr && rcv_int->bitwidth == arg_int->bitwidth && rcv_int->signed_ == arg_int->signed_;
				}

				shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
					if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
					return rcv;
				}
			};

			struct cast_op : public function {
				cast_op() {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override {
					if (rcv != nullptr) throw internal_codegen_error("tried to apply cast_op with a non-null reciever");
					g->s.push(args_t[0]->cast_to(g->gen->mod->getContext(), rcv_t, args[0], g->irb));
				}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					return args.size() == 1 && args[0]->can_cast_to(rcv);
				}

				shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
					if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
					return rcv;
				}
			};

			struct extern_fn : public function {
				llvm::Function* f;
				vector<pair<string, shared_ptr<type_id>>> args;
				shared_ptr<type_id> return_t;

				extern_fn(llvm::Function* f, vector<pair<string, shared_ptr<type_id>>> args,
					shared_ptr<type_id> rt) : f(f), args(args), return_t(rt) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override {
					if (rcv != nullptr) throw internal_codegen_error("tried to apply an external function with a non-null reciever");
					for (int i = 0; i < args.size(); ++i) {
						auto v = args[i];
						v->getType()->print(llvm::outs(), true);
						llvm::outs() << ",";
						llvm::outs().flush();
						if (v->getType() != args_t[i]->llvm_type(g->gen->mod->getContext())) {
							throw internal_codegen_error("tried to apply external function and found values that had types that did not match given argument types");
						}
					}
					llvm::outs() << "\n";
					g->s.push(g->irb.CreateCall(f, args));
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
					if (!can_apply(rcv, targs)) throw internal_codegen_error("tried to find return type for invalid function application");
					return return_t;
				}
			};

			struct llvm_function : public function {
				llvm::Function* f;
				parser::fn_decl decl;

				llvm_function(const parser::fn_decl& d, llvm::Function* f) : decl(d), f(f) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override {
					g->s.push(g->irb.CreateCall(f, args));
				}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					if (args.size() != decl.args.size()) return false;
					for (int i = 0; i < args.size(); ++i) {
						if (!args[i]->equals(decl.args[i].second)) return false;
					}
					return true;
				}

				shared_ptr<type_id> return_type(code_generator* gen, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
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

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override {
					auto v = g->irb.CreateAlloca(type->llvm_type(g->gen->mod->getContext()));
					auto i32t = llvm::Type::getInt32Ty(g->gen->mod->getContext());
					auto zero = llvm::ConstantInt::get(i32t, 0, false);
					for (int i = 0; i < args.size(); ++i) {
						auto p = g->irb.CreateGEP(v, { zero, llvm::ConstantInt::get(i32t, i, false) });
						g->irb.CreateStore(args[i], p);
					}
					g->s.push(g->irb.CreateGEP(v, {zero}));
				}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					if (!type->equals(rcv)) return false;
					if (args.size() != type->fields.size()) return false;
					for (int i = 0; i < args.size(); ++i) {
						if (!args[i]->equals(type->fields[i].second)) return false;
					}
					return true;
				}

				shared_ptr<type_id> return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
					if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
					return type;
				}
			};

			struct static_fn : public llvm_function {
				static_fn(const parser::fn_decl& d, llvm::Function* f) : llvm_function(d, f) {}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					return rcv->equals(this->decl.receiver) && llvm_function::can_apply(rcv, args);
				}
			};

			struct method : public llvm_function {

				method(const parser::fn_decl& d, llvm::Function* f) : llvm_function(d,f) {}

				void apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) override {
					if (rcv == nullptr) throw internal_codegen_error("tried to apply a method function with a non-null reciever");
					vector<llvm::Value*> aargs;

					auto alc = g->irb.CreateAlloca(rcv->getType());
					g->irb.CreateStore(rcv, alc);
					auto zero = llvm::ConstantInt::get(g->irb.getContext(), llvm::APInt(32, 0));
					auto ref = g->irb.CreateGEP(rcv->getType(), alc, { zero, });
					
					/*llvm::outs() << "--\n";
					f->getType()->print(llvm::outs(), true);
					llvm::outs() << "\n";
					rcv->getType()->print(llvm::outs());
					llvm::outs() << "\n";
					rcv_t->llvm_type(g->irb.getContext())->print(llvm::outs());
					llvm::outs() << "\n";
					ref->getType()->print(llvm::outs());
					llvm::outs() << "=";
					alc->getType()->print(llvm::outs());
					llvm::outs() << "--\n";
					llvm::outs().flush();*/

					aargs.push_back(ref);
					aargs.insert(aargs.end(), args.begin(), args.end());
					g->s.push(g->irb.CreateCall(f, aargs));
				}

				bool can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					if (!rcv->equals(decl.receiver)) return false;
					return llvm_function::can_apply(rcv, args);
				}

				shared_ptr<type_id> return_type(code_generator* gen, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) override {
					if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
					expr_context fncx;
					expr_typer ty{ gen, &fncx };
					for (int i = 0; i < decl.args.size(); ++i) {
						ty.cx->insert_or_assign(decl.args[i].first, pair<llvm::Value*, shared_ptr<type_id>>{ nullptr, args[i] });
					}
					ty.cx->insert_or_assign("self", pair<llvm::Value*, shared_ptr<type_id>>{ nullptr, make_shared<ptr_type>(rcv) });
					auto strct = dynamic_pointer_cast<struct_type>(rcv);
					if (strct != nullptr) {
						for (const auto& f : strct->fields) {
							ty.cx->insert_or_assign(f.first, pair<llvm::Value*, shared_ptr<type_id>>{ nullptr,f.second->resolve(gen) });
						}
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
				functions["~"].push_back(make_shared<cast_op>());
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
					else throw internal_codegen_error("floating point literals currently unsupported");
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
					auto glob = dynamic_pointer_cast<nkqc::ast::symbol_expr>(x.rcv);
					auto tx = dynamic_pointer_cast<nkqc::parser::type_expr>(x.rcv);
					shared_ptr<type_id> rcv_t;
					if (glob != nullptr && glob->v == "G") {
						rcv_t = nullptr;
					}
					else if (tx != nullptr) {
						rcv_t = tx->type->resolve(gen);
					}
					else {
						x.rcv->visit(this);
						rcv_t = s.top()->resolve(gen); s.pop();
					}
					auto f = gen->lookup_function(x.msgname, rcv_t, {});
					if (f != nullptr) {
						s.push(f->return_type(gen, rcv_t, {}));
					}
					else throw no_such_function_error("attempted to compute return type for unary function", x.msgname, rcv_t, {});
				}
				virtual void visit(const nkqc::ast::binary_msgsnd &x) {
					auto tx = dynamic_pointer_cast<parser::type_expr>(x.rcv);
					x.rhs->visit(this);
					shared_ptr<type_id> rhs = s.top()->resolve(gen), rcv = nullptr;
					s.pop();
					if (tx != nullptr) {
						rcv = tx->type->resolve(gen);
					}
					else {
						x.rcv->visit(this);
						rcv = s.top()->resolve(gen); s.pop();
					}
					auto sf = gen->lookup_function(x.op, rcv, { rhs });
					if (sf != nullptr)
						s.push(sf->return_type(gen, rcv, { rhs }));
					else
						throw no_such_function_error("attempted to compute return type", x.op, rcv, { rhs });
				}
				virtual void visit(const nkqc::ast::keyword_msgsnd &x) {
					auto glob = dynamic_pointer_cast<nkqc::ast::symbol_expr>(x.rcv);
					auto tx = dynamic_pointer_cast<nkqc::parser::type_expr>(x.rcv);
					shared_ptr<type_id> rcv_t;
					if (glob != nullptr && glob->v == "G") {
						rcv_t = nullptr;
					}
					else if (tx != nullptr) {
						rcv_t = tx->type->resolve(gen);
					}
					else {
						x.rcv->visit(this);
						rcv_t = s.top()->resolve(gen); s.pop();
					}
					vector<shared_ptr<type_id>> arg_t;
					for (const auto& arg : x.args) {
						arg->visit(this);
						arg_t.push_back(s.top()->resolve(gen)); s.pop();
					}
					auto f = gen->lookup_function(x.msgname, rcv_t, arg_t);
					if (f != nullptr) {
						s.push(f->return_type(gen, rcv_t, arg_t));
					}
					else throw no_such_function_error("attempted to compute return type", x.msgname, rcv_t, arg_t);
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
				return t.s.top()->resolve(this);
			}

			llvm::Type* type_of(shared_ptr<type_id> expr) {
				return expr->resolve(this)->llvm_type(mod->getContext());
			}

			llvm::FunctionType* type_of(const nkqc::parser::fn_decl& fn, expr_context* cx) {
				vector<llvm::Type*> params;
				if (fn.receiver != nullptr && !fn.static_function)
					params.push_back(type_of(make_shared<ptr_type>(fn.receiver)));
				for (const auto& arg : fn.args) {
					params.push_back(type_of(arg.second));
				}
				return llvm::FunctionType::get(type_of(fn.body, cx)->llvm_type(mod->getContext()), params, false);
			}


			struct expr_generator : public ast::expr_visiter<> {
				code_generator* gen;
				llvm::BasicBlock* bb;
				llvm::IRBuilder<> irb;
				expr_context* cx;
				stack<llvm::Value*> s;

				expr_generator(code_generator* gen, llvm::BasicBlock* bb, expr_context* cx)
					: gen(gen), bb(bb), cx(cx), irb(bb) {}

				virtual void visit(const nkqc::ast::id_expr &x) override {
					s.push(irb.CreateLoad(cx->at(x.v).first));
				}
				virtual void visit(const nkqc::ast::string_expr &x) override {
//					s.push(irb.CreateAlloca(llvm::ArrayType::get(llvm::Type::getInt8Ty(gen->mod->getContext()), x.v.size())));
					s.push(llvm::ConstantDataArray::get(gen->mod->getContext(),
						llvm::ArrayRef<uint8_t>((uint8_t*)x.v.c_str(), x.v.size())));
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
					s.push(irb.CreateRet(v));
				}
				void allocate() {
					auto a = irb.CreateAlloca(s.top()->getType());
					irb.CreateStore(s.top(), a);
					s.pop();
					s.push(a);
				}
				virtual void visit(const nkqc::ast::unary_msgsnd &x) override {
					auto glob = dynamic_pointer_cast<nkqc::ast::symbol_expr>(x.rcv);
					auto tx = dynamic_pointer_cast<nkqc::parser::type_expr>(x.rcv);
					shared_ptr<type_id> rcv_t;
					if (glob != nullptr && glob->v == "G")
						s.push(nullptr);
					else if (tx != nullptr) {
						s.push(nullptr);
						rcv_t = tx->type->resolve(gen);
					}
					else {
						rcv_t = gen->type_of(x.rcv, cx);
						x.rcv->visit(this);
					}
					auto rcv = s.top(); s.pop();
					auto f = gen->lookup_function(x.msgname, rcv_t, {});
					if (f != nullptr) {
						f->apply(this, rcv, {}, rcv_t, {});
					}
					else throw no_such_function_error("unary message", x.msgname, rcv_t, {});
				}
				virtual void visit(const nkqc::ast::binary_msgsnd &x) override {
					auto tx = dynamic_pointer_cast<parser::type_expr>(x.rcv);
					auto rhs_type = gen->type_of(x.rhs, cx);
					if (tx != nullptr) {
						auto txt = tx->type->resolve(gen);
						auto f = gen->lookup_function(x.op, txt, { rhs_type });
						if (f != nullptr) {
							x.rhs->visit(this);
							auto rhs = s.top();  s.pop();
							f->apply(this, nullptr, { rhs }, txt, { rhs_type });
						}
						else throw no_such_function_error("binary operator applied to type", x.op, tx->type, { rhs_type });
					}
					else {
						auto lhs_type = gen->type_of(x.rcv, cx);
						auto f = gen->lookup_function(x.op, lhs_type, { rhs_type });
						if (f != nullptr) {
							//	apply function
							x.rcv->visit(this);
							auto rcv = s.top(); s.pop();
							x.rhs->visit(this);
							auto rhs = s.top(); s.pop();
							f->apply(this, rcv, { rhs }, lhs_type, { rhs_type });
						}
						else {
							throw no_such_function_error("binary operator", x.op, lhs_type, { rhs_type });
						}
					}
				}
				virtual void visit(const nkqc::ast::keyword_msgsnd &x) override {
					auto glob = dynamic_pointer_cast<nkqc::ast::symbol_expr>(x.rcv);
					auto tx = dynamic_pointer_cast<nkqc::parser::type_expr>(x.rcv);
					shared_ptr<type_id> rcv_t;
					if (glob != nullptr && glob->v == "G")
						s.push(nullptr);
					else if (tx != nullptr) {
						s.push(nullptr);
						rcv_t = tx->type->resolve(gen);
					}
					else {
						rcv_t = gen->type_of(x.rcv, cx);
						x.rcv->visit(this);
					}
					auto rcv = s.top(); s.pop();
					vector<llvm::Value*> args; vector<shared_ptr<type_id>> arg_t;
					for (const auto& arg : x.args) {
						arg->visit(this);
						args.push_back(s.top()); s.pop();
						arg_t.push_back(gen->type_of(arg, cx));
					}
					auto f = gen->lookup_function(x.msgname, rcv_t, arg_t);
					if (f != nullptr) {
						f->apply(this, rcv, args, rcv_t, arg_t);
					}
					else throw no_such_function_error("keyword message", x.msgname, rcv_t, arg_t);
				}
				virtual void visit(const nkqc::ast::cascade_msgsnd &x) override {
				}
				virtual void visit(const nkqc::ast::assignment_expr &x) override {
					x.val->visit(this);
					allocate();
					cx->insert_or_assign(x.name, pair<llvm::Value*, shared_ptr<type_id>>{ s.top(), gen->type_of(x.val, cx) });
				}
			};

			void generate_expr(expr_context cx, shared_ptr<nkqc::ast::expr> expr, llvm::BasicBlock* block) {
				expr_generator xg{ this, block, &cx };
				expr->visit(&xg);
			}

			llvm::Function* define_function(nkqc::parser::fn_decl fn) {
				expr_context cx;
				for (const auto& arg : fn.args) {
					cx[arg.first] = pair<llvm::Value*, shared_ptr<type_id>>{ nullptr, arg.second };
				}
				auto cfn = dynamic_pointer_cast<ast::array_expr>(fn.body);
				if (cfn != nullptr) {
					vector<llvm::Type*> params;
					for (const auto& arg : fn.args) {
						params.push_back(type_of(arg.second));
					}
					auto ret_t = dynamic_pointer_cast<parser::type_expr>(cfn->vs[1])->type;
					auto fn_t = llvm::FunctionType::get(ret_t->llvm_type(mod->getContext()), params, false);
					auto name = dynamic_pointer_cast<ast::symbol_expr>(cfn->vs[0])->v;
					auto F = llvm::cast<llvm::Function>(mod->getOrInsertFunction(name, fn_t));
					F->setLinkage(llvm::Function::LinkageTypes::ExternalLinkage);
					//	llvm::Function::Create(fn_t, llvm::Function::ExternalLinkage, name, mod.get());
					functions[fn.selector].push_back(make_shared<extern_fn>(F, fn.args, ret_t));
					return F;
				}
				else {
					if (fn.receiver != nullptr)
						fn.receiver = fn.receiver->resolve(this);
					auto strct = dynamic_pointer_cast<struct_type>(fn.receiver);
					if (strct != nullptr && !fn.static_function) {
						for (const auto& f : strct->fields) {
							cx[f.first].second = f.second;
						}
					}
					auto F = llvm::cast<llvm::Function>(mod->getOrInsertFunction(fn.selector, type_of(fn, &cx)));
					auto entry_block = llvm::BasicBlock::Create(mod->getContext(), "entry", F);
					auto vals = F->arg_begin();
					if(fn.receiver != nullptr && !fn.static_function) {
						/*llvm::IRBuilder<> irb(entry_block);
						auto self = cx["self"].first = irb.CreateAlloca(v->getType()); vals++;
						irb.CreateStore(llvm::cast<llvm::Value>(&*vals), self);*/
						auto self = cx["self"].first = llvm::cast<llvm::Value>(&*vals);
						cx["self"].second = make_shared<ptr_type>(fn.receiver);
						/*auto llvm_argument_type = cx["self"].first->getType();
						llvm_argument_type->print(llvm::outs());
						llvm::outs() << "-";
						llvm_argument_type->getPointerElementType()->print(llvm::outs());
						llvm::outs() << "\n";
						auto nkqc_type = cx["self"].second->resolve(this)->llvm_type(mod->getContext());
						nkqc_type->print(llvm::outs());
						llvm::outs() << "-";
						nkqc_type->getPointerElementType()->print(llvm::outs());
						llvm::outs().flush();*/
						if (strct != nullptr) {
							auto zero = llvm::ConstantInt::get(mod->getContext(), llvm::APInt(32, 0));
							for (int i = 0; i < strct->fields.size(); ++i) {
								cx[strct->fields[i].first].first =
									llvm::GetElementPtrInst::Create(self->getType()->getPointerElementType(), self, { zero, llvm::ConstantInt::get(mod->getContext(), llvm::APInt(32, i)) }, "", entry_block);
							}
						}
					}
					for (const auto& arg : fn.args) {
						cx[arg.first] = { llvm::cast<llvm::Value>(&*vals), arg.second };
						vals++;
					}
					generate_expr(cx, dynamic_pointer_cast<ast::block_expr>(fn.body)->body, entry_block);
					if (fn.receiver != nullptr) {
						if (fn.static_function) {
							functions[fn.selector].push_back(make_shared<static_fn>(fn, F));
						}
						else {
							functions[fn.selector].push_back(make_shared<method>(fn, F));
						}
					}
					else functions[fn.selector].push_back(make_shared<global_fn>(fn, F));
					return F;
				}
			}

			void define_type(const string& name, shared_ptr<type_id> type) {
				types[name] = type_record{ type, {} };
				auto st = dynamic_pointer_cast<struct_type>(type);
				if (st != nullptr) {
					st->init(mod->getContext(), name);
					string csl = "";
					for (const auto& f : st->fields)
						csl += f.first + ":";
					functions[csl].push_back(make_shared<struct_initializer>(st));
				}
			}
		};
	}
}
