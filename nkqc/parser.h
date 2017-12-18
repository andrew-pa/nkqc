#pragma once
#include "ast.h"
#include "types.h"
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
			
			shared_ptr<type_id> parse_type();
		};

		struct type_expr : public ast::expr {
			shared_ptr<type_id> type;

			type_expr(shared_ptr<type_id> t) : type(t) {}

			void print(ostream& os) const override {
				os << "{";
				type->print(os);
				os << "}";
			}

			void visit(ast::expr_visiter<>* V) const override { throw; }
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

		struct decl {
			virtual ~decl() {}
		};

		struct fn_decl {
			shared_ptr<type_id> receiver;
			string selector;
			vector<pair<string, shared_ptr<type_id>>> args;
			shared_ptr<nkqc::ast::expr> body;

			fn_decl(const string& sel, vector<pair<string, shared_ptr<type_id>>> args, shared_ptr<nkqc::ast::expr> body)
				: selector(sel), args(args), body(body) {}
			fn_decl(shared_ptr<type_id> rev, const string& sel, vector<pair<string, shared_ptr<type_id>>> args, shared_ptr<nkqc::ast::expr> body)
				: receiver(rev), selector(sel), args(args), body(body) {}
		};

		struct file_parser : public expr_parser {

			tuple<string, vector<pair<string, shared_ptr<type_id>>>> parse_sel() {
				string sel; vector<pair<string, shared_ptr<type_id>>> args;
				string t = peek_token(true);
				assert(t.size() > 0);
				if (t[t.size() - 1] == ':') {
					while (more_token()) {
						t = get_token(true);
						assert(t[t.size() - 1] == ':');
						sel += t;
						next_ws();
						assert(curr_char() == '{'); next_char();
						next_ws();
						auto n = get_token();
						next_ws();
						args.push_back({ n, expr_parser::parse_type() });
						next_ws();
						assert(curr_char() == '}'); next_char();
						next_ws();
					}
				}
				else {
					sel = get_token();
				}
				return { sel, args };
			}

			void parse_all(const string& s, function<void(const fn_decl&)> FN) {
				buf = s; idx = 0;
				while (more()) {
					next_ws();
					auto t = get_token();
					next_ws();
					if (t == "fn") {
						//type_id pot_recv = parse_type();
						string sel; vector<pair<string,shared_ptr<type_id>>> args;
						tie(sel, args) = parse_sel();
						next_ws();
						FN(fn_decl(sel, args, _parse(false, false, false)));
					}
				}
				return;
			}

			shared_ptr<type_id> parse_type(const string& s) {
				buf = s; idx = 0;
				return expr_parser::parse_type();
			}
		};
	}
}
