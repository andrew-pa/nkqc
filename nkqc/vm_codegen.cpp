#include "vm_codegen.h"
#include <sstream>

namespace nkqc {
	namespace vm {
		namespace codegen {
			
#define visitf(T) void expr_emitter::visit(const ast:: T& xpr)

			visitf(id_expr) {
				auto lci = lc->tb.find(xpr.v);
				if (lci != lc->tb.end()) {
					lc->code.push_back(instruction(opcode::load_local, lci->second));
				}
				else {
					if (xpr.v == "true") {
						lc->code.push_back(instruction(opcode::special_value, (uint8_t)special_values::truev));
						return;
					} else if(xpr.v == "false") {
						lc->code.push_back(instruction(opcode::special_value, (uint8_t)special_values::falsev));
						return;
					} else if (xpr.v == "nil") {
						lc->code.push_back(instruction(opcode::special_value, (uint8_t)special_values::nil));
						return;
					}
					auto si = cx->find_string(xpr.v);
					for (const auto& c : cx->classes) {
						 if (lc->local_types["self"] == c.name) {
							auto ii = find(c.inst_vars.begin(), c.inst_vars.end(), si);
							if (ii != c.inst_vars.end()) {
								lc->code.push_back(instruction(opcode::load_instance_var, (uint32_t)distance(c.inst_vars.begin(),ii)));
								return;
							}
						}
					}
					//otherwise must be a class
					if (si == -1) si = cx->add_string(xpr.v); //insure that first mentions work anyways
					lc->code.push_back(instruction(opcode::class_for_name, (uint32_t)si));
				}
			}
			visitf(string_expr) {
				lc->code.push_back(instruction(opcode::push, (uint32_t)cx->add_string(xpr.v)));
				lc->code.push_back(instruction(opcode::special_value, (uint8_t)special_values::create_str));
			}
			visitf(number_expr) {
				if (xpr.type == 'i') {
					if (abs(xpr.iv) < numeric_limits<uint32_t>::max()) {
						lc->code.push_back(instruction(opcode::push, (uint32_t)xpr.iv));
					} else {/*64bit number, create a object (Integer64)*/}
				} else {/*float, create a object(Float)*/}
			}
			visitf(block_expr) {
				lc->code.push_back(instruction(opcode::create_block, (uint32_t)cx->blocks.size()));
				expr_emitter ex(cx);
				local_context nlc(lc); //lc->local_types["self"]);
				for (const auto& a : xpr.argnames) {
					nlc.alloc_local(a, 0);
				}
				ex.visit(nlc, xpr.body);
				cx->blocks.push_back(stblock(xpr.argnames.size(), nlc.code));
			}
			visitf(symbol_expr) {
				lc->code.push_back(instruction(opcode::push, (uint32_t)cx->add_string(xpr.v)));
				lc->code.push_back(instruction(opcode::special_value, (uint8_t)special_values::internsymbol));
			}
			visitf(char_expr) {
				//TODO: impl Char literal codegen
			}
			visitf(array_expr) {
				lc->code.push_back(instruction(opcode::push, (uint32_t)xpr.vs.size()));
				lc->code.push_back(instruction(opcode::special_value, (uint8_t)special_values::create_ary));
				auto tmparr = lc->alloc_local("__tmp_array", 0);
				lc->code.push_back(instruction(opcode::move_local, tmparr));
				auto atput_msg = (uint32_t)cx->add_string("at:put:");
				for (int i = 0; i < xpr.vs.size(); ++i) {
					xpr.vs[i]->visit(this);
					lc->code.push_back(instruction(opcode::push, (uint32_t)i));
					lc->code.push_back(instruction(opcode::load_local, tmparr));
					lc->code.push_back(instruction(opcode::send_message, atput_msg));
				}
				lc->code.push_back(instruction(opcode::load_local, tmparr));
				lc->free_local(tmparr);
			}
			visitf(seq_expr) {
				xpr.first->visit(this);
				xpr.second->visit(this);
			}
			visitf(return_expr) {
				xpr.val->visit(this);
			}
			visitf(unary_msgsnd) {
				xpr.rcv->visit(this);
				lc->code.push_back(instruction(opcode::send_message, 
					(uint32_t)cx->add_string(xpr.msgname)));
			}
			visitf(binary_msgsnd) {
				xpr.rhs->visit(this);
				xpr.rcv->visit(this);
				lc->code.push_back(instruction(opcode::send_message,
					(uint32_t)cx->add_string(xpr.op)));
			}
			visitf(keyword_msgsnd) {
				for (int i = xpr.args.size() - 1; i >= 0; i--) {
					xpr.args[i]->visit(this);
				}
				xpr.rcv->visit(this);
				lc->code.push_back(instruction(opcode::send_message,
					(uint32_t)cx->add_string(xpr.msgname)));
			}
			visitf(cascade_msgsnd) {
				xpr.rcv->visit(this);
				auto rcvl = lc->alloc_local("___tmp_rcv", 0);
				lc->code.push_back(instruction(opcode::copy_local, rcvl));
				for (const auto& m : xpr.msgs) {
					lc->code.push_back(instruction(opcode::discard));
					if(m.second.size()>0) for (int i = m.second.size() - 1; i >= 0; i--) {
						m.second[i]->visit(this);
					}
					lc->code.push_back(instruction(opcode::load_local, rcvl));
					lc->code.push_back(instruction(opcode::send_message,
						(uint32_t)cx->add_string(m.first)));
				}
				lc->free_local(rcvl);
			}
			visitf(assignment_expr) {
				auto si = cx->find_string(xpr.name);
				if (si > 0) {
					for (const auto& c : cx->classes) {
						if (lc->local_types["self"] == c.name) {
							auto ii = find(c.inst_vars.begin(), c.inst_vars.end(), si);
							if (ii != c.inst_vars.end()) {
								xpr.val->visit(this);
								lc->code.push_back(instruction(opcode::move_instance_var, (uint32_t)distance(c.inst_vars.begin(), ii)));
								return;
							}
						}
					}
				}
				auto lci = lc->alloc_local(xpr.name, 0); //TODO: why does this need to know the type of locals?
				xpr.val->visit(this);
				lc->code.push_back(instruction(opcode::move_local, lci));
			}
			visitf(tag_expr) {
				if (xpr.v.size() > 3 && xpr.v.substr(0,3) == "asm") {
					auto c = assemble(*cx, xpr.v.substr(4));
					lc->code.insert(lc->code.end(), c.begin(), c.end());
				}
			}

			void expr_emitter::visit(local_context& _lc, shared_ptr<ast::expr> xpr) {
				lc = &_lc;
				xpr->visit(this);
//#define visit_test(T) { auto p = dynamic_pointer_cast<ast:: T>(xpr); if(p) return visit(cx, lc, p); }
//				for_all_ast(visit_test)
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
					else if (op == "clsof") instr.push_back(instruction(opcode::class_of));
					else if (op == "error") instr.push_back(instruction(opcode::error));
					else if (op == "invoke_block") instr.push_back(instruction(opcode::invoke_block));
					else {
						string ex = ln.substr(sp+1);
						uint32_t exv = 0;
						if (ex[0] == '!') {
							exv = c.add_string(ex.substr(1));
						}
						else if(isdigit(ex[0])) {
							exv = (uint32_t)stoi(ex.c_str(),0,0);
						}
						else if (ex == "$stack") {
							instr.push_back(instruction(opcode::operand_from_stack));
						}
						if (op == "push") instr.push_back(instruction(opcode::push, exv));
						else if (op == "push8") instr.push_back(instruction(opcode::push8, (uint8_t)exv));
						else if (op == "ldlc") instr.push_back(instruction(opcode::load_local, (uint8_t)exv));
						else if (op == "mvlc") instr.push_back(instruction(opcode::move_local, (uint8_t)exv));
						else if (op == "cplc") instr.push_back(instruction(opcode::copy_local, (uint8_t)exv));
						else if (op == "ldinst") instr.push_back(instruction(opcode::load_instance_var, (uint32_t)exv));
						else if (op == "mvinst") instr.push_back(instruction(opcode::move_instance_var, (uint32_t)exv));
						else if (op == "cpinst") instr.push_back(instruction(opcode::copy_instance_var, (uint32_t)exv));
						else if (op == "sndmsg") instr.push_back(instruction(opcode::send_message, exv));
						else if (op == "clsnmd") instr.push_back(instruction(opcode::class_for_name, exv));
						else if (op == "br") instr.push_back(instruction(opcode::branch, exv));
						else if (op == "brt") instr.push_back(instruction(opcode::branch_true, exv));
						else if (op == "brf") instr.push_back(instruction(opcode::branch_false, exv));
						else if (op == "debug_print") instr.push_back(instruction(opcode::debug_print));
						else if (op == "math") {
							uint32_t rex = exv;
							
							if (ex == "i+") { rex = (uint32_t)math_opcode::iadd; }
							else if (ex == "i-") { rex = (uint32_t)math_opcode::isub; }
							else if (ex == "i*") { rex = (uint32_t)math_opcode::imul; }
							else if (ex == "i/") { rex = (uint32_t)math_opcode::idiv; }
							else if (ex == "i%") { rex = (uint32_t)math_opcode::irem; }
							else if (ex == "iabs") { rex = (uint32_t)math_opcode::iabs; }
							
							instr.push_back(instruction(opcode::math, rex));
						}
						else if (op == "cmp") {
							uint8_t rx = exv;
							if(ex == "eql") rx = (uint8_t)compare_opcode::equal;
							else if (ex == "neq") rx = (uint8_t)compare_opcode::not_equal;
							else if (ex == "less") rx = (uint8_t)compare_opcode::less;
							else if (ex == "lesseq") rx = (uint8_t)compare_opcode::less_equal;
							else if (ex == "grtr") rx = (uint8_t)compare_opcode::greater;
							else if (ex == "grtreq") rx = (uint8_t)compare_opcode::greater_equal;
							instr.push_back(instruction(opcode::compare, rx));
						}
						else if (op == "spvl") {
							uint8_t sv = exv;
							if (ex == "nil") sv = (uint8_t)special_values::nil;
							else if (ex == "true") sv = (uint8_t)special_values::truev;
							else if (ex == "false") sv = (uint8_t)special_values::falsev;
							else if (ex == "ninsv") sv = (uint8_t)special_values::num_instance_vars;
							else if (ex == "chrob") sv = (uint8_t)special_values::character_object;
							else if (ex == "hash") sv = (uint8_t)special_values::hash;
							else if (ex == "internsymbol") sv = (uint8_t)special_values::internsymbol;
							else if (ex == "create_ary") sv = (uint8_t)special_values::create_ary;
							else if (ex == "create_str") sv = (uint8_t)special_values::create_str;
							else if (ex == "istr") sv = (uint8_t)special_values::int_string;
							instr.push_back(instruction(opcode::special_value, sv));
						}
						else throw runtime_error("unknown opcode: " + op);
					}
				}
				return instr;
			}
		}
	}
}
