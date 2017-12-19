#pragma once
#include <llvm/IR/Type.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Constant.h>
#include <llvm/IR/Constants.h>
#include <llvm/ADT/APInt.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/IRBuilder.h>
#include "parser.h"

namespace nkqc {
	struct type_id {
		virtual llvm::Type* llvm_type(llvm::LLVMContext&) const = 0;
		virtual bool equals(shared_ptr<type_id> o) const = 0; // welcome to Java-land
		virtual void print(ostream& os) const = 0;

		virtual bool can_cast_to(shared_ptr<type_id>) const { return false; }
		virtual llvm::Value* cast_to(llvm::LLVMContext& cx, shared_ptr<type_id> target_type, llvm::Value* src, llvm::IRBuilder<>& b) const { return nullptr; }

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
		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) const override {
			return llvm::Type::getVoidTy(c);
		}
		virtual bool equals(shared_ptr<type_id> o) const override {
			return dynamic_pointer_cast<unit_type>(o) != nullptr;
		}
		virtual void print(ostream& os) const override {
			os << "()";
		}
	};
	struct bool_type : public type_id {
		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) const override {
			return llvm::Type::getInt1Ty(c);
		}
		virtual bool equals(shared_ptr<type_id> o) const override {
			return dynamic_pointer_cast<bool_type>(o) != nullptr;
		}
		virtual void print(ostream& os) const override {
			os << "bool";
		}
	};
	struct ptr_type;
	struct integer_type : public type_id {
		uint8_t bitwidth;
		bool signed_;

		integer_type(bool s, uint8_t bw)
			: signed_(s), bitwidth(bw) {}

		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) const override {
			return llvm::Type::getIntNTy(c, bitwidth);
		}

		virtual bool equals(shared_ptr<type_id> o) const override {
			auto p = dynamic_pointer_cast<integer_type>(o);
			return p != nullptr && p->bitwidth == bitwidth && p->signed_ == signed_;
		}
		
		virtual void print(ostream& os) const override {
			os << (signed_ ? "i" : "u") << (int)bitwidth;
		}

		virtual bool can_cast_to(shared_ptr<type_id> t) const {
			return dynamic_pointer_cast<integer_type>(t) != nullptr || dynamic_pointer_cast<ptr_type>(t) != nullptr;
		}
		virtual llvm::Value* cast_to(llvm::LLVMContext& cx, shared_ptr<type_id> target_type, llvm::Value* src, llvm::IRBuilder<>& irb) const {
			auto intag = dynamic_pointer_cast<integer_type>(target_type);
			if (intag == nullptr) {
				return irb.CreateBitOrPointerCast(src, target_type->llvm_type(cx));
			}
			return irb.CreateIntCast(src, intag->llvm_type(cx), intag->signed_);
		}
	};
	struct plain_type : public type_id {
		string name;
		plain_type(const string& n) : name(n) {}

		virtual llvm::Type* llvm_type(llvm::LLVMContext&) const override {
			return nullptr;
		}
		virtual bool equals(shared_ptr<type_id> o) const override {
			auto p = dynamic_pointer_cast<plain_type>(o);
			return p != nullptr && p->name == name;
		}
		virtual void print(ostream& os) const override {
			os << name;
		}
	};
	struct ptr_type : public type_id {
		shared_ptr<type_id> inner;
		ptr_type(shared_ptr<type_id> inner) : inner(inner) {}
		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) const override {
			return inner->llvm_type(c)->getPointerTo();
		}

		virtual bool equals(shared_ptr<type_id> o) const override {
			auto p = dynamic_pointer_cast<ptr_type>(o);
			return p != nullptr && inner->equals(p->inner);
		}
		virtual void print(ostream& os) const override {
			os << "*";
			inner->print(os);
		}
		
		virtual bool can_cast_to(shared_ptr<type_id> t) const {
			return dynamic_pointer_cast<integer_type>(t) != nullptr || dynamic_pointer_cast<ptr_type>(t) != nullptr;
		}
		virtual llvm::Value* cast_to(llvm::LLVMContext& cx, shared_ptr<type_id> target_type, llvm::Value* src, llvm::IRBuilder<>& irb) const {
			return irb.CreateBitOrPointerCast(src, target_type->llvm_type(cx));
		}
	};
	struct array_type : public type_id {
		size_t count;
		shared_ptr<type_id> element;
		array_type(size_t count, shared_ptr<type_id> e) : element(e), count(count) {}

		virtual llvm::Type* llvm_type(llvm::LLVMContext& c) const override {
			return llvm::ArrayType::get(element->llvm_type(c), count);
		}

		virtual bool equals(shared_ptr<type_id> o) const override {
			auto p = dynamic_pointer_cast<array_type>(o);
			return p != nullptr && count == p->count && element->equals(p->element);
		}
		virtual void print(ostream& os) const override {
			os << "[" << count << "]";
			element->print(os);
		}
		
		virtual bool can_cast_to(shared_ptr<type_id> t) const {
			auto pt = dynamic_pointer_cast<ptr_type>(t);
			return pt != nullptr && element->equals(pt->inner);
		}
		virtual llvm::Value* cast_to(llvm::LLVMContext& cx, shared_ptr<type_id> target_type, llvm::Value* src, llvm::IRBuilder<>& irb) const {
			if (!can_cast_to(target_type)) return nullptr;

			//auto v = new llvm::AllocaInst(target_type->llvm_type(cx), 0, "", bb);
			//llvm::StoreInst _(src, v, bb);
			auto alc = irb.CreateAlloca(src->getType());
			irb.CreateStore(src, alc);
			return irb.CreateGEP(llvm_type(cx), alc, {  llvm::ConstantInt::get(cx,llvm::APInt(32, 0)), llvm::ConstantInt::get(cx,llvm::APInt(32, 0)) });
		}
	};

}