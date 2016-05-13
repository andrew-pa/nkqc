#include "vm_interpreter.h"

namespace nkqc {
	namespace vm {
		namespace interpreter {
			

			vmcore::vmcore(const image& i) : strings(i.strings) {
				objects.push_back(nullptr); //object 0 == nullptr == nil
				auto	class_str = find_string("Class"),
						method_str = find_string("Method"),
						sint_str = find_string("SmallInteger"),
						array_str = find_string("Array");
				class_class_obj = new stobject(nullptr, 4);
				method_class_obj = new stobject(class_class_obj, 4);
				array_class_obj = new stobject(class_class_obj, 4);
				for (const auto& c : i.classes) {
					stobject* o = nullptr;
					if (c.name == class_str) o = class_class_obj;
					else if (c.name == method_str) o = method_class_obj;
					else if (c.name == array_str) o = array_class_obj;
					else o = new stobject(class_class_obj, 4);
					o->instance_vars[0] = c.name;
					if (c.name == sint_str) small_integer_class_obj = o;
					o->instance_vars[1] = class_idx[c.super];
					if (c.inst_vars.size() > 0) {
						auto ivaro = new stobject(array_class_obj, 1 + c.inst_vars.size());
						objects.push_back(ivaro);
						o->instance_vars[2] = value(ivaro);
						ivaro->instance_vars[0] = c.inst_vars.size();
						size_t i = 1;
						for (const auto& v : c.inst_vars) {
							ivaro->instance_vars[i++] = value(v);
						}
					}
					else o->instance_vars[2] = value(nullptr);

					if (c.methods.size() > 0) {
						auto maro = new stobject(array_class_obj, 1 + c.methods.size());
						objects.push_back(maro);
						o->instance_vars[3] = value(maro);
						maro->instance_vars[0] = c.methods.size();
						size_t i = 1;
						for (const auto& m : c.methods) {
							stobject* om = new stobject(method_class_obj, 3);
							om->instance_vars[0] = m.first;
							om->instance_vars[1] = m.second.arg_count;
							om->instance_vars[2] = code_chunks.size();
							code_chunks.push_back(m.second.code);
							objects.push_back(om);
							maro->instance_vars[i] = value(om);
						}
					}
					else o->instance_vars[3] = value(nullptr);

					class_idx[c.name] = o;
					objects.push_back(o);
				}
			}

			void vmcore::run(const vector<instruction>& code, map<uint8_t, value> ilc) {
				map<uint8_t, value> locals = ilc;
				for (int pc = 0; pc < code.size(); ++pc) {
					auto x = code[pc].extra;
					switch (code[pc].op) {
					case opcode::nop: break;
					case opcode::discard: stk.pop(); break;
					case opcode::push:
					case opcode::push8: stk.push(value(x)); break;
					case opcode::load_local: stk.push(locals[x]); break;
					case opcode::copy_local: locals[x] = stk.top(); break;
					case opcode::move_local: locals[x] = stk.top(); stk.pop(); break;
					case opcode::create_object: {
						auto cobj = class_idx[stk.top().integer()]; stk.pop();
						auto o = new stobject(cobj, cobj->instance_vars[2].integer());
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
							for (size_t i = 1; i < mar[0].integer(); ++i) {
								if (mar[i].object()->instance_vars[0].integer() == x) {
									mo = mar[i].object();
									break;
								}
							}
							class_of_recv = class_of_recv->instance_vars[1].object(); //get super class
						}
						for (int i = 0; i < mo->instance_vars[1].integer(); ++i) {
							nilc[i + 1] = stk.top(); stk.pop();
						}
						run(code_chunks[mo->instance_vars[2].integer()], nilc);
					} break;
					case opcode::math: 
						switch ((math_opcode)(x & 0x0000ffff)) {
						case math_opcode::iadd: {
							auto a = stk.top().integer(); stk.pop();
							auto b = stk.top().integer(); stk.pop();
							stk.push(a+b);
						} break;
						case math_opcode::isub: {
							auto a = stk.top().integer(); stk.pop();
							auto b = stk.top().integer(); stk.pop();
							stk.push(a - b);
						} break;
						case math_opcode::imul: {
							auto a = stk.top().integer(); stk.pop();
							auto b = stk.top().integer(); stk.pop();
							stk.push(a * b);
						} break;
						case math_opcode::idiv: {
							auto a = stk.top().integer(); stk.pop();
							auto b = stk.top().integer(); stk.pop();
							stk.push(a / b);
						} break;
						}
						break;
					}
				}
			}

		}
	}
}
