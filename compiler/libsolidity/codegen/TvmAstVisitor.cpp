/*
 * Copyright 2018-2021 TON DEV SOLUTIONS LTD.
 *
 * Licensed under the  terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License.
 *
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the  GNU General Public License for more details at: https://www.gnu.org/licenses/gpl-3.0.html
 */
/**
 * @author TON Labs <connect@tonlabs.io>
 * @date 2021
 * Visitor for TVM Solidity abstract syntax tree.
 */

#include <ostream>
#include <memory>

#include <libsolidity/codegen/TvmAstVisitor.hpp>
#include <liblangutil/Exceptions.h>
#include "TVMCommons.hpp"

using namespace solidity::frontend;

bool Printer::visit(AsymGen &_node) {
	tabs();
	m_out << _node.opcode() << std::endl;
	return false;
}

bool Printer::visit(DeclRetFlag &/*_node*/) {
	tabs();
	m_out << "FALSE ; decl return flag" << std::endl;
	return false;
}

bool Printer::visit(Opaque &_node) {
	_node.block()->accept(*this);
	return false;
}

bool Printer::visit(HardCode &_node) {
	for (const std::string& s : _node.code()) {
		tabs();
		m_out << s << std::endl;
	}
	return false;
}


//bool Printer::visit(Loc &) { return false; }

bool Printer::visit(Loc &_node) {
	tabs();
	m_out << ".loc " << _node.file() << ", " << _node.line() << std::endl;
	return false;
}

bool Printer::visit(TvmReturn &_node) {
	tabs();
	switch (_node.type()) {
		case TvmReturn::Type::RET:
			m_out << "RET";
			break;
			case TvmReturn::Type::IFRET:
			m_out << "IFRET";
			break;
			case TvmReturn::Type::IFNOTRET:
			m_out << "IFNOTRET";
			break;
	}
	endL();
	return false;
}

bool Printer::visit(ReturnOrBreakOrCont &_node) {
	tabs();
	m_out << "; start return" << std::endl;
	_node.body()->accept(*this);
	tabs();
	m_out << "; end return" << std::endl;
	return false;
}

bool Printer::visit(TvmException &_node) {
	tabs();
	m_out << _node.fullOpcode() << std::endl;
	return false;
}

bool Printer::visit(GenOpcode &_node) {
	tabs();
	if (_node.fullOpcode() == "BITNOT") m_out << "NOT";
	else if (_node.fullOpcode() == "TUPLE 1") m_out << "SINGLE";
	else if (_node.fullOpcode() == "TUPLE 2") m_out << "PAIR";
	else if (_node.fullOpcode() == "TUPLE 3") m_out << "TRIPLE";
	else if (_node.fullOpcode() == "UNTUPLE 1") m_out << "UNSINGLE";
	else if (_node.fullOpcode() == "UNTUPLE 2") m_out << "UNPAIR";
	else if (_node.fullOpcode() == "UNTUPLE 3") m_out << "UNTRIPLE";
	else if (isIn(_node.opcode(), "INDEX_EXCEP", "INDEX_NOEXCEP")) {
		int index = boost::lexical_cast<int>(_node.arg());
		if (index <= 15) {
			m_out << "INDEX " << index;
		} else {
			m_out << "PUSHINT " << index << std::endl;
			tabs();
			m_out << "INDEXVAR";
		}
	} else {
		m_out << _node.fullOpcode();
	}
	m_out << std::endl;
	return false;
}

bool Printer::visit(PushCellOrSlice &_node) {
	tabs();
	switch (_node.type()) {
		case PushCellOrSlice::Type::PUSHREF:
			m_out << "PUSHREF {";
			break;
		case PushCellOrSlice::Type::PUSHREFSLICE:
			m_out << "PUSHREFSLICE {";
			break;
		case PushCellOrSlice::Type::CELL:
			m_out << ".cell {";
			break;
	}
	endL();

	++m_tab;
	if (!_node.blob().empty()) {
		tabs();
		m_out << _node.blob() << std::endl;
	}
	if (_node.child()) {
		_node.child()->accept(*this);
	}
	--m_tab;

	tabs();
	m_out << "}" << std::endl;
	return false;
}

bool Printer::visit(Glob &_node) {
	tabs();
	switch (_node.opcode()) {
		case Glob::Opcode::GetOrGetVar:
			if (1 <= _node.index() && _node.index() <= 31) {
				m_out << "GETGLOB " << _node.index();
			} else {
				m_out << "PUSHINT " << _node.index() << std::endl;
				tabs();
				m_out << "GETGLOBVAR";
			}
			break;
		case Glob::Opcode::SetOrSetVar:
			if (1 <= _node.index() && _node.index() <= 31) {
				m_out << "SETGLOB " << _node.index();
			} else {
				m_out << "PUSHINT " << _node.index() << std::endl;
				tabs();
				m_out << "SETGLOBVAR";
			}
			break;
		case Glob::Opcode::POPROOT:
			m_out << "POPROOT";
			break;
		case Glob::Opcode::PUSHROOT:
			m_out << "PUSHROOT";
			break;
		case Glob::Opcode::POP_C3:
			m_out << "POP C3";
			break;
		case Glob::Opcode::PUSH_C7:
			m_out << "PUSH C7";
			break;
		case Glob::Opcode::PUSH_C3:
			m_out << "PUSH C3";
			break;
		case Glob::Opcode::POP_C7:
			m_out << "POP C7";
			break;
	}
	endL();
	return false;
}

bool Printer::visit(Stack &_node) {
	tabs();
	int i = _node.i();
	int j = _node.j();
	int k = _node.k();
	auto printSS = [&](){
		m_out << " S" << i;
		if (j != -1) {
			m_out << ", S" << j;
			if (k != -1) {
				m_out << ", S" << k;
			}
		}
	};
	auto printIndexes = [&](){
		solAssert(i != -1, "");
		m_out << " " << i;
		if (j != -1) {
			m_out << ", " << j;
			solAssert(k==-1, "");
		}
	};

	auto drop = [&](int n){
		if (n == 1) {
			m_out << "DROP";
		} else if (n == 2) {
			m_out << "DROP2";
		} else if (n <= 15) {
			m_out << "BLKDROP";
			printIndexes();
		} else {
			m_out << "PUSHINT " + std::to_string(n) << std::endl;
			tabs();
			m_out << "DROPX";
		}
	};

	switch (_node.opcode()) {
		case Stack::Opcode::DROP: {
			drop(i);
			break;
		}
		case Stack::Opcode::PUSH_S:
			solAssert(j == -1, "");
			if (i == 0) {
				m_out << "DUP";
			} else if (i == 1) {
				m_out << "OVER";
			} else {
				m_out << "PUSH S" << i;
			}
			break;
		case Stack::Opcode::XCHG: {
			if (i == 0) {
				if (j == 1) {
					m_out << "SWAP";
				} else {
					m_out << "XCHG S" << j;
				}
			} else {
				m_out << "XCHG S" << i << ", S" << j;
			}
			break;
		}
		case Stack::Opcode::BLKDROP2:
			if (i > 15 || j > 15) {
				m_out << "PUSHINT " << i << std::endl;
				tabs();
				m_out << "PUSHINT " << j << std::endl;
				tabs();
				m_out << "BLKSWX" << std::endl;
				tabs();
				drop(i);
			} else {
				solAssert((i >= 2 && j >= 1) || (i >= 1 && j >= 2), "");
				m_out << "BLKDROP2";
				printIndexes();
			}
			break;
		case Stack::Opcode::PUSH2_S:
			if (i == 1 && j == 0)
				m_out << "DUP2";
			else if (i == 3 && j == 2)
				m_out << "OVER2";
			else {
				m_out << "PUSH2";
				printSS();
			}
			break;
		case Stack::Opcode::POP_S:
			if (i == 1) {
				m_out << "NIP";
			} else {
				m_out << "POP";
				printSS();
			}
			break;
		case Stack::Opcode::BLKSWAP: {
			int bottom = _node.i();
			int top = _node.j();
			if (bottom == 1 && top == 1) {
				m_out << "SWAP";
			} else if (bottom == 1 && top == 2) {
				m_out << "ROT";
			} else if (bottom == 2 && top == 1) {
				m_out << "ROTREV";
			} else if (bottom == 2 && top == 2) {
				m_out << "SWAP2";
			} else if (1 <= bottom && bottom <= 16 && 1 <= top && top <= 16) {
				if (bottom == 1) {
					m_out << "ROLL " << top;
				} else if (top == 1) {
					m_out << "ROLLREV " << bottom;
				} else {
					m_out << "BLKSWAP";
					printIndexes();
				}
			} else {
				m_out << "PUSHINT " << bottom << std::endl;
				tabs();
				m_out << "PUSHINT " << top << std::endl;
				tabs();
				m_out << "BLKSWX";
			}
			break;
		}
		case Stack::Opcode::REVERSE:
			solAssert(2 <= i, "");
			if (i == 2 && j == 0) {
				m_out << "SWAP";
			} else if (i == 3 && j == 0) {
				m_out << "XCHG S2";
			} else if (2 <= i && i <= 17 && 0 <= j && j <= 15) {
				m_out << "REVERSE";
				printIndexes();
			} else {
				m_out << "PUSHINT " << i << std::endl;
				tabs();
				m_out << "PUSHINT " << j << std::endl;
				tabs();
				m_out << "REVX";
			}
			break;
		case Stack::Opcode::BLKPUSH:
			if (i == 2 && j == 1) {
				m_out << "DUP2";
			} else if (i == 2 && j == 3) {
				m_out << "OVER2";
			} else {
				if (i > 15)
					solAssert(j == 0, "");
				int rest = i;
				bool first = true;
				while (rest > 0) {
					if (!first) {
						m_out << std::endl;
						tabs();
					}
					m_out << "BLKPUSH " << std::min(15, rest) << ", " << j;

					rest -= 15;
					first = false;
				}
			}
			break;
		case Stack::Opcode::PUSH3_S:
			m_out << "PUSH3";
			printSS();
			break;

		case Stack::Opcode::TUCK:
			m_out << "TUCK";
			break;

		case Stack::Opcode::PUXC:
			m_out << "PUXC S" << i << ", S" << j;
			break;
	}
	endL();
	return false;
}

bool Printer::visit(CodeBlock &_node) {
	switch (_node.type()) {
		case CodeBlock::Type::None:
			break;
		default:
			tabs();
			m_out << CodeBlock::toString(_node.type()) << " {" << std::endl;
			++m_tab;
			break;
	}

	for (Pointer<TvmAstNode> const& inst : _node.instructions()) {
		inst->accept(*this);
	}

	switch (_node.type()) {
		case CodeBlock::Type::None:
			break;
		default:
			--m_tab;
			tabs();
			m_out << "}" << std::endl;
			break;
	}

	return false;
}

bool Printer::visit(SubProgram &_node) {
	tabs();
	switch (_node.type()) {
		case SubProgram::Type::CALLX:
			m_out << "PUSHCONT";
			break;
		case SubProgram::Type::CALLREF:
			m_out << "CALLREF";
			break;
	}
	m_out << " {" << std::endl;

	++m_tab;
	_node.block()->accept(*this);
	--m_tab;

	tabs();
	m_out << "}" << std::endl;

	switch (_node.type()) {
		case SubProgram::Type::CALLX:
			tabs();
			m_out << "CALLX" << std::endl;
			break;
		default:
			break;
	}

	return false;
}

bool Printer::visit(TvmCondition &_node)  {
	_node.trueBody()->accept(*this);
	_node.falseBody()->accept(*this);
	tabs();
	m_out << "IFELSE" << std::endl;
	return false;
}

bool Printer::visit(LogCircuit &_node) {
	tabs();
	m_out << "PUSHCONT {" << std::endl;

	++m_tab;
	_node.body()->accept(*this);
	--m_tab;

	tabs();
	m_out << "}" << std::endl;

	tabs();
	switch (_node.type()) {
		case LogCircuit::Type::AND:
			m_out << "IF";
			break;
		case LogCircuit::Type::OR:
			m_out << "IFNOT";
			break;
	}
	m_out << std::endl;

	return false;
}

bool Printer::visit(TvmIfElse &_node) {
	if (isIn(_node.type(),
			 TvmIfElse::Type::IFREF,
			 TvmIfElse::Type::IFNOTREF,
			 TvmIfElse::Type::IFJMPREF,
			 TvmIfElse::Type::IFNOTJMPREF)
	) {
		tabs();
		switch (_node.type()) {
			case TvmIfElse::Type::IFREF:
				m_out << "IFREF";
				break;
			case TvmIfElse::Type::IFNOTREF:
				m_out << "IFNOTREF";
				break;
			case TvmIfElse::Type::IFJMPREF:
				m_out << "IFJMPREF";
				break;
			case TvmIfElse::Type::IFNOTJMPREF:
				m_out << "IFNOTJMPREF";
				break;
			default:
				solUnimplemented("");
		}
		m_out << " {" << std::endl;
		++m_tab;
		for (Pointer<TvmAstNode> const& i : _node.trueBody()->instructions()) {
			i->accept(*this);
		}
		--m_tab;
		tabs();
		m_out << "}" << std::endl;
	} else {
		_node.trueBody()->accept(*this);
		if (_node.falseBody()) {
			_node.falseBody()->accept(*this);
		}
		tabs();
		switch (_node.type()) {
			case TvmIfElse::Type::IF:
				m_out << "IF";
				break;
			case TvmIfElse::Type::IFNOT:
				m_out << "IFNOT";
				break;
			case TvmIfElse::Type::IFJMP:
				m_out << "IFJMP";
				break;
			case TvmIfElse::Type::IFNOTJMP:
				m_out << "IFNOTJMP";
				break;
			case TvmIfElse::Type::IFELSE:
				m_out << "IFELSE";
				break;
			case TvmIfElse::Type::IFELSE_WITH_JMP:
				m_out << "CONDSEL" << std::endl;
				tabs();
				m_out << "JMPX";
				break;
			default:
				solUnimplemented("");
		}
		m_out << std::endl;
	}

	return false;
}

bool Printer::visit(TvmRepeat &_node) {
	_node.body()->accept(*this);
	tabs();
	m_out << "REPEAT" << std::endl;
	return false;
}

bool Printer::visit(TvmUntil &_node) {
	_node.body()->accept(*this);
	tabs();
	m_out << "UNTIL" << std::endl;
	return false;
}

bool Printer::visit(While &_node) {
	_node.condition()->accept(*this);
	_node.body()->accept(*this);
	tabs();
	m_out << "WHILE" << std::endl;
	return false;
}

bool Printer::visit(Contract &_node) {
	for (const std::string& pragma : _node.pragmas()) {
		m_out << pragma << std::endl;
		m_out << std::endl;
	}
	for (const Pointer<Function>& f : _node.functions()){
		f->accept(*this);
	}
	return false;
}

bool Printer::visit(Function &_node) {
	switch (_node.type()) {
		case Function::FunctionType::PrivateFunction:
			m_out << ".globl\t" << _node.name() << std::endl;
			m_out << ".type\t" << _node.name() << ", @function" << std::endl;
			break;
		case Function::FunctionType::Macro:
		case Function::FunctionType::MacroGetter:
			m_out << ".macro " << _node.name() << std::endl;
			break;
		case Function::FunctionType::MainInternal:
			solAssert(_node.name() == "main_internal", "");
			m_out << ".internal-alias :main_internal, 0" << std::endl
				<< ".internal :main_internal" << std::endl;
			break;
		case Function::FunctionType::MainExternal:
			solAssert(_node.name() == "main_external", "");
			m_out << ".internal-alias :main_external, -1" << std::endl
				  << ".internal :main_external" << std::endl;
			break;
		case Function::FunctionType::OnCodeUpgrade:
			solAssert(_node.name() == "onCodeUpgrade", "");
			m_out << ".internal-alias :onCodeUpgrade, 2" << std::endl
				  << ".internal :onCodeUpgrade" << std::endl;
			break;
		case Function::FunctionType::OnTickTock:
			solAssert(_node.name() == "onTickTock", "");
			m_out << ".internal-alias :onTickTock, -2" << std::endl
				  << ".internal :onTickTock" << std::endl;
			break;
	}
	_node.block()->accept(*this);
	endL();
	return false;
}

bool Printer::visitNode(TvmAstNode const&) {
	solUnimplemented("");
}

void Printer::endL() {
	m_out << std::endl;
}

void Printer::tabs() {
	solAssert(m_tab >= 0, "");
	m_out << std::string(m_tab, '\t');
}

bool LocSquasher::visit(CodeBlock &_node) {
	std::vector<Pointer<TvmAstNode>> res0;

	{
		std::vector<Pointer<TvmAstNode>> a = _node.instructions();
		std::vector<Pointer<TvmAstNode>> b;
		if (!a.empty()) {
			b.push_back(a.front());
			for (size_t i = 1; i < a.size(); ++i) {
				if (!b.empty() && dynamic_cast<Loc const *>(b.back().get()) &&
					dynamic_cast<Loc const *>(a[i].get()))
					b.pop_back();
				b.push_back(a[i]);
			}
		}
		res0 = b;
	}


	std::vector<Pointer<TvmAstNode>> res;
	std::optional<Pointer<Loc>> lastLoc;
	for (const Pointer<TvmAstNode>& node : res0) {
		auto loc = std::dynamic_pointer_cast<Loc>(node);
		if (loc) {
			if (!lastLoc || std::make_pair(lastLoc.value()->file(), lastLoc.value()->line()) !=
							std::make_pair(loc->file(), loc->line())) {
				res.push_back(node);
			}
			//
			lastLoc = loc;
		} else {
			res.push_back(node);
		}
	}


	_node.upd(res);

	return true;
}

void DeleterAfterRet::endVisit(CodeBlock &_node) {
	bool findRet{};
	std::vector<Pointer<TvmAstNode>> newInstrs;
	for (Pointer<TvmAstNode> const& opcode : _node.instructions()) {
		if (!findRet && to<ReturnOrBreakOrCont>(opcode.get())) {
			findRet = true;
			newInstrs.emplace_back(opcode);
		} else {
			if (!findRet || to<Loc>(opcode.get())) {
				newInstrs.emplace_back(opcode);
			}
		}
	}
	_node.upd(newInstrs);
}

bool DeleterCallX::visit(Function &_node) {
	Pointer<CodeBlock> const& block = _node.block();
	std::vector<Pointer<TvmAstNode>> const& inst = block->instructions();
	if (qtyWithoutLoc(inst) == 1) {
		std::vector<Pointer<TvmAstNode>> newCmds;
		for (Pointer<TvmAstNode> const& op : inst) {
			if (to<Loc>(op.get())) {
				newCmds.emplace_back(op);
			} else if (auto sub = to<SubProgram>(op.get()); sub) {
				newCmds.insert(newCmds.end(), sub->block()->instructions().begin(), sub->block()->instructions().end());
			} else {
				return false;
			}
		}
		block->upd(newCmds);
	}
	return false;
}

void LogCircuitExpander::endVisit(CodeBlock &_node) {
	std::vector<Pointer<TvmAstNode>> block;
	for (Pointer<TvmAstNode> const& opcode : _node.instructions()) {
		auto lc = to<LogCircuit>(opcode.get());
		if (lc && lc->canExpand()) {
			m_stackSize = 1;
			m_newInst = {};
			bool isPure = true;
			std::vector<Pointer<TvmAstNode>> const &inst = lc->body()->instructions();
			for (size_t i = 0; i < inst.size(); ++i) {
				Pointer<TvmAstNode> op = inst.at(i);
				if (i == 0) {
					solAssert(isDrop(inst.at(i)).value() == 1, "");
					continue;
				}
				if (to<LogCircuit>(op.get()) && i + 1 != inst.size()) {
					isPure = false; // never happens
				}

				isPure &= isPureOperation(op);
			}
			if (isPure) {
				solAssert(m_stackSize == 2, "");
				Pointer<TvmAstNode> tail = m_newInst.back();
				bool hasTailLogCircuit = !m_newInst.empty() && to<LogCircuit>(m_newInst.back().get());
				if (hasTailLogCircuit) {
					if (to<LogCircuit>(m_newInst.back().get())->type() != lc->type()) {
						block.emplace_back(opcode);
						continue;
					}

					m_newInst.pop_back(); // DUP
					m_newInst.pop_back(); // LogCircuit
				}
				switch (lc->type()) {
					case LogCircuit::Type::AND:
						m_newInst.emplace_back(gen("AND"));
						break;
					case LogCircuit::Type::OR:
						m_newInst.emplace_back(gen("OR"));
						break;
				}
				if (hasTailLogCircuit) {
					m_newInst.emplace_back(makePUSH(0)); // DUP
					m_newInst.emplace_back(tail); // LogCircuit
				}

				block.pop_back(); // remove DUP opcode
				block.insert(block.end(), m_newInst.begin(), m_newInst.end());
				continue;
			}
		}
		block.emplace_back(opcode);
	}
	_node.upd(block);
}

bool LogCircuitExpander::isPureOperation(Pointer<TvmAstNode> const& op) {
	auto gen = to<Gen>(op.get());
	if (gen && gen->isPure()) {
		m_newInst.emplace_back(op);
		m_stackSize += -gen->take() + gen->ret();
		return true;
	}

	if (to<LogCircuit>(op.get())) {
		m_newInst.emplace_back(op);
		m_stackSize += -2 + 1;
		return true;
	}

	auto stack = to<Stack>(op.get());
	if (stack && stack->opcode() == Stack::Opcode::PUSH_S) {
		int index = stack->i();
		if (index + 1 < m_stackSize) {
			m_newInst.emplace_back(makePUSH(index));
		} else {
			m_newInst.emplace_back(makePUSH(index + 1));
		}
		++m_stackSize;
		return true;
	}

	return false;
}
