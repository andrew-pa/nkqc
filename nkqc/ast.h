#pragma once
#include <string>
#include <memory>
#include <vector>
#include <iostream>
using namespace std;

#define for_all_ast(X) \
	X(id_expr)\
	X(string_expr)\
	X(number_expr)\
	X(block_expr)\
	X(symbol_expr)\
	X(char_expr)\
	X(array_expr)\
	X(tag_expr)\
	X(seq_expr)\
	X(return_expr)\
	X(unary_msgsnd)\
	X(binary_msgsnd)\
	X(keyword_msgsnd)\
	X(cascade_msgsnd)\
	X(assignment_expr)

namespace nkqc{
	namespace ast {
		/*
			<expr> := [id] | <literal> | <msgsnd> | '(' <expr> ')' | <expr> '.' <expr> | <block-expr>
						| <assignment-expr> | <return-expr>
			<literal> := [string] | [number] | [# symbol] | [# array/dict] | [$ char]
			<msgsnd> := <expr:rcv> (<unary-msgsnd> | <binary-msgsnd> | <keyword-msgsnd> | <cascaded-msgsnd>)
			<unary-msgsnd> := [id]
			<binary-msgsnd> := [binary-operator] <expr>
			<keyword-msgsnd> := ([id"ending in :"] <expr>)+
			<cascaded-msgsnd> := <msgsnd> ';' <msgsnd>

			<block-expr> := '[' (':'[id]+ '|')  <expr> ']'
			<assignment-expr> := <id> ':=' <expr>
			<return-expr> := '^' <expr>
		*/

#define prdt(T) struct T;
		for_all_ast(prdt)
#undef prdt

		struct expr_visiter {
#define visit_dcl(T) virtual void visit(const T& x) = 0;
			for_all_ast(visit_dcl)
#undef visit_dcl
		};

		struct expr {
			virtual void print(ostream& os) const = 0;
			virtual void visit(expr_visiter* V) const = 0;
			virtual ~expr() {}
		};

		struct id_expr : public expr { 
			string v;
			id_expr(const string& V) : v(V) {}

			void print(ostream& os) const override { os << v; }
			
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};

		struct string_expr : public expr { 
			string v;
			string_expr(const string& V) : v(V) {}
			void print(ostream& os) const override { os << "'" << v << "'"; }
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};

		struct tag_expr : public expr {
			string v;
			tag_expr(const string& V) : v(V) {}
			void print(ostream& os) const override { os << "<" << v << ">"; }
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};

		struct char_expr : public expr {
			string chr;
			char_expr(const string& ch) : chr(ch) {}
			void print(ostream& os) const override {
				os << "$" << chr;
			}
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};

		struct number_expr : public expr {
			union { int64_t iv; double fv; };
			char type;
			number_expr(int64_t i) : iv(i), type('i') {}
			number_expr(double f) : fv(f), type('f') {}
			void print(ostream& os) const override { os << (type == 'i' ? iv : fv); }
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};

		struct block_expr : public expr {
			vector<string> argnames; //without leading ':'
			shared_ptr<expr> body;
			block_expr(const vector<string>& an, shared_ptr<expr> b) : argnames(an), body(b) {}
			void print(ostream& os) const override {
				os << "[ ";
				for (const auto& a : argnames) os << ":" << a << " ";
				if (argnames.size() > 0) os << "| ";
				body->print(os);
				os << " ]";
			}
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};
		
		struct symbol_expr : public expr { 
			string v; //missing leading #
			symbol_expr(const string& V) : v(V) {}
			void print(ostream& os) const override { os << "#" << v; }
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};
		struct array_expr : public expr {
			vector<shared_ptr<expr>> vs;
			array_expr(const vector<shared_ptr<expr>>& Vs) : vs(Vs) {}
			void print(ostream& os) const override {
				os << "#( ";
				for (auto x : vs) {
					x->print(os);
					os << " ";
				}
				os << ")";
			}
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};

		struct seq_expr : public expr {
			shared_ptr<expr> first, second;
			seq_expr(shared_ptr<expr> f, shared_ptr<expr> s) : first(f), second(s) {}
			void print(ostream& os) const override { first->print(os); os << "." << endl; second->print(os); }
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};

		struct return_expr : public expr {
			shared_ptr<expr> val;

			return_expr(shared_ptr<expr> v) : val(v) {}
			void print(ostream& os) const override { os << "^ "; val->print(os); }
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};

		struct msgsnd_expr : public expr {
			shared_ptr<expr> rcv;
			msgsnd_expr(shared_ptr<expr> rcv_) : rcv(rcv_) {}
		};
		struct unary_msgsnd : public msgsnd_expr {
			string msgname;
			unary_msgsnd(shared_ptr<expr> rcv_, const string& mn) : msgsnd_expr(rcv_), msgname(mn) {}
			void print(ostream& os) const override { rcv->print(os); os << " " << msgname; }
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};
		/*enum class binary_operator { 
			add,			// + 
			sub,			// -
			mul,			// *
			div,			// / 
			mod,			// %
			equals,			// =
			less,			// <
			greater,		// >
			less_equal,		// <=
			greater_equal,	// >= 
			not,			// ! 
			not_equal,		// !=
			tilde,			// ~
			at,				// @
			hash,			// #
			dollar,			// $
			question,		// ?
			carat,			// ^
			and,			// &
			or,				// |
		};*/
		struct binary_msgsnd : public msgsnd_expr {
			string op;
			shared_ptr<expr> rhs;
			binary_msgsnd(shared_ptr<expr> rcv_, const string& op_, shared_ptr<expr> rhs_) : msgsnd_expr(rcv_), op(op_), rhs(rhs_) {}
			void print(ostream& os) const override { rcv->print(os); os << " " << op << " "; rhs->print(os); }
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};
		struct keyword_msgsnd : public msgsnd_expr {
			string msgname; //concat, [[a: 1 b: 2 c: 3]] ---> 'a:b:c:' with args={1,2,3}
			vector<shared_ptr<expr>> args;
			keyword_msgsnd(shared_ptr<expr> rcv_, const string& mn, const vector<shared_ptr<expr>>& args_) : msgsnd_expr(rcv_), msgname(mn), args(args_) {}
			void print(ostream& os) const override {
				rcv->print(os);
				os << " " << msgname << " ";
				for (auto a : args) { a->print(os); os << " "; }
			}
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};
		struct cascade_msgsnd : public msgsnd_expr {
			vector<pair<string,vector<shared_ptr<expr>>>> msgs;
			cascade_msgsnd(shared_ptr<expr> rcv,
				vector<pair<string, vector<shared_ptr<expr>>>> f) : msgsnd_expr(rcv), msgs(f) {}
			void print(ostream& os) const override { 
				//TODO: implement printing for cascades
			}
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};
		
		struct assignment_expr : public expr {
			string name;
			shared_ptr<expr> val;
			assignment_expr(const string& n, shared_ptr<expr> v) : name(n), val(v) {}
			void print(ostream& os) const override {
				os << name << " := ";
				val->print(os);
			}
			void visit(expr_visiter* V) const override { V->visit(*this); }
		};

		namespace top_level {
			/*
				<file> := <class>+
				<class> := <id> subclass: <id> '[' ('|' <id>+ '|') <method-dcl>+ ']'
				<method-dcl> := ('<static>') <sel> '[' ('|' <id>+ '|') <expr> ']'
				<sel> := <id> | <binop> | (<id>':' <id>)+
			*/

			struct method_decl {
				enum class modifier {
					none, varadic, static_
				};
				modifier mod;
				string sel;
				vector<string> args;
				vector<string> local_vars;
				shared_ptr<expr> body;

				method_decl(const string& s, const vector<string>& ag, const vector<string>& lv, shared_ptr<expr> b, modifier md = modifier::none)
					: mod(md), sel(s), args(ag), local_vars(lv), body(b) {}
			};

			struct class_decl {
				string super_name;
				string name;
				vector<string> instance_vars;
				vector<method_decl> methods;

				class_decl(const string& sn, const string& n, const vector<string>& iv, const vector<method_decl>& md)
					: super_name(sn), name(n), instance_vars(iv), methods(md) {}
			};
		}
	}
}
