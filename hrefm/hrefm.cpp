// hrefm.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

using std::string;
using std::list;

namespace hrefm
{
	class format
	{
		int m_lineWidth;
		int m_validLineWidth;
		int m_continuationColumn;
	};

	class inputFormat : public format
	{};

	class outputFormat 


	class comment
	{
		string text;
	};

	class statement
	{
		int m_lineNum;
		string m_label;
		string m_operation;
		string m_operand;
	};

	class module
	{
		string m_name;
		list<statement> m_statements;
	};
}

int main()
{
    return 0;
}

