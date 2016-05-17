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

		//TODO: 64bits; everywhere
		//TODO: document opcodes

		enum class opcode : uint8_t {
			nop,			//nop:				does nothing
			math,			//math:				do math operations (+,-,*,/)
			push,			//push:				push a 32bit value on to the stack
			push8,			//push8:			push a 8bit value on to the stack
			discard,		//discard:			pop a value off of the stack, discarding it
			load_local,		//load_local:		load local at index and push its value on to the stack
			move_local,		//move_local:		store the value on top of the stack to a local, then pop that value off of the stack
			copy_local,		//copy_local:		store the value on top of the stack to a local, but don't pop it off
			create_object,	//create_object:	create a object of the class named on top of the stack by its string(id) name
			class_for_name, //
			send_message,	//send_message:		send a object a message, object on top of stack, message sel string id in extra bytes
							//TODO: Should send_message have a variant that takes the sel id off the stack?
			//TODO: branching
			load_instance_var,
			move_instance_var,
			copy_instance_var,
		};

		enum class math_opcode : uint16_t {
			//TODO: other operations
			//TODO: decide wither this really needs 16bits
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
				case opcode::load_instance_var:
				case opcode::move_instance_var:
				case opcode::copy_instance_var:
				case opcode::class_for_name:
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
		//typedef int32_t class_id_t; //TODO: make class lookup based on strings like Java
		
		struct stmethod {
			size_t arg_count;
			vector<instruction> code;
			stmethod() {}
			stmethod(size_t argc, vector<instruction> cd) : arg_count(argc), code(cd) {}
		};
		struct stclass {
			string_id_t name;
			string_id_t super;
			vector<string_id_t> inst_vars;
			map<string_id_t, stmethod> methods;
			stclass() {}
			stclass(string_id_t sup, string_id_t nm, vector<string_id_t> iv, map<string_id_t, stmethod> mth) :
				super(sup), name(nm), inst_vars(iv), methods(mth) {}
		};

		//TODO: image is a misnomer, should be something more like class file or something
		//		perhaps imprint or class_image or something
		//		image should be a thing, but it should be VM state like objects and such
		struct image {
			vector<stclass> classes;
			vector<string> strings;
			
			image(vector<stclass> cs = {}, vector<string> ss = {}) : classes(cs), strings(ss) {}
			image(uint8_t* s);

			uint8_t* serialize() const;
		};
	}
}