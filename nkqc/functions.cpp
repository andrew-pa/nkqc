#include "llvm_codegen.h"

namespace nkqc {
	namespace codegen {
		// ------binary operator-----------------------------
		void code_generator::binary_llvm_op::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			g->s.push(g->irb.CreateBinOp(op, rcv, args[0]));
		}

		bool code_generator::binary_llvm_op::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (args.size() != 1) return false;
			auto rcv_int = dynamic_pointer_cast<integer_type>(rcv);
			auto arg_int = dynamic_pointer_cast<integer_type>(args[0]);
			return rcv_int != nullptr && arg_int != nullptr && rcv_int->bitwidth == arg_int->bitwidth && rcv_int->signed_ == arg_int->signed_;
		}

		shared_ptr<type_id> code_generator::binary_llvm_op::return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			return rcv;
		}
		// -------------------------------------------------

		// -----numeric operator----------------------------
		void code_generator::numeric_comp_op::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			if (floating) {
				g->s.push(g->irb.CreateFCmp(pred, rcv, args[0]));
			}
			else {
				g->s.push(g->irb.CreateICmp(pred, rcv, args[0]));
			}
		}

		bool code_generator::numeric_comp_op::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (args.size() != 1 || !rcv->equals(args[0])) return false;
			if (floating) {
				throw internal_codegen_error("floating not yet supported");
			}
			else {
				return dynamic_pointer_cast<integer_type>(rcv) != nullptr;
			}
		}

		shared_ptr<type_id> code_generator::numeric_comp_op::return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			return make_shared<bool_type>();
		}
		// -------------------------------------------------

		// -----casting operation---------------------------
		void code_generator::cast_op::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			if (rcv != nullptr) throw internal_codegen_error("tried to apply cast_op with a non-null reciever");
			g->s.push(args_t[0]->cast_to(g->gen->mod->getContext(), rcv_t, args[0], g->irb));
		}

		bool code_generator::cast_op::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			return args.size() == 1 && args[0]->can_cast_to(rcv);
		}

		shared_ptr<type_id> code_generator::cast_op::return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			return rcv;
		}
		// -------------------------------------------------

		// -----external function---------------------------
		void code_generator::extern_fn::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
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

		bool code_generator::extern_fn::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& targs) {
			if (rcv != nullptr) return false;
			if (targs.size() != args.size()) return false;
			for (int i = 0; i < args.size(); ++i) {
				if (!targs[i]->equals(args[i].second)) return false;
			}
			return true;
		}

		shared_ptr<type_id> code_generator::extern_fn::return_type(code_generator* gen, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& targs) {
			if (!can_apply(rcv, targs)) throw internal_codegen_error("tried to find return type for invalid function application");
			return return_t;
		}
		// -------------------------------------------------
		
		// -----generic llvm function-----------------------
		void code_generator::llvm_function::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			g->s.push(g->irb.CreateCall(f, args));
		}

		bool code_generator::llvm_function::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (args.size() != decl.args.size()) return false;
			for (int i = 0; i < args.size(); ++i) {
				if (!args[i]->equals(decl.args[i].second)) return false;
			}
			return true;
		}

		shared_ptr<type_id> code_generator::llvm_function::return_type(code_generator* gen, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			if (decl.return_type) {
				return decl.return_type->resolve(gen);
			}
			else {
				expr_context fncx;
				expr_typer ty{ gen, &fncx };
				for (int i = 0; i < decl.args.size(); ++i) {
					ty.cx->insert_or_assign(decl.args[i].first, args[i]);
				}
				ty.visit(decl.body);
				auto v = ty.s.top(); ty.s.pop();
				return v;
			}
		}
		// -------------------------------------------------

		// -----struct initializer--------------------------
		void code_generator::struct_initializer::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			auto v = g->irb.CreateAlloca(type->llvm_type(g->gen->mod->getContext()));
			auto i32t = llvm::Type::getInt32Ty(g->gen->mod->getContext());
			auto zero = llvm::ConstantInt::get(i32t, 0, false);
			for (int i = 0; i < args.size(); ++i) {
				auto p = g->irb.CreateGEP(v, { zero, llvm::ConstantInt::get(i32t, i, false) });
				g->irb.CreateStore(args[i], p);
			}
			g->s.push(g->irb.CreateLoad(g->irb.CreateGEP(v, { zero })));
		}

		bool code_generator::struct_initializer::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!type->equals(rcv)) return false;
			if (args.size() != type->fields.size()) return false;
			for (int i = 0; i < args.size(); ++i) {
				if (!args[i]->equals(type->fields[i].second)) return false;
			}
			return true;
		}

		shared_ptr<type_id> code_generator::struct_initializer::return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			return type;
		}
		// -------------------------------------------------

		// -----method--------------------------------------
		void code_generator::method::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			if (rcv == nullptr) throw internal_codegen_error("tried to apply a method function with a non-null reciever");
			vector<llvm::Value*> aargs;

			/*auto alc = g->irb.CreateAlloca(rcv->getType(), nullptr, "rcv");
			g->irb.CreateStore(rcv, alc);
			auto zero = llvm::ConstantInt::get(g->irb.getContext(), llvm::APInt(32, 0));
			auto ref = g->irb.CreateGEP(rcv->getType(), alc, { zero, });*/

			llvm::outs() << "\n--\nf = ";
			f->getType()->print(llvm::outs(), true);
			llvm::outs() << "\nrcv  = ";
			rcv->getType()->print(llvm::outs());
			llvm::outs() << "\nrcv_t = ";
			rcv_t->llvm_type(g->irb.getContext())->print(llvm::outs());
			llvm::outs() << " ~ ";
			llvm::outs().flush();
			rcv_t->print(cout);
			cout.flush();
			llvm::outs() << "\nd_rcv = ";
			decl.receiver->llvm_type(g->irb.getContext())->print(llvm::outs());
			llvm::outs() << " ~ ";
			llvm::outs().flush();
			decl.receiver->print(cout);
			cout.flush();
			/*llvm::outs() << "\n";
			ref->getType()->print(llvm::outs());
			llvm::outs() << "=";
			alc->getType()->print(llvm::outs());*/
			llvm::outs() << "\n--\n";
			llvm::outs().flush();

			aargs.push_back(rcv);
			aargs.insert(aargs.end(), args.begin(), args.end());
			g->s.push(g->irb.CreateCall(f, aargs));
		}

		bool code_generator::method::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!rcv->equals(decl.receiver)) return false;
			return llvm_function::can_apply(rcv, args);
		}

		shared_ptr<type_id> code_generator::method::return_type(code_generator* gen, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			expr_context fncx;
			expr_typer ty{ gen, &fncx };
			for (int i = 0; i < decl.args.size(); ++i) {
				ty.cx->insert_or_assign(decl.args[i].first, args[i]);
			}
			ty.cx->insert_or_assign("self", rcv);
			auto ptr = dynamic_pointer_cast<ptr_type>(rcv);
			if (ptr != nullptr) {
				auto strct = dynamic_pointer_cast<struct_type>(ptr->inner);
				if (strct != nullptr) {
					for (const auto& f : strct->fields) {
						ty.cx->insert_or_assign(f.first, f.second->resolve(gen));
					}
				}
			}
			ty.visit(decl.body);
			auto v = ty.s.top(); ty.s.pop();
			return v;
		}
		// -------------------------------------------------

		// -----pointer index-------------------------------
		void code_generator::pointer_index_op::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			g->s.push(g->irb.CreateLoad(g->irb.CreateGEP(rcv, args[0])));
		}
		bool code_generator::pointer_index_op::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			return dynamic_pointer_cast<ptr_type>(rcv) != nullptr && args.size() == 1 && dynamic_pointer_cast<integer_type>(args[0]) != nullptr;
		}
		shared_ptr<type_id> code_generator::pointer_index_op::return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			return dynamic_pointer_cast<ptr_type>(rcv)->inner;
		}
		// -------------------------------------------------

		// -----pointer index store-------------------------
		void code_generator::pointer_index_store_op::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			g->s.push(g->irb.CreateStore(args[1], g->irb.CreateGEP(rcv, args[0])));
		}
		bool code_generator::pointer_index_store_op::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			auto p = dynamic_pointer_cast<ptr_type>(rcv);
			return p != nullptr &&
				args.size() == 2 && dynamic_pointer_cast<integer_type>(args[0]) != nullptr &&
				p->inner->equals(args[1]);
		}
		shared_ptr<type_id> code_generator::pointer_index_store_op::return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			return make_shared<unit_type>();
		}
		// -------------------------------------------------

		// -----alloc---------------------------------------
		void code_generator::alloc_fn::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			if (rcv != nullptr) throw internal_codegen_error("tried to call alloc with a non-null reciever");
			auto t = rcv_t->llvm_type(g->irb.getContext());
			auto it = llvm::Type::getInt32Ty(g->irb.getContext());
			g->s.push(llvm::CallInst::CreateMalloc(g->irb.GetInsertBlock(),
				it, t, llvm::ConstantExpr::getTruncOrBitCast(llvm::ConstantExpr::getSizeOf(t), it), nullptr, nullptr, ""));
			g->irb.GetInsertBlock()->getInstList().push_back(llvm::cast<llvm::Instruction>(g->s.top()));
		}
		bool code_generator::alloc_fn::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			return args.size() == 0;
		}
		shared_ptr<type_id> code_generator::alloc_fn::return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			return make_shared<ptr_type>(rcv);
		}
		// -------------------------------------------------

		// -----alloc array---------------------------------
		void code_generator::alloc_array_fn::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			if (rcv != nullptr) throw internal_codegen_error("tried to call alloc with a non-null reciever");
			auto t = rcv_t->llvm_type(g->irb.getContext());
			auto it = (llvm::Type*)llvm::Type::getInt32Ty(g->irb.getContext());
			it->print(llvm::outs());
			llvm::outs() << "---";
			args[0]->getType()->print(llvm::outs());
			llvm::outs() << "\n";
			llvm::outs().flush();
			g->s.push(llvm::CallInst::CreateMalloc(g->irb.GetInsertBlock(),
				it, t, (llvm::Value*)llvm::ConstantExpr::getTruncOrBitCast(llvm::ConstantExpr::getSizeOf(t), it), args[0], nullptr, ""));
			g->irb.GetInsertBlock()->getInstList().push_back(llvm::cast<llvm::Instruction>(g->s.top()));
		}
		bool code_generator::alloc_array_fn::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			return args.size() == 1 && dynamic_pointer_cast<integer_type>(args[0]) != nullptr;
		}
		shared_ptr<type_id> code_generator::alloc_array_fn::return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			return make_shared<ptr_type>(rcv);
		}
		// -------------------------------------------------

		// -----free----------------------------------------
		void code_generator::free_fn::apply(expr_generator* g, llvm::Value* rcv, const vector<llvm::Value*>& args, shared_ptr<type_id> rcv_t, const vector<shared_ptr<type_id>>& args_t) {
			g->irb.GetInsertBlock()->getInstList().push_back(llvm::CallInst::CreateFree(g->irb.CreateLoad(rcv), g->irb.GetInsertBlock()));
		}
		bool code_generator::free_fn::can_apply(shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			return dynamic_pointer_cast<ptr_type>(rcv) != nullptr && args.size() == 0;
		}
		shared_ptr<type_id> code_generator::free_fn::return_type(code_generator* e, shared_ptr<type_id> rcv, const vector<shared_ptr<type_id>>& args) {
			if (!can_apply(rcv, args)) throw internal_codegen_error("tried to find return type for invalid function application");
			return make_shared<unit_type>();
		}
		// -------------------------------------------------
	}
}