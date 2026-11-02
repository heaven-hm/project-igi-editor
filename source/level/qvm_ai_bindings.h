#pragma once

#include "qvm_parser.h"

#include <cstddef>
#include <string>
#include <vector>

namespace igi {

// Finds the first integer literal passed to a named native QVM call.
//
// Retail AI scripts encode native calls as a symbol push followed by CALL;
// each CALL argument is a code address pointing at its own expression block.
// This helper intentionally accepts only a single literal expression so a
// malformed or dynamic script cannot silently produce a guessed route id.
int FindFirstCallIntegerArgument(
    const QVMFile& qvm,
    const std::string& function_name);

// Returns every statically resolvable integer literal at one argument index
// across calls to a named native function, preserving bytecode order.
std::vector<int> FindCallIntegerArguments(
    const QVMFile& qvm,
    const std::string& function_name,
    std::size_t argument_index);

} // namespace igi
