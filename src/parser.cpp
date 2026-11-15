#include "compiler.hpp"
#include "lexer.hpp"

#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sun {

namespace {

struct SourceLine {
    std::size_t number;
    std::string text;
    std::string raw_text;
};

struct Signature {
    std::vector<Type> parameters;
    std::vector<std::optional<std::string>> default_values;
    std::optional<Type> return_type;
};

struct Symbol {
    Type type;
    std::string cpp_name;
};

struct ExprValue {
    Type type;
    std::string cpp;
    std::optional<std::string> plain_string;
};

using Symbols = std::unordered_map<std::string, Symbol>;
using Signatures = std::unordered_map<std::string, Signature>;

void validate_call_arity(const std::string& name, const Signature& signature,
                         std::size_t supplied, std::size_t line) {
    std::size_t required = signature.parameters.size();
    for (std::size_t i = 0; i < signature.default_values.size(); ++i) {
        if (signature.default_values[i]) {
            required = i;
            break;
        }
    }
    if (supplied < required || supplied > signature.parameters.size()) {
        if (required == signature.parameters.size()) {
            throw ParseError(line, "function '" + name + "' expects " +
                                     std::to_string(signature.parameters.size()) +
                                     " argument(s), got " + std::to_string(supplied));
        }
        throw ParseError(line, "function '" + name + "' expects between " +
                                 std::to_string(required) + " and " +
                                 std::to_string(signature.parameters.size()) +
                                 " argument(s), got " + std::to_string(supplied));
    }
}

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

bool is_identifier_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_identifier_char(char c) {
    return is_identifier_start(c) || (c >= '0' && c <= '9');
}

bool is_identifier(const std::string& text) {
    if (text.empty() || !is_identifier_start(text.front())) return false;
    for (char c : text) {
        if (!is_identifier_char(c)) return false;
    }
    return true;
}

bool is_cpp_identifier(const std::string& text) {
    static const std::set<std::string> keywords = {
        "alignas", "alignof", "and", "asm", "auto", "bool", "break", "case",
        "catch", "char", "class", "const", "constexpr", "continue", "decltype",
        "default", "delete", "do", "double", "else", "enum", "explicit",
        "export", "extern", "false", "float", "for", "friend", "goto", "if",
        "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
        "nullptr", "operator", "or", "private", "protected", "public", "register",
        "return", "short", "signed", "sizeof", "static", "struct", "switch",
        "template", "this", "thread_local", "throw", "true", "try", "typedef",
        "typeid", "typename", "union", "unsigned", "using", "virtual", "void",
        "volatile", "while", "xor", "std"
    };
    return is_identifier(text) && keywords.find(text) == keywords.end();
}

bool is_ignored(const SourceLine& line) {
    return line.text.empty() || line.text[0] == '#';
}

bool starts_word(const std::string& text, const std::string& word) {
    return text == word ||
           (text.size() > word.size() && text.compare(0, word.size(), word) == 0 &&
            (text[word.size()] == ' ' || text[word.size()] == '\t' ||
             text[word.size()] == '(' || text[word.size()] == ':'));
}

Type parse_type(const std::string& name, std::size_t line) {
    if (name == "string") return Type::string;
    if (name == "number") return Type::number;
    if (name == "bool") return Type::boolean;
    if (name == "region") return Type::region;
    throw ParseError(line, "unknown type '" + name +
                               "' (use string, number, bool, or region)");
}

std::size_t find_assignment_operator(const std::string& text) {
    bool in_string = false;
    bool escaped = false;
    int depth = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
        } else if (c == '=' && depth == 0 &&
                   (i + 1 == text.size() || text[i + 1] != '=') &&
                   (i == 0 || (text[i - 1] != '=' && text[i - 1] != '!' &&
                               text[i - 1] != '<' && text[i - 1] != '>'))) {
            return i;
        }
    }
    return std::string::npos;
}

std::vector<std::string> split_arguments(const std::string& text, std::size_t line) {
    std::vector<std::string> result;
    if (trim(text).empty()) return result;

    std::size_t start = 0;
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')') {
            if (--depth < 0) throw ParseError(line, "unexpected ')'");
        } else if (c == ',' && depth == 0) {
            const auto part = trim(text.substr(start, i - start));
            if (part.empty()) throw ParseError(line, "empty argument");
            result.push_back(part);
            start = i + 1;
        }
    }
    if (in_string) throw ParseError(line, "unterminated string literal");
    if (depth != 0) throw ParseError(line, "unclosed '(' in argument list");
    const auto last = trim(text.substr(start));
    if (last.empty()) throw ParseError(line, "empty argument");
    result.push_back(last);
    return result;
}

std::pair<std::string, std::string> parse_call(const std::string& text, std::size_t line) {
    const auto open = text.find('(');
    if (open == std::string::npos) {
        throw ParseError(line, "expected a function call in the form name(...)");
    }
    const auto name = trim(text.substr(0, open));
    if (!is_identifier(name)) throw ParseError(line, "invalid function name in call");

    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    std::size_t close = std::string::npos;
    for (std::size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')' && --depth == 0) {
            close = i;
            break;
        }
    }
    if (in_string || close == std::string::npos) {
        throw ParseError(line, "malformed function call");
    }
    if (!trim(text.substr(close + 1)).empty()) {
        throw ParseError(line, "unexpected text after function call");
    }
    return {name, text.substr(open + 1, close - open - 1)};
}

std::size_t find_call_close(const std::string& text, std::size_t line) {
    const auto open = text.find('(');
    if (open == std::string::npos) throw ParseError(line, "expected '(' in function declaration");
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '(') {
            ++depth;
        } else if (c == ')' && --depth == 0) {
            return i;
        }
    }
    throw ParseError(line, "malformed function declaration");
}

std::string cpp_string(const std::string& value) {
    std::ostringstream result;
    result << '"';
    for (unsigned char c : value) {
        switch (c) {
        case '\\': result << "\\\\"; break;
        case '"': result << "\\\""; break;
        case '\n': result << "\\n"; break;
        case '\t': result << "\\t"; break;
        case '\r': result << "\\r"; break;
        default:
            if (c < 32 || c >= 127) {
                const char* digits = "0123456789abcdef";
                result << "\\x" << digits[c >> 4] << digits[c & 0x0f] << "\"\"";
            } else {
                result << static_cast<char>(c);
            }
        }
    }
    result << '"';
    return result.str();
}

class ExpressionParser {
public:
    ExpressionParser(const std::string& source, std::size_t line, const Symbols& symbols,
                     bool math_imported, bool memory_imported,
                     const Signatures& signatures,
                     bool allow_function_calls = true)
        : tokens_(tokenize(source, line)), line_(line), symbols_(symbols),
          math_imported_(math_imported), memory_imported_(memory_imported),
          signatures_(signatures),
          allow_function_calls_(allow_function_calls) {}

    ExprValue parse() {
        auto value = parse_binary(1);
        if (peek().kind != Token::Kind::end) {
            throw ParseError(line_, "unexpected token '" + peek().text + "' in expression");
        }
        return value;
    }

private:
    const Token& peek() const { return tokens_[position_]; }

    Token take() { return tokens_[position_++]; }

    bool accept(const std::string& symbol) {
        if (peek().text != symbol) return false;
        ++position_;
        return true;
    }

    static bool is_logical_operator(const Token& token) {
        return token.kind == Token::Kind::identifier &&
               (token.text == "and" || token.text == "or");
    }

    static int precedence(const std::string& op) {
        if (op == "or") return 1;
        if (op == "and") return 2;
        if (op == "==" || op == "!=" || op == "<" || op == "<=" ||
            op == ">" || op == ">=") return 3;
        if (op == "+" || op == "-") return 4;
        if (op == "*" || op == "/" || op == "%") return 5;
        return 0;
    }

    ExprValue parse_binary(int minimum_precedence) {
        auto left = parse_unary();
        while (peek().kind == Token::Kind::symbol || is_logical_operator(peek())) {
            const auto op = peek().text;
            const int op_precedence = precedence(op);
            if (op_precedence < minimum_precedence) break;
            take();
            auto right = parse_binary(op_precedence + 1);
            left = make_binary(op, left, right);
        }
        return left;
    }

    ExprValue parse_unary() {
        if (accept("-")) {
            auto operand = parse_unary();
            if (operand.type != Type::number) {
                throw ParseError(line_, "unary '-' requires a number");
            }
            return {Type::number, "(-(" + operand.cpp + "))", std::nullopt};
        }
        if (peek().kind == Token::Kind::identifier && peek().text == "not") {
            take();
            auto operand = parse_binary(3);
            if (operand.type != Type::boolean) {
                throw ParseError(line_, "'not' requires a bool value");
            }
            return {Type::boolean, "(!(" + operand.cpp + "))", std::nullopt};
        }
        return parse_primary();
    }

    ExprValue parse_primary() {
        if (accept("(")) {
            auto value = parse_binary(1);
            if (!accept(")")) throw ParseError(line_, "expected ')' after expression");
            value.plain_string.reset();
            return value;
        }
        if (peek().kind == Token::Kind::number) {
            const auto token = take();
            auto number = token.text;
            if (number.find_first_of(".eE") == std::string::npos) number += ".0";
            return {Type::number, number, std::nullopt};
        }
        if (peek().kind == Token::Kind::string) {
            const auto token = take();
            if (token.fstring) return parse_fstring(token.text);
            return {Type::string, "std::string(" + cpp_string(token.text) + ")",
                    token.text};
        }
        if (peek().kind == Token::Kind::identifier) {
            auto name = take().text;
            if (name == "true" || name == "false") {
                return {Type::boolean, name, std::nullopt};
            }
            if (name == "math" && accept(".")) {
                if (peek().kind != Token::Kind::identifier) {
                    throw ParseError(line_, "expected a math function after 'math.'");
                }
                name = take().text;
                if (!accept("(")) {
                    throw ParseError(line_, "expected '(' after math function name");
                }
                return parse_math_call(name);
            }
            if (name == "memory" && accept(".")) {
                if (peek().kind != Token::Kind::identifier) {
                    throw ParseError(line_, "expected a memory function after 'memory.'");
                }
                name = take().text;
                if (!accept("(")) {
                    throw ParseError(line_, "expected '(' after memory function name");
                }
                return parse_memory_call(name);
            }
            if (name == "istype" && accept("(")) return parse_istype_call();
            if (accept("(")) {
                const auto signature = signatures_.find(name);
                if (signature == signatures_.end()) return parse_math_call(name);
                if (!allow_function_calls_) {
                    throw ParseError(line_, "default values cannot call functions");
                }
                if (!signature->second.return_type) {
                    throw ParseError(line_, "function '" + name + "' does not return a value");
                }
                std::vector<ExprValue> arguments;
                if (!accept(")")) {
                    do {
                        arguments.push_back(parse_binary(1));
                    } while (accept(","));
                    if (!accept(")")) {
                        throw ParseError(line_, "expected ')' after function arguments");
                    }
                }
                validate_call_arity(name, signature->second, arguments.size(), line_);
                std::ostringstream call;
                call << "sun_fn_" << name << '(';
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    if (arguments[i].type != signature->second.parameters[i]) {
                        throw ParseError(line_, "argument " + std::to_string(i + 1) +
                                                 " of '" + name + "' must have type " +
                                                 cpp_type(signature->second.parameters[i]));
                    }
                    if (i) call << ", ";
                    call << arguments[i].cpp;
                }
                for (std::size_t i = arguments.size();
                     i < signature->second.parameters.size(); ++i) {
                    if (!signature->second.default_values[i]) {
                        throw ParseError(line_, "missing default value for parameter " +
                                                 std::to_string(i + 1) + " of '" + name + "'");
                    }
                    if (i) call << ", ";
                    call << *signature->second.default_values[i];
                }
                call << ')';
                return {*signature->second.return_type, call.str(), std::nullopt};
            }
            const auto found = symbols_.find(name);
            if (found == symbols_.end()) {
                throw ParseError(line_, "unknown value '" + name + "'");
            }
            return {found->second.type, found->second.cpp_name, std::nullopt};
        }
        throw ParseError(line_, "expected a number, string, bool, or variable");
    }

    ExprValue parse_istype_call() {
        const auto value = parse_binary(1);
        if (!accept(",")) {
            throw ParseError(line_, "istype expects a value and a type, e.g. istype(x, number)");
        }
        if (peek().kind != Token::Kind::identifier) {
            throw ParseError(line_, "istype type must be string, number, bool, or region");
        }
        const auto target_type = parse_type(take().text, line_);
        if (target_type == Type::region && !memory_imported_) {
            throw ParseError(line_, "the region type requires 'import memory'");
        }
        if (!accept(")")) throw ParseError(line_, "expected ')' after istype arguments");
        const bool matches = value.type == target_type;
        return {Type::boolean, matches ? "true" : "false", std::nullopt};
    }

    ExprValue parse_memory_call(const std::string& name) {
        if (!memory_imported_) throw ParseError(line_, "memory functions require 'import memory'");
        if (!allow_function_calls_) {
            throw ParseError(line_, "default values cannot call functions");
        }
        if (name == "write_u8") {
            throw ParseError(line_, "memory.write_u8 is a statement, not an expression");
        }
        std::vector<ExprValue> arguments;
        if (!accept(")")) {
            do {
                arguments.push_back(parse_binary(1));
            } while (accept(","));
            if (!accept(")")) throw ParseError(line_, "expected ')' after memory arguments");
        }
        if (name == "alloc") {
            if (arguments.size() != 1) {
                throw ParseError(line_, "memory.alloc expects 1 argument");
            }
            if (arguments[0].type != Type::number) {
                throw ParseError(line_, "memory.alloc size must have type number");
            }
            return {Type::region, "SunMemoryRegion::allocate(" + arguments[0].cpp + ")",
                    std::nullopt};
        }
        if (name == "size") {
            if (arguments.size() != 1 || arguments[0].type != Type::region) {
                throw ParseError(line_, "memory.size expects one region argument");
            }
            return {Type::number, "(" + arguments[0].cpp + ").size()", std::nullopt};
        }
        if (name == "read_u8") {
            if (arguments.size() != 2 || arguments[0].type != Type::region ||
                arguments[1].type != Type::number) {
                throw ParseError(line_, "memory.read_u8 expects (region, number offset)");
            }
            return {Type::number, "(" + arguments[0].cpp + ").read_u8(" +
                                     arguments[1].cpp + ")",
                    std::nullopt};
        }
        throw ParseError(line_, "unknown memory function '" + name + "'");
    }

    ExprValue parse_fstring(const std::string& text) {
        std::vector<std::string> parts;
        std::string literal;
        auto flush_literal = [&]() {
            if (literal.empty()) return;
            parts.push_back("std::string(" + cpp_string(literal) + ")");
            literal.clear();
        };
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '{') {
                if (i + 1 < text.size() && text[i + 1] == '{') {
                    literal.push_back('{');
                    i += 2;
                    continue;
                }
                flush_literal();
                const auto close = text.find('}', i + 1);
                if (close == std::string::npos) {
                    throw ParseError(line_, "missing '}' in f-string interpolation");
                }
                const auto name = text.substr(i + 1, close - i - 1);
                if (!is_identifier(name)) {
                    throw ParseError(line_, "f-string interpolation must be a variable name");
                }
                const auto found = symbols_.find(name);
                if (found == symbols_.end()) {
                    throw ParseError(line_, "unknown value '" + name + "'");
                }
                if (found->second.type == Type::region) {
                    throw ParseError(line_, "memory regions cannot be interpolated as text");
                }
                parts.push_back("sun_to_string(" + found->second.cpp_name + ")");
                i = close + 1;
                continue;
            }
            if (text[i] == '}') {
                if (i + 1 < text.size() && text[i + 1] == '}') {
                    literal.push_back('}');
                    i += 2;
                    continue;
                }
                throw ParseError(line_, "unmatched '}' in f-string");
            }
            literal.push_back(text[i++]);
        }
        flush_literal();
        if (parts.empty()) parts.push_back("std::string(\"\")");
        std::ostringstream code;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i) code << " + ";
            code << parts[i];
        }
        return {Type::string, code.str(), std::nullopt};
    }

    ExprValue parse_math_call(const std::string& name) {
        if (!allow_function_calls_) {
            throw ParseError(line_, "default values cannot call functions");
        }
        static const std::unordered_map<std::string, std::pair<std::string, std::size_t>> functions = {
            {"abs", {"std::abs", 1}}, {"ceil", {"std::ceil", 1}},
            {"cos", {"std::cos", 1}}, {"exp", {"std::exp", 1}},
            {"floor", {"std::floor", 1}}, {"log", {"std::log", 1}},
            {"pow", {"std::pow", 2}}, {"sin", {"std::sin", 1}},
            {"sqrt", {"std::sqrt", 1}}, {"tan", {"std::tan", 1}},
        };
        if (!math_imported_) throw ParseError(line_, "math functions require 'import math'");
        const auto function = functions.find(name);
        if (function == functions.end()) {
            throw ParseError(line_, "unknown math function '" + name + "'");
        }
        std::vector<ExprValue> arguments;
        if (!accept(")")) {
            do {
                arguments.push_back(parse_binary(1));
            } while (accept(","));
            if (!accept(")")) throw ParseError(line_, "expected ')' after math arguments");
        }
        if (arguments.size() != function->second.second) {
            throw ParseError(line_, "math function '" + name + "' expects " +
                                     std::to_string(function->second.second) +
                                     " argument(s), got " +
                                     std::to_string(arguments.size()));
        }
        std::ostringstream code;
        code << function->second.first << '(';
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (arguments[i].type != Type::number) {
                throw ParseError(line_, "math function '" + name + "' requires number arguments");
            }
            if (i) code << ", ";
            code << arguments[i].cpp;
        }
        code << ')';
        return {Type::number, code.str(), std::nullopt};
    }

    ExprValue make_binary(const std::string& op, const ExprValue& left,
                          const ExprValue& right) {
        if (op == "and" || op == "or") {
            if (left.type != Type::boolean || right.type != Type::boolean) {
                throw ParseError(line_, "logical operators require bool values");
            }
            const auto cpp_op = op == "and" ? "&&" : "||";
            return {Type::boolean, "((" + left.cpp + ") " + cpp_op + " (" + right.cpp + "))",
                    std::nullopt};
        }
        if (op == "+") {
            if (left.type == Type::number && right.type == Type::number) {
                return {Type::number, "((" + left.cpp + ") + (" + right.cpp + "))",
                        std::nullopt};
            }
            if (left.type == Type::string && right.type == Type::string) {
                return {Type::string, "((" + left.cpp + ") + (" + right.cpp + "))",
                        std::nullopt};
            }
            throw ParseError(line_, "'+' requires two numbers or two strings");
        }
        if (op == "==" || op == "!=") {
            if (left.type != right.type) {
                throw ParseError(line_, "equality comparison requires values of the same type");
            }
            if (left.type == Type::region) {
                throw ParseError(line_, "memory regions cannot be compared directly");
            }
            return {Type::boolean, "((" + left.cpp + ") " + op + " (" + right.cpp + "))",
                    std::nullopt};
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (left.type != Type::number || right.type != Type::number) {
                throw ParseError(line_, "ordered comparisons require number values");
            }
            return {Type::boolean, "((" + left.cpp + ") " + op + " (" + right.cpp + "))",
                    std::nullopt};
        }
        if (left.type != Type::number || right.type != Type::number) {
            throw ParseError(line_, "arithmetic operators require number values");
        }
        if (op == "%") {
            return {Type::number, "std::fmod((" + left.cpp + "), (" + right.cpp + "))",
                    std::nullopt};
        }
        return {Type::number, "((" + left.cpp + ") " + op + " (" + right.cpp + "))",
                std::nullopt};
    }

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    std::size_t line_;
    const Symbols& symbols_;
    bool math_imported_;
    bool memory_imported_;
    const Signatures& signatures_;
    bool allow_function_calls_;
};

std::string indent_statements(const std::vector<std::string>& statements, std::size_t spaces) {
    std::ostringstream result;
    const std::string prefix(spaces, ' ');
    for (const auto& statement : statements) {
        std::istringstream lines(statement);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty()) result << prefix;
            result << line << '\n';
        }
    }
    return result.str();
}

class SunParser {
public:
    explicit SunParser(std::istream& source) {
        std::string text;
        std::size_t number = 0;
        while (std::getline(source, text)) {
            ++number;
            const auto normalized = trim(text);
            lines_.push_back({number, normalized, text});
        }
    }

    Program parse() {
        Program result;
        std::size_t index = 0;
        bool seen_math = false;
        bool seen_memory = false;
        skip_ignored(index);
        while (index < lines_.size() && starts_word(lines_[index].text, "import")) {
            const auto module = trim(lines_[index].text.substr(6));
            if (module == "math") {
                if (seen_math) throw ParseError(lines_[index].number, "duplicate import 'math'");
                seen_math = true;
                result.math_imported = true;
            } else if (module == "memory") {
                if (seen_memory) throw ParseError(lines_[index].number, "duplicate import 'memory'");
                seen_memory = true;
                result.memory_imported = true;
            } else {
                throw ParseError(lines_[index].number,
                                 "supported imports are 'math' and 'memory'");
            }
            ++index;
            skip_ignored(index);
        }
        memory_imported_ = result.memory_imported;
        for (std::size_t i = index; i < lines_.size(); ++i) {
            if (is_ignored(lines_[i])) continue;
            if (lines_[i].text == "inline cpp:") {
                while (++i < lines_.size() && lines_[i].text != "endcpp") {}
                if (i == lines_.size()) {
                    throw ParseError(lines_.back().number + 1,
                                     "inline C++ block is missing 'endcpp'");
                }
                continue;
            }
            if (lines_[i].text == "endcpp") {
                throw ParseError(lines_[i].number, "'endcpp' without an open inline C++ block");
            }
            if (starts_word(lines_[i].text, "import")) {
                throw ParseError(lines_[i].number, "imports must appear before declarations and statements");
            }
            if (starts_word(lines_[i].text, "func")) {
                auto function = parse_function_header(lines_[i]);
                if (function.name == "print") {
                    throw ParseError(function.line, "function name 'print' is reserved");
                }
                if (function.name == "istype") {
                    throw ParseError(function.line, "function name 'istype' is reserved");
                }
                Signature signature;
                for (const auto& parameter : function.parameters) {
                    signature.parameters.push_back(parameter.type);
                    signature.default_values.push_back(std::nullopt);
                }
                signature.return_type = function.return_type;
                if (!signatures_.emplace(function.name, signature).second) {
                    throw ParseError(function.line, "function '" + function.name + "' is already defined");
                }
            }
        }
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            const auto& line = lines_[i];
            if (line.text == "inline cpp:") {
                while (++i < lines_.size() && lines_[i].text != "endcpp") {}
                continue;
            }
            if (!starts_word(line.text, "func")) continue;
            const auto function = parse_function_header(line);
            auto& signature = signatures_.at(function.name);
            Symbols no_symbols;
            for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                const auto& parameter = function.parameters[i];
                if (!parameter.default_value) continue;
                const auto value = ExpressionParser(*parameter.default_value, line.number,
                                                    no_symbols, result.math_imported,
                                                    result.memory_imported,
                                                    signatures_, false).parse();
                if (value.type != parameter.type) {
                    throw ParseError(line.number, "default for parameter '" + parameter.name +
                                                   "' must have type " +
                                                   cpp_type(parameter.type));
                }
                signature.default_values[i] = value.cpp;
            }
        }
        result.main_body = parse_main(index, result.math_imported, result.global_cpp);
        result.functions = std::move(functions_);
        return result;
    }

private:
    std::string parse_inline_cpp(std::size_t& index, const Symbols& symbols,
                                 bool global_scope = false) {
        const auto start = lines_[index];
        ++index;
        std::ostringstream raw;
        bool first = true;
        while (index < lines_.size() && lines_[index].text != "endcpp") {
            if (lines_[index].raw_text.find('\0') != std::string::npos) {
                throw ParseError(lines_[index].number, "NUL byte is not allowed in inline C++");
            }
            if (!first) raw << '\n';
            raw << lines_[index].raw_text;
            first = false;
            ++index;
        }
        if (index == lines_.size()) {
            throw ParseError(start.number, "inline C++ block is missing 'endcpp'");
        }
        ++index;
        if (global_scope) return raw.str();

        for (const auto& entry : symbols) {
            if (!is_cpp_identifier(entry.first)) {
                throw ParseError(start.number,
                                 "inline C++ cannot expose C++ reserved identifier '" +
                                     entry.first + "'");
            }
            for (const auto& other : symbols) {
                if (entry.first != other.first && entry.first == other.second.cpp_name) {
                    throw ParseError(start.number,
                                     "inline C++ alias '" + entry.first +
                                         "' conflicts with an internal Sun name");
                }
            }
        }
        std::ostringstream code;
        code << "{\n";
        for (const auto& entry : symbols) {
            code << "    auto& " << entry.first << " = " << entry.second.cpp_name << ";\n";
        }
        code << raw.str() << "\n}";
        return code.str();
    }

    Function parse_function_header(const SourceLine& line) {
        const auto header = trim(line.text.substr(4));
        if (header.empty()) throw ParseError(line.number, "expected 'func name(parameters)'");
        const auto close = find_call_close(header, line.number);
        const auto call = parse_call(header.substr(0, close + 1), line.number);
        const auto return_annotation = trim(header.substr(close + 1));
        std::optional<Type> return_type;
        if (!return_annotation.empty()) {
            if (return_annotation[0] != ':') {
                throw ParseError(line.number, "return type must follow ':' (e.g. ': number')");
            }
            return_type = parse_type(trim(return_annotation.substr(1)), line.number);
            if (*return_type == Type::region && !memory_imported_) {
                throw ParseError(line.number, "the region type requires 'import memory'");
            }
        }
        Function function;
        function.name = call.first;
        function.return_type = return_type;
        function.line = line.number;
        std::unordered_map<std::string, bool> seen;
        bool saw_default = false;
        for (const auto& parameter_text : split_arguments(call.second, line.number)) {
            const auto equals = find_assignment_operator(parameter_text);
            const auto declaration = trim(parameter_text.substr(0, equals));
            const auto colon = declaration.find(':');
            const auto name = trim(declaration.substr(0, colon));
            if (!is_identifier(name)) {
                throw ParseError(line.number, "invalid parameter name '" + name + "'");
            }
            if (!seen.emplace(name, true).second) {
                throw ParseError(line.number, "duplicate parameter '" + name + "'");
            }
            const auto type = colon == std::string::npos
                                  ? Type::string
                                  : parse_type(trim(declaration.substr(colon + 1)), line.number);
            if (type == Type::region && !memory_imported_) {
                throw ParseError(line.number, "the region type requires 'import memory'");
            }
            std::optional<std::string> default_value;
            if (equals != std::string::npos) {
                const auto source = trim(parameter_text.substr(equals + 1));
                if (source.empty()) {
                    throw ParseError(line.number, "default value for parameter '" + name +
                                                   "' cannot be empty");
                }
                default_value = source;
                saw_default = true;
            } else if (saw_default) {
                throw ParseError(line.number,
                                 "parameters without defaults cannot follow parameters with defaults");
            }
            function.parameters.push_back({name, type, default_value});
        }
        return function;
    }

    bool is_terminator(const std::string& text) const {
        return text == "end" || starts_word(text, "elif") || starts_word(text, "else");
    }

    void skip_ignored(std::size_t& index) const {
        while (index < lines_.size() && is_ignored(lines_[index])) ++index;
    }

    std::vector<std::string> parse_main(std::size_t& index, bool math_imported,
                                        std::vector<std::string>& global_cpp) {
        Symbols symbols;
        std::vector<std::string> body;
        while (index < lines_.size()) {
            skip_ignored(index);
            if (index == lines_.size()) break;
            if (lines_[index].text == "end") {
                throw ParseError(lines_[index].number, "'end' without an open block");
            }
            if (starts_word(lines_[index].text, "elif") ||
                starts_word(lines_[index].text, "else")) {
                throw ParseError(lines_[index].number, "unexpected branch without an 'if'");
            }
            if (starts_word(lines_[index].text, "func")) {
                auto function = parse_function_header(lines_[index]);
                ++index;
                Symbols function_symbols;
                for (const auto& parameter : function.parameters) {
                    function_symbols.emplace(
                        parameter.name,
                        Symbol{parameter.type, "sun_arg_" + parameter.name});
                }
                const auto previous_return_type = active_return_type_;
                const auto previous_function_state = inside_function_;
                active_return_type_ = function.return_type;
                inside_function_ = true;
                bool definitely_returns = false;
                function.body = parse_block(index, function_symbols, math_imported,
                                            &definitely_returns);
                active_return_type_ = previous_return_type;
                inside_function_ = previous_function_state;
                if (index == lines_.size() || lines_[index].text != "end") {
                    const auto error_line = lines_.empty() ? 1 : lines_.back().number + 1;
                    throw ParseError(error_line, "function '" + function.name + "' is missing 'end'");
                }
                if (function.return_type && !definitely_returns) {
                    throw ParseError(function.line,
                                     "function '" + function.name +
                                         "' must return a value on every path");
                }
                ++index;
                functions_.push_back(std::move(function));
                continue;
            }
            if (lines_[index].text == "inline cpp:") {
                global_cpp.push_back(parse_inline_cpp(index, symbols, true));
                continue;
            }
            body.push_back(parse_statement(index, symbols, math_imported, nullptr));
        }
        return body;
    }

    std::vector<std::string> parse_block(std::size_t& index, Symbols& symbols,
                                         bool math_imported,
                                         bool* definitely_returns = nullptr) {
        std::vector<std::string> body;
        bool returns = false;
        while (index < lines_.size()) {
            skip_ignored(index);
            if (index == lines_.size() || is_terminator(lines_[index].text)) break;
            if (starts_word(lines_[index].text, "func")) {
                throw ParseError(lines_[index].number, "nested function declarations are not supported");
            }
            if (starts_word(lines_[index].text, "import")) {
                throw ParseError(lines_[index].number, "imports must appear before declarations and statements");
            }
            bool statement_returns = false;
            body.push_back(parse_statement(index, symbols, math_imported,
                                           &statement_returns));
            returns = returns || statement_returns;
        }
        if (definitely_returns) *definitely_returns = returns;
        return body;
    }

    ExprValue expression(const std::string& text, std::size_t line, const Symbols& symbols,
                         bool math_imported) const {
        return ExpressionParser(text, line, symbols, math_imported, memory_imported_,
                                signatures_).parse();
    }

    ExprValue parse_condition(const SourceLine& line, const std::string& keyword,
                              const Symbols& symbols, bool math_imported) const {
        const auto text = trim(line.text.substr(keyword.size()));
        if (text.empty() || text.back() != ':') {
            throw ParseError(line.number, keyword + " condition must end with ':'");
        }
        auto condition = expression(trim(text.substr(0, text.size() - 1)),
                                   line.number, symbols, math_imported);
        if (condition.type != Type::boolean) {
            throw ParseError(line.number, keyword + " condition must have type bool");
        }
        return condition;
    }

    std::string parse_if(std::size_t& index, const Symbols& symbols, bool math_imported,
                         bool* definitely_returns) {
        const auto first_line = lines_[index];
        auto condition = parse_condition(first_line, "if", symbols, math_imported);
        ++index;
        Symbols branch_symbols = symbols;
        bool branch_returns = false;
        auto branch = parse_block(index, branch_symbols, math_imported, &branch_returns);
        bool all_branches_return = branch_returns;
        bool has_else = false;

        std::ostringstream code;
        code << "if (" << condition.cpp << ") {\n"
             << indent_statements(branch, 4) << '}';
        while (index < lines_.size() && starts_word(lines_[index].text, "elif")) {
            const auto branch_line = lines_[index];
            auto elif_condition = parse_condition(branch_line, "elif", symbols, math_imported);
            ++index;
            branch_symbols = symbols;
            branch_returns = false;
            branch = parse_block(index, branch_symbols, math_imported, &branch_returns);
            all_branches_return = all_branches_return && branch_returns;
            code << " else if (" << elif_condition.cpp << ") {\n"
                 << indent_statements(branch, 4) << '}';
        }
        if (index < lines_.size() && starts_word(lines_[index].text, "else")) {
            if (lines_[index].text != "else:") {
                throw ParseError(lines_[index].number, "expected 'else:'");
            }
            ++index;
            branch_symbols = symbols;
            branch_returns = false;
            branch = parse_block(index, branch_symbols, math_imported, &branch_returns);
            has_else = true;
            all_branches_return = all_branches_return && branch_returns;
            code << " else {\n" << indent_statements(branch, 4) << '}';
            if (index < lines_.size() && starts_word(lines_[index].text, "elif")) {
                throw ParseError(lines_[index].number, "'elif' cannot follow 'else'");
            }
            if (index < lines_.size() && starts_word(lines_[index].text, "else")) {
                throw ParseError(lines_[index].number, "only one 'else' branch is allowed");
            }
        }
        if (index == lines_.size() || lines_[index].text != "end") {
            const auto error_line = lines_.empty() ? 1 : lines_.back().number + 1;
            throw ParseError(error_line, "if block is missing 'end'");
        }
        ++index;
        if (definitely_returns) {
            *definitely_returns = has_else && all_branches_return;
        }
        return code.str();
    }

    std::string parse_while(std::size_t& index, Symbols& symbols, bool math_imported) {
        const auto line = lines_[index];
        auto condition = parse_condition(line, "while", symbols, math_imported);
        ++index;
        Symbols loop_symbols = symbols;
        ++loop_depth_;
        auto body = parse_block(index, loop_symbols, math_imported);
        --loop_depth_;
        if (index == lines_.size() || lines_[index].text != "end") {
            const auto error_line = lines_.empty() ? 1 : lines_.back().number + 1;
            throw ParseError(error_line, "while block is missing 'end'");
        }
        ++index;
        return "while (" + condition.cpp + ") {\n" + indent_statements(body, 4) + "}";
    }

    std::string parse_for(std::size_t& index, Symbols& symbols, bool math_imported) {
        const auto line = lines_[index];
        const auto text = trim(line.text.substr(3));
        if (text.empty() || text.back() != ':') {
            throw ParseError(line.number, "for loop must end with ':'");
        }
        const auto loop_spec = trim(text.substr(0, text.size() - 1));
        const auto in = loop_spec.find(" in ");
        if (in == std::string::npos) {
            throw ParseError(line.number, "expected 'for name in range(start, stop[, step]):'");
        }
        const auto name = trim(loop_spec.substr(0, in));
        const auto range = trim(loop_spec.substr(in + 4));
        if (!is_identifier(name)) {
            throw ParseError(line.number, "invalid loop variable '" + name + "'");
        }
        if (range.rfind("range(", 0) != 0 || range.back() != ')') {
            throw ParseError(line.number, "for loops currently require range(start, stop[, step])");
        }
        const auto arguments = split_arguments(range.substr(6, range.size() - 7), line.number);
        if (arguments.size() < 2 || arguments.size() > 3) {
            throw ParseError(line.number, "range expects 2 or 3 arguments");
        }
        std::vector<ExprValue> values;
        for (const auto& argument : arguments) {
            auto value = expression(argument, line.number, symbols, math_imported);
            if (value.type != Type::number) {
                throw ParseError(line.number, "range arguments must have type number");
            }
            values.push_back(std::move(value));
        }

        const auto id = next_loop_id_++;
        const auto start_name = "sun_for_start_" + std::to_string(id);
        const auto stop_name = "sun_for_stop_" + std::to_string(id);
        const auto step_name = "sun_for_step_" + std::to_string(id);
        const auto variable_name = "sun_loop_" + std::to_string(id) + "_" + name;
        const auto start_value = values[0].cpp;
        const auto stop_value = values[1].cpp;
        const auto step_value = values.size() == 3 ? values[2].cpp : "1.0";

        ++index;
        Symbols loop_symbols = symbols;
        loop_symbols[name] = {Type::number, variable_name};
        ++loop_depth_;
        auto body = parse_block(index, loop_symbols, math_imported);
        --loop_depth_;
        if (index == lines_.size() || lines_[index].text != "end") {
            const auto error_line = lines_.empty() ? 1 : lines_.back().number + 1;
            throw ParseError(error_line, "for block is missing 'end'");
        }
        ++index;

        std::ostringstream code;
        code << "{\n"
             << "    const double " << start_name << " = " << start_value << ";\n"
             << "    const double " << stop_name << " = " << stop_value << ";\n"
             << "    const double " << step_name << " = " << step_value << ";\n"
             << "    if (" << step_name
             << " == 0.0) throw std::runtime_error(\"range step cannot be zero\");\n"
             << "    for (double " << variable_name << " = " << start_name << "; "
             << "(" << step_name << " > 0.0 ? " << variable_name << " < " << stop_name
             << " : " << variable_name << " > " << stop_name << "); "
             << variable_name << " = sun_range_next(" << variable_name << ", "
             << step_name << ")) {\n"
             << indent_statements(body, 8)
             << "    }\n}";
        return code.str();
    }

    std::string parse_let(const SourceLine& line, Symbols& symbols, bool math_imported) {
        const auto declaration = trim(line.text.substr(3));
        const auto equals = declaration.find('=');
        if (equals == std::string::npos) {
            throw ParseError(line.number, "expected 'let name: type = expression'");
        }
        const auto typed_name = trim(declaration.substr(0, equals));
        const auto colon = typed_name.find(':');
        if (colon == std::string::npos) {
            throw ParseError(line.number, "local variables require an explicit type, e.g. 'let x: number = 1'");
        }
        const auto name = trim(typed_name.substr(0, colon));
        if (!is_identifier(name)) throw ParseError(line.number, "invalid variable name '" + name + "'");
        if (symbols.find(name) != symbols.end()) {
            throw ParseError(line.number, "value '" + name + "' is already declared in this scope");
        }
        const auto type = parse_type(trim(typed_name.substr(colon + 1)), line.number);
        if (type == Type::region && !memory_imported_) {
            throw ParseError(line.number, "the region type requires 'import memory'");
        }
        const auto value = expression(trim(declaration.substr(equals + 1)), line.number,
                                      symbols, math_imported);
        if (value.type != type) {
            throw ParseError(line.number, "cannot initialize " + cpp_type(type) +
                                           " with a " + cpp_type(value.type) + " value");
        }
        symbols.emplace(name, Symbol{type, "sun_var_" + name});
        return cpp_type(type) + " sun_var_" + name + " = " + value.cpp + ";";
    }

    std::string parse_assignment(const SourceLine& line, Symbols& symbols,
                                 bool math_imported) const {
        const auto equals = line.text.find('=');
        const auto name = trim(line.text.substr(0, equals));
        const auto found = symbols.find(name);
        if (found == symbols.end()) {
            throw ParseError(line.number, "cannot assign to undeclared value '" + name + "'");
        }
        const auto value = expression(trim(line.text.substr(equals + 1)), line.number,
                                      symbols, math_imported);
        if (value.type != found->second.type) {
            throw ParseError(line.number, "cannot assign " + cpp_type(value.type) +
                                           " to " + cpp_type(found->second.type) + " value '" +
                                           name + "'");
        }
        return found->second.cpp_name + " = " + value.cpp + ";";
    }

    std::string parse_return(const SourceLine& line, Symbols& symbols, bool math_imported,
                             bool* returns) const {
        if (!inside_function_) throw ParseError(line.number, "'return' is only valid inside a function");
        const auto value_text = trim(line.text.substr(6));
        if (active_return_type_) {
            if (value_text.empty()) {
                throw ParseError(line.number, "function must return a " +
                                               cpp_type(*active_return_type_) + " value");
            }
            const auto value = expression(value_text, line.number, symbols, math_imported);
            if (value.type != *active_return_type_) {
                throw ParseError(line.number, "return type mismatch: expected " +
                                               cpp_type(*active_return_type_) + ", got " +
                                               cpp_type(value.type));
            }
            if (returns) *returns = true;
            return "return " + value.cpp + ";";
        }
        if (!value_text.empty()) {
            throw ParseError(line.number, "void function cannot return a value");
        }
        if (returns) *returns = true;
        return "return;";
    }

    std::string parse_print(const SourceLine& line, const std::string& arguments_text,
                            const Symbols& symbols, bool math_imported) const {
        const auto arguments = split_arguments(arguments_text, line.number);
        if (arguments.empty()) throw ParseError(line.number, "expected print(expression)");
        const auto first = expression(arguments.front(), line.number, symbols, math_imported);
        if (first.type == Type::region) {
            throw ParseError(line.number, "memory regions cannot be printed directly");
        }
        ExprValue value = first;
        if (arguments.size() > 1 ||
            (first.plain_string && first.plain_string->find("%v") != std::string::npos)) {
            if (!first.plain_string) {
                throw ParseError(line.number, "formatted print requires a plain string literal first");
            }
            std::vector<ExprValue> values;
            for (std::size_t i = 1; i < arguments.size(); ++i) {
                const auto equals = arguments[i].find('=');
                if (equals == std::string::npos || trim(arguments[i].substr(0, equals)) != "%v") {
                    throw ParseError(line.number, "formatted print arguments must use the form %v=value");
                }
                values.push_back(expression(trim(arguments[i].substr(equals + 1)),
                                            line.number, symbols, math_imported));
                if (values.back().type == Type::region) {
                    throw ParseError(line.number, "memory regions cannot be formatted as text");
                }
            }
            std::vector<std::string> parts;
            std::string literal;
            std::size_t value_index = 0;
            const auto& format = *first.plain_string;
            for (std::size_t i = 0; i < format.size();) {
                if (format[i] != '%') {
                    literal.push_back(format[i++]);
                    continue;
                }
                if (i + 1 == format.size()) {
                    throw ParseError(line.number, "trailing '%' in formatted print; use '%%' for a literal percent");
                }
                if (format[i + 1] == '%') {
                    literal.push_back('%');
                    i += 2;
                } else if (format[i + 1] == 'v') {
                    if (value_index >= values.size()) {
                        throw ParseError(line.number, "not enough %v values for formatted print");
                    }
                    if (!literal.empty()) {
                        parts.push_back("std::string(" + cpp_string(literal) + ")");
                        literal.clear();
                    }
                    parts.push_back("sun_to_string(" + values[value_index].cpp + ")");
                    ++value_index;
                    i += 2;
                } else {
                    throw ParseError(line.number, "unsupported formatted print placeholder; use %v or %%");
                }
            }
            if (!literal.empty() || parts.empty()) {
                parts.push_back("std::string(" + cpp_string(literal) + ")");
            }
            if (value_index != values.size()) {
                throw ParseError(line.number, "too many %v values for formatted print");
            }
            std::ostringstream formatted;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (i) formatted << " + ";
                formatted << parts[i];
            }
            value = {Type::string, formatted.str(), std::nullopt};
        }
        return "std::cout << sun_to_string(" + value.cpp + ") << '\\n';";
    }

    std::string parse_memory_statement(const SourceLine& line, const std::string& text,
                                      const Symbols& symbols, bool math_imported) const {
        if (!memory_imported_) {
            throw ParseError(line.number, "memory functions require 'import memory'");
        }
        const auto call = parse_call(text.substr(7), line.number);
        if (call.first != "write_u8") {
            throw ParseError(line.number, "only memory.write_u8 is a standalone memory statement");
        }
        const auto arguments = split_arguments(call.second, line.number);
        if (arguments.size() != 3) {
            throw ParseError(line.number, "memory.write_u8 expects (region, offset, value)");
        }
        const auto region = expression(arguments[0], line.number, symbols, math_imported);
        const auto offset = expression(arguments[1], line.number, symbols, math_imported);
        const auto value = expression(arguments[2], line.number, symbols, math_imported);
        if (region.type != Type::region || offset.type != Type::number ||
            value.type != Type::number) {
            throw ParseError(line.number,
                             "memory.write_u8 expects (region, number offset, number value)");
        }
        return "(" + region.cpp + ").write_u8(" + offset.cpp + ", " + value.cpp + ");";
    }

    std::string parse_user_call(const SourceLine& line, const std::string& text,
                                const Symbols& symbols, bool math_imported) const {
        const auto call = parse_call(text, line.number);
        const auto signature = signatures_.find(call.first);
        if (signature == signatures_.end()) {
            throw ParseError(line.number, "unknown function '" + call.first + "'");
        }
        std::vector<ExprValue> values;
        for (const auto& argument : split_arguments(call.second, line.number)) {
            values.push_back(expression(argument, line.number, symbols, math_imported));
        }
        validate_call_arity(call.first, signature->second, values.size(), line.number);
        std::ostringstream code;
        code << "sun_fn_" << call.first << '(';
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (values[i].type != signature->second.parameters[i]) {
                throw ParseError(line.number, "argument " + std::to_string(i + 1) +
                                               " of '" + call.first + "' must have type " +
                                               cpp_type(signature->second.parameters[i]));
            }
            if (i) code << ", ";
            code << values[i].cpp;
        }
        for (std::size_t i = values.size(); i < signature->second.parameters.size(); ++i) {
            if (!signature->second.default_values[i]) {
                throw ParseError(line.number, "missing default value for parameter " +
                                               std::to_string(i + 1) + " of '" + call.first + "'");
            }
            if (i) code << ", ";
            code << *signature->second.default_values[i];
        }
        code << ");";
        return code.str();
    }

    bool is_assignment(const std::string& text) const {
        const auto equals = text.find('=');
        if (equals == std::string::npos || equals == 0 || equals + 1 == text.size()) return false;
        if (text[equals + 1] == '=' || text[equals - 1] == '=' ||
            text[equals - 1] == '!' || text[equals - 1] == '<' || text[equals - 1] == '>') {
            return false;
        }
        return is_identifier(trim(text.substr(0, equals)));
    }

    std::string parse_statement(std::size_t& index, Symbols& symbols, bool math_imported,
                                bool* definitely_returns) {
        const auto line = lines_[index];
        const auto& text = line.text;
        if (definitely_returns) *definitely_returns = false;
        if (text == "inline cpp:") return parse_inline_cpp(index, symbols);
        if (text == "endcpp") {
            throw ParseError(line.number, "'endcpp' without an open inline C++ block");
        }
        if (starts_word(text, "if")) {
            auto result = parse_if(index, symbols, math_imported, definitely_returns);
            return result;
        }
        if (starts_word(text, "while")) {
            auto result = parse_while(index, symbols, math_imported);
            if (definitely_returns) *definitely_returns = false;
            return result;
        }
        if (starts_word(text, "for")) {
            auto result = parse_for(index, symbols, math_imported);
            if (definitely_returns) *definitely_returns = false;
            return result;
        }
        if (starts_word(text, "let")) {
            auto result = parse_let(line, symbols, math_imported);
            ++index;
            return result;
        }
        if (text == "return" || starts_word(text, "return")) {
            auto result = parse_return(line, symbols, math_imported, definitely_returns);
            ++index;
            return result;
        }
        if (text == "break" || text == "continue") {
            if (loop_depth_ == 0) {
                throw ParseError(line.number, "'" + text +
                                               "' is only valid inside a loop");
            }
            ++index;
            return text + ";";
        }
        if (text.rfind("memory.", 0) == 0) {
            auto result = parse_memory_statement(line, text, symbols, math_imported);
            ++index;
            return result;
        }
        if (is_assignment(text)) {
            auto result = parse_assignment(line, symbols, math_imported);
            ++index;
            return result;
        }
        if (starts_word(text, "print")) {
            const auto call = parse_call(text, line.number);
            if (call.first != "print") {
                throw ParseError(line.number, "expected print(expression)");
            }
            auto result = parse_print(line, call.second, symbols, math_imported);
            ++index;
            return result;
        }
        auto result = parse_user_call(line, text, symbols, math_imported);
        ++index;
        return result;
    }

    std::vector<SourceLine> lines_;
    Signatures signatures_;
    std::vector<Function> functions_;
    std::optional<Type> active_return_type_;
    bool inside_function_ = false;
    std::size_t next_loop_id_ = 0;
    std::size_t loop_depth_ = 0;
    bool memory_imported_ = false;
};

} // namespace

std::string cpp_type(Type type) {
    switch (type) {
    case Type::string: return "std::string";
    case Type::number: return "double";
    case Type::boolean: return "bool";
    case Type::region: return "SunMemoryRegion";
    }
    throw std::logic_error("invalid Sun type");
}

Program parse(std::istream& source) {
    return SunParser(source).parse();
}

} // namespace sun
