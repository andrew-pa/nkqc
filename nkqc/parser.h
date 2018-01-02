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
				string n = get_token(isSel);
				idx = oi;
				return n;
			}

			inline void expect(bool x, const string& msg) {
#ifdef _DEBUG
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
			shared_ptr<type_id> receiver;
			bool static_function;
			string selector;
			vector<pair<string, shared_ptr<type_id>>> args;
			shared_ptr<nkqc::ast::expr> body;

			fn_decl(const string& sel, vector<pair<string, shared_ptr<type_id>>> args, shared_ptr<nkqc::ast::expr> body)
				: static_function(false), selector(sel), args(args), body(body) {}
			fn_decl(bool static_, shared_ptr<type_id> rev, const string& sel, vector<pair<string, shared_ptr<type_id>>> args, shared_ptr<nkqc::ast::expr> body)
				: static_function(static_), receiver(rev), selector(sel), args(args), body(body) {}
		};

		struct file_parser : public expr_parser {

			pair<string, shared_ptr<type_id>> parse_name_type_pair() {
				expect(curr_char() == '{', "missing opening curly brace for name-type pair"); next_char();
				next_ws();
				auto n = get_token();
				next_ws();
				pair<string, shared_ptr<type_id>> p = { n, expr_parser::parse_type() };
				next_ws();
				expect(curr_char() == '}', "missing closing curly brace for name-type pair"); next_char();
				return p;
			}

			tuple<string, vector<pair<string, shared_ptr<type_id>>>> parse_sel() {
				string sel; vector<pair<string, shared_ptr<type_id>>> args;
				string t = peek_token(true);
				expect(t.size() > 0, "expect token");
				if (t[t.size() - 1] == ':') {
					while (more_token()) {
						t = get_token(true);
						expect(t[t.size() - 1] == ':', "expect selector words to end with :");
						sel += t;
						next_ws();
						args.push_back(parse_name_type_pair());
						next_ws();
					}
				}
				else {
					sel = get_token();
				}
				return { sel, args };
			}

			void parse_all(const string& s, function<void(const fn_decl&)> FN, function<void(const string&, shared_ptr<type_id>)> S) {
				buf = s; idx = 0;
				while (more()) {
					next_ws();
					auto t = get_token();
					next_ws();
					if (t == "fn") {
						next_ws();
						shared_ptr<type_id> rcv = nullptr;
						bool static_ = false;
						if (curr_char() == '{' || curr_char() == '(') {
							auto opening = curr_char();
							static_ = opening == '(';
							next_char_ws();
							rcv = expr_parser::parse_type();
							next_ws();
							expect(curr_char() == (opening == '{' ? '}' : ')'), "expect closing token for reciever type");
							next_char_ws();
						}
						string sel; vector<pair<string,shared_ptr<type_id>>> args;
						tie(sel, args) = parse_sel();
						next_ws();
						FN(fn_decl(static_, rcv, sel, args, _parse(false, false, false)));
					}
					else if (t == "struct") {
						next_ws();
						string name = get_token();
						next_ws();
						expect(curr_char() == '|', "opening pipe for fields");
						next_char();
						vector<pair<string, shared_ptr<type_id>>> fields;
						while (curr_char() != '|') {
							next_ws();
							fields.push_back(parse_name_type_pair());
							next_ws();
						}
						expect(curr_char() == '|', "closing pipe for fields");
						next_char();
						S(name, make_shared<struct_type>(fields));
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
