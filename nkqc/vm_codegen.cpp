#include "vm_codegen.h"
#include <sstream>

namespace nkqc {
	namespace vm {
		namespace codegen {
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::id_expr> xpr) {
				auto lci = lc.tb.find(xpr->v);
				if (lci != lc.tb.end()) {
					cx.code.push_back(instruction(opcode::load_local, lci->second));
				}
				else {
					//could be a instance var or class name
				}
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::string_expr> xpr) {
				cx.code.push_back(instruction(opcode::push, (uint32_t)cx.add_string(xpr->v)));
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::number_expr> xpr) {
				if (xpr->type == 'i') {
					if (abs(xpr->iv) < INT_MAX) {
						cx.code.push_back(instruction(opcode::push, (uint32_t)xpr->iv));
					} else {/*64bit number, create a object (Integer64)*/}
				} else {/*float, create a object(Float)*/}
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::block_expr> xpr) {
				//create Block object
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::symbol_expr> xpr) {
				//create Symbol object
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::array_expr> xpr) {
				//create Array object
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::seq_expr> xpr) {
				visit(cx, lc, xpr->first);
				visit(cx, lc, xpr->second);
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::return_expr> xpr) {
				visit(cx, lc, xpr->val);
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::unary_msgsnd> xpr) {
				visit(cx, lc, xpr->rcv);
				cx.code.push_back(instruction(opcode::send_message, 
					(uint32_t)cx.add_string(xpr->msgname)));
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::binary_msgsnd> xpr) {
				visit(cx, lc, xpr->rhs);
				visit(cx, lc, xpr->rcv);
				cx.code.push_back(instruction(opcode::send_message,
					(uint32_t)cx.add_string(xpr->op)));
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::keyword_msgsnd> xpr) {
				for (int i = xpr->args.size() - 1; i >= 0; i--) {
					visit(cx, lc, xpr->args[i]);
				}
				visit(cx, lc, xpr->rcv);
				cx.code.push_back(instruction(opcode::send_message,
					(uint32_t)cx.add_string(xpr->msgname)));
			}
			void expr_emitter::visit(context& cx, local_context& lc,shared_ptr<ast::cascade_msgsnd> xpr) {
				
			}

			void expr_emitter::visit(context& cx, local_context& lc, shared_ptr<ast::expr> xpr) {
#define visit_test(T) { auto p = dynamic_pointer_cast<ast:: T>(xpr); if(p) return visit(cx, lc, p); }
				for_all_ast(visit_test)
			}

			const char* ws = " \t\n\r\f\v";

			// trim from end of string (right)
			inline std::string& rtrim(std::string& s, const char* t = ws)
			{
				s.erase(s.find_last_not_of(t) + 1);
				return s;
			}

			// trim from beginning of string (left)
			inline std::string& ltrim(std::string& s, const char* t = ws)
			{
				s.erase(0, s.find_first_not_of(t));
				return s;
			}

			// trim from both ends of string (left & right)
			inline std::string& trim(std::string& s, const char* t = ws)
			{
				return ltrim(rtrim(s, t), t);
			}

			vector<instruction> assemble(context& c, const string& src) {
				istringstream ss(src);
				vector<instruction> instr;
				while (!ss.eof()) {
					string ln; getline(ss, ln, ';');
					ln = trim(ln);
					auto sp = ln.find_first_of(' ');
					string op = ln.substr(0,sp);
					if (op == "nop") instr.push_back(instruction(opcode::nop));
					else if (op == "disc") instr.push_back(instruction(opcode::discard));
					else if (op == "crobj") instr.push_back(instruction(opcode::create_object));
					else {
						string ex = ln.substr(sp+1);
						uint32_t exv = 0;
						if (ex[0] == '!') {
							exv = c.add_string(ex.substr(1));
						}
						else if(isdigit(ex[0])) {
							exv = (uint32_t)stoi(ex.c_str(),0,0);
						}
						if (op == "push") instr.push_back(instruction(opcode::push, exv));
						else if (op == "push8") instr.push_back(instruction(opcode::push8, (uint8_t)exv));
						else if (op == "ldlc") instr.push_back(instruction(opcode::load_local, (uint8_t)exv));
						else if (op == "mvlc") instr.push_back(instruction(opcode::move_local, (uint8_t)exv));
						else if (op == "cplc") instr.push_back(instruction(opcode::copy_local, (uint8_t)exv));
						else if (op == "sndmsg") instr.push_back(instruction(opcode::send_message, exv));
						else if (op == "math") {
							uint32_t rex = 0;
							switch (ex[0]) {
							case '+': rex = (uint32_t)math_opcode::iadd; break;
							case '-': rex = (uint32_t)math_opcode::isub; break;
							case '*': rex = (uint32_t)math_opcode::imul; break;
							case '/': rex = (uint32_t)math_opcode::idiv; break;
							default: rex = exv; break;
							}
							instr.push_back(instruction(opcode::math, rex));
						}
						else throw runtime_error("unknown opcode: " + op);
					}
				}
				return instr;
			}
		}
	}
}