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
 * Peephole optimizer
 */

#pragma once

#include "TvmAstVisitor.hpp"

namespace solidity::frontend {
	class PeepholeOptimizer : public TvmAstVisitor {
	public:
		explicit PeepholeOptimizer(bool _withUnpackOpaque) : m_withUnpackOpaque{_withUnpackOpaque} {}
		void endVisit(CodeBlock &_node) override;
	private:
		bool m_withUnpackOpaque;
	};
} // end solidity::frontend

