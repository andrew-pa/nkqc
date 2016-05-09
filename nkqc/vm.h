#pragma once
#include <stdint.h>
#include <cassert>
#include <vector>
#include <map>
using namespace std;

namespace nkqc {
	namespace vm {
		/*
			VM arch
			Stack machine w/ local registers in function call
			Messages are named w/ strings (encoded as a ID # of that string)
			Each 32-bit value's highest bit indicates wiether that value is a SmallInteger or a Object
				IDs of all kinds are SmallIntegers
		*/

		enum class opcode : uint8_t {
			nop,
			math,
			push,
			push8,
			discard,
			load_local,
			move_local,
			copy_local,
			create_object,
			send_message,
		};

		enum class math_opcode : uint16_t {
			iadd, isub, imul, idiv
		};

		struct instruction {
			opcode op;
			uint32_t extra;

			instruction(opcode op_) : op(op_) { assert(extra_size() == 0); }
			instruction(opcode op_, uint8_t ex) : op(op_), extra(ex) { assert(extra_size() == 8); }
			instruction(opcode op_, uint32_t ex) : op(op_), extra(ex) { assert(extra_size() == 32); }

			uint8_t extra_size() const {
				switch (op)
				{
				case opcode::nop:
				case opcode::discard:
				case opcode::create_object:
					return 0;
				case opcode::push8:
				case opcode::load_local:
				case opcode::move_local:
				case opcode::copy_local:
					return 8;
				case opcode::send_message:
				case opcode::math:
				case opcode::push:
					return 32;
				}
			}

			size_t size() const { return sizeof(opcode) + extra_size(); }

			//read, will move ptr
			instruction(uint8_t*& ptr) {
				op = (opcode)*ptr;
				ptr += sizeof(opcode);
				if (extra_size() == 8) {
					extra = *ptr;
					ptr += sizeof(uint8_t);
				}
				else if (extra_size() == 32) {
					extra = *((uint32_t*)ptr);
					ptr += sizeof(uint32_t);
				}
			}

			size_t write(uint8_t* ptr) const {
				*ptr = (uint8_t)op;
				ptr += sizeof(uint8_t);
				if (extra_size() == 8) {
					*ptr = (uint8_t)extra;
					return 2;
				}
				else if (extra_size() == 32) {
					*((uint32_t*)ptr) = extra;
					return 5;
				}
				return 1;
			}
		};
		
		typedef int32_t string_id_t;
		typedef int32_t method_id_t;
		typedef int32_t class_id_t;
		
		struct stmethod {
			size_t arg_count;
			vector<instruction> code;
			stmethod() {}
			stmethod(size_t argc, vector<instruction> cd) : arg_count(argc), code(cd) {}
		};
		struct stclass {
			string_id_t name;
			class_id_t super;
			size_t num_inst_vars;
			map<string_id_t, stmethod> methods;
			stclass() {}
			stclass(class_id_t sup, string_id_t nm, size_t niv, map<string_id_t, stmethod> mth) :
				super(sup), name(nm), num_inst_vars(niv), methods(mth) {}
		};

		struct image {
			vector<stclass> classes;
			vector<string> strings;
			
			image(vector<stclass> cs = {}, vector<string> ss = {}) : classes(cs), strings(ss) {}
			image(uint8_t* s);

			uint8_t* serialize() const;
		};
	}
}