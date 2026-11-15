#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

enum class Type { string, number, boolean };

struct ParseError : std::runtime_error {
    ParseError(std::size_t line, const std::string& message)
        : std::runtime_error(message), line(line) {}
    std::size_t line;
};

struct SourceLine {
    std::size_t number;
    std::string text;
};

struct Parameter {
    std::string name;
    Type type;
};

struct Signature {
    std::vector<Type> parameters;
};

struct Function {
    std::string name;
    std::vector<Parameter> parameters;
    std::vector<std::string> body;
    std::size_t line;
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
    throw ParseError(line, "unknown type '" + name + "' (use string, number, or bool)");
}

std::string cpp_type(Type type) {
    switch (type) {
    case Type::string: return "std::string";
    case Type::number: return "double";
    case Type::boolean: return "bool";
    }
    throw std::logic_error("invalid Sun type");
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

struct Token {
    enum class Kind { identifier, number, string, symbol, end };
    Kind kind;
    std::string text;
    bool fstring = false;
};

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
                    if (i == source.size()) throw ParseError(line, "unterminated escape sequence");
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
        if (std::string("+-*/%<>()=,.").find(c) != std::string::npos) {
            tokens.push_back({Token::Kind::symbol, std::string(1, c)});
            ++i;
            continue;
        }
        throw ParseError(line, "unexpected character in expression");
    }
    tokens.push_back({Token::Kind::end, ""});
    return tokens;
}

class ExpressionParser {
public:
    ExpressionParser(const std::string& source, std::size_t line, const Symbols& symbols,
                     bool math_imported)
        : tokens_(tokenize(source, line)), line_(line), symbols_(symbols),
          math_imported_(math_imported) {}

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

    static int precedence(const std::string& op) {
        if (op == "==" || op == "!=" || op == "<" || op == "<=" ||
            op == ">" || op == ">=") return 1;
        if (op == "+" || op == "-") return 2;
        if (op == "*" || op == "/" || op == "%") return 3;
        return 0;
    }

    ExprValue parse_binary(int minimum_precedence) {
        auto left = parse_unary();
        while (peek().kind == Token::Kind::symbol) {
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
            if (accept("(")) return parse_math_call(name);
            const auto found = symbols_.find(name);
            if (found == symbols_.end()) {
                throw ParseError(line_, "unknown value '" + name + "'");
            }
            return {found->second.type, found->second.cpp_name, std::nullopt};
        }
        throw ParseError(line_, "expected a number, string, bool, or variable");
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
            text = trim(text);
            if (text.empty() || text[0] == '#') continue;
            lines_.push_back({number, text});
        }
    }

    struct Result {
        std::vector<Function> functions;
        std::vector<std::string> main_body;
        bool math_imported = false;
    };

    Result parse() {
        Result result;
        std::size_t index = 0;
        while (index < lines_.size() && starts_word(lines_[index].text, "import")) {
            if (lines_[index].text != "import math") {
                throw ParseError(lines_[index].number, "only 'import math' is currently supported");
            }
            if (result.math_imported) {
                throw ParseError(lines_[index].number, "duplicate import 'math'");
            }
            result.math_imported = true;
            ++index;
        }
        for (std::size_t i = index; i < lines_.size(); ++i) {
            if (starts_word(lines_[i].text, "import")) {
                throw ParseError(lines_[i].number, "imports must appear before declarations and statements");
            }
            if (starts_word(lines_[i].text, "func")) {
                auto function = parse_function_header(lines_[i]);
                if (function.name == "print") {
                    throw ParseError(function.line, "function name 'print' is reserved");
                }
                Signature signature;
                for (const auto& parameter : function.parameters) {
                    signature.parameters.push_back(parameter.type);
                }
                if (!signatures_.emplace(function.name, signature).second) {
                    throw ParseError(function.line, "function '" + function.name + "' is already defined");
                }
            }
        }
        result.main_body = parse_main(index, result.math_imported);
        result.functions = std::move(functions_);
        return result;
    }

private:
    Function parse_function_header(const SourceLine& line) {
        const auto header = trim(line.text.substr(4));
        if (header.empty()) throw ParseError(line.number, "expected 'func name(parameters)'");
        const auto call = parse_call(header, line.number);
        Function function{call.first, {}, {}, line.number};
        std::unordered_map<std::string, bool> seen;
        for (const auto& parameter_text : split_arguments(call.second, line.number)) {
            const auto colon = parameter_text.find(':');
            const auto name = trim(parameter_text.substr(0, colon));
            if (!is_identifier(name)) {
                throw ParseError(line.number, "invalid parameter name '" + name + "'");
            }
            if (!seen.emplace(name, true).second) {
                throw ParseError(line.number, "duplicate parameter '" + name + "'");
            }
            const auto type = colon == std::string::npos
                                  ? Type::string
                                  : parse_type(trim(parameter_text.substr(colon + 1)), line.number);
            function.parameters.push_back({name, type});
        }
        return function;
    }

    bool is_terminator(const std::string& text) const {
        return text == "end" || starts_word(text, "elif") || starts_word(text, "else");
    }

    std::vector<std::string> parse_main(std::size_t& index, bool math_imported) {
        Symbols symbols;
        std::vector<std::string> body;
        while (index < lines_.size()) {
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
                function.body = parse_block(index, function_symbols, math_imported);
                if (index == lines_.size() || lines_[index].text != "end") {
                    const auto error_line = lines_.empty() ? 1 : lines_.back().number + 1;
                    throw ParseError(error_line, "function '" + function.name + "' is missing 'end'");
                }
                ++index;
                functions_.push_back(std::move(function));
                continue;
            }
            body.push_back(parse_statement(index, symbols, math_imported));
        }
        return body;
    }

    std::vector<std::string> parse_block(std::size_t& index, Symbols& symbols,
                                         bool math_imported) {
        std::vector<std::string> body;
        while (index < lines_.size() && !is_terminator(lines_[index].text)) {
            if (starts_word(lines_[index].text, "func")) {
                throw ParseError(lines_[index].number, "nested function declarations are not supported");
            }
            if (starts_word(lines_[index].text, "import")) {
                throw ParseError(lines_[index].number, "imports must appear before declarations and statements");
            }
            body.push_back(parse_statement(index, symbols, math_imported));
        }
        return body;
    }

    ExprValue expression(const std::string& text, std::size_t line, const Symbols& symbols,
                         bool math_imported) const {
        return ExpressionParser(text, line, symbols, math_imported).parse();
    }

    ExprValue parse_condition(const SourceLine& line, const std::string& keyword,
                              const Symbols& symbols, bool math_imported) const {
        const auto text = trim(line.text.substr(keyword.size()));
        if (text.empty() || text.back() != ':') {
            throw ParseError(line.number, "condition must end with ':'");
        }
        auto condition = expression(trim(text.substr(0, text.size() - 1)),
                                   line.number, symbols, math_imported);
        if (condition.type != Type::boolean) {
            throw ParseError(line.number, "if conditions must have type bool");
        }
        return condition;
    }

    std::string parse_if(std::size_t& index, const Symbols& symbols, bool math_imported) {
        const auto first_line = lines_[index];
        auto condition = parse_condition(first_line, "if", symbols, math_imported);
        ++index;
        Symbols branch_symbols = symbols;
        auto branch = parse_block(index, branch_symbols, math_imported);

        std::ostringstream code;
        code << "if (" << condition.cpp << ") {\n"
             << indent_statements(branch, 4) << '}';
        while (index < lines_.size() && starts_word(lines_[index].text, "elif")) {
            const auto branch_line = lines_[index];
            auto elif_condition = parse_condition(branch_line, "elif", symbols, math_imported);
            ++index;
            branch_symbols = symbols;
            branch = parse_block(index, branch_symbols, math_imported);
            code << " else if (" << elif_condition.cpp << ") {\n"
                 << indent_statements(branch, 4) << '}';
        }
        if (index < lines_.size() && starts_word(lines_[index].text, "else")) {
            if (lines_[index].text != "else:") {
                throw ParseError(lines_[index].number, "expected 'else:'");
            }
            ++index;
            branch_symbols = symbols;
            branch = parse_block(index, branch_symbols, math_imported);
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
        const auto value = expression(trim(declaration.substr(equals + 1)), line.number,
                                      symbols, math_imported);
        if (value.type != type) {
            throw ParseError(line.number, "cannot initialize " + cpp_type(type) +
                                           " with a " + cpp_type(value.type) + " value");
        }
        symbols.emplace(name, Symbol{type, "sun_var_" + name});
        return cpp_type(type) + " sun_var_" + name + " = " + value.cpp + ";";
    }

    std::string parse_print(const SourceLine& line, const std::string& arguments_text,
                            const Symbols& symbols, bool math_imported) const {
        const auto arguments = split_arguments(arguments_text, line.number);
        if (arguments.empty()) throw ParseError(line.number, "expected print(expression)");
        const auto first = expression(arguments.front(), line.number, symbols, math_imported);
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
        return "std::cout << std::boolalpha << (" + value.cpp +
               ") << std::noboolalpha << '\\n';";
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
        if (values.size() != signature->second.parameters.size()) {
            throw ParseError(line.number, "function '" + call.first + "' expects " +
                                           std::to_string(signature->second.parameters.size()) +
                                           " argument(s), got " + std::to_string(values.size()));
        }
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
        code << ");";
        return code.str();
    }

    std::string parse_statement(std::size_t& index, Symbols& symbols, bool math_imported) {
        const auto line = lines_[index];
        const auto& text = line.text;
        if (starts_word(text, "if")) return parse_if(index, symbols, math_imported);
        if (starts_word(text, "let")) {
            auto result = parse_let(line, symbols, math_imported);
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
};

std::string transpile(const SunParser::Result& program) {
    std::ostringstream out;
    out << "#include <cmath>\n#include <iostream>\n#include <sstream>\n#include <string>\n";
    out << "\nstd::string sun_to_string(const std::string& value) {\n"
           "    return value;\n"
           "}\n\n"
           "template <typename T>\n"
           "std::string sun_to_string(const T& value) {\n"
           "    std::ostringstream stream;\n"
           "    stream << std::boolalpha << value;\n"
           "    return stream.str();\n"
           "}\n\n";
    for (const auto& function : program.functions) {
        out << "void sun_fn_" << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(function.parameters[i].type);
        }
        out << ");\n";
    }
    if (!program.functions.empty()) out << '\n';
    for (const auto& function : program.functions) {
        out << "void sun_fn_" << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(function.parameters[i].type) << " sun_arg_"
                << function.parameters[i].name;
        }
        out << ") {\n" << indent_statements(function.body, 4) << "}\n\n";
    }
    out << "int main() {\n" << indent_statements(program.main_body, 4)
        << "    return 0;\n}\n";
    return out.str();
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += '\'';
    return quoted;
}

struct Options {
    fs::path source;
    fs::path output;
    bool compile = false;
    bool compiler_explicit = false;
    std::string compiler = "g++";
};

void print_usage(std::ostream& out) {
    out << "Usage: sun <source.su> [-o <output.cpp>] [--compile] [--compiler <path>]\n";
}

Options parse_options(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        print_usage(std::cout);
        std::exit(0);
    }
    if (argc < 2) throw std::runtime_error("missing source file");
    Options options;
    options.source = argv[1];
    options.output = options.source;
    options.output.replace_extension(".cpp");
    for (int i = 2; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-o") {
            if (++i >= argc) throw std::runtime_error("'-o' requires an output path");
            options.output = argv[i];
        } else if (argument == "--compile") {
            options.compile = true;
        } else if (argument == "--compiler") {
            if (++i >= argc) throw std::runtime_error("'--compiler' requires a compiler path");
            options.compiler = argv[i];
            options.compiler_explicit = true;
        } else if (argument == "-h" || argument == "--help") {
            print_usage(std::cout);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option '" + argument + "'");
        }
    }
    if (options.source.extension() != ".su") {
        throw std::runtime_error("source file must have the .su extension");
    }
    if (options.output == "-") {
        if (options.compile) throw std::runtime_error("cannot compile when C++ output is sent to stdout");
    } else if (fs::absolute(options.source).lexically_normal() ==
               fs::absolute(options.output).lexically_normal()) {
        throw std::runtime_error("C++ output path must not overwrite the source file");
    }
    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        std::ifstream input(options.source);
        if (!input) {
            throw std::runtime_error("cannot open source file '" + options.source.string() + "'");
        }
        const auto program = SunParser(input).parse();
        const auto generated = transpile(program);

        if (options.output == "-") {
            std::cout << generated;
        } else {
            std::ofstream output(options.output);
            if (!output) {
                throw std::runtime_error("cannot write C++ output '" + options.output.string() + "'");
            }
            output << generated;
            if (!output) throw std::runtime_error("failed while writing C++ output");
        }
        if (options.compile) {
            const char* environment_compiler = std::getenv("CXX");
            const auto compiler = environment_compiler && !options.compiler_explicit
                                      ? std::string(environment_compiler)
                                      : options.compiler;
            auto executable = options.source;
            executable.replace_extension("");
            const std::string command = shell_quote(compiler) + " -std=c++17 -Wall -Wextra -pedantic " +
                                        shell_quote(options.output.string()) + " -o " +
                                        shell_quote(executable.string());
            const int result = std::system(command.c_str());
            if (result != 0) {
                throw std::runtime_error("C++ compilation failed (exit code " +
                                         std::to_string(result) + ")");
            }
            std::cerr << "Compiled executable: " << executable.string() << '\n';
        }
        return 0;
    } catch (const ParseError& error) {
        std::cerr << "sun: line " << error.line << ": error: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "sun: error: " << error.what() << '\n';
        if (argc < 2) print_usage(std::cerr);
    }
    return 1;
}
