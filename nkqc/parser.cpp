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
		
		pair<pair<string, vector<shared_ptr<expr>>>,int>  expr_parser::parse_msgsnd_core() {
			string tst = peek_token(true);
			assert(tst.size() > 0);
			pair<string, vector<shared_ptr<expr>>> msg;
			int msgt = -1;
			if (tst[tst.size() - 1] == ':') {
				string msgn; vector<shared_ptr<expr>> args;
				while (more_token()) {
					string mnp = get_token(true);
					assert(mnp[mnp.size() - 1] == ':');
					msgn += mnp;
					next_ws();
					args.push_back(_parse(false, false));
				}
				msg = { tst == "value:" ? tst : msgn, args }; //hacky way to make sure that varadic messages of form (value: 1 value: 2 value: 3) work
				msgt = 0;
			}
			else if (is_binary_op()) {
				auto op = get_binary_op();
				next_ws();
				msg = { op,{ _parse(false,true) } };
				msgt = 1;
			}
			else {
				msg = { get_token(),{} };
				msgt = 2;
			}
			return { msg,msgt };
		}

		shared_ptr<ast::msgsnd_expr> expr_parser::parse_msgsnd(shared_ptr<expr> rcv, bool akm) {
			if (!more_char() || isterm(0,false)) return nullptr;
			if (!akm) {
				auto t = peek_token(true);
				if (t.size() > 0 && t[t.size() - 1] == ':') return nullptr;
			}
			auto M = parse_msgsnd_core();
			next_ws();
			if (curr_char() == ';') {
				vector<pair<string, vector<shared_ptr<expr>>>> msgs;
				msgs.push_back(M.first);
				do {
					next_char_ws();
					msgs.push_back(parse_msgsnd_core().first);
					next_ws();
				} while (curr_char() == ';');
				return make_shared<cascade_msgsnd>(rcv, msgs);
			}
			else {
				auto msg = M.first;
				switch (M.second)
				{
				case 0:
					return make_shared<keyword_msgsnd>(rcv, msg.first, msg.second);
				case 1:
					return make_shared<binary_msgsnd>(rcv, msg.first, msg.second[0]);
				case 2:
					return make_shared<unary_msgsnd>(rcv, msg.first);
				}
			}
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
				if (curr_char() == ':') { //begining of first arg
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
				next_char();next_char();
				string v;
				while (curr_char() != '<') {
					v += curr_char();
					next_char();
				}
				next_char();
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
					next_char();
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
			while (allow_any_msgsnd && ((more_char() && !isterm(0,false)) || is_binary_op())) {
				auto msg = parse_msgsnd(current_expr,allow_keyword_msgsnd);
				if (msg) current_expr = msg; else break;
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
			auto kwd = get_token(true);
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
			next_char_ws();
			return make_shared<ast::top_level::class_decl>(super_class, class_name, iv, md);
		}

		ast::top_level::method_decl class_parser::parse_method() {
			next_ws();
			auto fkom = get_token(true);
			ast::top_level::method_decl::modifier md = ast::top_level::method_decl::modifier::none;
			string sel; vector<string> args;
			if (fkom[0] == '!') {
				auto m = fkom.substr(1, fkom.size() - 1);
				if (m == "static") md = ast::top_level::method_decl::modifier::static_;
				else if(m == "varadic") md = ast::top_level::method_decl::modifier::varadic;
				next_ws();
				fkom = get_token(true);
			}
			
			if (fkom[fkom.size()-1] == ':') {
				sel = fkom;
				next_char();
				while (curr_char() != '[') {
					next_ws();
					args.push_back(get_token());
					next_ws();
					if (curr_char() == '[') break; //TODO: this is stupid
					auto nk = get_token(true);
					assert(nk[nk.size()-1] == ':');
					sel += nk;
				}
			} 
			else if (!isalnum(fkom[0]) && (fkom.size() > 1 || !isalnum(fkom[1]))) { //binary operator
				sel = fkom;
				//for (int i = 0; i < fkom.size(); ++i) next_char();
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