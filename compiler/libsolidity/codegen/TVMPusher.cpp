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
 * @date 2019
 */

#include <libsolidity/ast/TypeProvider.h>

#include "DictOperations.hpp"
#include "TVMPusher.hpp"
#include "TVMExpressionCompiler.hpp"
#include "TVMStructCompiler.hpp"
#include "TVMABI.hpp"
#include "TVMConstants.hpp"

#include <boost/range/adaptor/map.hpp>

using namespace solidity::frontend;

StackPusher::StackPusher(TVMCompilerContext *ctx, const int stackSize) :
	m_ctx(ctx)
{
	change(stackSize);
	m_instructions.emplace_back();
}

void StackPusher::pushLoc(const std::string& file, int line) {
	auto op = createNode<Loc>(file, line);
	m_instructions.back().opcodes.emplace_back(op);
}

void StackPusher::pushString(const std::string& _str, bool toSlice) {
	std::string hexStr = stringToBytes(_str); // 2 * len(_str) == len(hexStr). One symbol to 2 hex digits
	if (4 * hexStr.length() <= TvmConst::MaxPushSliceBitLength && toSlice) {
		push(+1, "PUSHSLICE x" + hexStr);
		return ;
	}

	const int saveStackSize = stackSize();
	const int length = hexStr.size();
	const int symbolQty = ((TvmConst::CellBitLength / 8) * 8) / 4; // one symbol in string == 8 bit. Letter can't be divided by 2 cells
	PushCellOrSlice::Type type = toSlice ? PushCellOrSlice::Type::PUSHREFSLICE : PushCellOrSlice::Type::PUSHREF;
	std::vector<std::pair<PushCellOrSlice::Type, std::string>> data;
	int builderQty = 0;
	int start = 0;
	do {
		std::string slice = hexStr.substr(start, std::min(symbolQty, length - start));
		data.emplace_back(type, ".blob x" + slice);
		start += symbolQty;
		++builderQty;
		type = PushCellOrSlice::Type::CELL;
	} while (start < length);

	Pointer<PushCellOrSlice> cell;
	for (const auto&[t, d] : data | boost::adaptors::reversed) {
		cell = createNode<PushCellOrSlice>(t, d, cell);
	}
	solAssert(cell != nullptr, "");
	m_instructions.back().opcodes.emplace_back(cell);
	change(0, 1);

	ensureSize(saveStackSize + 1, "");
}

void StackPusher::pushLog() {
	push(0, "CTOS");
	push(0, "STRDUMP");
	drop();
}

// TODO move to function compiler
Pointer<Function> StackPusher::generateC7ToT4Macro() {
	const std::vector<Type const *>& memberTypes = m_ctx->notConstantStateVariableTypes();
	const int stateVarQty = memberTypes.size();
	if (ctx().tooMuchStateVariables()) {
		const int saveStack = stackSize();
		pushC7();
		push(+1, "FALSE");
		setIndexQ(stateVarQty + TvmConst::C7::FirstIndexForVariables);
		untuple(stateVarQty + TvmConst::C7::FirstIndexForVariables + 1);
		drop();
		reverse(stateVarQty + TvmConst::C7::FirstIndexForVariables, 0);
		drop(TvmConst::C7::FirstIndexForVariables);
		solAssert(saveStack + stateVarQty == stackSize(), "");
	} else {
		for (int i = stateVarQty - 1; i >= 0; --i) {
			getGlob(TvmConst::C7::FirstIndexForVariables + i);
		}
	}
	if (ctx().storeTimestampInC4()) {
		getGlob(TvmConst::C7::ReplayProtTime);
	}
	getGlob(TvmConst::C7::TvmPubkey);
	push(+1, "NEWC");
	push(-2 + 1, "STU 256");
	if (ctx().storeTimestampInC4()) {
		push(-2 + 1, "STU 64");
	}
	push(-1 + 1, "STONE"); // constructor flag
	if (m_ctx->usage().hasAwaitCall()) {
		push(-1 + 1, "STZERO");
	}
	if (!memberTypes.empty()) {
		ChainDataEncoder encoder{this};
		EncodePosition position{m_ctx->getOffsetC4(), memberTypes,
								m_ctx->usage().hasAwaitCall() ? 1 : 0};
		encoder.encodeParameters(memberTypes, position);
	}

	push(-1 + 1, "ENDC");
	popRoot();
	Pointer<CodeBlock> block = getBlock();
	auto f = createNode<Function>(0, 0, "c7_to_c4", Function::FunctionType::Macro, block);
	return f;
}

// TODO unit with generateC7ToT4Macro
Pointer<Function> StackPusher::generateC7ToT4MacroForAwait() {
	const std::vector<Type const *>& memberTypes = m_ctx->notConstantStateVariableTypes();
	if (ctx().storeTimestampInC4()) {
		getGlob(TvmConst::C7::ReplayProtTime);
	}
	getGlob(TvmConst::C7::TvmPubkey);
	push(+1, "NEWC");
	push(-2 + 1, "STU 256");
	if (ctx().storeTimestampInC4()) {
		push(-2 + 1, "STU 64");
	}
	push(-1 + 1, "STONE"); // constructor flag
	push(-1 + 1, "STONE");
	exchange(1);
	push(createNode<HardCode>(std::vector<std::string>{
		"NEWC",
		"STSLICE",
		"PUSH c0",
		"PUSH c3",
		"PUSHCONT {",
		"	; -- c0 c3 cc",
		"	SETCONT c3",
		"	SETCONT c0",
		"	;; 5 sys vars  -- func stack  --- 2 bldrs --- cont",
		"	BLKSWAP 2, 1",
		"	DEPTH",
		"	ADDCONST -7 ; 5 system args + 2 bldrs",
		"	PUSHINT 2",
		"	BLKSWX",
		"	;; 5 sys args -- 2 bldrs -- func stack -- cont",
		"	GETGLOB " + toString(TvmConst::C7::MsgPubkey),
		"	GETGLOB " + toString(TvmConst::C7::SenderAddress),
		"	GETGLOB " + toString(TvmConst::C7::AwaitAnswerId),
		"	BLKSWAP 1, 3",
		"	DEPTH",
		"	ADDCONST -8 ; 5 system args + 2 bldrs + cont",
		"	PUSHINT -1",
		"	SETCONTVARARGS",
		"	SWAP",
		"	STCONT",
		"	ENDC ; -- suspended-code-cell",
		"	STREFR",
	}, 0, 0, false));
	if (!memberTypes.empty()) {
		for (int i = memberTypes.size() - 1; i >= 0; --i) {
			getGlob(TvmConst::C7::FirstIndexForVariables + i);
		}
		blockSwap(1, memberTypes.size());
		ChainDataEncoder encoder{this};
		EncodePosition position{m_ctx->getOffsetC4(), memberTypes,
								m_ctx->usage().hasAwaitCall() ? 1 : 0};
		encoder.encodeParameters(memberTypes, position);
	}
	push(createNode<HardCode>(std::vector<std::string>{
		"	ENDC",
		"	POPROOT",
		"	THROW 0",
		"}",
		"CALLCC",
	}, 0, 0, false));
	return createNode<Function>(0, 0, "c7_to_c4_for_await", Function::FunctionType::Macro, getBlock());
}

bool StackPusher::doesFitInOneCellAndHaveNoStruct(Type const* key, Type const* value) {
	int keyLength = lengthOfDictKey(key);
	return
		TvmConst::MAX_HASH_MAP_INFO_ABOUT_KEY +
		keyLength +
		maxBitLengthOfDictValue(value)
		<
		TvmConst::CellBitLength;
}

int StackPusher::maxBitLengthOfDictValue(Type const* type) {
	switch (toDictValueType(type->category())) {
		case DictValueType::Enum:
		case DictValueType::Integer:
		case DictValueType::Bool:
		case DictValueType::FixedBytes:
		case DictValueType::FixedPoint: {
			TypeInfo ti{type};
			return ti.numBits;
		}

		case DictValueType::Address:
		case DictValueType::Contract:
			return AddressInfo::maxBitLength();

		case DictValueType::Array: {
			if (isStringOrStringLiteralOrBytes(type))
				return 0;
			return 32 + 1;
		}

		case DictValueType::Mapping:
		case DictValueType::ExtraCurrencyCollection:
		case DictValueType::Optional:
			return 1;

		case DictValueType::VarInteger: {
			auto vi = to<VarInteger>(type);
			return integerLog2(vi->getNumber()) + 8 * vi->getNumber();
		}

		case DictValueType::TvmCell:
			return 0;

		case DictValueType::TvmSlice:
			solUnimplemented("");

		case DictValueType::Struct: {
			auto st = to<StructType>(type);
			int sum = 0;
			for (const ASTPointer<VariableDeclaration>& m : st->structDefinition().members()) {
				int cur = maxBitLengthOfDictValue(m->type());
				sum += cur;
			}
			return sum;
		}

		case DictValueType::Function: {
			return 32;
		}
	}

	solUnimplemented("Unsupported " + type->toString());
}

DataType
StackPusher::prepareValueForDictOperations(Type const *keyType, Type const *valueType, bool isValueBuilder) {
	// stack: value

	switch (toDictValueType(valueType->category())) {
		case DictValueType::TvmSlice: {
			return isValueBuilder ? DataType::Builder : DataType::Slice;
		}

		case DictValueType::Address:
		case DictValueType::Contract: {
			if (!doesFitInOneCellAndHaveNoStruct(keyType, valueType)) {
				solAssert(!isValueBuilder, "");
				push(+1, "NEWC");
				push(-1, "STSLICE");
				push(0, "ENDC");
				return DataType::Cell;
			}
			return isValueBuilder ? DataType::Builder : DataType::Slice;
		}

		case DictValueType::Array: {
			if (isByteArrayOrString(valueType)) {
				if (isValueBuilder) {
					push(-1 + 1, "ENDC");
				}
				return DataType::Cell;
			}
			[[fallthrough]];
		}

		case DictValueType::Bool:
		case DictValueType::Enum:
		case DictValueType::ExtraCurrencyCollection:
		case DictValueType::FixedBytes:
		case DictValueType::FixedPoint:
		case DictValueType::Integer:
		case DictValueType::Mapping:
		case DictValueType::Optional:
		case DictValueType::VarInteger:
		case DictValueType::Function:
		{
			if (!isValueBuilder) {
				push(+1, "NEWC");
				store(valueType, false);
			}
			if (!doesFitInOneCellAndHaveNoStruct(keyType, valueType)) {
				push(+1, "NEWC");
				push(-1, "STBREF");
			}
			return DataType::Builder;
		}

		case DictValueType::Struct: {
			if (!isValueBuilder) {
				StructCompiler sc{this, to<StructType>(valueType)};
				sc.tupleToBuilder();
			}
			if (!doesFitInOneCellAndHaveNoStruct(keyType, valueType)) {
				push(0, "ENDC");
				return DataType::Cell;
			}
			return DataType::Builder;
		}

		case DictValueType::TvmCell: {
			if (isValueBuilder) {
				 push(0, "ENDC");
			}
			return DataType::Cell;
		}
	}
	solUnimplemented("");
}

// delMin/delMax
// min/max
// fetch
// at/[] - for arrays and mappings
bool StackPusher::doesDictStoreValueInRef(Type const* keyType, Type const* valueType) {
	switch (toDictValueType(valueType->category())) {
		case DictValueType::TvmCell:
			return true;

		case DictValueType::TvmSlice:
			return false;

		case DictValueType::Array: {
			if (isByteArrayOrString(valueType)) {
				return true;
			}
			return !doesFitInOneCellAndHaveNoStruct(keyType, valueType);
		}


		case DictValueType::Address:
		case DictValueType::Bool:
		case DictValueType::Contract:
		case DictValueType::Enum:
		case DictValueType::ExtraCurrencyCollection:
		case DictValueType::FixedBytes:
		case DictValueType::FixedPoint:
		case DictValueType::Integer:
		case DictValueType::Mapping:
		case DictValueType::Optional:
		case DictValueType::VarInteger:
		case DictValueType::Struct:
		case DictValueType::Function:
			return !doesFitInOneCellAndHaveNoStruct(keyType, valueType);
	}
	solUnimplemented("");
}

// false - value isn't in ref
// true - value is in ref
void StackPusher::recoverKeyAndValueAfterDictOperation(
	Type const* keyType,
	Type const* valueType,
	bool haveKey,
	bool didUseOpcodeWithRef,
	const DecodeType& decodeType,
	bool saveOrigKeyAndNoTuple
)
{
	const bool isValueStruct = valueType->category() == Type::Category::Struct;
	const bool pushRefCont =
		isValueStruct &&
		!didUseOpcodeWithRef &&
		!doesDictStoreValueInRef(keyType, valueType);

	// stack: value [key]
	auto preloadValue = [&]() {
		if (haveKey) {
			// stack: value key
			if (saveOrigKeyAndNoTuple) {
				pushS(0); // stack: value key key
			}
			if (keyType->category() == Type::Category::Struct) {
				StructCompiler sc{this, to<StructType>(keyType)};
				sc.convertSliceToTuple();
				// stack: value slice Tuple
			}
			if (saveOrigKeyAndNoTuple)
				rot();
			else
				exchange(1);
			// stack: slice key value
		}
		// stack: [slice, key] value

		switch (toDictValueType(valueType->category())) {
			case DictValueType::Address:
			case DictValueType::Contract:
			case DictValueType::TvmSlice:
			{
				if (didUseOpcodeWithRef) {
					push(0, "CTOS");
				} else if (doesDictStoreValueInRef(keyType, valueType)) {
					push(0, "PLDREF");
					push(0, "CTOS");
				}
				break;
			}
			case DictValueType::Array:
				if (isByteArrayOrString(valueType)) {
					if (!didUseOpcodeWithRef) {
						push(0, "PLDREF");
					}
					break;
				}
				[[fallthrough]];
			case DictValueType::Bool:
			case DictValueType::Enum:
			case DictValueType::ExtraCurrencyCollection:
			case DictValueType::FixedBytes:
			case DictValueType::FixedPoint:
			case DictValueType::Integer:
			case DictValueType::Mapping:
			case DictValueType::Optional:
			case DictValueType::Struct:
			case DictValueType::VarInteger:
			case DictValueType::Function:
			{
				bool pushCallRef = false;
				if (didUseOpcodeWithRef) {
					push(0, "CTOS");
					pushCallRef = true;
				} else if (doesDictStoreValueInRef(keyType, valueType)) {
					push(0, "PLDREF");
					push(0, "CTOS");
					pushCallRef = true;
				}
				pushCallRef &= isValueStruct;
				if (pushCallRef) {
					startContinuation();
				}
				preload(valueType);
				if (pushCallRef) {
					callRef(1, 1);
				}
				break;
			}
			case DictValueType::TvmCell:
			{
				if (!didUseOpcodeWithRef) {
					push(0, "PLDREF");
				}
				break;
			}
		}
	};

	auto checkOnMappingOrOptional = [&]() {
		if (optValueAsTuple(valueType)) {
			tuple(1);
		}
	};

	switch (decodeType) {
		case DecodeType::DecodeValue:
			if (pushRefCont) {
				startContinuation();
			}
			preloadValue();
			if (pushRefCont) {
				callRef(1, 1);
			}
			break;
		case DecodeType::DecodeValueOrPushDefault: {
			startContinuation();
			preloadValue();
			pushRefCont ? endContinuationFromRef() : endContinuation();

			bool hasEmptyPushCont = tryPollEmptyPushCont();
			startContinuation();
			pushDefaultValue(valueType, false);
			pushRefCont ? endContinuationFromRef() : endContinuation();

			if (hasEmptyPushCont)
				_ifNot();
			else
				ifElse();
			break;
		}
		case DecodeType::DecodeValueOrPushNull: {
			if (!saveOrigKeyAndNoTuple) {
				pushAsym("NULLSWAPIFNOT");
			}

			startContinuation();
			preloadValue();
			if (haveKey) {
				if (!saveOrigKeyAndNoTuple) {
					tuple(2);
				}
			} else {
				checkOnMappingOrOptional();
			}
			isValueStruct ? endContinuationFromRef() : endContinuation();

			if (saveOrigKeyAndNoTuple) {
				startContinuation();
				push(+1, "NULL");
				push(+1, "NULL");
				push(+1, "NULL");
				push(-3, ""); // fix stack
				endContinuation();

				ifElse();
			} else {
				_if();
			}

			break;
		}
		case DecodeType::PushNullOrDecodeValue: {
			pushAsym("NULLSWAPIF");

			startContinuation();
			preloadValue();
			checkOnMappingOrOptional();
			endContinuation();

			_ifNot();
			break;
		}
	}
}

void StackPusher::setDict(Type const &keyType, Type const &valueType, const DataType& dataType, SetDictOperation operation) {
	DictSet d{*this, keyType, valueType, dataType, operation};
	d.dictSet();
}

void StackPusher::pushInlineFunction(const Pointer<CodeBlock>& block, int take, int ret) {
	solAssert(block->type() == CodeBlock::Type::None, "");
	for (Pointer<TvmAstNode> const& i : block->instructions()) {
		m_instructions.back().opcodes.emplace_back(i);
	}
	change(take, ret);
}

void StackPusher::pollLastRetOpcode() {
	std::vector<Pointer<TvmAstNode>>& opcodes = m_instructions.back().opcodes;
	int offset = 0;
	int size = opcodes.size();
	while (offset < size && isLoc(opcodes.at(opcodes.size() - 1 - offset)))
		++offset;
	int begPos = size - 1 - offset;

	auto opcode = to<ReturnOrBreakOrCont>(opcodes.at(begPos).get());
	solAssert(opcode, "");
	vector<Pointer<TvmAstNode>> instructions = opcode->body()->instructions();
	solAssert(!instructions.empty(), "");
	auto ret = to<TvmReturn>(instructions.back().get());
	solAssert(ret, "");
	solAssert(ret->type() == TvmReturn::Type::RET, "");
	instructions.pop_back();

	opcodes.erase(opcodes.begin() + begPos);
	opcodes.insert(opcodes.begin() + begPos, instructions.begin(), instructions.end());
}

bool StackPusher::tryPollEmptyPushCont() {
	std::vector<Pointer<TvmAstNode>>& opcodes = m_instructions.back().opcodes;
	solAssert(opcodes.size() >= 2, "");
	auto block = dynamic_pointer_cast<CodeBlock>(opcodes.back());
	solAssert(block != nullptr, "");
	if (block->instructions().empty()) {
		opcodes.pop_back();
		return true;
	}
	return false;
}

TVMCompilerContext &StackPusher::ctx() {
	return *m_ctx;
}

void StackPusher::change(int delta) {
	solAssert(lockStack >= 0, "");
	if (lockStack == 0) {
		m_stack2.change(delta);
	}
}

void StackPusher::change(int take, int ret) {
	change(-take + ret);
}

int StackPusher::stackSize() const {
	return m_stack2.size();
}

void StackPusher::ensureSize(int savedStackSize, const string &location, const ASTNode* node) {
	if (lockStack == 0) {
		m_stack2.ensureSize(savedStackSize, location, node);
	}
}

void StackPusher::startOpaque() {
	++lockStack;
	m_instructions.emplace_back();
}

void StackPusher::endOpaque(int take, int ret, bool isPure) {
	--lockStack;
	solAssert(m_instructions.size() >= 2, "");
	PusherBlock block = m_instructions.back();
	m_instructions.pop_back();
	auto bl = createNode<CodeBlock>(CodeBlock::Type::None, block.opcodes);
	auto node = createNode<Opaque>(bl, take, ret, isPure);
	m_instructions.back().opcodes.push_back(node);
	change(take, ret);
}

void StackPusher::declRetFlag() {
	m_instructions.back().opcodes.push_back(createNode<DeclRetFlag>());
	change(0, 1);
}

Pointer<AsymGen>
StackPusher::asym(const string& cmd) {
	auto f = [&](const std::string& pattert) {
		istringstream iss(cmd);
		string real;
		iss >> real;
		return real == pattert;
	};

	auto dictRem = [&]() {
		for (std::string key : {"", "I", "U"}) {
			for (std::string op : {"MIN", "MAX"}) {
				for (std::string suf : {"", "REF"}) {
					std::string candidat = "DICT" + key + "REM" + op + suf;
					if (candidat == cmd) {
						return true;
					}
				}
			}
		}
		return false;
	};

	auto dictSomeGet = [&]() {
		for (std::string key : {"", "I", "U"}) {
			for (std::string op : {"SETGET", "ADDGET", "REPLACEGET"}) {
				for (std::string suf : {"", "REF", "B"}) {
					std::string candidat = "DICT" + key + op + suf;
					if (candidat == cmd) {
						return true;
					}
				}
			}
		}
		return false;
	};

	Pointer<AsymGen> opcode;
	if (f("CONFIGPARAM")) { opcode = createNode<AsymGen>(cmd, 1, 1, 2); }
	else if (f("NULLSWAPIF")) { opcode = createNode<AsymGen>(cmd, 1, 1, 2); }
	else if (f("NULLSWAPIFNOT")) { opcode = createNode<AsymGen>(cmd, 1, 1, 2); }

	else if (f("LDDICTQ")) { opcode = createNode<AsymGen>(cmd, 1, 2, 3); }
	else if (f("LDIQ")) { opcode = createNode<AsymGen>(cmd, 1, 2, 3); }
	else if (f("LDMSGADDRQ")) { opcode = createNode<AsymGen>(cmd, 1, 2, 3); }
	else if (f("LDUQ")) { opcode = createNode<AsymGen>(cmd, 1, 2, 3); }

	else if (f("DICTMIN")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }
	else if (f("DICTIMIN")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }
	else if (f("DICTUMIN")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }
	else if (f("DICTMINREF")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }
	else if (f("DICTIMINREF")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }
	else if (f("DICTUMINREF")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }

	else if (f("DICTMAX")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }
	else if (f("DICTIMAX")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }
	else if (f("DICTUMAX")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }
	else if (f("DICTMAXREF")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }
	else if (f("DICTIMAXREF")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }
	else if (f("DICTUMAXREF")) { opcode = createNode<AsymGen>(cmd, 2, 1, 3); }

	else if (f("CDATASIZEQ")) { opcode = createNode<AsymGen>(cmd, 2, 1, 4); }
	else if (f("SDATASIZEQ")) { opcode = createNode<AsymGen>(cmd, 2, 1, 4); }

	else if (dictRem()) { opcode = createNode<AsymGen>(cmd, 2, 2, 3); }

	// TODO use function generator
	else if (f("DICTGET")) { opcode = createNode<AsymGen>(cmd, 3, 1, 2); }
	else if (f("DICTIGET")) { opcode = createNode<AsymGen>(cmd, 3, 1, 2); }
	else if (f("DICTUGET")) { opcode = createNode<AsymGen>(cmd, 3, 1, 2); }
	else if (f("DICTGETREF")) { opcode = createNode<AsymGen>(cmd, 3, 1, 2); }
	else if (f("DICTIGETREF")) { opcode = createNode<AsymGen>(cmd, 3, 1, 2); }
	else if (f("DICTUGETREF")) { opcode = createNode<AsymGen>(cmd, 3, 1, 2); }

	else if (f("DICTGETNEXT")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTGETNEXTEQ")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTGETPREV")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTGETPREVEQ")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTIGETNEXT")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTIGETNEXTEQ")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTIGETPREV")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTIGETPREVEQ")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTUGETNEXT")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTUGETNEXTEQ")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTUGETPREV")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }
	else if (f("DICTUGETPREVEQ")) { opcode = createNode<AsymGen>(cmd, 3, 1, 3); }

	else if (dictSomeGet()) { opcode = createNode<AsymGen>(cmd, 4, 2, 3); }

	else solAssert(opcode, "StackPusher::asym " + cmd);
	return opcode;
}

void StackPusher::push(Pointer<Stack> opcode) {
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::push(Pointer<AsymGen> opcode) {
	// no stack changing
	solAssert(lockStack >= 0, "");
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::push(Pointer<HardCode> opcode) {
	m_instructions.back().opcodes.push_back(opcode);
	change(opcode->take(), opcode->ret());
}

void StackPusher::pushAsym(std::string const& opcode) {
	solAssert(lockStack >= 1, "");
	Pointer<AsymGen> node = asym(opcode);
	m_instructions.back().opcodes.push_back(node);
}

void StackPusher::push(int stackDiff, const string &cmd) {
	if (cmd.empty()) {
		change(stackDiff); // TODO delete
		return;
	}

	Pointer<GenOpcode> opcode = gen(cmd);
	solAssert(stackDiff == -opcode->take() + opcode->ret(), "stackDiff == -opcode->take() + opcode->ret() " + cmd);
	change(opcode->take(), opcode->ret());
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::pushCellOrSlice(Pointer<PushCellOrSlice> opcode) {
	solAssert(!m_instructions.empty(), "");
	m_instructions.back().opcodes.push_back(opcode);
	change(0, 1);
}

void StackPusher::startContinuation() {
	m_instructions.emplace_back();
}

void StackPusher::endCont(CodeBlock::Type type) {
	solAssert(!m_instructions.empty(), "");
	PusherBlock block = m_instructions.back();
	m_instructions.pop_back();
	auto b = createNode<CodeBlock>(type, block.opcodes);
	solAssert(!m_instructions.empty(), "");
	m_instructions.back().opcodes.push_back(b);
}

void StackPusher::endContinuation() {
	endCont(CodeBlock::Type::PUSHCONT);
}

void StackPusher::endContinuationFromRef() {
	endCont(CodeBlock::Type::PUSHREFCONT);
}

void StackPusher::endRetOrBreakOrCont(int _take) {
	solAssert(!m_instructions.empty(), "");
	PusherBlock block = m_instructions.back();
	m_instructions.pop_back();
	auto b = createNode<CodeBlock>(CodeBlock::Type::None, block.opcodes);
	auto r = createNode<ReturnOrBreakOrCont>(_take, b);
	solAssert(!m_instructions.empty(), "");
	m_instructions.back().opcodes.push_back(r);
}

void StackPusher::endLogCircuit(bool _canExpand, LogCircuit::Type type) {
	solAssert(!m_instructions.empty(), "");
	PusherBlock block = m_instructions.back();
	m_instructions.pop_back();
	auto b = createNode<CodeBlock>(CodeBlock::Type::None, block.opcodes);
	auto lc = createNode<LogCircuit>(_canExpand, type, b);
	solAssert(!m_instructions.empty(), "");
	m_instructions.back().opcodes.push_back(lc);
}

void StackPusher::callRefOrCallX(int take, int ret, SubProgram::Type type) {
	solAssert(!m_instructions.empty(), "");
	PusherBlock block = m_instructions.back();
	m_instructions.pop_back();
	auto b = createNode<CodeBlock>(CodeBlock::Type::None, block.opcodes);
	auto subProg = createNode<SubProgram>(take, ret, type, b);
	solAssert(!m_instructions.empty(), "");
	m_instructions.back().opcodes.push_back(subProg);
}

void StackPusher::callRef(int take, int ret) {
	callRefOrCallX(take, ret, SubProgram::Type::CALLREF);
}

void StackPusher::callX(int take, int ret) {
	callRefOrCallX(take, ret, SubProgram::Type::CALLX);
}

void StackPusher::ifElse(bool withJmp) {
	solAssert(m_instructions.back().opcodes.size() >= 3, "");
	auto falseBlock = dynamic_pointer_cast<CodeBlock>(m_instructions.back().opcodes.back());
	solAssert(falseBlock != nullptr, "");
	m_instructions.back().opcodes.pop_back();
	auto trueBlock = dynamic_pointer_cast<CodeBlock>(m_instructions.back().opcodes.back());
	solAssert(trueBlock != nullptr, "");
	m_instructions.back().opcodes.pop_back();
	TvmIfElse::Type type = withJmp ? TvmIfElse::Type::IFELSE_WITH_JMP : TvmIfElse::Type::IFELSE;
	auto b = createNode<TvmIfElse>(type, trueBlock, falseBlock);
	m_instructions.back().opcodes.push_back(b);
}

void StackPusher::pushConditional(int ret) {
	solAssert(m_instructions.back().opcodes.size() >= 3, "");
	auto falseBlock = dynamic_pointer_cast<CodeBlock>(m_instructions.back().opcodes.back());
	solAssert(falseBlock != nullptr, "");
	m_instructions.back().opcodes.pop_back();
	auto trueBlock = dynamic_pointer_cast<CodeBlock>(m_instructions.back().opcodes.back());
	solAssert(trueBlock != nullptr, "");
	m_instructions.back().opcodes.pop_back();
	auto b = createNode<TvmCondition>(trueBlock, falseBlock, ret);
	m_instructions.back().opcodes.push_back(b);
	push(ret, "");
}

void StackPusher::if_or_ifnot(TvmIfElse::Type t) {
	solAssert(m_instructions.back().opcodes.size() >= 1, "");
	auto trueBlock = dynamic_pointer_cast<CodeBlock>(m_instructions.back().opcodes.back());
	solAssert(trueBlock, "");
	m_instructions.back().opcodes.pop_back();
	auto b = createNode<TvmIfElse>(t, trueBlock);
	m_instructions.back().opcodes.push_back(b);
}

void StackPusher::_if() {
	if_or_ifnot(TvmIfElse::Type::IF);
}

void StackPusher::_ifNot() {
	if_or_ifnot(TvmIfElse::Type::IFNOT);
}

void StackPusher::ifJmp() {
	if_or_ifnot(TvmIfElse::Type::IFJMP);
}

void StackPusher::ifRef() {
	endContinuation();
	if_or_ifnot(TvmIfElse::Type::IFREF);
}

void StackPusher::ifNotRef() {
	endContinuation();
	if_or_ifnot(TvmIfElse::Type::IFNOTREF);
}

void StackPusher::ifJmpRef() {
	endContinuation();
	if_or_ifnot(TvmIfElse::Type::IFJMPREF);
}
void StackPusher::ifNotJmpRef() {
	endContinuation();
	if_or_ifnot(TvmIfElse::Type::IFNOTJMPREF);
}

void StackPusher::repeatOrUntil(bool isRepeat) {
	solAssert(m_instructions.back().opcodes.size() >= 1, "");

	auto loopBody = dynamic_pointer_cast<CodeBlock>(m_instructions.back().opcodes.back());
	m_instructions.back().opcodes.pop_back();
	solAssert(loopBody != nullptr, "");

	Pointer<TvmAstNode> b;
	if (isRepeat)
		b = createNode<TvmRepeat>(loopBody);
	else
		b = createNode<TvmUntil>(loopBody);
	m_instructions.back().opcodes.push_back(b);
}

void StackPusher::repeat() {
	repeatOrUntil(true);
}

void StackPusher::until() {
	repeatOrUntil(false);
}

void StackPusher::_while() {
	solAssert(m_instructions.back().opcodes.size() >= 3, ""); // TODO >= 2 ?
	auto body = dynamic_pointer_cast<CodeBlock>(m_instructions.back().opcodes.back());
	solAssert(body != nullptr, "");
	m_instructions.back().opcodes.pop_back();
	auto condition = dynamic_pointer_cast<CodeBlock>(m_instructions.back().opcodes.back());
	solAssert(condition != nullptr, "");
	m_instructions.back().opcodes.pop_back();
	auto b = createNode<While>(condition, body);
	m_instructions.back().opcodes.push_back(b);
}

void StackPusher::ret() {
	auto opcode = makeRET();
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::ifret() {
	auto opcode = makeIFRET();
	m_instructions.back().opcodes.push_back(opcode);
	change(1, 0);
}

void StackPusher::_throw(std::string cmd) {
	auto opcode = makeTHROW(cmd);
	m_instructions.back().opcodes.push_back(opcode);
	change(opcode->take(), opcode->ret());
}

TVMStack &StackPusher::getStack() {
//	solUnimplemented("");
	return m_stack2; // TODO delete
}

void StackPusher::untuple(int n) {
	solAssert(0 <= n, "");
	if (n <= 15) {
		push(-1 + n, "UNTUPLE " + toString(n));
	} else {
		solAssert(n <= 255, "");
		pushInt(n);
		auto b = createNode<GenOpcode>("UNTUPLEVAR", 2, n);
		m_instructions.back().opcodes.push_back(b);
		change(2, n);
	}
}

void StackPusher::indexWithExcep(int index) {
	solAssert(0 <= index && index <= 254, "");
	push(-1 + 1, "INDEX_EXCEP " + toString(index));
}

void StackPusher::indexNoexcep(int index) {
	solAssert(0 <= index && index <= 254, "");
	push(-1 + 1, "INDEX_NOEXCEP " + toString(index));
}

void StackPusher::setIndex(int index) {
	solAssert(0 <= index, "");
	if (index <= 15) {
		push(-2 + 1, "SETINDEX " + toString(index));
	} else {
		solAssert(index <= 254, "");
		pushInt(index);
		push(-3 + 1, "SETINDEXVAR");
	}
}

void StackPusher::setIndexQ(int index) {
	solAssert(0 <= index, "");
	if (index <= 15) {
		push(-2 + 1, "SETINDEXQ " + toString(index));
	} else {
		solAssert(index <= 254, "");
		pushInt(index);
		push(-1 - 2 + 1, "SETINDEXVARQ");
	}
}

void StackPusher::tuple(int qty) {
	solAssert(0 <= qty, "");
	if (qty <= 15) {
		push(-qty + 1, "TUPLE " + toString(qty));
	} else {
		solAssert(qty <= 255, "");
		pushInt(qty);
		auto opcode = createNode<GenOpcode>("TUPLEVAR", qty + 1, 1);
		m_instructions.back().opcodes.push_back(opcode);
		change(qty + 1, 1);
	}
}

void StackPusher::resetAllStateVars() {
	for (VariableDeclaration const *variable: ctx().notConstantStateVariables()) {
		pushDefaultValue(variable->type());
		setGlob(variable);
	}
}

void StackPusher::getGlob(VariableDeclaration const *vd) {
	const int index = ctx().getStateVarIndex(vd);
	getGlob(index);
}

void StackPusher::getGlob(int index) {
	solAssert(index >= 0, "");
	Pointer<Inst> opcode = createNode<Glob>(Glob::Opcode::GetOrGetVar, index);
	change(+1);
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::pushC4() {
	Pointer<Inst> opcode = createNode<Glob>(Glob::Opcode::PUSHROOT);
	change(+1);
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::popRoot() {
	Pointer<Inst> opcode = createNode<Glob>(Glob::Opcode::POPROOT);
	change(-1);
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::pushC3() {
	auto opcode = createNode<Glob>(Glob::Opcode::PUSH_C3);
	change(+1);
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::pushC7() {
	auto opcode = createNode<Glob>(Glob::Opcode::PUSH_C7);
	change(+1);
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::popC3() {
	auto opcode = createNode<Glob>(Glob::Opcode::POP_C3);
	change(-1);
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::popC7() {
	auto opcode = createNode<Glob>(Glob::Opcode::POP_C7);
	change(-1);
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::execute(int take, int ret) {
	auto opcode = createNode<GenOpcode>("EXECUTE", take, ret);
	change(take, ret);
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::setGlob(int index) {
	Pointer<Inst> opcode = makeSetGlob(index);
	change(-1);
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::setGlob(VariableDeclaration const *vd) {
	const int index = ctx().getStateVarIndex(vd);
	solAssert(index >= 0, "");
	setGlob(index);
}

void StackPusher::pushS(int i) {
	solAssert(i >= 0, "");
	m_instructions.back().opcodes.push_back(makePUSH(i));
	change(+1);
}

void StackPusher::dup2() {
	m_instructions.back().opcodes.push_back(makePUSH2(1, 0));
	change(+2);
}

void StackPusher::pushS2(int i, int j) {
	solAssert(i >= 0 && j >= 0, "");
	m_instructions.back().opcodes.push_back(makePUSH2(i, j));
	change(+2);
}

void StackPusher::popS(int i) {
	solAssert(i >= 1, "");
	m_instructions.back().opcodes.push_back(makePOP(i));
	change(-1);
}

void StackPusher::pushInt(const bigint& i) {
	push(+1, "PUSHINT " + toString(i));
}

bool StackPusher::fastLoad(const Type* type) {
	// slice
	switch (type->category()) {
		case Type::Category::Optional: {
			startOpaque();
			const int saveStakeSize = stackSize();
			auto opt = to<OptionalType>(type);

			auto f = [&](bool reverseOrder) {
				if (isSmallOptional(opt)) {
					load(opt->valueType(), reverseOrder);
				} else {
					push(-1 + 2, "LDREFRTOS");
					std::unique_ptr<StructCompiler> sc;
					if (auto st = to<StructType>(opt->valueType())) {
						sc = std::make_unique<StructCompiler>(this, st);
					} else if (auto tt = to<TupleType>(opt->valueType())) {
						sc = std::make_unique<StructCompiler>(this, tt);
					} else {
						solUnimplemented("");
					}
					sc->convertSliceToTuple();
					if (!reverseOrder) {
						exchange(1);
					}
				}
			};

			push(+1, "LDI 1"); // hasValue slice
			exchange(1);  // slice hasValue
			push(-1, ""); // fix stack

			startContinuation();
			if (optValueAsTuple(opt->valueType())) {
				f(true);
				tuple(1);
				exchange(1);
			} else {
				f(false);
			}
			endContinuation();
			push(-1, ""); // fix stack
			if (!hasLock()) {
				solAssert(saveStakeSize == getStack().size(), "");
			}

			startContinuation();
			pushNull();
			exchange(1);
			endContinuation();
			push(-1, ""); // fix stack
			if (!hasLock()) {
				solAssert(saveStakeSize == getStack().size(), "");
			}

			ifElse();
			push(+1, ""); // fix stack
			if (!hasLock()) {
				solAssert(saveStakeSize + 1 == getStack().size(), "");
			}
			endOpaque(1, 2);

			return true;
		}
		case Type::Category::Tuple: {
			auto tup = to<TupleType>(type);
			for (auto t : tup->components()) {
				load(t, false);
			}
			blockSwap(tup->components().size(), 1);
			tuple(tup->components().size());
			return false;
		}
		case Type::Category::TvmCell:
			push(-1 + 2, "LDREF");
			return true;
		case Type::Category::Struct: {
			auto st = to<StructType>(type);
			std::vector<ASTPointer<VariableDeclaration>> const& members = st->structDefinition().members();
			for (const ASTPointer<VariableDeclaration>& t : members) {
				load(t->type(), false);
			}
			blockSwap(members.size(), 1);
			tuple(members.size());
			exchange(1);
			return true;
		}
		case Type::Category::Address:
		case Type::Category::Contract:
			push(-1 + 2, "LDMSGADDR");
			return true;
		case Type::Category::Enum:
		case Type::Category::Integer:
		case Type::Category::Bool:
		case Type::Category::FixedPoint:
		case Type::Category::FixedBytes: {
			TypeInfo ti{type};
			solAssert(ti.isNumeric, "");
			string cmd = ti.isSigned ? "LDI " : "LDU ";
			push(-1 + 2, cmd + toString(ti.numBits));
			return true;
		}
		case Type::Category::Function: {
			push(-1 + 2, "LDU 32");
			return true;
		}
		case Type::Category::Array: {
			auto arrayType = to<ArrayType>(type);
			if (arrayType->isByteArray()) {
				push(-1 + 2, "LDREF");
				return true;
			} else {
				push(-1 + 2, "LDU 32");
				push(-1 + 2, "LDDICT");
				rotRev();
				push(-2 + 1, "PAIR");
				return false;
			}
		}
		case Type::Category::Mapping:
			push(-1 + 2, "LDDICT");
			return true;
		default:
			solUnimplemented(type->toString());
	}
	// true  => value slice
	// false => slice value
}

void StackPusher::load(const Type *type, bool reverseOrder) {
	// slice
	bool directOrder = fastLoad(type);
	if (directOrder == reverseOrder) {
		exchange(1);
	}
	// reverseOrder? slice member : member slice
}

void StackPusher::preload(const Type *type) {
	const int stackSize = this->stackSize();
	// on stack there is slice
	switch (type->category()) {
		case Type::Category::Optional: {
			load(type, false);
			drop();
			break;
		}
		case Type::Category::Address:
		case Type::Category::Contract:
			push(-1 + 2, "LDMSGADDR");
			drop(1);
			break;
		case Type::Category::TvmCell:
			push(0, "PLDREF");
			break;
		case Type::Category::Struct: {
			auto structType = to<StructType>(type);
			StructCompiler sc{this, structType};
			sc.convertSliceToTuple();
			break;
		}
		case Type::Category::Integer:
		case Type::Category::Enum:
		case Type::Category::Bool:
		case Type::Category::FixedPoint:
		case Type::Category::FixedBytes: {
			TypeInfo ti{type};
			solAssert(ti.isNumeric, "");
			string cmd = ti.isSigned ? "PLDI " : "PLDU ";
			push(-1 + 1, cmd + toString(ti.numBits));
			break;
		}
		case Type::Category::Function: {
			push(-1 + 1, "PLDU 32");
			break;
		}
		case Type::Category::Array: {
			auto arrayType = to<ArrayType>(type);
			if (arrayType->isByteArray()) {
				push(0, "PLDREF");
			} else {
				push(-1 + 2, "LDU 32");
				push(-1 + 1, "PLDDICT");
				push(-2 + 1, "PAIR");
				// stack: array
			}
			break;
		}
		case Type::Category::Mapping:
		case Type::Category::ExtraCurrencyCollection:
			push(-1 + 1, "PLDDICT");
			break;
		case Type::Category::VarInteger:
			push(-1 + 2, "LDVARUINT32");
			drop();
			break;
		case Type::Category::Tuple: {
			const auto[types, names] = getTupleTypes(to<TupleType>(type));
			StructCompiler sc{this, types, names};
			sc.convertSliceToTuple();
			break;
		}
		default:
			solUnimplemented("Decode isn't supported for " + type->toString(true));
	}
	ensureSize(stackSize);
}

void StackPusher::store(
	const Type *type,
	bool reverse
) {
	// value   builder  -> reverse = false
	// builder value	-> reverse = true
	const int stackSize = this->stackSize();
	int deltaStack = 1;
	switch (type->category()) {
		case Type::Category::Optional: {
			startOpaque();
			auto optType = to<OptionalType>(type);
			if (!reverse)
				exchange(1);	// builder value
			pushS(0);	// builder value value
			push(-1 + 1, "ISNULL");	// builder value isnull
			push(-1, ""); // fix stack
			ensureSize(stackSize);

			startContinuation();
			// builder value
			drop(1); // builder
			stzeroes(1);  // builder'
			endContinuation();
			push(+1, ""); // fix stack
			getStack().ensureSize(stackSize);

			startContinuation();
			// builder value
			if (isIn(optType->valueType()->category(), Type::Category::Optional, Type::Category::Mapping)) {
				untuple(1);
			}
			// builder value
			if (isSmallOptional(optType)) {
				exchange(1); // value builder
				stones(1); // value builder'
				store(optType->valueType(), false); // builder''
			} else {
				// builder' value
				std::unique_ptr<StructCompiler> sc;
				if (optType->valueType()->category() == Type::Category::Tuple) {
					auto tup = to<TupleType>(optType->valueType());
					sc = std::make_unique<StructCompiler>(this, tup);
				} else if (optType->valueType()->category() == Type::Category::Struct) {
					auto st = to<StructType>(optType->valueType());
					sc = std::make_unique<StructCompiler>(this, st);
				} else {
					// TODO add test for struct
					solUnimplemented("");
				}
				sc->tupleToBuilder();
				push(-2 + 1, "STBREFR");
				stones(1); // builder'
			}
			endContinuation();
			push(+1, ""); // fix stack
			ensureSize(stackSize);

			ifElse();
			endOpaque(2, 1, false);
			break;
		}
		case Type::Category::TvmCell:
			push(-1, reverse? "STREFR" : "STREF"); // builder
			break;
		case Type::Category::Struct: {
			auto structType = to<StructType>(type);
			if (!reverse)
				exchange(1);
			auto members = structType->structDefinition().members();
			untuple(members.size());
			this->reverse(members.size() + 1, 0);
			for (const auto& member : members)
				store(member->type(), false);
			break;
		}
		case Type::Category::Address:
		case Type::Category::Contract:
		case Type::Category::TvmSlice:
			push(-1, reverse? "STSLICER" : "STSLICE"); // builder slice-value
			break;
		case Type::Category::Integer:
		case Type::Category::Enum:
		case Type::Category::Bool:
		case Type::Category::FixedBytes:
		case Type::Category::FixedPoint:
			push(-1, storeIntegralOrAddress(type, reverse));
			break;
		case Type::Category::Function: {
			push(-1, reverse ? "STUR 32" : "STU 32");
			break;
		}
		case Type::Category::Mapping:
		case Type::Category::ExtraCurrencyCollection:
			if (reverse) {
				exchange(1); // builder dict
			}
			// dict builder
			push(-1, "STDICT"); // builder
			break;
		case Type::Category::Array: {
			auto arrayType = to<ArrayType>(type);
			if (arrayType->isByteArray()) {
				push(-1, reverse? "STREFR" : "STREF"); // builder
			} else {
				if (!reverse) {
					exchange(1); // builder arr
				}
				push(-1 + 2, "UNPAIR"); // builder size dict
				exchange(2);// dict size builder
				push(-1, "STU 32"); // dict builder'
				push(-1, "STDICT"); // builder''
			}
			break;
		}
		case Type::Category::TvmBuilder:
			push(-1, std::string("STB")  + (reverse ? "R " : ""));
			break;
		case Type::Category::Tuple: {
			if (!reverse)
				exchange(1);	// builder value

			const auto[types, names] = getTupleTypes(to<TupleType>(type));
			StructCompiler sc{this, types, names};
			sc.tupleToBuilder();
			push(-2 + 1, "STBR");
			break;
		}
		case Type::Category::VarInteger: {
			if (!reverse)
				exchange(1);	// builder value

			push(-1, "STVARUINT32"); // builder
			break;
		}
		default: {
			solUnimplemented("Encode isn't supported for " + type->toString(true));
		}
	}

	ensureSize(stackSize - deltaStack);
}

void StackPusher::pushZeroAddress() {
	push(+1, "PUSHSLICE x8000000000000000000000000000000000000000000000000000000000000000001_");
}

void StackPusher::addBinaryNumberToString(std::string &s, bigint value, int bitlen) {
	solAssert(value >= 0, "");
	for (int i = 0; i < bitlen; ++i) {
		s += value % 2 == 0? "0" : "1";
		value /= 2;
	}
	std::reverse(s.rbegin(), s.rbegin() + bitlen);
}

std::string StackPusher::binaryStringToSlice(const std::string &_s) {
	std::string s = _s;
	bool haveCompletionTag = false;
	if (s.size() % 4 != 0) {
		haveCompletionTag = true;
		s += "1";
		s += std::string((4 - s.size() % 4) % 4, '0');
	}
	std::string ans;
	for (int i = 0; i < static_cast<int>(s.length()); i += 4) {
		int x = stoi(s.substr(i, 4), nullptr, 2);
		std::stringstream sstream;
		sstream << std::hex << x;
		ans += sstream.str();
	}
	if (haveCompletionTag) {
		ans += "_";
	}
	return ans;
}

std::string StackPusher::toBitString(const std::string& slice) {
	std::string bitString;
	if (slice.at(0) == 'x') {
		for (std::size_t i = 1; i < slice.size(); ++i) {
			if (i + 2 == slice.size() && slice[i + 1] == '_') {
				size_t pos{};
				int value = std::stoi(slice.substr(i, 1), &pos, 16);
				solAssert(pos == 1, "");
				int bitLen = 4;
				while (true) {
					bool isOne = value % 2 == 1;
					--bitLen;
					value /= 2;
					if (isOne) {
						break;
					}
				}
				StackPusher::addBinaryNumberToString(bitString, value, bitLen);
				break;
			}
			size_t pos{};
			auto sss = slice.substr(i, 1);
			int value = std::stoi(sss, &pos, 16);
			solAssert(pos == 1, "");
			StackPusher::addBinaryNumberToString(bitString, value, 4);
		}
	} else {
		if (isIn(slice, "0", "1")) {
			return slice;
		}
		solUnimplemented("");
	}
	return bitString;
}

std::vector<std::string> StackPusher::unitSlices(const std::string& sliceA, const std::string& sliceB) {
	return unitBitString(toBitString(sliceA), toBitString(sliceB));
}

std::vector<std::string> StackPusher::unitBitString(const std::string& bitStringA, const std::string& bitStringB) {
	const std::string& bitString = bitStringA + bitStringB;
	std::vector<std::string> opcodes;
	for (int i = 0; i < static_cast<int>(bitString.length()); i += 4 * TvmConst::MaxPushSliceBitLength) {
		opcodes.push_back(bitString.substr(i, std::min(4 * TvmConst::MaxPushSliceBitLength, static_cast<int>(bitString.length()) - i)));
	}
	for (std::string& opcode : opcodes) {
		opcode = "x" + StackPusher::binaryStringToSlice(opcode);
	}
	return opcodes;
}

std::string StackPusher::tonsToBinaryString(Literal const *literal) {
	Type const* type = literal->annotation().type;
	u256 value = type->literalValue(literal);
	return tonsToBinaryString(value);
}

std::string StackPusher::tonsToBinaryString(const u256& value) {
	return tonsToBinaryString(bigint(value));
}

std::string StackPusher::tonsToBinaryString(bigint value) {
	std::string s;
	int len = 256;
	for (int i = 0; i < 256; ++i) {
		if (value == 0) {
			len = i;
			break;
		}
		s += value % 2 == 0? "0" : "1";
		value /= 2;
	}
	solAssert(len < 120, "Ton value should fit 120 bit");
	while (len % 8 != 0) {
		s += "0";
		len++;
	}
	std::reverse(s.rbegin(), s.rbegin() + len);
	len = len/8;
	std::string res;
	for (int i = 0; i < 4; ++i) {
		res += len % 2 == 0? "0" : "1";
		len /= 2;
	}
	std::reverse(res.rbegin(), res.rbegin() + 4);
	return res + s;
}

std::string StackPusher::boolToBinaryString(bool value) {
	return value ? "1" : "0";
}

std::string StackPusher::literalToSliceAddress(Literal const *literal, bool pushSlice) {
	Type const* type = literal->annotation().type;
	u256 value = type->literalValue(literal);
//		addr_std$10 anycast:(Maybe Anycast) workchain_id:int8 address:bits256 = MsgAddressInt;
	std::string s;
	s += "10";
	s += "0";
	s += std::string(8, '0');
	addBinaryNumberToString(s, value);
	if (pushSlice)
		push(+1, "PUSHSLICE x" + binaryStringToSlice(s));
	return s;
}

bigint StackPusher::pow10(int power) {
	bigint r = 1;
	for (int i = 1; i <= power; ++i) {
		r *= 10;
	}
	return r;
}

void StackPusher::hardConvert(Type const *leftType, Type const *rightType) {
	// case opt(T) = T
	if (leftType->category() == Type::Category::Optional && *leftType != *rightType) {
		auto l = to<OptionalType>(leftType);
		hardConvert(l->valueType(), rightType);
		if (optValueAsTuple(l->valueType())) {
			tuple(1);
		}
		return;
	}



	bool impl = rightType->isImplicitlyConvertibleTo(*leftType);

	auto fixedPointFromFixedPoint = [this, impl](FixedPointType const* l, FixedPointType const* r) {
		int powerDiff = l->fractionalDigits() - r->fractionalDigits();
		if (powerDiff != 0) {
			if (powerDiff > 0) {
				pushInt(pow10(powerDiff));
				push(-2 + 1, "MUL");
			} else {
				pushInt(pow10(-powerDiff));
				push(-2 + 1, "DIV");
			}
		}
		if (!impl)
			checkFit(l);
	};

	auto integerFromFixedPoint = [this, impl](IntegerType const* l, FixedPointType const* r) {
		int powerDiff = r->fractionalDigits();
		if (powerDiff > 0) {
			pushInt(pow10(powerDiff));
			push(-2 + 1, "DIV");
		}
		if (!impl)
			checkFit(l);
	};

	auto integerFromInteger = [this, impl](IntegerType const* l, IntegerType const* /*r*/) {
		if (!impl)
			checkFit(l);
	};

	auto fixedPointFromInteger = [this, impl](FixedPointType const* l, IntegerType const* /*r*/) {
		int powerDiff = l->fractionalDigits();
		if (powerDiff > 0) {
			pushInt(pow10(powerDiff));
			push(-2 + 1, "MUL");
		}
		if (!impl)
			checkFit(l);
	};

	auto fixedBytesFromFixedBytes = [this](FixedBytesType const* l, FixedBytesType const* r) {
		int diff = 8 * (l->numBytes() - r->numBytes());
		if (diff > 0) {
			push(0, "LSHIFT " + std::to_string(diff));
		} else if (diff < 0) {
			push(0, "RSHIFT " + std::to_string(-diff));
		}
	};

	auto fixedBytesFromBytes = [this](FixedBytesType const* r) {
		size_t bits = r->numBytes() * 8;
		push(0, "CTOS");
		push(0, "PLDU " + std::to_string(bits));
	};

	auto fixedBytesFromStringLiteral = [this](FixedBytesType const* l, StringLiteralType const* r) {
		size_t bytes = 0;
		u256 value = 0;
		for (char c : r->value()) {
			value = value * 256 + c;
			++bytes;
		}
		while (bytes < l->numBytes()) {
			value *= 256;
			++bytes;
		}
		drop(1); // delete old value
		push(+1, "PUSHINT " + toString(value));
	};

	auto fromFixedPoint = [&](FixedPointType const* r) {
		switch (leftType->category()) {
			case Type::Category::FixedPoint:
				fixedPointFromFixedPoint(to<FixedPointType>(leftType), r);
				break;
			case Type::Category::Integer:
				integerFromFixedPoint(to<IntegerType>(leftType), r);
				break;
			default:
				solUnimplemented("");
				break;
		}
	};

	auto fromInteger = [&](IntegerType const* r) {
		switch (leftType->category()) {
			case Type::Category::FixedPoint:
				fixedPointFromInteger(to<FixedPointType>(leftType), r);
				break;
			case Type::Category::Integer:
				integerFromInteger(to<IntegerType>(leftType), r);
				break;
			case Type::Category::FixedBytes:
				// do nothing here
				break;
			case Type::Category::Address:
				solUnimplemented("See FunctionCallCompiler::typeConversion");
				break;
			default:
				solUnimplemented(leftType->toString());
				break;
		}
	};

	auto tupleFromTuple = [&](TupleType const* leftType, TupleType const* rightType) {
		std::vector<TypePointer> const& lc = leftType->components();
		std::vector<TypePointer> const& rc = rightType->components();
		solAssert(lc.size() == rc.size(), "");
		int n = lc.size();
		for (int i = n - 1; 0 <= i; --i) {
			hardConvert(lc.at(i), rc.at(i));
			if (n >= 2) {
				blockSwap(n - 1, 1);
			}
		}
	};
	
	
	
	switch (rightType->category()) {

		case Type::Category::RationalNumber: {
			Type const* mt = rightType->mobileType();
			if (mt->category() == Type::Category::Integer) {
				fromInteger(to<IntegerType>(mt));
			} else if (mt->category() == Type::Category::FixedPoint) {
				fromFixedPoint(to<FixedPointType>(mt));
			} else {
				solUnimplemented("");
			}
			break;
		}

		case Type::Category::FixedPoint: {
			fromFixedPoint(to<FixedPointType>(rightType));
			break;
		}

		case Type::Category::Integer: {
			fromInteger(to<IntegerType>(rightType));
			break;
		}

		case Type::Category::FixedBytes: {
			auto r = to<FixedBytesType>(rightType);
			switch (leftType->category()) {
				case Type::Category::FixedBytes: {
					fixedBytesFromFixedBytes(to<FixedBytesType>(leftType), r);
					break;
				}
				case Type::Category::Integer: {
					auto intType = to<IntegerType>(leftType);
					if (intType && !intType->isSigned() &&
						(intType->numBits() >= r->numBytes() * 8))
						break;
					solUnimplemented("");
					break;
				}
				case Type::Category::FixedPoint: {
					auto fixType = to<FixedPointType>(leftType);
					if (fixType && fixType->isSigned() &&
						(fixType->numBits() >= r->numBytes() * 8)) {
						fromInteger(to<IntegerType>(rightType));
						break;
					}
					solUnimplemented("");
					break;
				}
				default:
					solUnimplemented("");
					break;
			}
			break;
		}

		case Type::Category::Array: {
			auto r = to<ArrayType>(rightType);
			if (!r->isByteArray()) {
				break;
			}
			// bytes or string
			switch (leftType->category()) {
				case Type::Category::FixedBytes:
					fixedBytesFromBytes(to<FixedBytesType>(leftType));
					break;
				case Type::Category::Array:
					break;
				default:
					solUnimplemented("");
					break;
			}
			break;
		}

		case Type::Category::Address:
		case Type::Category::Bool:
		case Type::Category::Contract:
		case Type::Category::Enum:
		case Type::Category::ExtraCurrencyCollection:
		case Type::Category::Function:
		case Type::Category::Mapping:
		case Type::Category::Optional: // TODO !!!
		case Type::Category::TvmVector:
		case Type::Category::Struct:
		case Type::Category::TvmBuilder:
		case Type::Category::TvmCell:
		case Type::Category::TvmSlice:
		case Type::Category::Null:
			break;

		case Type::Category::Tuple: {
			auto r = to<TupleType>(rightType);
			switch (leftType->category()) {
				case Type::Category::Tuple:
					tupleFromTuple(to<TupleType>(leftType), r);
					break;
				default:
					solUnimplemented(leftType->toString());
					break;
			}
			break;
		}

		case Type::Category::StringLiteral: {
			auto r = to<StringLiteralType>(rightType);
			switch (leftType->category()) {
				case Type::Category::FixedBytes:
					fixedBytesFromStringLiteral(to<FixedBytesType>(leftType), r);
					break;
				case Type::Category::Array:
					break;
				default:
					solUnimplemented(leftType->toString());
					break;
			}
			break;
		}

		default:
			solUnimplemented(rightType->toString());
			break;
	}
}

void StackPusher::checkFit(Type const *type) {
	switch (type->category()) {

		case Type::Category::Integer: {
			auto it = to<IntegerType>(type);
			if (it->isSigned()) {
				push(0, "FITS " + toString(it->numBits()));
			} else {
				push(0, "UFITS " + toString(it->numBits()));
			}
			break;
		}

		case Type::Category::FixedPoint: {
			auto fp = to<FixedPointType>(type);
			if (fp->isSigned()) {
				push(0, "FITS " + toString(fp->numBits()));
			} else {
				push(0, "UFITS " + toString(fp->numBits()));
			}
			break;
		}

		default:
			solUnimplemented("");
			break;
	}

}

void StackPusher::pushParameter(std::vector<ASTPointer<VariableDeclaration>> const& params) {
	for (const ASTPointer<VariableDeclaration>& variable: params) {
		getStack().add(variable.get(), true);
	}
}

void StackPusher::pushMacroCallInCallRef(int take, int ret, const string& functionName) {
	startContinuation();
	pushCall(take, ret, functionName);
	callRef(take, ret);
}

void StackPusher::pushCallOrCallRef(
	const string &functionName,
	const FunctionType *ft,
	const std::optional<std::pair<int, int>>& deltaStack
) {
	int take{};
	int ret{};
	if (deltaStack.has_value()) {
		std::tie(take, ret) = deltaStack.value();
	} else {
		take = ft->parameterTypes().size();
		ret = ft->returnParameterTypes().size();
	}

	if (boost::ends_with(functionName, "_macro") || functionName == ":onCodeUpgrade") {
		pushMacroCallInCallRef(take, ret, functionName);
		return;
	}

	auto _to = to<FunctionDefinition>(&ft->declaration());
	FunctionDefinition const* v = m_ctx->getCurrentFunction();
	bool hasLoop = m_ctx->addAndDoesHaveLoop(v, _to);
	if (hasLoop) {
		pushCall(take, ret, functionName);
	} else {
		pushMacroCallInCallRef(take, ret, functionName + "_macro");
	}
}

void StackPusher::pushCall(int take, int ret, const std::string& functionName) {
	change(take, ret);
	auto opcode = createNode<GenOpcode>("CALL $" + functionName + "$", take, ret);
	m_instructions.back().opcodes.push_back(opcode);
}

void StackPusher::drop(int cnt) {
	// TODO add assert cnt >= 0
	if (cnt >= 1) {
		auto opcode = makeDROP(cnt);
		push(-cnt, "");
		m_instructions.back().opcodes.push_back(opcode);
	}
}

void StackPusher::blockSwap(int down, int up) {
	solAssert(0 <= down, "");
	solAssert(0 <= up, "");
	if (down == 0 || up == 0) {
		return;
	}
	push(createNode<Stack>(Stack::Opcode::BLKSWAP, down, up));
}

void StackPusher::reverse(int i, int j) {
	push(makeREVERSE(i, j));
}

void StackPusher::dropUnder(int droppedCount, int leftCount) {
	// drop dropCount elements that are situated under top leftCount elements
	solAssert(leftCount >= 0, "");
	solAssert(droppedCount >= 0, "");

	if (droppedCount == 0) {
		// do nothing
	} else if (leftCount == 0) {
		drop(droppedCount);
	} else if (droppedCount == 1 && leftCount == 1) {
		popS(1);
	} else {
		push(createNode<Stack>(Stack::Opcode::BLKDROP2, droppedCount, leftCount));
		change(-droppedCount);
	}
}

void StackPusher::exchange(int i) {
	Pointer<Stack> opcode = makeXCH_S(i);
	push(opcode);
}

void StackPusher::rot() {
	push(makeROT());
}

void StackPusher::rotRev() {
	push(makeROTREV());
}

TypePointer StackPusher::parseIndexType(Type const *type) {
	if (to<ArrayType>(type)) {
		return TypeProvider::uint(32);
	}
	if (auto mappingType = to<MappingType>(type)) {
		return mappingType->keyType();
	}
	if (auto currencyType = to<ExtraCurrencyCollectionType>(type)) {
		return currencyType->keyType();
	}
	solUnimplemented("");
}

TypePointer StackPusher::parseValueType(IndexAccess const &indexAccess) {
	if (auto currencyType = to<ExtraCurrencyCollectionType>(indexAccess.baseExpression().annotation().type)) {
		return currencyType->realValueType();
	}
	return indexAccess.annotation().type;
}

bool StackPusher::tryAssignParam(Declaration const *name) {
	auto& stack = getStack();
	if (stack.isParam(name)) {
		int idx = stack.getOffset(name);
		solAssert(idx >= 0, "");
		if (idx == 0) {
			// nothing
		} else {
			popS(idx);
		}
		return true;
	}
	return false;
}

void StackPusher::prepareKeyForDictOperations(Type const *key, bool doIgnoreBytes) {
	// stack: key
	if (isStringOrStringLiteralOrBytes(key) || key->category() == Type::Category::TvmCell) {
		if (!doIgnoreBytes) {
			push(-1 + 1, "HASHCU");
		}
	} else if (key->category() == Type::Category::Struct) {
		StructCompiler sc{this, to<StructType>(key)};
		sc.tupleToBuilder();
		push(0, "ENDC");
		push(0, "CTOS");
	}
}

int StackPusher::int_msg_info(const std::set<int> &isParamOnStack, const std::map<int, std::string> &constParams,
									bool isDestBuilder) {
	// int_msg_info$0  ihr_disabled:Bool  bounce:Bool(#1)  bounced:Bool
	//				 src:MsgAddress  dest:MsgAddressInt(#4)
	//				 value:CurrencyCollection(#5,#6)  ihr_fee:Grams  fwd_fee:Grams
	//				 created_lt:uint64  created_at:uint32
	//				 = CommonMsgInfoRelaxed;

	// currencies$_ grams:Grams other:ExtraCurrencyCollection = CurrencyCollection;

	static const std::vector<int> zeroes {1, 1, 1,
									2, 2,
									4, 1, 4, 4,
									64, 32};
	std::string bitString = "0";
	int maxBitStringSize = 0;
	push(+1, "NEWC");
	for (int param = 0; param < static_cast<int>(zeroes.size()); ++param) {
		solAssert(constParams.count(param) == 0 || isParamOnStack.count(param) == 0, "");

		if (constParams.count(param) != 0) {
			bitString += constParams.at(param);
			maxBitStringSize += constParams.at(param).length();
		} else if (isParamOnStack.count(param) == 0) {
			bitString += std::string(zeroes.at(param), '0');
			maxBitStringSize += zeroes.at(param);
			solAssert(param != TvmConst::int_msg_info::dest, "");
		} else {
			appendToBuilder(bitString);
			bitString = "";
			switch (param) {
				case TvmConst::int_msg_info::bounce:
					push(-1, "STI 1");
					++maxBitStringSize;
					break;
				case TvmConst::int_msg_info::dest:
					if (isDestBuilder) {
						push(-1, "STB");
					} else {
						push(-1, "STSLICE");
					}
					maxBitStringSize += AddressInfo::maxBitLength();
					break;
				case TvmConst::int_msg_info::tons:
					exchange(1);
					push(-1, "STGRAMS");
					maxBitStringSize += VarUIntegerInfo::maxTonBitLength();
					break;
				case TvmConst::int_msg_info::currency:
					push(-1, "STDICT");
					++maxBitStringSize;
					break;
				default:
					solUnimplemented("");
			}
		}
	}
	appendToBuilder(bitString);
	bitString = "";
	return maxBitStringSize;
}

int StackPusher::ext_msg_info(const set<int> &isParamOnStack, bool isOut = true) {
	// ext_in_msg_info$10 src:MsgAddressExt dest:MsgAddressInt
	// import_fee:Grams = CommonMsgInfo;
	//
	// ext_out_msg_info$11 src:MsgAddressInt dest:MsgAddressExt
	// created_lt:uint64 created_at:uint32 = CommonMsgInfo;

	std::vector<int> zeroes {2, 2};
	if (isOut) {
		zeroes.push_back(64);
		zeroes.push_back(32);
	} else {
		zeroes.push_back(4);
	}
	std::string bitString = isOut ? "11" : "10";
	int maxBitStringSize = 0;
	push(+1, "NEWC");
	for (int param = 0; param < static_cast<int>(zeroes.size()); ++param) {
		if (isParamOnStack.count(param) == 0) {
			bitString += std::string(zeroes.at(param), '0');
		} else {
			maxBitStringSize += bitString.size();
			appendToBuilder(bitString);
			bitString = "";
			if (param == TvmConst::ext_msg_info::dest) {
				push(-1, "STSLICE");
				maxBitStringSize += AddressInfo::maxBitLength();
			} else if (param == TvmConst::ext_msg_info::src) {
				push(-1, "STB");
				maxBitStringSize += TvmConst::ExtInboundSrcLength;
			} else {
				solUnimplemented("");
			}
		}
	}
	maxBitStringSize += bitString.size();
	appendToBuilder(bitString);
	bitString = "";
	return maxBitStringSize;
}


void StackPusher::appendToBuilder(const std::string &bitString) {
	// stack: builder
	if (bitString.empty()) {
		return;
	}

	size_t count = std::count_if(bitString.begin(), bitString.end(), [](char c) { return c == '0'; });
	if (count == bitString.size()) {
		stzeroes(count);
	} else {
		const std::string hex = binaryStringToSlice(bitString);
		if (hex.length() * 4 <= 8 * 7 + 1) {
			push(0, "STSLICECONST x" + hex);
		} else {
			push(+1, "PUSHSLICE x" + binaryStringToSlice(bitString));
			push(-1, "STSLICER");
		}
	}
}

void StackPusher::checkOptionalValue() {
	push(-1 + 1, "ISNULL");
	_throw("THROWIF " + toString(TvmConst::RuntimeException::GetOptionalException));
}

void StackPusher::stzeroes(int qty) {
	if (qty > 0) {
		// builder
		if (qty == 1) {
			push(0, "STSLICECONST 0");
		} else {
			pushInt(qty); // builder qty
			push(-1, "STZEROES");
		}
	}
}

void StackPusher::stones(int qty) {
	if (qty > 0) {
		// builder
		if (qty == 1) {
			push(0, "STSLICECONST 1");
		} else {
			pushInt(qty); // builder qty
			push(-1, "STONES");
		}
	}
}

void StackPusher::sendrawmsg() {
	push(-2, "SENDRAWMSG");
}

void StackPusher::sendIntMsg(const std::map<int, Expression const *> &exprs,
								   const std::map<int, std::string> &constParams,
								   const std::function<void(int)> &appendBody,
								   const std::function<void()> &pushSendrawmsgFlag,
								   bool isAwait,
								   size_t callParamsOnStack,
								   const std::function<void()> &appendStateInit) {
	std::set<int> isParamOnStack;
	size_t pushedValCnt = 0;
	for (auto &[param, expr] : exprs | boost::adaptors::reversed) {
		isParamOnStack.insert(param);
		TVMExpressionCompiler{*this}.compileNewExpr(expr);
		if (param != TvmConst::int_msg_info::dest)
			++pushedValCnt;
		else if (isAwait) {
			pushS(0);
			++pushedValCnt;
			blockSwap(pushedValCnt + callParamsOnStack, 1);
		}
	}
	sendMsg(isParamOnStack, constParams, appendBody, appendStateInit, pushSendrawmsgFlag);
}



void StackPusher::prepareMsg(
	const std::set<int>& isParamOnStack,
	const std::map<int, std::string> &constParams,
	const std::function<void(int)> &appendBody,
	const std::function<void()> &appendStateInit,
	MsgType messageType,
	bool isDestBuilder
) {
	int msgInfoSize = 0;
	switch (messageType) {
		case MsgType::Internal:
			msgInfoSize = int_msg_info(isParamOnStack, constParams, isDestBuilder);
			break;
		case MsgType::ExternalOut:
			msgInfoSize = ext_msg_info(isParamOnStack);
			break;
		case MsgType::ExternalIn:
			msgInfoSize = ext_msg_info(isParamOnStack, false);
			break;
	}
	// stack: builder

	if (appendStateInit) {
		// stack: values... builder
		appendToBuilder("1");
		appendStateInit();
		++msgInfoSize;
		// stack: builder-with-stateInit
	} else {
		appendToBuilder("0"); // there is no StateInit
	}

	++msgInfoSize;

	if (appendBody) {
		// stack: values... builder
		appendBody(msgInfoSize);
		// stack: builder-with-body
	} else {
		appendToBuilder("0"); // there is no body
	}

	// stack: builder'
	push(0, "ENDC"); // stack: cell
}

void StackPusher::sendMsg(const std::set<int>& isParamOnStack,
								const std::map<int, std::string> &constParams,
								const std::function<void(int)> &appendBody,
								const std::function<void()> &appendStateInit,
								const std::function<void()> &pushSendrawmsgFlag,
								MsgType messageType,
								bool isDestBuilder) {
	prepareMsg(isParamOnStack, constParams, appendBody, appendStateInit, messageType, isDestBuilder);
	if (pushSendrawmsgFlag) {
		pushSendrawmsgFlag();
	} else {
		pushInt(TvmConst::SENDRAWMSG::DefaultFlag);
	}
	sendrawmsg();
}

int TVMStack::size() const {
	return m_size;
}

void TVMStack::change(int diff) {
	if (diff != 0) {
		m_size += diff;
		solAssert(m_size >= 0, "");
	}
}

void TVMStack::change(int take, int ret) {
	solAssert(take >= 0, "");
	solAssert(ret >= 0, "");
	change(-take + ret);
}

bool TVMStack::isParam(Declaration const *name) const {
	return getStackSize(name) != -1;
}

void TVMStack::add(Declaration const *name, bool doAllocation) {
	solAssert(name != nullptr, "");
	if (doAllocation) {
		++m_size;
	}
	if (static_cast<int>(m_stackSize.size()) < m_size) {
		m_stackSize.resize(m_size);
	}
	m_stackSize.at(m_size - 1) = name;
}

int TVMStack::getOffset(Declaration const *name) const {
	solAssert(isParam(name), "");
	int stackSize = getStackSize(name);
	return getOffset(stackSize);
}

int TVMStack::getOffset(int stackSize) const {
	return m_size - 1 - stackSize;
}

int TVMStack::getStackSize(Declaration const *name) const {
	for (int i = m_size - 1; i >= 0; --i) {
		if (i < static_cast<int>(m_stackSize.size()) && m_stackSize.at(i) == name) {
			return i;
		}
	}
	return -1;
}

void TVMStack::ensureSize(int savedStackSize, const string &location, const ASTNode* node) const {
	if (node != nullptr && savedStackSize != m_size) {
		cast_error(*node, string{} + "Stack size error: expected: " + toString(savedStackSize)
								   + " but real: " + toString(m_size) + " at " + location);
	}
	solAssert(savedStackSize == m_size, "stack: exp:" + toString(savedStackSize)
				+ " real: " + toString(m_size) + " at " + location);
}

void TVMStack::takeLast(int n) {
	solAssert(m_size >= n, "");
	solAssert(int(m_stackSize.size()) >= m_size, "");
	m_stackSize.resize(m_size);
	m_stackSize = vector<Declaration const*>(m_stackSize.end() - n, m_stackSize.end());
	m_size = n;
	solAssert(int(m_stackSize.size()) == n, "");
}

void TVMCompilerContext::initMembers(ContractDefinition const *contract) {
	solAssert(!m_contract, "");
	m_contract = contract;

	for (ContractDefinition const* c : contract->annotation().linearizedBaseContracts) {
		for (FunctionDefinition const *_function : c->definedFunctions()) {
			const std::set<CallableDeclaration const*>& b = _function->annotation().baseFunctions;
			m_baseFunctions.insert(b.begin(), b.end());
		}
	}

	ignoreIntOverflow = m_pragmaHelper.haveIgnoreIntOverflow();
	for (VariableDeclaration const *variable: notConstantStateVariables()) {
		m_stateVarIndex[variable] = TvmConst::C7::FirstIndexForVariables + m_stateVarIndex.size();
	}
}

TVMCompilerContext::TVMCompilerContext(ContractDefinition const *contract, PragmaDirectiveHelper const &pragmaHelper) :
	m_pragmaHelper{pragmaHelper},
	m_usage{*contract}
{
	initMembers(contract);
}

int TVMCompilerContext::getStateVarIndex(VariableDeclaration const *variable) const {
	return m_stateVarIndex.at(variable);
}

std::vector<VariableDeclaration const *> TVMCompilerContext::notConstantStateVariables() const {
	std::vector<VariableDeclaration const*> variableDeclarations;
	std::vector<ContractDefinition const*> mainChain = getContractsChain(getContract());
	for (ContractDefinition const* contract : mainChain) {
		for (VariableDeclaration const *variable: contract->stateVariables()) {
			if (!variable->isConstant()) {
				variableDeclarations.push_back(variable);
			}
		}
	}
	return variableDeclarations;
}

bool TVMCompilerContext::tooMuchStateVariables() const {
	return notConstantStateVariables().size() >= TvmConst::C7::FirstIndexForVariables + 6;
}

std::vector<Type const *> TVMCompilerContext::notConstantStateVariableTypes() const {
	std::vector<Type const *> types;
	for (VariableDeclaration const * var : notConstantStateVariables()) {
		types.emplace_back(var->type());
	}
	return types;
}

PragmaDirectiveHelper const &TVMCompilerContext::pragmaHelper() const {
	return m_pragmaHelper;
}

bool TVMCompilerContext::hasTimeInAbiHeader() const {
	switch (m_pragmaHelper.abiVersion()) {
		case AbiVersion::V1:
			return true;
		case AbiVersion::V2_1:
			return m_pragmaHelper.haveTime() || afterSignatureCheck() == nullptr;
	}
	solUnimplemented("");
}

bool TVMCompilerContext::isStdlib() const {
	return m_contract->name() == "stdlib";
}

string TVMCompilerContext::getFunctionInternalName(FunctionDefinition const* _function, bool calledByPoint) const {
	if (isStdlib()) {
		return _function->name();
	}
	if (_function->name() == "onCodeUpgrade") {
		return ":onCodeUpgrade";
	}
	if (_function->isFallback()) {
		return "fallback";
	}

	std::string functionName;
	if (calledByPoint && isBaseFunction(_function)) {
		functionName = _function->annotation().contract->name() + "_" + _function->name();
	} else {
		functionName = _function->name() + "_internal";
	}

	return functionName;
}

string TVMCompilerContext::getLibFunctionName(FunctionDefinition const* _function, bool withObject) {
	std::string name = _function->annotation().contract->name() +
			(withObject ? "_with_obj_" : "_no_obj_") +
			_function->name();
	return name;
}

string TVMCompilerContext::getFunctionExternalName(FunctionDefinition const *_function) {
	const string& fname = _function->name();
	solAssert(_function->isPublic(), "Internal error: expected public function: " + fname);
	if (_function->isConstructor()) {
		return "constructor";
	}
	if (_function->isFallback()) {
		return "fallback";
	}
	return fname;
}

const ContractDefinition *TVMCompilerContext::getContract() const {
	return m_contract;
}

bool TVMCompilerContext::ignoreIntegerOverflow() const {
	return ignoreIntOverflow;
}

FunctionDefinition const *TVMCompilerContext::afterSignatureCheck() const {
	for (FunctionDefinition const* f : m_contract->definedFunctions()) {
		if (f->name() == "afterSignatureCheck") {
			return f;
		}
	}
	return nullptr;
}

bool TVMCompilerContext::storeTimestampInC4() const {
	return hasTimeInAbiHeader() && afterSignatureCheck() == nullptr;
}

int TVMCompilerContext::getOffsetC4() const {
	return
		256 + // pubkey
		(storeTimestampInC4() ? 64 : 0) +
		1 + // constructor flag
		(m_usage.hasAwaitCall() ? 1 : 0);
}

void TVMCompilerContext::addLib(FunctionDefinition const* f) {
	m_libFunctions.insert(f);
}

std::vector<std::pair<VariableDeclaration const*, int>> TVMCompilerContext::getStaticVariables() const {
	int shift = 0;
	std::vector<std::pair<VariableDeclaration const*, int>> res;
	for (VariableDeclaration const* v : notConstantStateVariables()) {
		if (v->isStatic()) {
			res.emplace_back(v, TvmConst::C4::PersistenceMembersStartIndex + shift++);
		}
	}
	return res;
}

void TVMCompilerContext::addInlineFunction(const std::string& name, Pointer<CodeBlock> body) {
	solAssert(m_inlinedFunctions.count(name) == 0, "");
	m_inlinedFunctions[name] = body;
}

Pointer<CodeBlock> TVMCompilerContext::getInlinedFunction(const std::string& name) {
	return m_inlinedFunctions.at(name);
}

void TVMCompilerContext::addPublicFunction(uint32_t functionId, const std::string& functionName) {
	m_publicFunctions.emplace_back(functionId, functionName);
}

const std::vector<std::pair<uint32_t, std::string>>& TVMCompilerContext::getPublicFunctions() {
	std::sort(m_publicFunctions.begin(), m_publicFunctions.end());
	return m_publicFunctions;
}

bool TVMCompilerContext::addAndDoesHaveLoop(FunctionDefinition const* _v, FunctionDefinition const* _to) {
	graph[_v].insert(_to);
	graph[_to]; // creates default value if there is no such key
	for (const auto& k : graph | boost::adaptors::map_keys) {
		color[k] = Color::White;
	}
	bool hasLoop{};
	for (const auto& k : graph | boost::adaptors::map_keys) {
		if (dfs(k)) {
			hasLoop = true;
			graph[_v].erase(_to);
			break;
		}
	}
	return hasLoop;
}

bool TVMCompilerContext::isBaseFunction(CallableDeclaration const* d) const {
	return m_baseFunctions.count(d) != 0;
}

bool TVMCompilerContext::dfs(FunctionDefinition const* v) {
	if (color.at(v) == Color::Black) {
		return false;
	}
	if (color.at(v) == Color::Red) {
		return true;
	}
	// It's white
	color.at(v) = Color::Red;
	for (FunctionDefinition const* _to : graph.at(v)) {
		if (dfs(_to)) {
			return true;
		}
	}
	color.at(v) = Color::Black;
	return false;
}

void StackPusher::pushNull() {
	push(+1, "NULL");
}

void StackPusher::pushDefaultValue(Type const* type, bool isResultBuilder) {
	startOpaque();
	Type::Category cat = type->category();
	switch (cat) {
		case Type::Category::Address:
		case Type::Category::Contract:
			pushZeroAddress();
			if (isResultBuilder) {
				push(+1, "NEWC");
				push(-1, "STSLICE");
			}
			break;
		case Type::Category::Bool:
		case Type::Category::FixedBytes:
		case Type::Category::Integer:
		case Type::Category::Enum:
		case Type::Category::VarInteger:
			push(+1, "PUSHINT 0");
			if (isResultBuilder) {
				push(+1, "NEWC");
				push(-1, storeIntegralOrAddress(type, false));
			}
			break;
		case Type::Category::Array:
		case Type::Category::TvmCell:
			if (cat == Type::Category::TvmCell || to<ArrayType>(type)->isByteArray()) {
				if (isResultBuilder) {
					push(+1, "NEWC");
				} else {
					pushCellOrSlice(createNode<PushCellOrSlice>(PushCellOrSlice::Type::PUSHREF, "", nullptr));
				}
				break;
			}
			if (!isResultBuilder) {
				pushInt(0);
				push(+1, "NEWDICT");
				push(-2 + 1, "PAIR");
			} else {
				push(+1, "NEWC");
				pushInt(33);
				push(-1, "STZEROES");
			}
			break;
		case Type::Category::Mapping:
		case Type::Category::ExtraCurrencyCollection:
			if (isResultBuilder) {
				push(+1, "NEWC");
				stzeroes(1);
			} else {
				push(+1, "NEWDICT");
			}
			break;
		case Type::Category::Struct: {
			auto structType = to<StructType>(type);
			StructCompiler structCompiler{this, structType};
			structCompiler.createDefaultStruct(isResultBuilder);
			break;
		}
		case Type::Category::TvmSlice:
			if (isResultBuilder) {
				push(+1, "NEWC");
			} else {
				push(+1, "PUSHSLICE x8_");
			}
			break;
		case Type::Category::TvmBuilder:
			push(+1, "NEWC");
			break;
		case Type::Category::Function: {
			pushInt(TvmConst::FunctionId::DefaultValueForFunctionType);
			if (isResultBuilder) {
				solUnimplemented("TODO");
				push(+1, "NEWC");
				push(-1, "STU 32");
			}
			break;
		}
		case Type::Category::Optional:
			push(+1, "NULL");
			break;
		case Type::Category::FixedPoint:
			pushInt(0);
			break;
		case Type::Category::TvmVector:
			tuple(0);
			break;
		default:
			solUnimplemented("");
	}
	endOpaque(0, 1, true);
}



void StackPusher::getDict(
	Type const& keyType,
	Type const& valueType,
	const GetDictOperation op,
	const DataType& dataType
) {
	GetFromDict d(*this, keyType, valueType, op, dataType);
	d.getDict();
}

void StackPusher::byteLengthOfCell() {
	pushInt(0xFFFFFFFF);
	push(-2 + 3, "CDATASIZE");
	drop(1);
	dropUnder(1, 1);
	push(-1 + 1, "RSHIFT 3");
}

void StackPusher::was_c4_to_c7_called() {
	getGlob(TvmConst::C7::TvmPubkey);
	push(-1 + 1, "ISNULL");
}

void StackPusher::checkCtorCalled() {
	getGlob(TvmConst::C7::ConstructorFlag);
	_throw("THROWIFNOT " + toString(TvmConst::RuntimeException::CallThatWasBeforeCtorCall));
}

void StackPusher::checkIfCtorCalled(bool ifFlag) {
	startContinuation();
	checkCtorCalled();
	if (ifFlag) {
		ifJmpRef();
	} else {
		ifNotJmpRef();
	}
}

void StackPusher::add(StackPusher const& pusher) {
	solAssert(pusher.m_instructions.size() == 1, "");
	for (const Pointer<TvmAstNode>& op : pusher.m_instructions.back().opcodes) {
		m_instructions.back().opcodes.emplace_back(op);
	}
}

void StackPusher::clear() {
	m_instructions.clear();
	m_instructions.emplace_back();
}

void StackPusher::takeLast(int n) {
	m_stack2.takeLast(n);
}
