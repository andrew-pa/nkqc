#include "vm_interpreter.h"

namespace nkqc {
	namespace vm {
		namespace interpreter {
			

			vmcore::vmcore(const image& i) : strings(i.strings), blocks(i.blocks) {
				objects.push_back(nullptr); //object 0 == nullptr == nil
				objects.push_back(nullptr); //object 1 == true
				objects.push_back(nullptr); //object 2 == false
				auto	class_str = find_string("Class"),
						method_str = find_string("Method"),
						sint_str = find_string("SmallInteger"),
						array_str = find_string("Array"),
						bool_str = find_string("Boolean"),
						block_str = find_string("Block");
				class_class_obj = new stobject(nullptr, 4);
				method_class_obj = new stobject(class_class_obj, 4);
				array_class_obj = new stobject(class_class_obj, 4);
				block_class_obj = new stobject(class_class_obj, 4); //TODO: make it so that these become subclasses of ObjectClass instead!
				for (const auto& c : i.classes) {
					stobject* o = nullptr;
					if (c.name == class_str) o = class_class_obj;
					else if (c.name == method_str) o = method_class_obj;
					else if (c.name == array_str) o = array_class_obj;
					else if (c.name == block_str) o = block_class_obj;
					//TODO: !!! sort out ClassClass stuff
					else {
						stobject* cls_of_this_cls = nullptr;
						if (c.flgs == stclass::flags::meta_class) { cls_of_this_cls = class_class_obj; }
						else {
							auto n = strings[c.name] + "Class";
							cls_of_this_cls = class_idx[find_string(n)];
						}
						o = new stobject(cls_of_this_cls, 4);
					}
					o->instance_vars[0] = c.name;
					if (c.name == sint_str) small_integer_class_obj = o;
					o->instance_vars[1] = class_idx[c.super];
					//if (c.inst_vars.size() > 0) {
						auto ivaro = new stobject(array_class_obj, c.inst_vars.size());
						objects.push_back(ivaro);
						o->instance_vars[2] = value(ivaro);
						size_t i = 0;
						for (const auto& v : c.inst_vars) {
							ivaro->instance_vars[i++] = value(v);
						}
					//}
					//else o->instance_vars[2] = value(nullptr);

					//if (c.methods.size() > 0) {
						auto maro = new stobject(array_class_obj, c.methods.size());
						objects.push_back(maro);
						o->instance_vars[3] = value(maro);
						i = 0;
						for (const auto& m : c.methods) {
							stobject* om = new stobject(method_class_obj, 3);
							om->instance_vars[0] = m.first;
							om->instance_vars[1] = m.second.arg_count;
							om->instance_vars[2] = code_chunks.size();
							code_chunks.push_back(m.second.code);
							objects.push_back(om);
							maro->instance_vars[i++] = value(om);
						}
					//}
					//else o->instance_vars[3] = value(nullptr);

					class_idx[c.name] = o;
					objects.push_back(o);
				}
				objects[1] = new stobject(class_idx[bool_str], 1);
				objects[2] = new stobject(class_idx[bool_str], 1);
			}

			void vmcore::run(const vector<instruction>& code, map<uint8_t, value> ilc) {
				map<uint8_t, value> locals = ilc;
				for (int pc = 0; pc < code.size(); ++pc) {
					auto x = code[pc].extra;
					start_interpret_switch:
					switch (code[pc].op) {
					case opcode::operand_from_stack: {
						x = stk.top().integer();
						stk.pop();
						++pc;
						goto start_interpret_switch;
					} break;
					case opcode::nop: break;
					case opcode::discard: if(!stk.empty()) stk.pop(); break; //makes discard a nop if there's nothing, which makes sense for most of it's uses
					case opcode::push:
					case opcode::push8: stk.push(value(x)); break;
					case opcode::load_local: stk.push(locals[x]); break;
					case opcode::copy_local: locals[x] = stk.top(); break;
					case opcode::move_local: locals[x] = stk.top(); stk.pop(); break;
					case opcode::load_instance_var: stk.push(locals[0].object()->instance_vars[x]); break;
					case opcode::move_instance_var: locals[0].object()->instance_vars[x] = stk.top(); stk.pop(); break;
					case opcode::copy_instance_var: locals[0].object()->instance_vars[x] = stk.top(); break;
					case opcode::class_for_name: {
						auto ci = x;
						if (ci == (uint32_t)-1) {
							ci = stk.top().integer();
							stk.pop();
						}
						stk.push(class_idx[x]);
					} break;
					case opcode::class_of: {
						auto vo = stk.top(); stk.pop();
						stobject* cls = nullptr;
						if (!vo.is_object) {
							cls = small_integer_class_obj;
						}
						else {
							cls = vo.object()->cls;
						}
						stk.push(cls);
					} break;
					case opcode::create_object: {
						auto cobj = /*class_idx[stk.top().integer()]*/stk.top().object(); stk.pop();
						auto o = new stobject(cobj, cobj->instance_vars[2].object() == nullptr ? 0 : 
							cobj->instance_vars[2].object()->instance_vars.size());
						objects.push_back(o);
						stk.push(value(o));
					} break;
					case opcode::send_message: {
						auto vo = stk.top(); stk.pop();
						map<uint8_t, value> nilc = { {0,vo} };
						stobject* class_of_recv = nullptr;
						if (!vo.is_object) {
							class_of_recv = small_integer_class_obj;
						} else {
							class_of_recv = vo.object()->cls;
						}
						stobject* mo = nullptr;
						while (class_of_recv != nullptr) {
							auto mar = class_of_recv->instance_vars[3].object()->instance_vars;
							for (size_t i = 0; i < mar.size(); ++i) {
								if (mar[i].object()->instance_vars[0].integer() == x) {
									mo = mar[i].object();
									goto found_method;
								}
							}
							class_of_recv = class_of_recv->instance_vars[1].object(); //get super class
						}
						found_method:
						for (int i = 0; i < mo->instance_vars[1].integer(); ++i) {
							nilc[i + 1] = stk.top(); stk.pop();
						}
						run(code_chunks[mo->instance_vars[2].integer()], nilc);
					} break;
					case opcode::compare: {
						auto a = stk.top().intval; stk.pop();
						auto b = stk.top().intval; stk.pop();
						bool cmp;
						switch ((compare_opcode)x) {
						case compare_opcode::equal: cmp = (a == b); break;
						case compare_opcode::not_equal: cmp = (a != b); break;
						case compare_opcode::less: cmp = (a < b); break;
						case compare_opcode::less_equal: cmp = (a <= b); break;
						case compare_opcode::greater: cmp = (a > b); break;
						case compare_opcode::greater_equal: cmp = (a >= b); break;
						}
						stk.push(cmp ? objects[1] : objects[2]);
					}break;
					case opcode::math: {
						auto o = (math_opcode)(x & 0x0000ffff);
						auto a = stk.top().integer(); stk.pop();
						if (o == math_opcode::iabs) {
							stk.push(abs((int)a));
						}
						else {
							auto b = stk.top().integer(); stk.pop();
							switch (o) {
							case math_opcode::iadd:	stk.push(a + b); break;
							case math_opcode::isub: stk.push(a - b); break;
							case math_opcode::imul: stk.push(a * b); break;
							case math_opcode::idiv: stk.push(a / b); break;
							case math_opcode::irem: stk.push(a % b); break;
							}
						}
					}break;
					case opcode::branch: pc = x-1; break;
					case opcode::branch_true: {
						auto a = stk.top().object(); stk.pop();
						if (a == objects[1]) pc = x - 1;
					} break;
					case opcode::branch_false: {
						auto a = stk.top().object(); stk.pop();
						if (a == objects[2]) pc = x - 1;
					} break;
					case opcode::create_block: {
						auto b = new stobject(block_class_obj, 2);
						b->instance_vars[0] = x; //block id
						auto cla = (b->instance_vars[1] = new stobject(array_class_obj, locals.size())).object();
						auto I = locals.begin();
						for (int i = 0; I != locals.end(); ++i, ++I) {
							cla->instance_vars[i] =
								new stobject(array_class_obj, 2);
							cla->instance_vars[i].object()->instance_vars[0] =
								I->first;
							cla->instance_vars[i].object()->instance_vars[1] =
								I->second;
						}
						stk.push(b);
					} break;
					case opcode::invoke_block: {
						auto bo = stk.top().object(); stk.pop();
						assert(bo->cls == block_class_obj);
						auto b = blocks[bo->instance_vars[0].integer()];
						map<uint8_t, value> nilc;
						for (auto& lclp : bo->instance_vars[1].object()->instance_vars) {
							nilc[lclp.object()->instance_vars[0].integer()] =
								lclp.object()->instance_vars[1];
						}
						for (int i = 0; i < b.arg_count; ++i) {
							nilc[i + 1] = stk.top(); stk.pop();
						}
						run(b.code, nilc);
					} break;
					case opcode::special_value: {
						switch ((special_values)x) {
						case special_values::nil: stk.push(value(nullptr)); break;
						case special_values::truev: stk.push(objects[1]); break;
						case special_values::falsev: stk.push(objects[2]); break;
						case special_values::num_instance_vars: { 
							auto v = stk.top().object(); stk.pop(); 
							stk.push(v->instance_vars.size()); 
						} break;
						case special_values::character_object: break;
						case special_values::hash: { 
							auto v = stk.top().object(); stk.pop(); 
							stk.push(hash<stobject*>()(v));
						} break;
						}
					} break;
					case opcode::error: throw x;

					}
				}
			}

		}
	}
}
