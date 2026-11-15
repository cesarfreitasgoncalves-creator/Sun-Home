#include "lexer.hpp"

namespace sun {
namespace {

bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_identifier_char(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

} // namespace

std::vector<Token> tokenize(const std::string& source, std::size_t line) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < source.size()) {
        const char c = source[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        const bool fstring = c == 'f' && i + 1 < source.size() && source[i + 1] == '"';
        if (c == '"' || fstring) {
            if (fstring) ++i;
            ++i;
            std::string value;
            bool closed = false;
            while (i < source.size()) {
                char current = source[i++];
                if (current == '"') {
                    closed = true;
                    break;
                }
                if (current == '\\') {
                    if (i == source.size()) {
                        throw ParseError(line, "unterminated escape sequence");
                    }
                    current = source[i++];
                    switch (current) {
                    case 'n': value.push_back('\n'); break;
                    case 't': value.push_back('\t'); break;
                    case 'r': value.push_back('\r'); break;
                    case '\\': value.push_back('\\'); break;
                    case '"': value.push_back('"'); break;
                    default: throw ParseError(line, "unsupported escape sequence");
                    }
                } else {
                    if (current == '\0') {
                        throw ParseError(line, "NUL byte is not allowed in string literal");
                    }
                    value.push_back(current);
                }
            }
            if (!closed) throw ParseError(line, "unterminated string literal");
            tokens.push_back({Token::Kind::string, value, fstring});
            continue;
        }
        if (is_identifier_start(c)) {
            const auto start = i++;
            while (i < source.size() && is_identifier_char(source[i])) ++i;
            tokens.push_back({Token::Kind::identifier, source.substr(start, i - start)});
            continue;
        }
        if (c >= '0' && c <= '9') {
            const auto start = i++;
            while (i < source.size() && source[i] >= '0' && source[i] <= '9') ++i;
            if (i < source.size() && source[i] == '.') {
                ++i;
                while (i < source.size() && source[i] >= '0' && source[i] <= '9') ++i;
            }
            if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                ++i;
                if (i < source.size() && (source[i] == '+' || source[i] == '-')) ++i;
                const auto exponent_start = i;
                while (i < source.size() && source[i] >= '0' && source[i] <= '9') ++i;
                if (i == exponent_start) throw ParseError(line, "malformed number exponent");
            }
            tokens.push_back({Token::Kind::number, source.substr(start, i - start)});
            continue;
        }
        if (i + 1 < source.size()) {
            const auto pair = source.substr(i, 2);
            if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=") {
                tokens.push_back({Token::Kind::symbol, pair});
                i += 2;
                continue;
            }
        }
        if (std::string("+-*/%<>()=,.[]").find(c) != std::string::npos) {
            tokens.push_back({Token::Kind::symbol, std::string(1, c)});
            ++i;
            continue;
        }
        throw ParseError(line, "unexpected character in expression");
    }
    tokens.push_back({Token::Kind::end, ""});
    return tokens;
}

} // namespace sun
