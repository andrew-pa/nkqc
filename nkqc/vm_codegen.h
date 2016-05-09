#pragma once
#include "vm.h"
#include "ast.h"
#include <stack>
#include <algorithm>

namespace nkqc {
	namespace vm {
		namespace codegen {
			struct context {
				vector<instruction> code;
				vector<string> strings;
				vector<stclass> classes;

				string_id_t add_string(const string& s) {
					auto si = find(strings.begin(), strings.end(), s);
					if (si == strings.end()) {
						strings.push_back(s);
						return strings.size() - 1;
					}
					else return distance(strings.begin(), si);
				}
			};
			struct local_context {
				map<string, uint8_t> tb;
				map<string, class_id_t> local_types;
				stack<uint8_t> unused;
				uint8_t next;
				local_context(class_id_t self_type) : next(0) {
					alloc_local("self", self_type); //ensure that self is always local#0
				}

				uint8_t alloc_local(const string& name, class_id_t type) {
					uint8_t v = next;
					if (unused.size() > 0 && tb.find(name) == tb.end()) {
						v = unused.top();
						unused.pop();
					}
					else next++;
					local_types[name] = type;
					return tb[name] = v;
				}

				void free_local(uint8_t v) {
					if (v == 0) throw; //don't free self
					unused.push(v);
					auto x = find_if(tb.begin(), tb.end(), [&](const pair<string, uint8_t>& vv) {
						return vv.second == v;
					});
					if (x != tb.end()) tb.erase(x);
				}
			};

			vector<instruction> assemble(context& c, const string& src);

			class expr_emitter {
#define visit_f_decl(T) void visit(context& cx, local_context& lc, shared_ptr<ast:: T>);
				for_all_ast(visit_f_decl)
#undef visit_f_decl
			public:
				void visit(context& cx, local_context &lc, shared_ptr<ast::expr> xpr);
			};
		}
	}
}