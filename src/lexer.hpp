#pragma once

#include "compiler.hpp"

#include <string>
#include <vector>

namespace sun {

struct Token {
    enum class Kind { identifier, number, string, symbol, end };
    Kind kind;
    std::string text;
    bool fstring = false;
    Keyword keyword = Keyword::none;
};

std::vector<Token> tokenize(const std::string& source, std::size_t line);
Keyword keyword_at_start(const std::string& source);

} // namespace sun
