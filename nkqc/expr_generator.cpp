#include "llvm_codegen.h"

namespace nkqc {
	namespace codegen {
		void code_generator::expr_generator::visit(const nkqc::ast::id_expr &x) {
			if (x.v == "true")
				s.push(llvm::ConstantInt::get(llvm::Type::getInt1Ty(gen->mod->getContext()), 1));
			else if (x.v == "false")
				s.push(llvm::ConstantInt::get(llvm::Type::getInt1Ty(gen->mod->getContext()), 0));
			else
				s.push(irb.CreateLoad(cx->at(x.v).first));
		}
		void code_generator::expr_generator::visit(const nkqc::ast::string_expr &x)  {
			//					s.push(irb.CreateAlloca(llvm::ArrayType::get(llvm::Type::getInt8Ty(gen->mod->getContext()), x.v.size())));
			s.push(llvm::ConstantDataArray::get(gen->mod->getContext(),
				llvm::ArrayRef<uint8_t>((uint8_t*)x.v.c_str(), x.v.size() + 1)));
		}
		void code_generator::expr_generator::visit(const nkqc::ast::number_expr &x) {
			s.push(llvm::ConstantInt::get(llvm::Type::getInt32Ty(gen->mod->getContext()), x.iv));
		}
		void code_generator::expr_generator::visit(const nkqc::ast::block_expr &x) {
			// if we get here this is a proper closure
			throw;
		}
		void code_generator::expr_generator::visit(const nkqc::ast::symbol_expr &x) {
			throw;
		}
		void code_generator::expr_generator::visit(const nkqc::ast::char_expr &x) {
			throw;
		}
		void code_generator::expr_generator::visit(const nkqc::ast::array_expr &x) {
			throw;
		}
		void code_generator::expr_generator::visit(const nkqc::ast::tag_expr &x) {
			cout << "tag " << x.v << endl;
		}
		void code_generator::expr_generator::visit(const nkqc::ast::seq_expr &x) {
			x.first->visit(this);
			x.second->visit(this);
		}
		void code_generator::expr_generator::visit(const nkqc::ast::return_expr &x) {
			x.val->visit(this);
			auto v = s.top(); s.pop();
			s.push(irb.CreateRet(v));
		}
		
		void code_generator::expr_generator::visit(const nkqc::ast::unary_msgsnd &x) {
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
				// all receivers are passed by reference
				rcv_t = gen->type_of(x.rcv, cx);
				auto id = dynamic_pointer_cast<ast::id_expr>(x.rcv);
				if (id != nullptr) {
					s.push(cx->at(id->v).first);
				}
				else x.rcv->visit(this);
				if (rcv_t->receive_by_ref()) {
					rcv_t = make_shared<ptr_type>(rcv_t);
					//if (!llvm::isa<llvm::AllocaInst>(s.top())) //->getType()->isPointerTy()) {
					//	allocate();
				}
			}
			auto rcv = s.top(); s.pop();
			auto f = gen->lookup_function(x.msgname, rcv_t, {});
			if (f != nullptr) {
				f->apply(this, rcv, {}, rcv_t, {});
			}
			else throw no_such_function_error("unary message", x.msgname, rcv_t, {});
		}
		void code_generator::expr_generator::visit(const nkqc::ast::binary_msgsnd &x) {
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
		void code_generator::expr_generator::visit(const nkqc::ast::keyword_msgsnd &x) {
			auto glob = dynamic_pointer_cast<nkqc::ast::symbol_expr>(x.rcv);
			auto tx = dynamic_pointer_cast<nkqc::parser::type_expr>(x.rcv);
			auto block_rcv = dynamic_pointer_cast<nkqc::ast::block_expr>(x.rcv);
			vector<shared_ptr<type_id>> arg_t;
			for (const auto& arg : x.args)
				arg_t.push_back(gen->type_of(arg, cx));
			shared_ptr<type_id> rcv_t;
			if (glob != nullptr && glob->v == "G")
				s.push(nullptr);
			else if (tx != nullptr) {
				s.push(nullptr);
				rcv_t = tx->type->resolve(gen);
			}
			else if (block_rcv != nullptr) {
				if (x.msgname == "whileTrue:") {
					if (arg_t.size() != 1) throw no_such_function_error("while loop must only have body", x.msgname, nullptr, arg_t);
					auto cond_t = gen->type_of(block_rcv->body, cx);
					if (dynamic_pointer_cast<bool_type>(cond_t) == nullptr)
						throw no_such_function_error("while condition must be of bool type", x.msgname, cond_t, arg_t);
					auto F = irb.GetInsertBlock()->getParent();
					//		before-loop-code
					//loop:
					//		loop-check, branch to after-loop if done
					//		loop-body
					//		branch to loop
					//after-loop:
					//		after-loop-code
					auto loop_chk_bb = llvm::BasicBlock::Create(irb.getContext(), "loopchk", F);
					irb.CreateBr(loop_chk_bb);
					auto loop_bb = llvm::BasicBlock::Create(irb.getContext(), "loop");
					auto loopend_bb = llvm::BasicBlock::Create(irb.getContext(), "loopend");
					auto body_blk = dynamic_pointer_cast<ast::block_expr>(x.args[0]);
					if (body_blk == nullptr) throw no_such_function_error("while loop body must be block", x.msgname, nullptr, arg_t);

					expr_generator loop_chk_gen(gen, loop_chk_bb, cx);
					cx->push_scope();
					block_rcv->body->visit(&loop_chk_gen);
					loop_chk_gen.irb.CreateCondBr(loop_chk_gen.s.top(), loop_bb, loopend_bb);
					loop_chk_gen.s.pop();
					cx->pop_scope();

					expr_generator loop_gen(gen, loop_bb, cx);
					cx->push_scope();
					body_blk->body->visit(&loop_gen);
					loop_gen.irb.CreateBr(loop_chk_bb);
					cx->pop_scope();
					F->getBasicBlockList().push_back(loop_bb);

					F->getBasicBlockList().push_back(loopend_bb);
					irb.SetInsertPoint(loopend_bb);
					return;
				}
				else {
					throw no_such_function_error("raw block receiver", x.msgname, nullptr, arg_t);
				}
			}
			else {
				rcv_t = gen->type_of(x.rcv, cx);
				x.rcv->visit(this);
				if (dynamic_pointer_cast<bool_type>(rcv_t) != nullptr) {
					if (x.msgname == "ifTrue:ifFalse:") {
						if (arg_t.size() != 2 || !arg_t[0]->equals(arg_t[1]))
							throw no_such_function_error("if statment branches must have same type", x.msgname, rcv_t, arg_t);
						auto F = irb.GetInsertBlock()->getParent();
						auto true_bb = llvm::BasicBlock::Create(irb.getContext(), "then", F);
						auto false_bb = llvm::BasicBlock::Create(irb.getContext(), "else");
						auto merge_bb = llvm::BasicBlock::Create(irb.getContext(), "merge");
						irb.CreateCondBr(s.top(), true_bb, false_bb);
						expr_generator true_gen(gen, true_bb, cx), false_gen(gen, false_bb, cx);
						auto blk = dynamic_pointer_cast<ast::block_expr>(x.args[0]);
						if (blk) {
							cx->push_scope();
							blk->body->visit(&true_gen);
							true_gen.irb.CreateBr(merge_bb);
							cx->pop_scope();
						}
						else {
							x.args[0]->visit(&true_gen);
							true_gen.irb.CreateBr(merge_bb);
						}
						F->getBasicBlockList().push_back(false_bb);
						blk = dynamic_pointer_cast<ast::block_expr>(x.args[1]);
						if (blk) {
							cx->push_scope();
							blk->body->visit(&false_gen);
							false_gen.irb.CreateBr(merge_bb);
							cx->pop_scope();
						}
						else {
							x.args[1]->visit(&false_gen);
							false_gen.irb.CreateBr(merge_bb);
						}
						F->getBasicBlockList().push_back(merge_bb);
						irb.SetInsertPoint(merge_bb);
						if (dynamic_pointer_cast<unit_type>(arg_t[0]) != nullptr) {
							auto phi = irb.CreatePHI(arg_t[0]->llvm_type(irb.getContext()), 2);
							phi->addIncoming(true_gen.s.top(), true_bb);
							phi->addIncoming(false_gen.s.top(), false_bb);
							s.push(phi);
						}
						return;
					}
				}
			}

			auto rcv = s.top(); s.pop();
			vector<llvm::Value*> args;
			for (const auto& arg : x.args) {
				arg->visit(this);
				args.push_back(s.top()); s.pop();
			}
			auto f = gen->lookup_function(x.msgname, rcv_t, arg_t);
			if (f != nullptr) {
				f->apply(this, rcv, args, rcv_t, arg_t);
			}
			else throw no_such_function_error("keyword message", x.msgname, rcv_t, arg_t);
		}
		void code_generator::expr_generator::visit(const nkqc::ast::cascade_msgsnd &x) {
		}
		void code_generator::expr_generator::visit(const nkqc::ast::assignment_expr &x) {
			x.val->visit(this);
			auto v = cx->find(x.name);
			if (v == cx->end() || v->second.first == nullptr) {
				allocate();
				cx->insert_or_assign(x.name, gen->type_of(x.val, cx), s.top());
			}
			else {
				llvm::outs() << "assignment: ";
				v->second.first->getType()->print(llvm::outs());
				llvm::outs() << " -> ";
				s.top()->getType()->print(llvm::outs());
				llvm::outs() << "\n";
				llvm::outs().flush();
				irb.CreateStore(s.top(), v->second.first);
			}
		}
	}
}