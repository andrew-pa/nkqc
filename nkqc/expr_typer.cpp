#include "llvm_codegen.h"

namespace nkqc {
	namespace codegen {
		void code_generator::expr_typer::visit(const nkqc::ast::id_expr &x) {
			if (x.v == "true" || x.v == "false") {
				s.push(make_shared<bool_type>());
			}
			else s.push(cx->at(x.v).second);
		}
		void code_generator::expr_typer::visit(const nkqc::ast::string_expr &x) {
			s.push(make_shared<array_type>(x.v.size(), make_shared<integer_type>(false, 8)));
		}
		void code_generator::expr_typer::visit(const nkqc::ast::number_expr &x) {
			if (x.type == 'i') {
				s.push(make_shared<integer_type>(true, 32));
			}
			else throw internal_codegen_error("floating point literals currently unsupported");
		}
		void code_generator::expr_typer::visit(const nkqc::ast::block_expr &x) {
			x.body->visit(this);
		}
		void code_generator::expr_typer::visit(const nkqc::ast::symbol_expr &x) {
		}
		void code_generator::expr_typer::visit(const nkqc::ast::char_expr &x) {
		}
		void code_generator::expr_typer::visit(const nkqc::ast::array_expr &x) {
		}
		void code_generator::expr_typer::visit(const nkqc::ast::tag_expr &x) {
		}
		void code_generator::expr_typer::visit(const nkqc::ast::seq_expr &x) {
			x.first->visit(this);
			s.pop(); // discard type of first expression, evaluate it only for side effects
			x.second->visit(this);
		}
		void code_generator::expr_typer::visit(const nkqc::ast::return_expr &x) {
			x.val->visit(this);
		}
		void code_generator::expr_typer::visit(const nkqc::ast::unary_msgsnd &x) {
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
				if (rcv_t->receive_by_ref())
					rcv_t = make_shared<ptr_type>(rcv_t);
			}
			auto f = gen->lookup_function(x.msgname, rcv_t, {});
			if (f != nullptr) {
				s.push(f->return_type(gen, rcv_t, {}));
			}
			else throw no_such_function_error("attempted to compute return type for unary function", x.msgname, rcv_t, {});
		}
		void code_generator::expr_typer::visit(const nkqc::ast::binary_msgsnd &x) {
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
				if (rcv->receive_by_ref())
					rcv = make_shared<ptr_type>(rcv);
			}
			auto sf = gen->lookup_function(x.op, rcv, { rhs });
			if (sf != nullptr)
				s.push(sf->return_type(gen, rcv, { rhs }));
			else
				throw no_such_function_error("attempted to compute return type", x.op, rcv, { rhs });
		}
		void code_generator::expr_typer::visit(const nkqc::ast::keyword_msgsnd &x) {
			auto glob = dynamic_pointer_cast<nkqc::ast::symbol_expr>(x.rcv);
			auto tx = dynamic_pointer_cast<nkqc::parser::type_expr>(x.rcv);
			auto block_rcv = dynamic_pointer_cast<nkqc::ast::block_expr>(x.rcv);
			shared_ptr<type_id> rcv_t;
			if (glob != nullptr && glob->v == "G") {
				rcv_t = nullptr;
			}
			else if (tx != nullptr) {
				rcv_t = tx->type->resolve(gen);
			}
			else if (block_rcv != nullptr) {
				s.push(make_shared<unit_type>());
				return;
			}
			else {
				x.rcv->visit(this);
				rcv_t = s.top()->resolve(gen); s.pop();
				if (rcv_t->receive_by_ref())
					rcv_t = make_shared<ptr_type>(rcv_t);
			}
			vector<shared_ptr<type_id>> arg_t;
			for (const auto& arg : x.args) {
				arg->visit(this);
				arg_t.push_back(s.top()->resolve(gen)); s.pop();
			}
			if (dynamic_pointer_cast<bool_type>(rcv_t) != nullptr) {
				if (x.msgname == "ifTrue:ifFalse:") {
					s.push(arg_t[0]);
					return;
				}
			}
			auto f = gen->lookup_function(x.msgname, rcv_t, arg_t);
			if (f != nullptr) {
				s.push(f->return_type(gen, rcv_t, arg_t));
			}
			else throw no_such_function_error("attempted to compute return type", x.msgname, rcv_t, arg_t);
		}
		void code_generator::expr_typer::visit(const nkqc::ast::cascade_msgsnd &x) {
		}
		void code_generator::expr_typer::visit(const nkqc::ast::assignment_expr &x) {
			x.val->visit(this);
			cx->insert_or_assign(x.name, s.top());
			s.pop();
			s.push(make_shared<unit_type>());
		}
	}
}