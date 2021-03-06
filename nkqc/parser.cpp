#include "parser.h"

namespace nkqc {
	using namespace ast;

	namespace parser {

		shared_ptr<type_id> parser::parse_type() {
			switch (curr_char()) {
			case '*':
				next_char();
				return make_shared<ptr_type>(parse_type());
			case '[': {
				next_char();
				string numv;
				do {
					numv += curr_char();
					next_char();
				} while (more_token() && isdigit(curr_char()));
				expect(curr_char() == ']', "expected closing square bracket for array"); next_char();
				return make_shared<array_type>(atoll(numv.c_str()), parse_type());
			}
			case 'u':
			case 'i': {
				char type = curr_char();
				next_char();
				string numv;
				do {
					numv += curr_char();
					next_char();
				} while (more_token() && isdigit(curr_char()));
				// this should fall through to default case if there isn't a number afterwards
				return make_shared<integer_type>(type == 'i', atoi(numv.c_str()));
			}
			case '(': {
				next_char();
				if (curr_char() == ')') {
					next_char();
					return make_shared<unit_type>();
				}
				else {
					next_ws();
					vector<shared_ptr<type_id>> args;
					while (curr_char() != ')') {
						next_ws();
						args.push_back(parse_type());
					}
					next_char();
					next_ws();
					expect(curr_char() == '-' && peek_char() == '>', "expected function arrow after arguments");
					next_char(); next_char();
					next_ws();
					return make_shared<function_type>(args, parse_type());
				}
			}
			default: {
				auto tk = get_token();
				if (tk == "bool") return make_shared<bool_type>();
				return make_shared<plain_type>(tk);
			}
			}
		}


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
			expect(curr_char() == '\'', "missing closing quote for string");
			next_char();
			return v;
		}
		
		pair<pair<string, vector<shared_ptr<expr>>>,int>  expr_parser::parse_msgsnd_core() {
			string tst = peek_token(true);
			expect(tst.size() > 0, "expect token");
			pair<string, vector<shared_ptr<expr>>> msg;
			int msgt = -1;
			if (tst[tst.size() - 1] == ':') {
				string msgn; vector<shared_ptr<expr>> args;
				while (more_token()) {
					string mnp = get_token(true);
					expect(mnp[mnp.size() - 1] == ':', "expect selector to end with :");
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
				expect(curr_char() == ')', "missing closing paren");
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
						expect(a[0] == ':', "expected block argument to start with :");
						args.push_back(a.substr(1));
						next_ws();
					}
				}
				if (args.size() > 0) {
					expect(curr_char() == '|', "expected delimiting | for block arguments"); next_char();
				}
				next_ws();
				auto b = _parse(true, true);
				next_ws();
				expect(curr_char() == ']', "expected closing square bracket");
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
					expect(curr_char() == ')', "missing closing paren for array");
					next_char();
					current_expr = make_shared<array_expr>(xs);
				}
				else {
					current_expr = make_shared<symbol_expr>(get_token());
				}
			}
			else if (curr_char() == '{') {
				next_char();
				current_expr = make_shared<type_expr>(parse_type());
				expect(curr_char() == '}', "missing closing curly brace for type");
				next_char();
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

		pair<string, shared_ptr<type_id>> file_parser::parse_name_type_pair() {
			expect(curr_char() == '{', "missing opening curly brace for name-type pair"); next_char();
			next_ws();
			auto n = get_token();
			next_ws();
			pair<string, shared_ptr<type_id>> p = { n, expr_parser::parse_type() };
			next_ws();
			expect(curr_char() == '}', "missing closing curly brace for name-type pair"); next_char();
			return p;
		}

		tuple<string, vector<pair<string, shared_ptr<type_id>>>> file_parser::parse_sel() {
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

		void file_parser::parse_all(const string& s, function<void(const fn_decl&)> FN, function<void(const string&, shared_ptr<type_id>)> S) {
			buf = s; idx = 0;
			while (more()) {
				next_ws();
				auto t = get_token();
				next_ws();
				if (t == "fn") {
					next_ws();
					shared_ptr<type_id> rcv = nullptr, ret = nullptr;
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
					string sel; vector<pair<string, shared_ptr<type_id>>> args;
					tie(sel, args) = parse_sel();
					next_ws();
					if (curr_char() == '-' && peek_char(1) == '>') {
						next_char(); next_char_ws();
						ret = expr_parser::parse_type();
						next_ws();
					}
					FN(fn_decl(static_, rcv, sel, args, _parse(false, false, false), ret));
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
}

}