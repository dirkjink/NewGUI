/*
 * Copyright 2026, The NewGUI Authors. All rights reserved.
 * Distributed under the terms of the MIT License.
 * SPDX-License-Identifier: MIT
 */


#include "PluralForms.h"

#include <cctype>
#include <string>


namespace ngui::locale {


namespace {

constexpr int kMaxDepth = 64;
constexpr std::size_t kMaxNodes = 512;

} // namespace


// A recursive descent parser of the formula, in C's precedence.
class PluralForms::Parser {
public:
	Parser(std::string_view text, std::vector<Node>* nodes)
		:
		fText(text),
		fNodes(nodes)
	{
	}

	// The whole formula; false if it is none.
	bool Parse(std::uint32_t* root)
	{
		if (!_Choice(root))
			return false;
		_Skip();
		return fPosition == fText.size();
	}

private:
	using Kind = Node::Kind;

	void _Skip()
	{
		while (fPosition < fText.size()
			&& std::isspace(static_cast<unsigned char>(fText[fPosition])) != 0) {
			fPosition++;
		}
	}

	// Takes \a token if it comes next.
	bool _Take(std::string_view token)
	{
		_Skip();
		if (fText.substr(fPosition, token.size()) != token)
			return false;
		fPosition += token.size();
		return true;
	}

	bool _Add(Node node, std::uint32_t* index)
	{
		if (fNodes->size() >= kMaxNodes)
			return false;
		*index = static_cast<std::uint32_t>(fNodes->size());
		fNodes->push_back(node);
		return true;
	}

	bool _Binary(Kind kind, std::uint32_t left, std::uint32_t right, std::uint32_t* index)
	{
		Node node;
		node.kind = kind;
		node.operands[0] = left;
		node.operands[1] = right;
		return _Add(node, index);
	}

	// condition ? a : b
	bool _Choice(std::uint32_t* index)
	{
		if (++fDepth > kMaxDepth)
			return false;
		std::uint32_t condition = 0;
		bool ok = _Or(&condition);
		if (ok && _Take("?")) {
			std::uint32_t yes = 0;
			std::uint32_t no = 0;
			ok = _Choice(&yes) && _Take(":") && _Choice(&no);
			if (ok) {
				Node node;
				node.kind = Kind::Choice;
				node.operands[0] = condition;
				node.operands[1] = yes;
				node.operands[2] = no;
				ok = _Add(node, index);
			}
		} else
			*index = condition;
		fDepth--;
		return ok;
	}

	bool _Or(std::uint32_t* index)
	{
		if (!_And(index))
			return false;
		while (_Take("||")) {
			std::uint32_t right = 0;
			if (!_And(&right) || !_Binary(Kind::Or, *index, right, index))
				return false;
		}
		return true;
	}

	bool _And(std::uint32_t* index)
	{
		if (!_Equality(index))
			return false;
		while (_Take("&&")) {
			std::uint32_t right = 0;
			if (!_Equality(&right) || !_Binary(Kind::And, *index, right, index))
				return false;
		}
		return true;
	}

	bool _Equality(std::uint32_t* index)
	{
		if (!_Relation(index))
			return false;
		while (true) {
			Kind kind;
			if (_Take("=="))
				kind = Kind::Equal;
			else if (_Take("!="))
				kind = Kind::NotEqual;
			else
				return true;
			std::uint32_t right = 0;
			if (!_Relation(&right) || !_Binary(kind, *index, right, index))
				return false;
		}
	}

	bool _Relation(std::uint32_t* index)
	{
		if (!_Sum(index))
			return false;
		while (true) {
			Kind kind;
			if (_Take("<="))
				kind = Kind::LessEqual;
			else if (_Take(">="))
				kind = Kind::GreaterEqual;
			else if (_Take("<"))
				kind = Kind::Less;
			else if (_Take(">"))
				kind = Kind::Greater;
			else
				return true;
			std::uint32_t right = 0;
			if (!_Sum(&right) || !_Binary(kind, *index, right, index))
				return false;
		}
	}

	bool _Sum(std::uint32_t* index)
	{
		if (!_Product(index))
			return false;
		while (true) {
			Kind kind;
			if (_Take("+"))
				kind = Kind::Add;
			else if (_Take("-"))
				kind = Kind::Subtract;
			else
				return true;
			std::uint32_t right = 0;
			if (!_Product(&right) || !_Binary(kind, *index, right, index))
				return false;
		}
	}

	bool _Product(std::uint32_t* index)
	{
		if (!_Unary(index))
			return false;
		while (true) {
			Kind kind;
			if (_Take("*"))
				kind = Kind::Multiply;
			else if (_Take("/"))
				kind = Kind::Divide;
			else if (_Take("%"))
				kind = Kind::Remainder;
			else
				return true;
			std::uint32_t right = 0;
			if (!_Unary(&right) || !_Binary(kind, *index, right, index))
				return false;
		}
	}

	bool _Unary(std::uint32_t* index)
	{
		if (++fDepth > kMaxDepth)
			return false;
		bool ok = false;
		if (_Take("!")) {
			std::uint32_t operand = 0;
			if (_Unary(&operand)) {
				Node node;
				node.kind = Kind::Not;
				node.operands[0] = operand;
				ok = _Add(node, index);
			}
			fDepth--;
			return ok;
		}
		ok = _Primary(index);
		fDepth--;
		return ok;
	}

	bool _Primary(std::uint32_t* index)
	{
		_Skip();
		if (_Take("(")) {
			return _Choice(index) && _Take(")");
		}
		if (_Take("n")) {
			Node node;
			node.kind = Kind::Variable;
			return _Add(node, index);
		}
		if (fPosition >= fText.size()
			|| std::isdigit(static_cast<unsigned char>(fText[fPosition])) == 0) {
			return false;
		}
		std::uint64_t value = 0;
		while (fPosition < fText.size()
			&& std::isdigit(static_cast<unsigned char>(fText[fPosition])) != 0) {
			// large numbers saturate
			const auto digit = static_cast<std::uint64_t>(fText[fPosition] - '0');
			value = value > (UINT64_MAX - digit) / 10 ? UINT64_MAX : value * 10 + digit;
			fPosition++;
		}
		Node node;
		node.kind = Kind::Number;
		node.value = value;
		return _Add(node, index);
	}

	std::string_view	fText;
	std::size_t			fPosition = 0;
	int					fDepth = 0;
	std::vector<Node>*	fNodes;
};


std::expected<PluralForms, Error>
PluralForms::Parse(std::string_view header)
{
	// "nplurals=N; plural=EXPRESSION;"
	const std::size_t countAt = header.find("nplurals=");
	const std::size_t formulaAt = header.find("plural=", countAt == std::string_view::npos
		? 0 : countAt + 9);
	if (countAt == std::string_view::npos || formulaAt == std::string_view::npos)
		return Failure(ErrorCode::BadData, "no plural rule");

	std::size_t count = 0;
	std::size_t i = countAt + 9;
	while (i < header.size() && header[i] == ' ')
		i++;
	const std::size_t digits = i;
	while (i < header.size() && std::isdigit(static_cast<unsigned char>(header[i])) != 0
		&& count <= kMaxForms) {
		count = count * 10 + static_cast<std::size_t>(header[i] - '0');
		i++;
	}
	if (i == digits || count == 0 || count > kMaxForms)
		return Failure(ErrorCode::BadData, "a plural rule needs 1 to 16 forms");

	std::string_view formula = header.substr(formulaAt + 7);
	formula = formula.substr(0, formula.find_first_of(";\n"));

	PluralForms forms;
	forms.fCount = count;
	std::uint32_t root = 0;
	Parser parser(formula, &forms.fNodes);
	// a node comes after its operands, so the root is the last
	if (!parser.Parse(&root) || root + 1 != forms.fNodes.size())
		return Failure(ErrorCode::BadData, "the plural formula cannot be read");
	return forms;
}


std::size_t
PluralForms::Count() const
{
	return fCount;
}


std::size_t
PluralForms::FormFor(std::uint64_t n) const
{
	const std::uint64_t form = fNodes.empty() ? (n != 1 ? 1 : 0)
		: _Evaluate(static_cast<std::uint32_t>(fNodes.size() - 1), n);
	return form < fCount ? static_cast<std::size_t>(form) : fCount - 1;
}


std::uint64_t
PluralForms::_Evaluate(std::uint32_t index, std::uint64_t n) const
{
	const Node& node = fNodes[index];
	const auto operand = [&](int i) { return _Evaluate(node.operands[i], n); };
	switch (node.kind) {
		case Node::Kind::Number:
			return node.value;
		case Node::Kind::Variable:
			return n;
		case Node::Kind::Not:
			return operand(0) == 0 ? 1 : 0;
		case Node::Kind::Multiply:
			return operand(0) * operand(1);
		case Node::Kind::Divide:
		{
			const std::uint64_t divisor = operand(1);
			return divisor == 0 ? 0 : operand(0) / divisor;
		}
		case Node::Kind::Remainder:
		{
			const std::uint64_t divisor = operand(1);
			return divisor == 0 ? 0 : operand(0) % divisor;
		}
		case Node::Kind::Add:
			return operand(0) + operand(1);
		case Node::Kind::Subtract:
			return operand(0) - operand(1);
		case Node::Kind::Less:
			return operand(0) < operand(1) ? 1 : 0;
		case Node::Kind::Greater:
			return operand(0) > operand(1) ? 1 : 0;
		case Node::Kind::LessEqual:
			return operand(0) <= operand(1) ? 1 : 0;
		case Node::Kind::GreaterEqual:
			return operand(0) >= operand(1) ? 1 : 0;
		case Node::Kind::Equal:
			return operand(0) == operand(1) ? 1 : 0;
		case Node::Kind::NotEqual:
			return operand(0) != operand(1) ? 1 : 0;
		case Node::Kind::And:
			return operand(0) != 0 && operand(1) != 0 ? 1 : 0;
		case Node::Kind::Or:
			return operand(0) != 0 || operand(1) != 0 ? 1 : 0;
		case Node::Kind::Choice:
			return operand(0) != 0 ? operand(1) : operand(2);
	}
	return 0;
}


} // namespace ngui::locale
