// hrefm.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

using std::string;
using std::list;
using std::tuple;
using std::get;
using std::function;
using boost::optional;
using std::runtime_error;
using std::size_t;

namespace hrefm
{
	struct format
	{
		size_t m_lineWidth;
		size_t m_validLineWidth;
		size_t m_continuationColumn;

		format()
			: m_lineWidth{ 80 }, m_validLineWidth{ 72 }, m_continuationColumn{ 15 }
		{}
	};

	struct inputFormat : public format
	{};

	struct outputFormat : public format
	{
		char m_continuationChar;
	};

	class comment
	{
		bool m_standalone;
		size_t m_lineNum;
		string m_text;

	public:
		comment(bool standalone, size_t lineNum, string text)
			: m_standalone{ standalone }, m_lineNum{ lineNum }, m_text{ text }
		{
		}
	};

	struct statement
	{
		size_t m_indent;
		size_t m_lineNum;
		string m_label;
		string m_operation;
		string m_operand;

	private:
		statement()
			: m_indent {0}
		{
		}

	public:
		static tuple<statement, optional<comment>> parse(string longLine, inputFormat f, size_t lineNum)
		{
			statement s;
			s.m_lineNum = lineNum;

			optional<comment> c{};

			string commentText;
			enum class state_t { label, find_operation, operation, find_operand, operand, operand_quote, find_comment, comment } state{ state_t::label };
			for (auto i = longLine.begin(); i != longLine.end(); i++)
			{
				switch (state)
				{
				case state_t::label:
					if (!std::isgraph(*i))
						state = state_t::find_operation;
					else
						s.m_label += *i;
					break;
				case state_t::find_operation:
					if (std::isgraph(*i))
						state = state_t::operation;
					else
						break;
				case state_t::operation:
					if (!std::isgraph(*i))
						state = state_t::find_operand;
					else
						s.m_operation += *i;
					break;
				case state_t::find_operand:
					if (std::isgraph(*i))
						state = state_t::operand;
					else
						break;
				case state_t::operand:
					if (!std::isgraph(*i))
						state = state_t::find_comment;
					else
					{
						if (*i == '\'')
						{
							if (!boost::algorithm::ends_with(s.m_operand, "L"))
								state = state_t::operand_quote;
						}
						s.m_operand += *i;
					}
					break;
				case state_t::operand_quote:
					s.m_operand += *i;
					if (*i == '\'')
						state = state_t::operand;
					break;
				case state_t::find_comment:
					if (std::isgraph(*i))
						state = state_t::comment;
					else
						break;
				case state_t::comment:
					commentText += *i;
					break;
				}
			}

			if (!commentText.empty())
				c = comment{ false, lineNum, commentText };

			return{ s, c };
		}

		string write()
		{
			std::stringstream ss;
			ss << m_label << " " << string(m_indent, ' ') << m_operation << " " << m_operand;

			return ss.str();
		}
	};

	struct transform
	{
		function<void(statement)> replace_statement;
		function<void(comment)> add_comment;
	};

	class rule
	{
	public:
		virtual void apply(statement s, transform & t) = 0;
	};

	class module
	{
		list<statement> m_statements;
		list<comment> m_comments;

	private:
		module()
		{
		}

	public:
		static module parse(std::istream & in, inputFormat f)
		{
			module m;
			string longLine;

			bool continuation{ false };
			size_t lineNum{ 0 };
			while (true)
			{
				string line;

				if (in.bad())
					throw runtime_error{ "input stream: bad" };
				if (in.eof())
				{
					if (continuation)
						throw runtime_error{ "syntax: unexpected end of file" };
					break;
				}

				std::getline(in, line);
				lineNum++;

				if (line.length() > f.m_lineWidth)
					throw runtime_error{ "input stream: invalid line length" };

				line = line.substr(0, f.m_validLineWidth);

				if (line.length() < f.m_validLineWidth)
					line += string(f.m_validLineWidth - line.length(), ' ');

				if (!continuation)
					longLine.clear();
				else
					if (!boost::algorithm::all(boost::make_iterator_range(line.begin(), line.begin() + f.m_continuationColumn - 1), !boost::algorithm::is_graph()))
						throw runtime_error{ "syntax: bad continuation" };

				if (std::isgraph(line[f.m_validLineWidth - 1]))
					continuation = true;
				else
					continuation = false;

				if (continuation)
					line[f.m_validLineWidth - 1] = ' ';

				if (!longLine.empty())
					line = line.substr(f.m_continuationColumn);

				if (continuation)
					boost::trim_right(line);

				longLine += line;

				if (!continuation)
				{
					if (longLine[0] == '*')
					{
						m.m_comments.emplace_back(true, lineNum, longLine);
					}
					else
					{
						auto sc = statement::parse(longLine, f, lineNum);
						m.m_statements.push_back(get<0>(sc));
						if (get<1>(sc))
							m.m_comments.push_back(get<1>(sc).get());
					}
				}
			}

			return m;
		}

		void apply_rule(rule & r)
		{
			for (auto &s : m_statements)
			{
				transform t{
					[&s](statement newStatement) {
						s = newStatement;
					},
					[this](comment c) {
						m_comments.push_back(c);
					},
				};

				r.apply(s, t);
			}
		}

		void write(std::ostream & out)
		{
			for (auto &s : m_statements)
			{
				out << s.write();
				out << std::endl;
			}
		}
	};

	class operand_to_comment_rule : public rule
	{
	public:
		void apply(statement s, transform & t)
		{
			if (boost::algorithm::starts_with(s.m_operand, "@"))
			{
				t.add_comment({ false, s.m_lineNum, s.m_operand });
				s.m_operand = "";
				t.replace_statement(s);
			}
		}
	};

	class empty_operand_rule : public rule
	{
	public:
		void apply(statement s, transform & t)
		{
			if (!s.m_operation.empty() && s.m_operand.empty())
			{
				s.m_operand = ",";
				t.replace_statement(s);
			}
		}
	};

	class indent_if : public rule
	{
	private:
		size_t m_level;
	public:
		indent_if()
			: m_level{ 0 }
		{
		}

		void apply(statement s, transform & t)
		{
			auto thisLevel{ m_level };
			if (s.m_operation == "IF" && !boost::algorithm::contains(s.m_operand, "GOTO="))
				m_level++;
			else if (s.m_operation == "IFEND")
			{
				m_level--;
				thisLevel = m_level;
			}
			else if (s.m_operation == "ELSE" || s.m_operation == "ELSEIF")
				thisLevel = m_level - 1;

			if (thisLevel < 0)
				throw runtime_error(" indent_if rule: non matching ELSE or ELSEIF");

			if (m_level < 0)
				throw runtime_error(" indent_if rule: non matching ENDIF");

			if (thisLevel > 0)
			{
				s.m_indent += 2 * thisLevel;
				t.replace_statement(s);
			}
		}
	};

	class indent_repeat : public rule
	{
	private:
		size_t m_level;
	public:
		indent_repeat()
			: m_level{ 0 }
		{
		}

		void apply(statement s, transform & t)
		{
			auto thisLevel{ m_level };
			if (s.m_operation == "REPEAT")
				m_level++;
			else if (s.m_operation == "REPEND")
			{
				m_level--;
				thisLevel = m_level;
			}

			if (m_level < 0)
				throw runtime_error(" indent_if rule: non matching REPEND");

			if (thisLevel > 0)
			{
				s.m_indent += 2 * thisLevel;
				t.replace_statement(s);
			}
		}
	};
}

using namespace hrefm;

int main()
{
	std::ios_base::sync_with_stdio(false);

	inputFormat inFormat;

	auto m{ module::parse(std::cin, inFormat) };

	m.apply_rule(operand_to_comment_rule{});
	m.apply_rule(empty_operand_rule{});
	m.apply_rule(indent_if{});
	m.apply_rule(indent_repeat{});

	m.write(std::cout);

	return 0;
}

