#include "parser.h"

namespace nkqc {
	using namespace ast;

	namespace parser {
		//TODO: fix this so that it is std compliant
		shared_ptr<ast::number_expr> expr_parser::parse_number()
		{
			string numv;
			do {
				numv += curr_char();
				next_char();
			} while (more_token() && (isdigit(curr_char()) || curr_char() == '.'));
			if (numv.find('.') != numv.npos)
				return make_shared<number_expr>(atof(numv.c_str()));
			else
				return make_shared<number_expr>(atoll(numv.c_str()));
		}
		
		string expr_parser::parse_string_lit() {
			string v;
			while (more_char() && curr_char() != '\'') {
				v += curr_char(); next_char();
			}
			assert(curr_char() == '\'');
			next_char();
			return v;
		}
		
		shared_ptr<ast::msgsnd_expr> expr_parser::parse_keyword_msgsnd(shared_ptr<expr> rcv) {
			string msgn; vector<shared_ptr<expr>> args;
			while (more_token()) {
				string mnp = get_token();
				assert(mnp[mnp.size() - 1] == ':');
				msgn += mnp;
				next_ws();
				args.push_back(_parse(false, false));
			}
			return make_shared<keyword_msgsnd>(rcv, msgn, args);
			
		}

		shared_ptr<ast::msgsnd_expr> expr_parser::parse_msgsnd(shared_ptr<expr> rcv, bool akm) {
			string tst = peek_token();
			assert(tst.size() > 0);
			shared_ptr<ast::msgsnd_expr> ms;
			if (akm && tst[tst.size() - 1] == ':') {
				ms = parse_keyword_msgsnd(rcv);
			}
			else if(is_binary_op()) {
				auto op = get_binary_op();
				next_ws();
				ms = make_shared<binary_msgsnd>(rcv, op, _parse(false,true));
			}
			else {
				ms = make_shared<unary_msgsnd>(rcv, get_token());
			}
			next_ws();
			if (curr_char() == ';') {
				next_char_ws();
				//ms = shared_ptr<cascade_msgsnd>(new cascade_msgsnd(ms, parse_msgsnd(rcv, true)));
				ms = make_shared<cascade_msgsnd>(ms, parse_msgsnd(rcv, true));
			}
			return ms;
		}

		shared_ptr<expr> expr_parser::_parse(bool allow_compound, bool allow_keyword_msgsnd, bool allow_any_msgsnd) {
			next_ws();
			shared_ptr<expr> current_expr = nullptr;
			if(curr_char() == '(') {
				next_char_ws();
				current_expr = _parse(true,true);
				next_ws();
				assert(curr_char() == ')');
				next_char_ws();
			}
			else if (curr_char() == '^') {
				next_char_ws();
				return make_shared<return_expr>(_parse(false, true));
			}
			//	-- literals --
			else if (curr_char() == '[') {
				next_char_ws();
				vector<string> args;
				if (peek_char() == ':') { //begining of first arg
					while (curr_char() != '|') {
						string a = get_token();
						assert(a[0] == ':');
						args.push_back(a.substr(1));
						next_ws();
					}
				}
				if (args.size() > 0) {
					assert(curr_char() == '|'); next_char();
				}
				next_ws();
				auto b = _parse(true, true);
				next_ws();
				assert(curr_char() == ']');
				next_char();
				current_expr = make_shared<block_expr>(args, b);
			}
			else if (next_is_number()) {
				current_expr = parse_number();
			}
			else if (curr_char() == '>' && peek_char() == '-') {
				string v;
				while (curr_char() != '<') {
					v += curr_char();
					next_char();
				}
				current_expr = make_shared<tag_expr>(v);
			}
			else if (curr_char() == '\'') {
				next_char();
				current_expr = make_shared<string_expr>(parse_string_lit());
			}
			else if (curr_char() == '$') {
				next_char();
				current_expr = make_shared<char_expr>(get_token());
			}
			else if (curr_char() == '#') {
				next_char();
				if (curr_char() == '\'') {
					current_expr = make_shared<symbol_expr>(parse_string_lit());
				}
				else if (curr_char() == '(') {
					next_char();
					vector<shared_ptr<expr>> xs;
					while (curr_char() != ')') {
						next_ws();
						xs.push_back(_parse(false, false, false));
						next_ws();
					}
					assert(curr_char() == ')');
					next_char();
					current_expr = make_shared<array_expr>(xs);
				}
				else {
					current_expr = make_shared<symbol_expr>(get_token());
				}
			}
			// -- id --
			else {
				auto idx = make_shared<id_expr>(get_token());
				current_expr = idx;
				next_ws();
				if (allow_compound && curr_char() == ':' && peek_char() == '=') {
					next_char(); next_char();
					current_expr = make_shared<assignment_expr>(idx->v, _parse(false, true));
				}
			}
			next_ws();
			// -- msgsnd --
			while (allow_any_msgsnd && more_token() || is_binary_op()) {
				current_expr = parse_msgsnd(current_expr,allow_keyword_msgsnd);
			}
			// -- compound --
			if (allow_compound && curr_char() == '.') {
				next_char();
				current_expr = make_shared<seq_expr>(current_expr, _parse(true,true));
			}
			return current_expr;
		}
		


		shared_ptr<ast::top_level::class_decl> class_parser::_parse() {
			next_ws();
			auto super_class = get_token();
			next_ws();
			auto kwd = get_token();
			assert(kwd == "subclass:");
			next_ws();
			auto class_name = get_token();
			next_ws();
			assert(curr_char() == '[');
			next_char_ws();
			vector<string> iv;
			if (curr_char() == '|') {
				next_char_ws();
				while (curr_char() != '|') {
					iv.push_back(get_token());
					next_ws();
				}
				next_char_ws();
			}
			vector<ast::top_level::method_decl> md;
			while (curr_char() != ']') {
				md.push_back(parse_method());
				next_ws();
			}
			next_char();
			return make_shared<ast::top_level::class_decl>(super_class, class_name, iv, md);
		}

		ast::top_level::method_decl class_parser::parse_method() {
			next_ws();
			auto fkom = get_token();
			ast::top_level::method_decl::modifier md = ast::top_level::method_decl::modifier::none;
			string sel; vector<string> args;
			if (fkom[0] == '<') {
				assert(fkom[fkom.size() - 1] == '>');
				auto m = fkom.substr(1, fkom.size() - 1);
				if (m == "static") md = ast::top_level::method_decl::modifier::static_;
			}
			
			if (fkom[fkom.size() - 1] == ':') {
				sel = fkom;
				while (curr_char() != '[') {
					next_ws();
					args.push_back(get_token());
					next_ws();
					auto nk = get_token();
					assert(nk[nk.size() - 1] == ':');
					sel += nk;
				}
			} 
			else if (!isalnum(fkom[0]) && (fkom.size() > 1 || !isalnum(fkom[1]))) { //binary operator
				sel = fkom;
				next_ws();
				args.push_back(get_token());
				next_ws();
			}
			else { //unary op
				sel = fkom;
				next_ws();
			}
			assert(curr_char() == '[');
			next_char_ws();
			vector<string> lv;
			if (curr_char() == '|') {
				next_char_ws();
				while (curr_char() != '|') {
					lv.push_back(get_token());
					next_ws();
				}
				next_char();
			}
			expr_parser x;
			auto rv = ast::top_level::method_decl(sel, args, lv, x.parse(*this), md);
			next_ws();
			assert(curr_char() == ']');
			next_char();
			return rv;
		}



		string preprocess(const string & s) {
			string fs;
			size_t fqp = s.find('"');
			size_t lfqp = 0;
			while (fqp != s.npos) {
				auto lqp = s.find('"', fqp + 1);
				assert(lqp != s.npos);
				fs += s.substr(lfqp, fqp - lfqp);
				fqp = s.find('"', lqp + 1);
				lfqp = lqp + 1;
			}
			return fs + s.substr(lfqp);
		}
	}		


}