#pragma once
#pragma once
#include "vm.h"
#include "ast.h"
#include <stack>
#include <algorithm>

namespace nkqc {
	namespace vm {
		namespace interpreter {
			struct stobject;

			struct value {
				bool is_object : 1;
				union {
					size_t objectp : 31;
					uint32_t intval : 31;
				};
				value() : is_object(false), intval(0) {}
				value(stobject* obj) : is_object(true), objectp((size_t)obj) {}
				value(uint32_t v) : is_object(false), intval(v) {}
				inline uint32_t integer() {
					assert(!is_object);
					return intval;
				}
				inline stobject* object() {
					assert(is_object);
					return (stobject*)objectp;
				}
			};
			
			struct stobject {
				stobject* cls;
				vector<value> instance_vars;
				stobject(stobject* c, size_t niv) : cls(c), instance_vars(niv, value()) {}
			};

			struct vmcore {
				//vector<stclass> classes;
				map<string_id_t, stobject*> class_idx; //class name => class object
				vector<stobject*> objects;
				vector<string> strings;
				vector<vector<instruction>> code_chunks;
				vector<stblock> blocks;

				stack<value> stk;

				stobject *small_integer_class_obj, 
						*class_class_obj, 
						*method_class_obj, 
						*array_class_obj,
						*block_class_obj;

				string_id_t find_string(const string& str) {
					auto s = find(strings.begin(), strings.end(), str);
					if (s!=strings.end()) return distance(strings.begin(), s);
					else return -1;
				}


				vmcore() {}

				vmcore(const image& i);

				//unique_ptr<image> create_image() const;

				void run(const vector<instruction>& code, map<uint8_t, value> ilc = {});

			};
		}
	}
}