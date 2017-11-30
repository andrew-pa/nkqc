#pragma once
#include "ast.h"
#include <cassert>

namespace nkqc {
	namespace parser {
		struct parser {
			string buf;
			uint32_t idx;
			inline bool more() { return idx < buf.size(); }
		protected:

			inline void next_char() { idx++; }
			inline char curr_char() { if (idx > buf.size()) return '\0'; return buf[idx]; }
			inline char peek_char(int of = 1) { if (idx + of > buf.size()) return '\0'; return buf[idx + of]; }
			inline bool more_char() { return idx < buf.size(); }

			inline void next_ws() {
				if (curr_char() == '\"') {
					next_char();
					while (more_char() && curr_char() != '\"') next_char();
					next_char();
				}
				while (more_char() && (isspace(curr_char()) || iscntrl(curr_char()))) next_char();
			}
			
			inline bool istermc(char c) {
				return c == '\0' || isspace(c)
					|| c == '(' || c == ')'
					|| c == '[' || c == ']' || c == '|'
					|| c == ';' || c == '.';
			}
			inline bool isterm(int off = 0, bool bop = true) {
				char c = peek_char(off);
				return istermc(c)
					|| (bop && is_binary_op(off));
			}

			inline void next_char_ws() {
				next_char(); next_ws();
			}

			inline bool more_token() {
				return more_char() && !isterm();
			}

			inline bool next_is_number() {
				return isdigit(curr_char()) || ((curr_char() == '-' || curr_char() == '.') && isdigit(peek_char()));
			}

			
			inline bool is_binary_op(int off = 0) {
				auto c = peek_char(off); auto nc = peek_char(off + 1);
				return (!istermc(c) && !isalnum(c)) && (istermc(nc) || !isalnum(nc)); /*c == '+' || c == '-' || c == '*' || c == '/' || c == '%' || c == '='
					|| ((c == '!' || c == '<' || c == '>') && nc == '=')
					|| c == '<' || c == '>'
					|| c == '&' || c == '|'*/;
			}

			inline string get_binary_op() {
				if (!is_binary_op()) return "";
				auto c = curr_char(); auto nc = peek_char();
				string res(1, c);
				if (!istermc(nc) && !isalnum(nc)/*(c == '!' || c == '<' || c == '>') && nc == '='*/) {
					res += nc;
					next_char();
				}
				next_char();
				return res;
			}

			//TODO: message selector parsing in both method headers and sends expects get_token and peek_token to return tokens ending in ':', but it doesn't do that
			inline string get_token(bool isSel = false) {
				string n;
				do {
					n += curr_char();
					next_char();
				} while (more_char() && !isterm(0,!isSel));
				if (isSel && curr_char() == ':') { n += ':'; next_char(); }
				return n;
			}

			inline string peek_token(bool isSel = false) {
				int oi = idx;
				string n = get_token(isSel);
				idx = oi;
				return n;
			}
		};

		class expr_parser : public parser {
		public:

			inline shared_ptr<ast::expr> parse(const string& s) {
				buf = s;
				idx = 0;
				return _parse(true,true);
			}
			inline shared_ptr<ast::expr> parse(parser& p) {
				buf = p.buf;
				idx = p.idx;
				auto rv = _parse(true, true);
				p.idx = idx;
				return rv;
			}
		protected:
			shared_ptr<ast::number_expr> parse_number();
			string parse_string_lit();
			shared_ptr<ast::msgsnd_expr> parse_msgsnd(shared_ptr<ast::expr> rcv, bool akm);
			pair<pair<string, vector<shared_ptr<ast::expr>>>, int> parse_msgsnd_core();

			shared_ptr<ast::expr> _parse(bool allow_compound, bool allow_keyword_msgsnd, bool allow_any_msgsend = true);

		};

		class class_parser : public parser {
		public:
			class_parser(const string& s) { buf = s; idx = 0; }

			inline shared_ptr<ast::top_level::class_decl> parse() {
				if (!more())return nullptr;
				return _parse();
			}
		protected:
			shared_ptr<ast::top_level::class_decl> _parse();
			ast::top_level::method_decl parse_method();

		};

		string preprocess(const string& s);
	}
}
