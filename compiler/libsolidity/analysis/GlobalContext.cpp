/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @author Christian <c@ethdev.com>
 * @author Gav Wood <g@ethdev.com>
 * @date 2014
 * Container of the (implicit and explicit) global objects.
 */

#include <libsolidity/analysis/GlobalContext.h>

#include <libsolidity/ast/AST.h>
#include <libsolidity/ast/TypeProvider.h>
#include <libsolidity/ast/Types.h>
#include <memory>

using namespace std;

namespace solidity::frontend
{

/// Magic variables get negative ids for easy differentiation
int magicVariableToID(std::string const& _name)
{
	if (_name == "abi") return -1;
	else if (_name == "addmod") return -2;
	else if (_name == "assert") return -3;
	else if (_name == "block") return -4;
	else if (_name == "blockhash") return -5;
	else if (_name == "ecrecover") return -6;
	else if (_name == "format") return -105;
	else if (_name == "gasleft") return -7;
	else if (_name == "keccak256") return -8;
	else if (_name == "log0") return -10;
	else if (_name == "log1") return -11;
	else if (_name == "log2") return -12;
	else if (_name == "log3") return -13;
	else if (_name == "log4") return -14;
	else if (_name == "logtvm") return -102;
	else if (_name == "math") return -103;
	else if (_name == "rnd") return -105;
	else if (_name == "msg") return -15;
	else if (_name == "mulmod") return -16;
	else if (_name == "now") return -17;
	else if (_name == "require") return -18;
	else if (_name == "revert") return -19;
	else if (_name == "ripemd160") return -20;
	else if (_name == "selfdestruct") return -21;
	else if (_name == "sha256") return -22;
	else if (_name == "sha3") return -23;
	else if (_name == "stoi") return -106;
	else if (_name == "suicide") return -24;
	else if (_name == "super") return -25;
	else if (_name == "tvm") return -101;
	else if (_name == "tx") return -26;
	else if (_name == "type") return -27;
	else if (_name == "this") return -28;
	else if (_name == "gasToValue") return -60;
	else if (_name == "valueToGas") return -61;
	else if (_name == "bitSize") return -62;
	else if (_name == "uBitSize") return -63;
	else
		solAssert(false, "Unknown magic variable: \"" + _name + "\".");
}

inline vector<shared_ptr<MagicVariableDeclaration const>> constructMagicVariables()
{
	static auto const magicVarDecl = [](string const& _name, Type const* _type) {
		return make_shared<MagicVariableDeclaration>(magicVariableToID(_name), _name, _type);
	};

	return {
		magicVarDecl("abi", TypeProvider::magic(MagicType::Kind::ABI)),
		magicVarDecl("addmod", TypeProvider::function(strings{"uint256", "uint256", "uint256"}, strings{"uint256"}, FunctionType::Kind::AddMod, false, StateMutability::Pure)),
		magicVarDecl("assert", TypeProvider::function(strings{"bool"}, strings{}, FunctionType::Kind::Assert, false, StateMutability::Pure)),
		magicVarDecl("block", TypeProvider::magic(MagicType::Kind::Block)),
		magicVarDecl("blockhash", TypeProvider::function(strings{"uint256"}, strings{"bytes32"}, FunctionType::Kind::BlockHash, false, StateMutability::View)),
		magicVarDecl("ecrecover", TypeProvider::function(strings{"bytes32", "uint8", "bytes32", "bytes32"}, strings{"address"}, FunctionType::Kind::ECRecover, false, StateMutability::Pure)),
		magicVarDecl("format", TypeProvider::function(strings{}, strings{"string"}, FunctionType::Kind::Format, true, StateMutability::Pure)),
		magicVarDecl("gasleft", TypeProvider::function(strings(), strings{"uint256"}, FunctionType::Kind::GasLeft, false, StateMutability::View)),
		magicVarDecl("keccak256", TypeProvider::function(strings{"bytes memory"}, strings{"bytes32"}, FunctionType::Kind::KECCAK256, false, StateMutability::Pure)),
		magicVarDecl("log0", TypeProvider::function(strings{"bytes32"}, strings{}, FunctionType::Kind::Log0)),
		magicVarDecl("log1", TypeProvider::function(strings{"bytes32", "bytes32"}, strings{}, FunctionType::Kind::Log1)),
		magicVarDecl("log2", TypeProvider::function(strings{"bytes32", "bytes32", "bytes32"}, strings{}, FunctionType::Kind::Log2)),
		magicVarDecl("log3", TypeProvider::function(strings{"bytes32", "bytes32", "bytes32", "bytes32"}, strings{}, FunctionType::Kind::Log3)),
		magicVarDecl("log4", TypeProvider::function(strings{"bytes32", "bytes32", "bytes32", "bytes32", "bytes32"}, strings{}, FunctionType::Kind::Log4)),
		magicVarDecl("logtvm", TypeProvider::function(strings{"string"}, strings{}, FunctionType::Kind::LogTVM, false, StateMutability::Pure)),
		magicVarDecl("math", TypeProvider::magic(MagicType::Kind::Math)),
		magicVarDecl("rnd", TypeProvider::magic(MagicType::Kind::Rnd)),
		magicVarDecl("msg", TypeProvider::magic(MagicType::Kind::Message)),
		magicVarDecl("mulmod", TypeProvider::function(strings{"uint256", "uint256", "uint256"}, strings{"uint256"}, FunctionType::Kind::MulMod, false, StateMutability::Pure)),
		magicVarDecl("now", TypeProvider::uint(32)),
		magicVarDecl("require", TypeProvider::function(strings{}, strings{}, FunctionType::Kind::Require, true, StateMutability::Pure)),
		magicVarDecl("revert", TypeProvider::function(strings(), strings(), FunctionType::Kind::Revert, true, StateMutability::Pure)),
		magicVarDecl("ripemd160", TypeProvider::function(strings{"bytes memory"}, strings{"bytes20"}, FunctionType::Kind::RIPEMD160, false, StateMutability::Pure)),
		magicVarDecl("selfdestruct", TypeProvider::function(strings{"address payable"}, strings{}, FunctionType::Kind::Selfdestruct)),
		magicVarDecl("sha256", TypeProvider::function({"TvmSlice"}, {"uint256"}, FunctionType::Kind::SHA256, false, StateMutability::Pure)),
		magicVarDecl("sha256", TypeProvider::function({"bytes"}, {"uint256"}, FunctionType::Kind::SHA256, false, StateMutability::Pure)),
		magicVarDecl("sha3", TypeProvider::function(strings{"bytes memory"}, strings{"bytes32"}, FunctionType::Kind::KECCAK256, false, StateMutability::Pure)),
		magicVarDecl("stoi", TypeProvider::function(strings{"string"}, strings{"uint256", "bool"}, FunctionType::Kind::Stoi, false, StateMutability::Pure)),
		magicVarDecl("suicide", TypeProvider::function(strings{"address payable"}, strings{}, FunctionType::Kind::Selfdestruct)),
		magicVarDecl("tvm", TypeProvider::magic(MagicType::Kind::TVM)),
		magicVarDecl("tx", TypeProvider::magic(MagicType::Kind::Transaction)),
		magicVarDecl("type", TypeProvider::function(
			strings{"address"} /* accepts any contract type, handled by the type checker */,
			strings{} /* returns a MagicType, handled by the type checker */,
			FunctionType::Kind::MetaType,
			false,
			StateMutability::Pure
		)),
		magicVarDecl("valueToGas", TypeProvider::function({"uint128", "int8"}, {"uint128"}, FunctionType::Kind::ValueToGas, false, StateMutability::Pure)),
		magicVarDecl("gasToValue", TypeProvider::function({"uint128", "int8"}, {"uint128"}, FunctionType::Kind::GasToValue, false, StateMutability::Pure)),
		magicVarDecl("bitSize", TypeProvider::function({"int"}, {"uint16"}, FunctionType::Kind::BitSize, false, StateMutability::Pure)),
		magicVarDecl("uBitSize", TypeProvider::function({"uint"}, {"uint16"}, FunctionType::Kind::UBitSize, false, StateMutability::Pure)),
	};
}

GlobalContext::GlobalContext(): m_magicVariables{constructMagicVariables()}
{
}

void GlobalContext::setCurrentContract(ContractDefinition const& _contract)
{
	m_currentContract = &_contract;
}

vector<Declaration const*> GlobalContext::declarations() const
{
	vector<Declaration const*> declarations;
	declarations.reserve(m_magicVariables.size());
	for (auto& variable: m_magicVariables)
		declarations.push_back(variable.get());
	return declarations;
}

MagicVariableDeclaration const* GlobalContext::currentThis() const
{
	if (!m_thisPointer[m_currentContract])
		m_thisPointer[m_currentContract] = make_shared<MagicVariableDeclaration>(magicVariableToID("this"), "this", TypeProvider::contract(*m_currentContract));
	return m_thisPointer[m_currentContract].get();

}

MagicVariableDeclaration const* GlobalContext::currentSuper() const
{
	if (!m_superPointer[m_currentContract])
		m_superPointer[m_currentContract] = make_shared<MagicVariableDeclaration>(magicVariableToID("super"), "super", TypeProvider::contract(*m_currentContract, true));
	return m_superPointer[m_currentContract].get();
}

}
