#pragma once

#include "compiler.hpp"

#include <optional>

namespace sun {

std::optional<Expression> fold_constant_expression(Type type,
                                                   const Expression& expression);

} // namespace sun
