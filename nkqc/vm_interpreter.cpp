#include "vm_interpreter.h"

namespace nkqc {
	namespace vm {
		namespace interpreter {
			

			vmcore::vmcore(const image& i) : strings(i.strings) {
				objects.push_back(nullptr); //object 0 == nullptr == nil
				stobject* class_class_object, *method_class_object;
				class_id_t ci = 0;
				for (const auto& c : i.classes) {
					stobject* o = new stobject(class_class_object, 4);
					o->instance_vars[0] = c.name;
					o->instance_vars[1] = class_idx[c.super];
					o->instance_vars[2] = c.num_inst_vars;
					o->instance_vars[3] = arrays.size();
					vector<value> mthd_map;
					for (const auto& m : c.methods) {
						stobject* om = new stobject(method_class_object, 3);
						om->instance_vars[0] = m.first;
						om->instance_vars[1] = m.second.arg_count;
						om->instance_vars[2] = code_chunks.size();
						code_chunks.push_back(m.second.code);
						mthd_map.push_back(om);
					}
					arrays.push_back(mthd_map);
					class_idx[ci++] = o;
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
						auto cobj = stk.top().object(); stk.pop();
						auto o = new stobject(cobj, cobj->instance_vars[2].integer());
						objects.push_back(o);
						stk.push(value(o));
					} break;
					case opcode::send_message: {
						auto vo = stk.top(); stk.pop();
						map<uint8_t, value> nilc = { {0,vo} };
						/*stmethod* m = nullptr;
						if (!vo.is_object) {
							m = &classes[small_integer_class_id].methods[x];
						}
						else {
							auto o = vo.object();
							auto c = classes[o->cls];
							m = &c.methods[x];
						}*/
						stobject* mo;
						if (!vo.is_object) {
							mo = small_integer_class;
						} else {
							auto co = vo.object()->cls;
							for (const auto& mm : arrays[co->instance_vars[3].integer()]) {
								if(mm.object()->instance_vars[0] == x)
							}
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
