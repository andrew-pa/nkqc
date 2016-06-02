#include "vm.h"
#include <memory.h>

#define store(P, V) { auto v = V; *((decltype(v)*)P) = v; P += sizeof(v); }

namespace nkqc {
	namespace vm {
		struct header {
			size_t num_classes;
			size_t num_strings;
			size_t num_blocks;
		};

		image::image(uint8_t* s) {
			header* hp = (header*)s;
			uint8_t* dp = s + sizeof(header);
			for (size_t i = 0; i < hp->num_classes; ++i) {
				string_id_t name = *((string_id_t*)dp);	dp += sizeof(string_id_t);
				string_id_t super = *((string_id_t*)dp);	dp += sizeof(string_id_t);
				size_t num_inst_vars = *((size_t*)dp);		dp += sizeof(size_t);
				vector<string_id_t> iv;
				for (size_t i = 0; i < num_inst_vars; ++i) {
					iv.push_back(*(string_id_t*)dp);
					dp += sizeof(string_id_t);
				}
				size_t num_methods = *((size_t*)dp);		dp += sizeof(size_t);
				map<string_id_t, stmethod> mthds;
				for (size_t mi = 0; mi < num_methods; ++mi) {
					string_id_t sid = *((string_id_t*)dp); dp += sizeof(string_id_t);
					size_t arg_count = *((size_t*)dp);		dp += sizeof(size_t);
					size_t code_size = *((size_t*)dp);		dp += sizeof(size_t);
					vector<instruction> i;
					for (size_t ci = 0; ci < code_size; ++ci) {
						i.push_back(instruction(dp));
					}
					mthds[sid] = stmethod(arg_count, i);
				}
				classes.push_back(stclass(super, name, iv, mthds));	
			}
			for (size_t i = 0; i < hp->num_strings; ++i) {
				size_t len = *((size_t*)dp);		dp += sizeof(size_t);
				char* c = new char[len];
				memcpy(c, dp, len);
				dp += len;
				strings.push_back(string(c));
			}
			for (size_t i = 0; i < hp->num_blocks; ++i) {
				size_t arg_count = *((size_t*)dp);		dp += sizeof(size_t);
				size_t code_size = *((size_t*)dp);		dp += sizeof(size_t);
				vector<instruction> ins;
				for (size_t ci = 0; ci < code_size; ++ci) {
					ins.push_back(instruction(dp));
				}
				blocks.push_back(stblock(arg_count, ins));
			}
		}

		uint8_t* image::serialize() const {
			size_t total_size = sizeof(header); //size in bytes
			for (const auto& c : classes) {
				//Super ID, Name ID, # of methods
				total_size += sizeof(string_id_t) * 2 + sizeof(size_t);
				for (const auto& m : c.methods) {
					total_size += sizeof(string_id_t) + sizeof(size_t);
					for (const auto& i : m.second.code) total_size += i.size();
				}
			}
			for (const auto& s : strings) total_size += strings.size() + sizeof(size_t); //string bytes + string length
			for (const auto& b : blocks) {
				total_size += sizeof(size_t) * 2;
				for (const auto& i : b.code) total_size += i.size();
			}

			uint8_t* data_rootp = new uint8_t[total_size];
			//write header
			header* headerp = (header*)data_rootp;
			headerp->num_classes = classes.size();
			headerp->num_strings = strings.size();
			headerp->num_blocks = strings.size();

			uint8_t* dp = data_rootp + sizeof(header);
			for (const auto& c : classes) {
				store(dp, c.name);
				store(dp, c.super);
				store(dp, c.inst_vars.size());
				for (auto iv : c.inst_vars) {
					store(dp, iv);
				}
				store(dp, c.methods.size());
				for (const auto& m : c.methods) {
					store(dp, m.first);
					store(dp, m.second.arg_count);
					store(dp, m.second.code.size());
					for (const auto& i : m.second.code)
						dp += i.write(dp);
				}
			}
			for (const auto& s : strings) {
				store(dp, s.size());
				memcpy(dp, s.data(), s.size());
				dp += s.size();
			}
			for (const auto& b : blocks) {
				store(dp, b.arg_count);
				store(dp, b.code.size());
				for (const auto& i : b.code)
					dp += i.write(dp);
			}

			return data_rootp;
		}
	}
}