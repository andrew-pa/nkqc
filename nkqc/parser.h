#pragma once
#include "ast.h"
#include "types.h"
#include <cassert>
#include <sstream>

namespace nkqc {
	namespace parser {
		struct parse_error : public runtime_error {
			uint32_t line, col;
			parse_error(const string& m, uint32_t ln, uint32_t cl)
				: runtime_error(m), line(ln), col(cl) {}
		};
		struct parser {
			string buf;
			uint32_t idx, line, col;
			inline bool more() { return idx < buf.size(); }
			inline void reset(const string& s) {
				buf = s;
				idx = line = col = 0;
			}
			inline void copy_state(const parser& p) {
				buf = p.buf;
				idx = p.idx;
				line = p.line;
				col = p.col;
			}
		protected:

			inline void next_char() {
				idx++; col++;
				if (curr_char() == '\n') { line++; col = 0; }
			}
			inline char curr_char() { if (idx > buf.size()) return '\0'; return buf[idx]; }
			inline char peek_char(int of = 1) { if (idx + of > buf.size()) return '\0'; return buf[idx + of]; }
			inline bool more_char() { return idx < buf.size(); }

			inline void next_ws() {
				while (more_char() && (isspace(curr_char()) || iscntrl(curr_char()) || curr_char() == '\"')) {
					if (curr_char() == '\"') {
						next_char();
						while (more_char() && curr_char() != '\"') next_char();
						next_char();
					}
					else next_char();
				}
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
				size_t ol = line, oc = col;
				string n = get_token(isSel);
				idx = oi;
				line = ol; col = oc;
				return n;
			}

			inline void expect(bool x, const string& msg) {
#ifdef _DEBUG
				if(!x) cout << "error at " << line << ", " << col << ": " << msg << endl;
				assert(x);
#endif
				if(!x) throw parse_error(msg, line, col);
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
				reset(s);
				return _parse(true,true);
			}
			inline shared_ptr<ast::expr> parse(parser& p) {
				copy_state(p);
				auto rv = _parse(true, true);
				p.copy_state(*this);
				return rv;
			}
		protected:
			shared_ptr<ast::number_expr> parse_number();
			string parse_string_lit();
			shared_ptr<ast::msgsnd_expr> parse_msgsnd(shared_ptr<ast::expr> rcv, bool akm);
			pair<pair<string, vector<shared_ptr<ast::expr>>>, int> parse_msgsnd_core();

			shared_ptr<ast::expr> _parse(bool allow_compound, bool allow_keyword_msgsnd, bool allow_any_msgsend = true);

		};

		string preprocess(const string& s);

		struct decl {
			virtual ~decl() {}
		};

		struct fn_decl {
			shared_ptr<type_id> receiver, return_type;
			bool static_function;
			string selector;
			vector<pair<string, shared_ptr<type_id>>> args;
			shared_ptr<nkqc::ast::expr> body;

			fn_decl(const string& sel, vector<pair<string, shared_ptr<type_id>>> args, shared_ptr<nkqc::ast::expr> body, shared_ptr<type_id> ret)
				: static_function(false), selector(sel), args(args), body(body), return_type(ret) {}
			fn_decl(bool static_, shared_ptr<type_id> rev, const string& sel, vector<pair<string, shared_ptr<type_id>>> args, shared_ptr<nkqc::ast::expr> body, shared_ptr<type_id> ret)
				: static_function(static_), receiver(rev), selector(sel), args(args), body(body), return_type(ret) {}
		};

		struct file_parser : public expr_parser {

			pair<string, shared_ptr<type_id>> parse_name_type_pair();

			tuple<string, vector<pair<string, shared_ptr<type_id>>>> parse_sel();

			void parse_all(const string& s, function<void(const fn_decl&)> FN, function<void(const string&, shared_ptr<type_id>)> S);

			shared_ptr<type_id> parse_type(const string& s) {
				buf = s; idx = 0;
				return expr_parser::parse_type();
			}
		};
	}
}
