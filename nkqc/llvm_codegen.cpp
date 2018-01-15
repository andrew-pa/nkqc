#include "llvm_codegen.h"

namespace nkqc {
	namespace codegen {
		code_generator::code_generator(shared_ptr<llvm::Module> mod)
			: mod(mod) {
			functions["+"].push_back(make_shared<binary_llvm_op>(llvm::BinaryOperator::BinaryOps::Add));
			functions["*"].push_back(make_shared<binary_llvm_op>(llvm::BinaryOperator::BinaryOps::Mul));
			functions["-"].push_back(make_shared<binary_llvm_op>(llvm::BinaryOperator::BinaryOps::Sub));
			functions["/"].push_back(make_shared<binary_llvm_op>(llvm::BinaryOperator::BinaryOps::SDiv));
			functions["%"].push_back(make_shared<binary_llvm_op>(llvm::BinaryOperator::BinaryOps::SRem));
			functions["=="].push_back(make_shared<numeric_comp_op>(llvm::CmpInst::Predicate::ICMP_EQ, false));
			functions["!="].push_back(make_shared<numeric_comp_op>(llvm::CmpInst::Predicate::ICMP_NE, false));
			functions["<"].push_back(make_shared<numeric_comp_op>(llvm::CmpInst::Predicate::ICMP_SLT, false));
			functions[">"].push_back(make_shared<numeric_comp_op>(llvm::CmpInst::Predicate::ICMP_SGT, false));
			functions["<="].push_back(make_shared<numeric_comp_op>(llvm::CmpInst::Predicate::ICMP_SLE, false));
			functions[">="].push_back(make_shared<numeric_comp_op>(llvm::CmpInst::Predicate::ICMP_SGE, false));
			functions["~"].push_back(make_shared<cast_op>());
			functions["at:"].push_back(make_shared<pointer_index_op>());
			functions["at:put:"].push_back(make_shared<pointer_index_store_op>());
			functions["alloc"].push_back(make_shared<alloc_fn>());
			functions["allocArrayOf:"].push_back(make_shared<alloc_array_fn>());
			functions["free"].push_back(make_shared<free_fn>());
		}

		llvm::Function* code_generator::define_function(nkqc::parser::fn_decl fn) {
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
				F->setDLLStorageClass(llvm::GlobalValue::DLLStorageClassTypes::DLLImportStorageClass);
				functions[fn.selector].push_back(make_shared<extern_fn>(F, fn.args, ret_t));
				return F;
			}
			else {
				shared_ptr<struct_type> strct = nullptr;
				if (fn.receiver != nullptr) { // pre-resolve the receiver type and store it in case the function itself needs it
					fn.receiver = fn.receiver->resolve(this);
					strct = dynamic_pointer_cast<struct_type>(fn.receiver);
					if (!fn.static_function)
						// all receivers are passed by reference to allow for mutation
						fn.receiver = make_shared<ptr_type>(fn.receiver);
				}
				// declare instance variables
				if (strct != nullptr && !fn.static_function) {
					for (const auto& f : strct->fields) {
						cx[f.first].second = f.second;
					}
				}
				vector<llvm::Type*> params;
				if (fn.receiver != nullptr && !fn.static_function)
					params.push_back(type_of(fn.receiver));
				for (const auto& arg : fn.args) {
					params.push_back(type_of(arg.second));
				}
				shared_ptr<type_id> return_type;
				if (fn.return_type) return_type = fn.return_type->resolve(this);
				else return_type = type_of(fn.body, &cx);
				auto F_t = llvm::FunctionType::get(return_type->llvm_type(mod->getContext()), params, false);
				auto F = llvm::cast<llvm::Function>(mod->getOrInsertFunction(fn.selector, F_t));
				auto entry_block = llvm::BasicBlock::Create(mod->getContext(), "entry", F);
				auto vals = F->arg_begin();
				// for member functions/methods initialize `self` variable and instance variables
				if (fn.receiver != nullptr && !fn.static_function) {
					/*llvm::IRBuilder<> irb(entry_block);
					auto self = cx["self"].first = irb.CreateAlloca(v->getType()); vals++;
					irb.CreateStore(llvm::cast<llvm::Value>(&*vals), self);*/
					auto self = cx["self"].first = llvm::cast<llvm::Value>(&*vals);
					cx["self"].second = fn.receiver;
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
						// assign values to instance variables
						auto zero = llvm::ConstantInt::get(mod->getContext(), llvm::APInt(32, 0));
						for (int i = 0; i < strct->fields.size(); ++i) {
							cx[strct->fields[i].first].first =
								llvm::GetElementPtrInst::Create(self->getType()->getPointerElementType(), self, { zero, llvm::ConstantInt::get(mod->getContext(), llvm::APInt(32, i)) }, "", entry_block);
						}
					}
				}
				llvm::IRBuilder<> irb(entry_block);
				for (const auto& arg : fn.args) {
					auto alc = irb.CreateAlloca(vals->getType());
					irb.CreateStore(llvm::cast<llvm::Value>(&*vals), alc);
					cx[arg.first] = { alc, arg.second };
					vals++;
				}
				if (fn.receiver != nullptr) {
					if (fn.static_function) {
						functions[fn.selector].push_back(make_shared<static_fn>(fn, F));
					}
					else {
						functions[fn.selector].push_back(make_shared<method>(fn, F));
					}
				}
				else functions[fn.selector].push_back(make_shared<global_fn>(fn, F));
				generate_expr(cx, dynamic_pointer_cast<ast::block_expr>(fn.body)->body, entry_block);
				return F;
			}
		}

		void code_generator::define_type(const string& name, shared_ptr<type_id> type) {
			types[name] = type_record{ type,{} };
			auto st = dynamic_pointer_cast<struct_type>(type);
			if (st != nullptr) {
				st->init(mod->getContext(), name);
				string csl = "";
				for (const auto& f : st->fields)
					csl += f.first + ":";
				functions[csl].push_back(make_shared<struct_initializer>(st));
			}
		}
	}
}