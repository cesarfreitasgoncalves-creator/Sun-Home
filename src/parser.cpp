#include "compiler.hpp"
#include "lexer.hpp"

#include <algorithm>
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
    std::vector<std::optional<Expression>> default_expressions;
    std::optional<Type> return_type;
    std::string cpp_name;
};

struct Symbol {
    Type type;
    std::string cpp_name;
};

struct ExprValue {
    Type type;
    std::string cpp;
    std::optional<std::string> plain_string;
    Expression ast;

    ExprValue(Type type, std::string cpp, std::optional<std::string> plain_string,
              Expression ast)
        : type(type), cpp(std::move(cpp)), plain_string(std::move(plain_string)),
          ast(std::move(ast)) {}
};

Expression make_expression(Expression::Kind kind, Type type, std::string value,
                           std::vector<Expression> children = {}) {
    return {kind, type, std::move(value), std::move(children)};
}

ExprValue make_expr_value(Type type, std::string cpp, Expression ast,
                          std::optional<std::string> plain_string = std::nullopt) {
    return {type, std::move(cpp), std::move(plain_string), std::move(ast)};
}

using Symbols = std::unordered_map<std::string, Symbol>;
using Signatures = std::unordered_map<std::string, Signature>;
using Types = std::unordered_map<std::string, Type>;
using CustomTypes = std::unordered_map<std::string, CustomTypeDefinition>;

bool is_list_type(Type type);
bool is_dictionary_type(Type type);
Type list_type_for(Type item_type);

bool can_assign(Type expected, Type actual) {
    return expected == actual ||
           (expected == Type::number && actual == Type::integer) ||
           (is_list_type(expected) && actual == Type::empty_list) ||
           (is_dictionary_type(expected) && actual == Type::empty_dictionary);
}

std::string converted_value(Type expected, Type actual, const std::string& value) {
    if (expected == Type::number && actual == Type::integer) {
        return "static_cast<double>(" + value + ")";
    }
    return value;
}

Expression converted_expression(Type expected, Type actual, Expression value) {
    if (expected == Type::number && actual == Type::integer) {
        return make_expression(Expression::Kind::cast, expected, "double", {std::move(value)});
    }
    return value;
}

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

std::string strip_comment(const std::string& text) {
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = text[i];
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '#') {
            return text.substr(0, i);
        }
    }
    return text;
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

bool is_dictionary_type(Type type) {
    return type.kind == Type::Kind::dictionary;
}

bool is_dict_key_type(Type type) {
    return type == Type::string || type == Type::integer ||
           type == Type::boolean || type == Type::character;
}

std::string scalar_type_name(Type type) {
    switch (type.kind) {
    case Type::Kind::string: return "string";
    case Type::Kind::number: return "number";
    case Type::Kind::boolean: return "bool";
    case Type::Kind::integer: return "int";
    case Type::Kind::character: return "char";
    default: throw std::logic_error("type is not a dictionary scalar");
    }
}

Type scalar_type_from_name(const std::string& name, std::size_t line) {
    if (name == "string" || name == "str") return Type::string;
    if (name == "number") return Type::number;
    if (name == "bool") return Type::boolean;
    if (name == "int") return Type::integer;
    if (name == "char") return Type::character;
    throw ParseError(line, "dictionary key and value types must be string, number, bool, int, or char");
}

std::pair<Type, Type> dictionary_types(Type type) {
    const auto separator = type.name.find(',');
    if (separator == std::string::npos) {
        throw std::logic_error("dictionary type is missing its key or value type");
    }
    return {scalar_type_from_name(type.name.substr(0, separator), 0),
            scalar_type_from_name(type.name.substr(separator + 1), 0)};
}

Type parse_type(const std::string& name, std::size_t line, const Types& types) {
    if (const auto custom = types.find(name); custom != types.end()) return custom->second;
    if (name.rfind("dict[", 0) == 0 && name.back() == ']') {
        const auto parameters = name.substr(5, name.size() - 6);
        const auto comma = parameters.find(',');
        if (comma == std::string::npos || parameters.find(',', comma + 1) != std::string::npos) {
            throw ParseError(line, "dictionary type uses dict[key_type, value_type]");
        }
        const auto key = scalar_type_from_name(trim(parameters.substr(0, comma)), line);
        const auto value = scalar_type_from_name(trim(parameters.substr(comma + 1)), line);
        if (!is_dict_key_type(key)) {
            throw ParseError(line, "dictionary keys must have type string, int, bool, or char");
        }
        return {Type::Kind::dictionary, scalar_type_name(key) + "," +
                                           scalar_type_name(value)};
    }
    if (name.rfind("list[", 0) == 0 && name.back() == ']') {
        const auto item = parse_type(name.substr(5, name.size() - 6), line, types);
        if (item.kind == Type::Kind::custom || item == Type::region ||
            is_dictionary_type(item) ||
            item.kind == Type::Kind::list_number || item.kind == Type::Kind::list_integer ||
            item.kind == Type::Kind::list_string || item.kind == Type::Kind::list_boolean ||
            item.kind == Type::Kind::list_character) {
            throw ParseError(line, "lists of custom types or nested lists are not supported");
        }
        return list_type_for(item);
    }
    if (name == "string" || name == "str") return Type::string;
    if (name == "number") return Type::number;
    if (name == "bool") return Type::boolean;
    if (name == "region") return Type::region;
    if (name == "int") return Type::integer;
    if (name == "char") return Type::character;
    throw ParseError(line, "unknown type '" + name +
                               "' (use string, number, bool, int, char, region, list[type], or dict[key, value])");
}

bool is_list_type(Type type) {
    return type == Type::list_number || type == Type::list_integer ||
           type == Type::list_string || type == Type::list_boolean ||
           type == Type::list_character;
}

Type list_type_for(Type item_type) {
    switch (item_type.kind) {
    case Type::Kind::number: return Type::list_number;
    case Type::Kind::integer: return Type::list_integer;
    case Type::Kind::string: return Type::list_string;
    case Type::Kind::boolean: return Type::list_boolean;
    case Type::Kind::character: return Type::list_character;
    default: throw std::logic_error("unsupported list item type");
    }
}

Type list_item_type(Type list_type) {
    switch (list_type.kind) {
    case Type::Kind::list_number: return Type::number;
    case Type::Kind::list_integer: return Type::integer;
    case Type::Kind::list_string: return Type::string;
    case Type::Kind::list_boolean: return Type::boolean;
    case Type::Kind::list_character: return Type::character;
    default: throw std::logic_error("type is not a list");
    }
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
                     bool math_imported, bool memory_imported, bool io_imported,
                     const Signatures& signatures, const Types& types,
                     const CustomTypes& custom_types,
                     bool allow_function_calls = true)
        : tokens_(tokenize(source, line)), line_(line), symbols_(symbols),
          math_imported_(math_imported), memory_imported_(memory_imported),
          io_imported_(io_imported),
          signatures_(signatures), types_(types), custom_types_(custom_types),
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
            if (operand.type == Type::integer) {
                return make_expr_value(
                    Type::integer, "sun_int_neg(" + operand.cpp + ")",
                    make_expression(Expression::Kind::unary, Type::integer, "sun_int_neg",
                                    {operand.ast}));
            }
            if (operand.type != Type::number) {
                throw ParseError(line_, "unary '-' requires a number or int");
            }
            return make_expr_value(
                Type::number, "(-(" + operand.cpp + "))",
                make_expression(Expression::Kind::unary, Type::number, "-", {operand.ast}));
        }
        if (peek().kind == Token::Kind::identifier && peek().text == "not") {
            take();
            auto operand = parse_binary(3);
            if (operand.type != Type::boolean) {
                throw ParseError(line_, "'not' requires a bool value");
            }
            return make_expr_value(
                Type::boolean, "(!(" + operand.cpp + "))",
                make_expression(Expression::Kind::unary, Type::boolean, "!", {operand.ast}));
        }
        return parse_primary();
    }

    ExprValue parse_primary() {
        if (accept("(")) {
            auto value = parse_binary(1);
            if (!accept(")")) throw ParseError(line_, "expected ')' after expression");
            value.plain_string.reset();
            return parse_postfix(std::move(value));
        }
        if (accept("[")) {
            std::vector<ExprValue> values;
            if (!accept("]")) {
                do {
                    values.push_back(parse_binary(1));
                } while (accept(","));
                if (!accept("]")) throw ParseError(line_, "expected ']' after list items");
            }
            if (values.empty()) {
                return parse_postfix(make_expr_value(
                    Type::empty_list, "{}", make_expression(
                        Expression::Kind::empty_list, Type::empty_list, "")));
            }
            const auto item_type = values.front().type;
            if (is_list_type(item_type) || item_type == Type::region ||
                item_type == Type::empty_list) {
                throw ParseError(line_, "lists cannot contain regions or nested lists");
            }
            std::ostringstream list;
            list << "std::vector<" << cpp_type(item_type) << ">{";
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (values[i].type != item_type) {
                    throw ParseError(line_, "list items must all have the same type");
                }
                if (i) list << ", ";
                list << values[i].cpp;
            }
            list << '}';
            std::vector<Expression> items;
            for (const auto& value : values) items.push_back(value.ast);
            return parse_postfix(make_expr_value(
                list_type_for(item_type), list.str(),
                make_expression(Expression::Kind::list, list_type_for(item_type),
                               cpp_type(item_type), std::move(items))));
        }
        if (accept("{")) {
            std::vector<std::pair<ExprValue, ExprValue>> entries;
            if (!accept("}")) {
                do {
                    auto key = parse_binary(1);
                    if (!accept(":")) {
                        throw ParseError(line_, "dictionary entries use key: value");
                    }
                    auto value = parse_binary(1);
                    if (!is_dict_key_type(key.type)) {
                        throw ParseError(line_, "dictionary keys must have type string, int, bool, or char");
                    }
                    if (value.type.kind == Type::Kind::custom ||
                        value.type == Type::region || is_list_type(value.type) ||
                        is_dictionary_type(value.type) ||
                        value.type == Type::empty_list ||
                        value.type == Type::empty_dictionary) {
                        throw ParseError(line_, "dictionary values must have a supported scalar type");
                    }
                    if (!entries.empty() &&
                        (key.type != entries.front().first.type ||
                         value.type != entries.front().second.type)) {
                        throw ParseError(line_, "dictionary keys and values must each have one consistent type");
                    }
                    entries.emplace_back(std::move(key), std::move(value));
                } while (accept(","));
                if (!accept("}")) {
                    throw ParseError(line_, "expected '}' after dictionary entries");
                }
            }
            if (entries.empty()) {
                return parse_postfix(make_expr_value(
                    Type::empty_dictionary, "{}",
                    make_expression(Expression::Kind::empty_dictionary,
                                   Type::empty_dictionary, "")));
            }
            const auto key_type = entries.front().first.type;
            const auto value_type = entries.front().second.type;
            const Type dictionary_type{
                Type::Kind::dictionary,
                scalar_type_name(key_type) + "," + scalar_type_name(value_type)};
            std::ostringstream dictionary;
            dictionary << cpp_type(dictionary_type) << '{';
            std::vector<Expression> items;
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (i) dictionary << ", ";
                dictionary << '{' << entries[i].first.cpp << ", "
                           << entries[i].second.cpp << '}';
                items.push_back(entries[i].first.ast);
                items.push_back(entries[i].second.ast);
            }
            dictionary << '}';
            return parse_postfix(make_expr_value(
                dictionary_type, dictionary.str(),
                make_expression(Expression::Kind::dictionary, dictionary_type,
                                cpp_type(key_type) + "|" + cpp_type(value_type),
                                std::move(items))));
        }
        if (peek().kind == Token::Kind::number) {
            const auto token = take();
            auto number = token.text;
            if (number.find_first_of(".eE") == std::string::npos) number += ".0";
            return parse_postfix(make_expr_value(
                Type::number, number,
                make_expression(Expression::Kind::literal, Type::number, number)));
        }
        if (peek().kind == Token::Kind::string) {
            const auto token = take();
            if (token.fstring) return parse_fstring(token.text);
            return make_expr_value(
                Type::string, "std::string(" + cpp_string(token.text) + ")",
                make_expression(Expression::Kind::string_literal, Type::string, token.text),
                token.text);
        }
        if (peek().kind == Token::Kind::identifier) {
            auto name = take().text;
            const auto custom_type = custom_types_.find(name);
            if (custom_type != custom_types_.end()) {
                if (!custom_type->second.is_enum && accept("(")) {
                    return parse_custom_constructor(custom_type->second);
                }
                if (custom_type->second.is_enum && accept(".")) {
                    if (peek().kind != Token::Kind::identifier) {
                        throw ParseError(line_, "expected an enum value after '" + name + ".'");
                    }
                    const auto value = take().text;
                    const auto found = std::find(custom_type->second.enum_values.begin(),
                                                 custom_type->second.enum_values.end(), value);
                    if (found == custom_type->second.enum_values.end()) {
                        throw ParseError(line_, "unknown value '" + value +
                                                 "' in enum '" + name + "'");
                    }
                    const auto variant = std::find_if(
                        custom_type->second.enum_variants.begin(),
                        custom_type->second.enum_variants.end(),
                        [&](const EnumVariant& candidate) {
                            return candidate.name == value;
                        });
                    const bool payload_enum = std::any_of(
                        custom_type->second.enum_variants.begin(),
                        custom_type->second.enum_variants.end(),
                        [](const EnumVariant& candidate) {
                            return !candidate.fields.empty();
                        });
                    if (payload_enum) {
                        std::vector<ExprValue> arguments;
                        const bool has_arguments = accept("(");
                        if (has_arguments) arguments = parse_expression_arguments();
                        if (variant == custom_type->second.enum_variants.end()) {
                            throw ParseError(line_, "missing enum variant metadata");
                        }
                        if (arguments.size() != variant->fields.size()) {
                            throw ParseError(line_, "enum variant '" + value +
                                                       "' expects " +
                                                       std::to_string(variant->fields.size()) +
                                                       " payload value(s)");
                        }
                        std::ostringstream inner;
                        inner << "SunType_" << name << '_' << value << '{';
                        std::vector<Expression> payload;
                        for (std::size_t i = 0; i < arguments.size(); ++i) {
                            if (i) inner << ", ";
                            const auto& expected = variant->fields[i].type;
                            if (!can_assign(expected, arguments[i].type)) {
                                throw ParseError(line_, "payload '" +
                                                           variant->fields[i].name +
                                                           "' of enum variant '" + value +
                                                           "' has the wrong type");
                            }
                            const auto converted = converted_expression(
                                expected, arguments[i].type, arguments[i].ast);
                            inner << converted_value(expected, arguments[i].type,
                                                     arguments[i].cpp);
                            payload.push_back(converted);
                        }
                        inner << '}';
                        auto variant_expression = make_expression(
                            Expression::Kind::aggregate, types_.at(name),
                            "SunType_" + name + '_' + value, std::move(payload));
                        auto enum_expression = make_expression(
                            Expression::Kind::aggregate, types_.at(name),
                            "SunType_" + name, {std::move(variant_expression)});
                        return make_expr_value(
                            types_.at(name),
                            "SunType_" + name + '{' + inner.str() + '}',
                            std::move(enum_expression));
                    }
                    const auto cpp_value = "SunType_" + name + "::" + value;
                    return make_expr_value(
                        types_.at(name), cpp_value,
                        make_expression(Expression::Kind::variable, types_.at(name), cpp_value));
                }
                throw ParseError(line_, "type '" + name + "' must be constructed or qualified");
            }
            if (name == "true" || name == "false") {
                return parse_postfix(make_expr_value(
                    Type::boolean, name,
                    make_expression(Expression::Kind::literal, Type::boolean, name)));
            }
            if (name == "input" && accept("(")) return parse_input_call();
            if ((name == "int" || name == "number") && accept("(")) {
                auto value = parse_binary(1);
                if (!accept(")")) throw ParseError(line_, "expected ')' after numeric conversion");
                if (name == "int") {
                    if (value.type != Type::number && value.type != Type::integer) {
                        throw ParseError(line_, "int() requires a number or int value");
                    }
                    if (value.type == Type::integer) return parse_postfix(value);
                    return parse_postfix(make_expr_value(
                        Type::integer, "sun_to_int(" + value.cpp + ")",
                        make_expression(Expression::Kind::call, Type::integer, "sun_to_int",
                                        {value.ast})));
                }
                if (value.type != Type::number && value.type != Type::integer) {
                    throw ParseError(line_, "number() requires a number or int value");
                }
                if (value.type == Type::number) return parse_postfix(value);
                return parse_postfix(make_expr_value(
                    Type::number, "static_cast<double>(" + value.cpp + ")",
                    make_expression(Expression::Kind::cast, Type::number, "double",
                                    {value.ast})));
            }
            if (name == "char" && accept("(")) {
                const auto value = parse_binary(1);
                if (!accept(")")) throw ParseError(line_, "expected ')' after char conversion");
                if (value.type != Type::string) {
                    throw ParseError(line_, "char() requires a string");
                }
                return parse_postfix(make_expr_value(
                    Type::character, "sun_to_char(" + value.cpp + ")",
                    make_expression(Expression::Kind::call, Type::character, "sun_to_char",
                                    {value.ast})));
            }
            if (name == "len" && accept("(")) return parse_len_call();
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
            if (name == "io" && accept(".")) {
                if (peek().kind != Token::Kind::identifier) {
                    throw ParseError(line_, "expected an io function after 'io.'");
                }
                name = take().text;
                if (!accept("(")) {
                    throw ParseError(line_, "expected '(' after io function name");
                }
                return parse_io_call(name);
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
                const auto cpp_name = signature->second.cpp_name.empty()
                                          ? "sun_fn_" + name
                                          : signature->second.cpp_name;
                call << cpp_name << '(';
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    if (!can_assign(signature->second.parameters[i], arguments[i].type)) {
                        throw ParseError(line_, "argument " + std::to_string(i + 1) +
                                                 " of '" + name + "' must have type " +
                                                 cpp_type(signature->second.parameters[i]));
                    }
                    if (i) call << ", ";
                    call << converted_value(signature->second.parameters[i],
                                            arguments[i].type, arguments[i].cpp);
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
                std::vector<Expression> call_arguments;
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    call_arguments.push_back(converted_expression(
                        signature->second.parameters[i], arguments[i].type, arguments[i].ast));
                }
                for (std::size_t i = arguments.size();
                     i < signature->second.parameters.size(); ++i) {
                    call_arguments.push_back(
                        *signature->second.default_expressions[i]);
                }
                return parse_postfix(make_expr_value(
                    *signature->second.return_type, call.str(),
                    make_expression(Expression::Kind::call, *signature->second.return_type,
                                   cpp_name, std::move(call_arguments))));
            }
            const auto found = symbols_.find(name);
            if (found == symbols_.end()) {
                throw ParseError(line_, "unknown value '" + name + "'");
            }
            return parse_postfix(make_expr_value(
                found->second.type, found->second.cpp_name,
                make_expression(Expression::Kind::variable, found->second.type,
                                found->second.cpp_name)));
        }
        throw ParseError(line_, "expected a number, string, bool, or variable");
    }

    ExprValue parse_postfix(ExprValue value) {
        while (true) {
            if (accept("[")) {
                if (!is_list_type(value.type) && !is_dictionary_type(value.type)) {
                    throw ParseError(line_, "only lists and dictionaries can be indexed");
                }
                const auto index = parse_binary(1);
                if (!accept("]")) throw ParseError(line_, "expected ']' after index");
                if (is_dictionary_type(value.type)) {
                    const auto types = dictionary_types(value.type);
                    if (index.type != types.first) {
                        throw ParseError(line_, "dictionary key must have type " +
                                                 cpp_type(types.first));
                    }
                    value = make_expr_value(
                        types.second, "sun_dict_at(" + value.cpp + ", " + index.cpp + ")",
                        make_expression(Expression::Kind::call, types.second, "sun_dict_at",
                                        {value.ast, index.ast}));
                    continue;
                }
                if (index.type != Type::integer && index.type != Type::number) {
                    throw ParseError(line_, "list index must have type int or number");
                }
                const auto type = list_item_type(value.type);
                value = make_expr_value(
                    type, "sun_list_at(" + value.cpp + ", " + index.cpp + ")",
                    make_expression(Expression::Kind::call, type, "sun_list_at",
                                    {value.ast, index.ast}));
                continue;
            }
            if (!accept(".")) break;
            if (value.type.kind != Type::Kind::custom) {
                throw ParseError(line_, "member access requires a class or struct value");
            }
            if (peek().kind != Token::Kind::identifier) {
                throw ParseError(line_, "expected a field or method name after '.'");
            }
            const auto member = take().text;
            const auto type = custom_types_.find(value.type.name);
            if (type == custom_types_.end() || type->second.is_enum) {
                throw ParseError(line_, "enum values do not have fields or methods");
            }
            if (accept("(")) {
                const auto signature = signatures_.find(value.type.name + "." + member);
                if (signature == signatures_.end()) {
                    throw ParseError(line_, "unknown method '" + member +
                                             "' on '" + value.type.name + "'");
                }
                if (!signature->second.return_type) {
                    throw ParseError(line_, "void method '" + member +
                                             "' cannot be used as a value");
                }
                const auto arguments = parse_expression_arguments();
                validate_call_arity(member, signature->second, arguments.size(), line_);
                std::ostringstream call;
                call << "(" << value.cpp << ")." << member << '(';
                append_call_arguments(call, member, signature->second, arguments);
                call << ')';
                std::vector<Expression> call_arguments{value.ast};
                for (std::size_t i = 0; i < arguments.size(); ++i) {
                    call_arguments.push_back(converted_expression(
                        signature->second.parameters[i], arguments[i].type,
                        arguments[i].ast));
                }
                value = make_expr_value(
                    *signature->second.return_type, call.str(),
                    make_expression(Expression::Kind::member_call,
                                    *signature->second.return_type, member,
                                    std::move(call_arguments)));
                continue;
            }
            const auto field = std::find_if(
                type->second.fields.begin(), type->second.fields.end(),
                [&](const Field& candidate) { return candidate.name == member; });
            if (field == type->second.fields.end()) {
                throw ParseError(line_, "unknown field '" + member +
                                         "' on '" + value.type.name + "'");
            }
            value = make_expr_value(
                field->type, "(" + value.cpp + ")." + member,
                make_expression(Expression::Kind::member, field->type, member,
                                {value.ast}));
        }
        return value;
    }

    ExprValue parse_custom_constructor(const CustomTypeDefinition& definition) {
        if (!allow_function_calls_) {
            throw ParseError(line_, "default values cannot construct objects");
        }
        const auto arguments = parse_expression_arguments();
        const auto init = std::find_if(definition.methods.begin(), definition.methods.end(),
                                       [](const Function& method) {
                                           return method.name == "init";
                                       });
        if (definition.is_class && init != definition.methods.end()) {
            const auto signature = signatures_.find(definition.name + ".init");
            validate_call_arity(definition.name, signature->second, arguments.size(), line_);
            std::ostringstream call;
            call << "sun_make_" << definition.name << '(';
            append_call_arguments(call, definition.name, signature->second, arguments);
            call << ')';
            std::vector<Expression> constructor_arguments;
            for (std::size_t i = 0; i < arguments.size(); ++i) {
                constructor_arguments.push_back(converted_expression(
                    signature->second.parameters[i], arguments[i].type,
                    arguments[i].ast));
            }
            return make_expr_value(
                types_.at(definition.name), call.str(),
                make_expression(Expression::Kind::call, types_.at(definition.name),
                                "sun_make_" + definition.name,
                                std::move(constructor_arguments)));
        }
        if (arguments.size() != definition.fields.size()) {
            throw ParseError(line_, "constructor '" + definition.name + "' expects " +
                                     std::to_string(definition.fields.size()) +
                                     " field value(s), got " +
                                     std::to_string(arguments.size()));
        }
        std::ostringstream construction;
        construction << "SunType_" << definition.name << '{';
        std::vector<Expression> fields;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (!can_assign(definition.fields[i].type, arguments[i].type)) {
                throw ParseError(line_, "constructor field '" + definition.fields[i].name +
                                         "' requires type " +
                                         cpp_type(definition.fields[i].type));
            }
            if (i) construction << ", ";
            construction << converted_value(definition.fields[i].type,
                                            arguments[i].type, arguments[i].cpp);
            fields.push_back(converted_expression(definition.fields[i].type,
                                                  arguments[i].type, arguments[i].ast));
        }
        construction << '}';
        return make_expr_value(
            types_.at(definition.name), construction.str(),
            make_expression(Expression::Kind::aggregate, types_.at(definition.name),
                            "SunType_" + definition.name, std::move(fields)));
    }

    std::vector<ExprValue> parse_expression_arguments() {
        std::vector<ExprValue> arguments;
        if (!accept(")")) {
            do {
                arguments.push_back(parse_binary(1));
            } while (accept(","));
            if (!accept(")")) throw ParseError(line_, "expected ')' after arguments");
        }
        return arguments;
    }

    void append_call_arguments(std::ostringstream& call, const std::string& name,
                               const Signature& signature,
                               const std::vector<ExprValue>& arguments) const {
        validate_call_arity(name, signature, arguments.size(), line_);
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (!can_assign(signature.parameters[i], arguments[i].type)) {
                throw ParseError(line_, "argument " + std::to_string(i + 1) +
                                         " of '" + name + "' must have type " +
                                         cpp_type(signature.parameters[i]));
            }
            if (i) call << ", ";
            call << converted_value(signature.parameters[i], arguments[i].type,
                                    arguments[i].cpp);
        }
    }

    ExprValue parse_input_call() {
        if (!allow_function_calls_) {
            throw ParseError(line_, "default values cannot call functions");
        }
        const auto prompt = parse_binary(1);
        auto type = Type::string;
        if (accept(",")) {
            if (peek().kind != Token::Kind::identifier) {
                throw ParseError(line_, "input type must be string/str, number, int, bool, or char");
            }
            type = parse_type(take().text, line_, types_);
        }
        if (type == Type::region || is_list_type(type)) {
            throw ParseError(line_, "input supports only string, number, int, bool, or char");
        }
        if (!accept(")")) throw ParseError(line_, "expected ')' after input arguments");
        if (prompt.type != Type::string) {
            throw ParseError(line_, "input prompt must have type string");
        }
        std::string helper;
        switch (type.kind) {
        case Type::Kind::string: helper = "sun_input_string"; break;
        case Type::Kind::number: helper = "sun_input_number"; break;
        case Type::Kind::integer: helper = "sun_input_int"; break;
        case Type::Kind::boolean: helper = "sun_input_bool"; break;
        case Type::Kind::character: helper = "sun_input_char"; break;
        default: throw ParseError(line_, "unsupported input type");
        }
        return make_expr_value(
            type, helper + "(" + prompt.cpp + ")",
            make_expression(Expression::Kind::call, type, helper, {prompt.ast}));
    }

    ExprValue parse_len_call() {
        const auto value = parse_binary(1);
        if (!accept(")")) throw ParseError(line_, "expected ')' after len argument");
        if (value.type == Type::string) {
            return make_expr_value(
                Type::integer, "static_cast<std::int64_t>(" + value.cpp + ".size())",
                make_expression(Expression::Kind::cast, Type::integer, "std::int64_t",
                                {make_expression(Expression::Kind::member_call, Type::integer,
                                                 "size", {value.ast})}));
        }
        if (is_list_type(value.type) || is_dictionary_type(value.type)) {
            return make_expr_value(
                Type::integer, "static_cast<std::int64_t>(" + value.cpp + ".size())",
                make_expression(Expression::Kind::cast, Type::integer, "std::int64_t",
                                {make_expression(Expression::Kind::member_call, Type::integer,
                                                 "size", {value.ast})}));
        }
        throw ParseError(line_, "len() requires a string, list, or dictionary");
    }

    ExprValue parse_istype_call() {
        const auto value = parse_binary(1);
        if (!accept(",")) {
            throw ParseError(line_, "istype expects a value and a type, e.g. istype(x, number)");
        }
        if (peek().kind != Token::Kind::identifier) {
            throw ParseError(line_, "istype type must be string, number, bool, int, char, or region");
        }
        const auto target_type = parse_type(take().text, line_, types_);
        if (target_type == Type::region && !memory_imported_) {
            throw ParseError(line_, "the region type requires 'import memory'");
        }
        if (!accept(")")) throw ParseError(line_, "expected ')' after istype arguments");
        const bool matches = value.type == target_type;
        const auto result = matches ? "true" : "false";
        return make_expr_value(
            Type::boolean, result,
            make_expression(Expression::Kind::literal, Type::boolean, result));
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
            if (!is_numeric(arguments[0].type)) {
                throw ParseError(line_, "memory.alloc size must have type number or int");
            }
            return make_expr_value(
                Type::region,
                "SunMemoryRegion::allocate(" + as_number(arguments[0]) + ")",
                make_expression(Expression::Kind::call, Type::region,
                                "SunMemoryRegion::allocate",
                                {as_number_expression(arguments[0])}));
        }
        if (name == "size") {
            if (arguments.size() != 1 || arguments[0].type != Type::region) {
                throw ParseError(line_, "memory.size expects one region argument");
            }
            return make_expr_value(
                Type::number, "(" + arguments[0].cpp + ").size()",
                make_expression(Expression::Kind::member_call, Type::number, "size",
                                {arguments[0].ast}));
        }
        if (name == "read_u8") {
            if (arguments.size() != 2 || arguments[0].type != Type::region ||
                !is_numeric(arguments[1].type)) {
                throw ParseError(line_, "memory.read_u8 expects (region, number or int offset)");
            }
            return make_expr_value(
                Type::number, "(" + arguments[0].cpp + ").read_u8(" +
                                  as_number(arguments[1]) + ")",
                make_expression(Expression::Kind::member_call, Type::number, "read_u8",
                                {arguments[0].ast, as_number_expression(arguments[1])}));
        }
        throw ParseError(line_, "unknown memory function '" + name + "'");
    }

    ExprValue parse_io_call(const std::string& name) {
        if (!io_imported_) throw ParseError(line_, "io functions require 'import io'");
        if (!allow_function_calls_) {
            throw ParseError(line_, "default values cannot call functions");
        }
        if (name == "write_text" || name == "append_text") {
            throw ParseError(line_, "io." + name + " is a statement, not an expression");
        }
        std::vector<ExprValue> arguments;
        if (!accept(")")) {
            do {
                arguments.push_back(parse_binary(1));
            } while (accept(","));
            if (!accept(")")) throw ParseError(line_, "expected ')' after io arguments");
        }
        if (name == "read_text") {
            if (arguments.size() != 1 || arguments[0].type != Type::string) {
                throw ParseError(line_, "io.read_text expects one string path");
            }
            return make_expr_value(
                Type::string, "sun_io_read_text(" + arguments[0].cpp + ")",
                make_expression(Expression::Kind::call, Type::string, "sun_io_read_text",
                                {arguments[0].ast}));
        }
        if (name == "exists") {
            if (arguments.size() != 1 || arguments[0].type != Type::string) {
                throw ParseError(line_, "io.exists expects one string path");
            }
            return make_expr_value(
                Type::boolean, "sun_io_exists(" + arguments[0].cpp + ")",
                make_expression(Expression::Kind::call, Type::boolean, "sun_io_exists",
                                {arguments[0].ast}));
        }
        throw ParseError(line_, "unknown io function '" + name + "'");
    }

    ExprValue parse_fstring(const std::string& text) {
        std::vector<std::string> parts;
        std::vector<Expression> expressions;
        std::string literal;
        auto flush_literal = [&]() {
            if (literal.empty()) return;
            parts.push_back("std::string(" + cpp_string(literal) + ")");
            expressions.push_back(make_expression(Expression::Kind::string_literal,
                                                  Type::string, literal));
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
                if (found->second.type.kind == Type::Kind::custom &&
                    !custom_types_.at(found->second.type.name).is_enum) {
                    throw ParseError(line_, "class and struct values cannot be interpolated directly");
                }
                parts.push_back("sun_to_string(" + found->second.cpp_name + ")");
                expressions.push_back(make_expression(
                    Expression::Kind::call, Type::string, "sun_to_string",
                    {make_expression(Expression::Kind::variable, found->second.type,
                                     found->second.cpp_name)}));
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
        if (parts.empty()) {
            parts.push_back("std::string(\"\")");
            expressions.push_back(make_expression(Expression::Kind::string_literal,
                                                  Type::string, ""));
        }
        std::ostringstream code;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i) code << " + ";
            code << parts[i];
        }
        auto value = expressions.front();
        for (std::size_t i = 1; i < expressions.size(); ++i) {
            value = make_expression(Expression::Kind::binary, Type::string, "+",
                                    {std::move(value), std::move(expressions[i])});
        }
        return make_expr_value(Type::string, code.str(), std::move(value));
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
        std::vector<Expression> call_arguments;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (!is_numeric(arguments[i].type)) {
                throw ParseError(line_, "math function '" + name +
                                         "' requires number or int arguments");
            }
            if (i) code << ", ";
            code << as_number(arguments[i]);
            call_arguments.push_back(as_number_expression(arguments[i]));
        }
        code << ')';
        return make_expr_value(
            Type::number, code.str(),
            make_expression(Expression::Kind::call, Type::number,
                            function->second.first, std::move(call_arguments)));
    }

    ExprValue make_binary(const std::string& op, const ExprValue& left,
                          const ExprValue& right) {
        if (op == "and" || op == "or") {
            if (left.type != Type::boolean || right.type != Type::boolean) {
                throw ParseError(line_, "logical operators require bool values");
            }
            const auto cpp_op = op == "and" ? "&&" : "||";
            return make_expr_value(
                Type::boolean,
                "((" + left.cpp + ") " + cpp_op + " (" + right.cpp + "))",
                make_expression(Expression::Kind::binary, Type::boolean, cpp_op,
                                {left.ast, right.ast}));
        }
        if (op == "+") {
            if (left.type == Type::integer && right.type == Type::integer) {
                return make_expr_value(
                    Type::integer, "sun_int_add(" + left.cpp + ", " + right.cpp + ")",
                    make_expression(Expression::Kind::call, Type::integer, "sun_int_add",
                                    {left.ast, right.ast}));
            }
            if (left.type == Type::string && right.type == Type::string) {
                return make_expr_value(
                    Type::string, "((" + left.cpp + ") + (" + right.cpp + "))",
                    make_expression(Expression::Kind::binary, Type::string, "+",
                                    {left.ast, right.ast}));
            }
            if (is_numeric(left.type) && is_numeric(right.type)) {
                const auto left_number = as_number_expression(left);
                const auto right_number = as_number_expression(right);
                return {Type::number,
                        "((" + as_number(left) + ") + (" + as_number(right) + "))",
                        std::nullopt,
                        make_expression(Expression::Kind::binary, Type::number, "+",
                                        {left_number, right_number})};
            }
            throw ParseError(line_, "'+' requires two numeric values or two strings");
        }
        if (op == "==" || op == "!=") {
            if (is_numeric(left.type) && is_numeric(right.type)) {
                const auto left_number = as_number_expression(left);
                const auto right_number = as_number_expression(right);
                return {Type::boolean,
                        "((" + as_number(left) + ") " + op + " (" + as_number(right) + "))",
                        std::nullopt,
                        make_expression(Expression::Kind::binary, Type::boolean, op,
                                        {left_number, right_number})};
            }
            if (left.type != right.type) {
                throw ParseError(line_, "equality comparison requires values of the same type");
            }
            if (left.type == Type::region) {
                throw ParseError(line_, "memory regions cannot be compared directly");
            }
            if (left.type.kind == Type::Kind::custom &&
                !custom_types_.at(left.type.name).is_enum) {
                throw ParseError(line_, "class and struct values cannot be compared directly");
            }
            return make_expr_value(
                Type::boolean, "((" + left.cpp + ") " + op + " (" + right.cpp + "))",
                make_expression(Expression::Kind::binary, Type::boolean, op,
                                {left.ast, right.ast}));
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if (!is_numeric(left.type) || !is_numeric(right.type)) {
                throw ParseError(line_, "ordered comparisons require number or int values");
            }
            if (left.type == Type::integer && right.type == Type::integer) {
                return {Type::boolean, "((" + left.cpp + ") " + op + " (" +
                                           right.cpp + "))", std::nullopt,
                        make_expression(Expression::Kind::binary, Type::boolean, op,
                                        {left.ast, right.ast})};
            }
            const auto left_number = as_number_expression(left);
            const auto right_number = as_number_expression(right);
            return {Type::boolean,
                    "((" + as_number(left) + ") " + op + " (" + as_number(right) + "))",
                    std::nullopt,
                    make_expression(Expression::Kind::binary, Type::boolean, op,
                                    {left_number, right_number})};
        }
        if (left.type == Type::integer && right.type == Type::integer) {
            if (op == "-") {
                return make_expr_value(
                    Type::integer, "sun_int_sub(" + left.cpp + ", " + right.cpp + ")",
                    make_expression(Expression::Kind::call, Type::integer, "sun_int_sub",
                                    {left.ast, right.ast}));
            }
            if (op == "*") {
                return make_expr_value(
                    Type::integer, "sun_int_mul(" + left.cpp + ", " + right.cpp + ")",
                    make_expression(Expression::Kind::call, Type::integer, "sun_int_mul",
                                    {left.ast, right.ast}));
            }
            if (op == "/") {
                return make_expr_value(
                    Type::integer, "sun_int_div(" + left.cpp + ", " + right.cpp + ")",
                    make_expression(Expression::Kind::call, Type::integer, "sun_int_div",
                                    {left.ast, right.ast}));
            }
            if (op == "%") {
                return make_expr_value(
                    Type::integer, "sun_int_mod(" + left.cpp + ", " + right.cpp + ")",
                    make_expression(Expression::Kind::call, Type::integer, "sun_int_mod",
                                    {left.ast, right.ast}));
            }
        }
        if (!is_numeric(left.type) || !is_numeric(right.type)) {
            throw ParseError(line_, "arithmetic operators require number or int values");
        }
        if (op == "%") {
            return make_expr_value(
                Type::number, "std::fmod((" + as_number(left) + "), (" +
                                  as_number(right) + "))",
                make_expression(Expression::Kind::call, Type::number, "std::fmod",
                                {as_number_expression(left),
                                 as_number_expression(right)}));
        }
        return make_expr_value(
            Type::number, "((" + as_number(left) + ") " + op + " (" +
                              as_number(right) + "))",
            make_expression(Expression::Kind::binary, Type::number, op,
                            {as_number_expression(left), as_number_expression(right)}));
    }

    static bool is_numeric(Type type) {
        return type == Type::number || type == Type::integer;
    }

    static std::string as_number(const ExprValue& value) {
        return value.type == Type::integer
                   ? "static_cast<double>(" + value.cpp + ")"
                   : value.cpp;
    }

    static Expression as_number_expression(const ExprValue& value) {
        return value.type == Type::integer
                   ? make_expression(Expression::Kind::cast, Type::number, "double",
                                     {value.ast})
                   : value.ast;
    }

    std::vector<Token> tokens_;
    std::size_t position_ = 0;
    std::size_t line_;
    const Symbols& symbols_;
    bool math_imported_;
    bool memory_imported_;
    bool io_imported_;
    const Signatures& signatures_;
    const Types& types_;
    const CustomTypes& custom_types_;
    bool allow_function_calls_;
};

class SunParser {
public:
    explicit SunParser(std::istream& source) {
        std::string text;
        std::size_t number = 0;
        bool in_inline_cpp = false;
        while (std::getline(source, text)) {
            ++number;
            const auto source_text = in_inline_cpp ? text : strip_comment(text);
            const auto normalized = trim(source_text);
            lines_.push_back({number, normalized, text});
            if (!in_inline_cpp && normalized == "inline cpp:") in_inline_cpp = true;
            else if (in_inline_cpp && normalized == "endcpp") in_inline_cpp = false;
        }
    }

    Program parse() {
        Program result;
        std::size_t index = 0;
        bool seen_math = false;
        bool seen_memory = false;
        bool seen_io = false;
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
            } else if (module == "io") {
                if (seen_io) throw ParseError(lines_[index].number, "duplicate import 'io'");
                seen_io = true;
                result.io_imported = true;
            } else {
                throw ParseError(lines_[index].number,
                                 "supported imports are 'math', 'memory', and 'io'");
            }
            ++index;
            skip_ignored(index);
        }
        memory_imported_ = result.memory_imported;
        io_imported_ = result.io_imported;
        prepare_custom_types(index, result);
        int declaration_depth = 0;
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
            if (lines_[i].text == "end") {
                if (declaration_depth > 0) --declaration_depth;
                continue;
            }
            if (declaration_depth == 0 && starts_word(lines_[i].text, "func")) {
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
                    signature.default_expressions.push_back(std::nullopt);
                }
                signature.return_type = function.return_type;
                if (!signatures_.emplace(function.name, signature).second) {
                    throw ParseError(function.line, "function '" + function.name + "' is already defined");
                }
                global_function_lines_.push_back(i);
            }
            if (starts_word(lines_[i].text, "func") ||
                starts_word(lines_[i].text, "if") ||
                starts_word(lines_[i].text, "while") ||
                starts_word(lines_[i].text, "for") ||
                starts_word(lines_[i].text, "match") ||
                lines_[i].text == "try:") {
                ++declaration_depth;
            }
        }
        for (const auto i : global_function_lines_) {
            const auto& line = lines_[i];
            const auto function = parse_function_header(line);
            auto& signature = signatures_.at(function.name);
            Symbols no_symbols;
            for (std::size_t i = 0; i < function.parameters.size(); ++i) {
                const auto& parameter = function.parameters[i];
                if (!parameter.default_value) continue;
                const auto value = ExpressionParser(*parameter.default_value, line.number,
                                                    no_symbols, result.math_imported,
                                                    result.memory_imported, result.io_imported,
                                                    signatures_, types_, custom_types_,
                                                    false).parse();
                if (!can_assign(parameter.type, value.type)) {
                    throw ParseError(line.number, "default for parameter '" + parameter.name +
                                                   "' must have type " +
                                                   cpp_type(parameter.type));
                }
                signature.default_values[i] = converted_value(parameter.type, value.type,
                                                              value.cpp);
                signature.default_expressions[i] =
                    converted_expression(parameter.type, value.type, value.ast);
                if (value.type == Type::empty_list) signature.default_values[i] = "{}";
            }
        }
        result.main_body = parse_main(index, result.math_imported, result.global_cpp);
        result.functions = std::move(functions_);
        return result;
    }

private:
    struct TypeRange {
        std::size_t start;
        std::size_t end;
        std::string kind;
        std::string name;
    };

    void prepare_custom_types(std::size_t first, Program& program) {
        std::vector<TypeRange> ranges;
        for (std::size_t i = first; i < lines_.size(); ++i) {
            const auto& text = lines_[i].text;
            std::string kind;
            if (starts_word(text, "class")) kind = "class";
            else if (starts_word(text, "struct")) kind = "struct";
            else if (starts_word(text, "enum")) kind = "enum";
            else continue;
            const auto name = trim(text.substr(kind.size()));
            if (!is_identifier(name)) {
                throw ParseError(lines_[i].number, "expected " + kind + " name");
            }
            if (types_.find(name) != types_.end()) {
                throw ParseError(lines_[i].number, "type '" + name + "' is already defined");
            }
            std::size_t end = i + 1;
            int depth = 1;
            for (; end < lines_.size(); ++end) {
                const auto& candidate = lines_[end].text;
                if (candidate == "end") {
                    if (--depth == 0) break;
                    continue;
                }
                if (starts_word(candidate, "func") || starts_word(candidate, "if") ||
                    starts_word(candidate, "while") || starts_word(candidate, "for") ||
                    starts_word(candidate, "match") ||
                    candidate == "try:") {
                    ++depth;
                }
            }
            if (end == lines_.size()) {
                throw ParseError(lines_[i].number, kind + " '" + name + "' is missing 'end'");
            }
            const Type custom_type{Type::Kind::custom, name};
            types_.emplace(name, custom_type);
            ranges.push_back({i, end, kind, name});
            i = end;
        }

        for (const auto& range : ranges) {
            CustomTypeDefinition definition;
            definition.name = range.name;
            definition.is_class = range.kind == "class";
            definition.is_enum = range.kind == "enum";
            custom_types_.emplace(range.name, std::move(definition));
        }

        for (const auto& range : ranges) {
            CustomTypeDefinition definition;
            definition.name = range.name;
            definition.is_class = range.kind == "class";
            definition.is_enum = range.kind == "enum";
            std::unordered_map<std::string, bool> seen_members;
            for (std::size_t i = range.start + 1; i < range.end; ++i) {
                const auto& line = lines_[i];
                if (is_ignored(line)) continue;
                if (starts_word(line.text, "func")) {
                    if (!definition.is_class) {
                        throw ParseError(line.number, "only classes can declare methods");
                    }
                    auto method = parse_function_header(line);
                    method.owner_type = range.name;
                    if (method.name == "init" && method.return_type) {
                        throw ParseError(line.number, "class init methods cannot declare a return type");
                    }
                    if (method.name == "init" &&
                        std::any_of(method.parameters.begin(), method.parameters.end(),
                                    [](const Parameter& parameter) {
                                        return parameter.default_value.has_value();
                                    })) {
                        throw ParseError(line.number, "class init parameters cannot have defaults");
                    }
                    if (method.name != "init" &&
                        std::any_of(method.parameters.begin(), method.parameters.end(),
                                    [](const Parameter& parameter) {
                                        return parameter.default_value.has_value();
                                    })) {
                        throw ParseError(line.number,
                                         "class method parameters cannot have defaults");
                    }
                    if (!seen_members.emplace(method.name, true).second) {
                        throw ParseError(line.number, "duplicate class member '" + method.name + "'");
                    }
                    Signature signature;
                    for (const auto& parameter : method.parameters) {
                        signature.parameters.push_back(parameter.type);
                        signature.default_values.push_back(std::nullopt);
                        signature.default_expressions.push_back(std::nullopt);
                    }
                    signature.return_type = method.return_type;
                    if (!signatures_.emplace(range.name + "." + method.name,
                                             std::move(signature)).second) {
                        throw ParseError(line.number, "duplicate class method '" + method.name + "'");
                    }
                    definition.methods.push_back(std::move(method));
                    std::size_t method_end = i + 1;
                    int method_depth = 1;
                    for (; method_end <= range.end; ++method_end) {
                        const auto& nested = lines_[method_end].text;
                        if (nested == "end") {
                            if (--method_depth == 0) break;
                            continue;
                        }
                        if (starts_word(nested, "func") || starts_word(nested, "if") ||
                            starts_word(nested, "while") || starts_word(nested, "for") ||
                            starts_word(nested, "match") ||
                            nested == "try:") {
                            ++method_depth;
                        }
                    }
                    if (method_depth != 0) {
                        throw ParseError(line.number,
                                         "class method '" + definition.methods.back().name +
                                             "' is missing 'end'");
                    }
                    i = method_end;
                    continue;
                }
                if (definition.is_enum) {
                    const auto variant_text = trim(line.text);
                    const auto open = variant_text.find('(');
                    const auto variant_call = open == std::string::npos
                                                  ? std::pair<std::string, std::string>{variant_text, ""}
                                                  : parse_call(variant_text, line.number);
                    const auto variant_name = variant_call.first;
                    if (!is_identifier(variant_name) ||
                        !seen_members.emplace(variant_name, true).second) {
                        throw ParseError(line.number, "expected a unique enum value name");
                    }
                    EnumVariant variant;
                    variant.name = variant_name;
                    if (open != std::string::npos) {
                        std::unordered_map<std::string, bool> seen_fields;
                        for (const auto& field_text :
                             split_arguments(variant_call.second, line.number)) {
                            const auto colon = field_text.find(':');
                            if (colon == std::string::npos) {
                                throw ParseError(line.number,
                                                 "enum payload fields use 'name: type'");
                            }
                            const auto field_name = trim(field_text.substr(0, colon));
                            const auto field_type = parse_type(
                                trim(field_text.substr(colon + 1)), line.number, types_);
                            if (!is_identifier(field_name) ||
                                !seen_fields.emplace(field_name, true).second) {
                                throw ParseError(line.number,
                                                 "invalid or duplicate enum payload field '" +
                                                     field_name + "'");
                            }
                            if (field_type.kind == Type::Kind::custom ||
                                field_type == Type::region || is_list_type(field_type) ||
                                is_dictionary_type(field_type)) {
                                throw ParseError(line.number,
                                                 "enum payload fields must have scalar types");
                            }
                            variant.fields.push_back({field_name, field_type});
                        }
                    }
                    definition.enum_values.push_back(variant_name);
                    definition.enum_variants.push_back(std::move(variant));
                    continue;
                }
                const auto colon = line.text.find(':');
                if (colon == std::string::npos) {
                    throw ParseError(line.number, "class and struct fields use 'name: type'");
                }
                const auto field_name = trim(line.text.substr(0, colon));
                if (!is_identifier(field_name) ||
                    !seen_members.emplace(field_name, true).second) {
                    throw ParseError(line.number, "invalid or duplicate field '" + field_name + "'");
                }
                const auto field_type = parse_type(trim(line.text.substr(colon + 1)),
                                                   line.number, types_);
                if (field_type.kind == Type::Kind::custom ||
                    field_type == Type::region || field_type == Type::empty_list ||
                    is_dictionary_type(field_type)) {
                    throw ParseError(line.number, "unsupported field type");
                }
                definition.fields.push_back({field_name, field_type});
            }
            if (definition.is_enum && definition.enum_values.empty()) {
                throw ParseError(lines_[range.start].number,
                                 "enum '" + range.name + "' must declare at least one value");
            }
            custom_types_[range.name] = definition;
            program.custom_types.push_back(std::move(definition));
        }

        for (const auto& range : ranges) {
            const auto definition = custom_types_.at(range.name);
            for (const auto& method : definition.methods) {
                std::size_t method_line = range.start + 1;
                while (method_line < range.end) {
                    if (starts_word(lines_[method_line].text, "func")) {
                        const auto header = trim(lines_[method_line].text.substr(4));
                        const auto close = find_call_close(header, lines_[method_line].number);
                        if (parse_call(header.substr(0, close + 1),
                                       lines_[method_line].number).first == method.name) break;
                    }
                    ++method_line;
                }
                if (method_line == range.end) {
                    throw ParseError(lines_[range.start].number,
                                     "could not locate class method '" + method.name + "'");
                }
                Symbols symbols;
                symbols.emplace("self", Symbol{types_.at(range.name), "(*this)"});
                for (const auto& parameter : method.parameters) {
                    symbols.emplace(parameter.name,
                                    Symbol{parameter.type, "sun_arg_" + parameter.name});
                }
                auto function = method;
                ++method_line;
                const auto previous_return_type = active_return_type_;
                const auto previous_function_state = inside_function_;
                active_return_type_ = function.return_type;
                inside_function_ = true;
                bool definitely_returns = false;
                function.body = parse_block(method_line, symbols, program.math_imported,
                                            &definitely_returns);
                active_return_type_ = previous_return_type;
                inside_function_ = previous_function_state;
                if (method_line == range.end) {
                    throw ParseError(lines_[range.start].number,
                                     "class method '" + function.name + "' is missing 'end'");
                }
                if (function.return_type && !definitely_returns) {
                    throw ParseError(function.line,
                                     "method '" + function.name +
                                         "' must return a value on every path");
                }
                functions_.push_back(std::move(function));
            }
            for (std::size_t i = range.start; i <= range.end; ++i) {
                lines_[i].text.clear();
                lines_[i].raw_text.clear();
            }
        }
        for (auto& definition : program.custom_types) {
            for (auto& method : definition.methods) {
                for (const auto& function : functions_) {
                    if (function.owner_type == definition.name &&
                        function.name == method.name) {
                        method = function;
                        break;
                    }
                }
            }
            custom_types_[definition.name] = definition;
        }
    }

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
            return_type = parse_type(trim(return_annotation.substr(1)), line.number, types_);
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
                                  : parse_type(trim(declaration.substr(colon + 1)), line.number,
                                               types_);
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
        return text == "end" || starts_word(text, "elif") || starts_word(text, "else") ||
               starts_word(text, "catch");
    }

    void skip_ignored(std::size_t& index) const {
        while (index < lines_.size() && is_ignored(lines_[index])) ++index;
    }

    Block parse_main(std::size_t& index, bool math_imported,
                     std::vector<std::string>& global_cpp) {
        Symbols symbols;
        Block body;
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
            const auto statement_line = lines_[index].number;
            auto statement = parse_statement(index, symbols, math_imported, nullptr);
            statement.line = statement_line;
            body.push_back(std::move(statement));
        }
        return body;
    }

    Block parse_block(std::size_t& index, Symbols& symbols, bool math_imported,
                      bool* definitely_returns = nullptr, bool stop_at_case = false) {
        local_function_scopes_.emplace_back();
        Block body;
        bool returns = false;
        while (index < lines_.size()) {
            skip_ignored(index);
            if (index == lines_.size() || is_terminator(lines_[index].text) ||
                (stop_at_case && starts_word(lines_[index].text, "case"))) break;
            if (starts_word(lines_[index].text, "func")) {
                const auto statement_line = lines_[index].number;
                auto statement = parse_local_function(index, symbols, math_imported);
                statement.line = statement_line;
                body.push_back(std::move(statement));
                continue;
            }
            if (starts_word(lines_[index].text, "import")) {
                throw ParseError(lines_[index].number, "imports must appear before declarations and statements");
            }
            bool statement_returns = false;
            const auto statement_line = lines_[index].number;
            auto statement = parse_statement(index, symbols, math_imported,
                                             &statement_returns);
            statement.line = statement_line;
            body.push_back(std::move(statement));
            returns = returns || statement_returns;
        }
        const auto local_functions = std::move(local_function_scopes_.back());
        local_function_scopes_.pop_back();
        for (const auto& name : local_functions) signatures_.erase(name);
        if (definitely_returns) *definitely_returns = returns;
        return body;
    }

    Statement parse_local_function(std::size_t& index, const Symbols& symbols,
                                   bool math_imported) {
        const auto line = lines_[index];
        auto function = parse_function_header(line);
        if (function.name == "print" || function.name == "istype") {
            throw ParseError(line.number, "function name '" + function.name + "' is reserved");
        }
        if (signatures_.find(function.name) != signatures_.end()) {
            throw ParseError(line.number, "function '" + function.name +
                                             "' is already visible in this scope");
        }
        if (std::any_of(function.parameters.begin(), function.parameters.end(),
                        [](const Parameter& parameter) {
                            return parameter.default_value.has_value();
                        })) {
            throw ParseError(line.number,
                             "nested function parameters cannot have default values");
        }

        const auto id = next_local_function_id_++;
        const auto cpp_name = "sun_local_function_" + std::to_string(id);
        Signature signature;
        for (const auto& parameter : function.parameters) {
            signature.parameters.push_back(parameter.type);
            signature.default_values.push_back(std::nullopt);
            signature.default_expressions.push_back(std::nullopt);
        }
        signature.return_type = function.return_type;
        signature.cpp_name = cpp_name;
        signatures_.emplace(function.name, std::move(signature));
        local_function_scopes_.back().push_back(function.name);

        Symbols function_symbols = symbols;
        for (const auto& parameter : function.parameters) {
            function_symbols[parameter.name] =
                Symbol{parameter.type, "sun_arg_" + parameter.name};
        }
        ++index;
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
            throw ParseError(error_line, "nested function '" + function.name +
                                             "' is missing 'end'");
        }
        ++index;
        if (function.return_type && !definitely_returns) {
            throw ParseError(function.line,
                             "nested function '" + function.name +
                                 "' must return a value on every path");
        }
        return Statement{
            LocalFunctionStatement{cpp_name, std::move(function.parameters),
                                   function.return_type, std::move(function.body)},
            line.number};
    }

    ExprValue expression(const std::string& text, std::size_t line, const Symbols& symbols,
                         bool math_imported) const {
        return ExpressionParser(text, line, symbols, math_imported, memory_imported_, io_imported_,
                                signatures_, types_, custom_types_).parse();
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

    Statement parse_if(std::size_t& index, const Symbols& symbols, bool math_imported,
                       bool* definitely_returns) {
        const auto first_line = lines_[index];
        auto condition = parse_condition(first_line, "if", symbols, math_imported);
        ++index;
        Symbols branch_symbols = symbols;
        bool branch_returns = false;
        auto branch = parse_block(index, branch_symbols, math_imported, &branch_returns);
        bool all_branches_return = branch_returns;
        bool has_else = false;

        IfStatement statement;
        statement.branches.push_back({condition.cpp, std::move(branch)});
        while (index < lines_.size() && starts_word(lines_[index].text, "elif")) {
            const auto branch_line = lines_[index];
            auto elif_condition = parse_condition(branch_line, "elif", symbols, math_imported);
            ++index;
            branch_symbols = symbols;
            branch_returns = false;
            branch = parse_block(index, branch_symbols, math_imported, &branch_returns);
            all_branches_return = all_branches_return && branch_returns;
            statement.branches.push_back({elif_condition.cpp, std::move(branch)});
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
            statement.else_body = std::move(branch);
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
        return Statement{std::move(statement), first_line.number};
    }

    Statement parse_match(std::size_t& index, const Symbols& symbols, bool math_imported,
                          bool* definitely_returns) {
        const auto line = lines_[index];
        const auto header = trim(line.text.substr(5));
        if (header.empty() || header.back() != ':') {
            throw ParseError(line.number, "match value must end with ':'");
        }
        const auto value = expression(trim(header.substr(0, header.size() - 1)),
                                      line.number, symbols, math_imported);
        const auto enum_type = custom_types_.find(value.type.name);
        if (value.type.kind == Type::Kind::custom &&
            (enum_type == custom_types_.end() || !enum_type->second.is_enum)) {
            throw ParseError(line.number, "match requires a scalar or enum value");
        }
        if (value.type.kind != Type::Kind::custom &&
            value.type != Type::number && value.type != Type::integer &&
            value.type != Type::boolean && value.type != Type::string &&
            value.type != Type::character) {
            throw ParseError(line.number, "match requires a scalar or enum value");
        }

        MatchStatement statement;
        statement.value = value.ast;
        statement.temporary_name = "sun_match_value_" + std::to_string(line.number);
        ++index;
        bool has_case = false;
        bool has_else = false;
        bool all_cases_return = true;
        std::size_t case_index = 0;
        while (index < lines_.size()) {
            skip_ignored(index);
            if (index == lines_.size() || lines_[index].text == "end") break;
            const auto case_line = lines_[index];
            if (has_else) {
                throw ParseError(case_line.number, "no case may follow match 'else'");
            }
            if (starts_word(case_line.text, "else")) {
                if (case_line.text != "else:") {
                    throw ParseError(case_line.number, "expected 'else:'");
                }
                has_else = true;
                ++index;
                auto branch_symbols = symbols;
                bool branch_returns = false;
                statement.else_body = parse_block(index, branch_symbols, math_imported,
                                                  &branch_returns);
                all_cases_return = all_cases_return && branch_returns;
                break;
            }
            if (!starts_word(case_line.text, "case")) {
                throw ParseError(case_line.number, "expected 'case pattern:' or 'else:'");
            }
            const auto case_header = trim(case_line.text.substr(4));
            if (case_header.empty() || case_header.back() != ':') {
                throw ParseError(case_line.number, "expected 'case pattern:'");
            }
            const auto pattern_text = trim(case_header.substr(0, case_header.size() - 1));
            if (pattern_text.empty()) {
                throw ParseError(case_line.number, "case pattern cannot be empty");
            }

            MatchCase match_case;
            Symbols branch_symbols = symbols;
            const auto open = pattern_text.find('(');
            const auto qualified = trim(pattern_text.substr(0, open));
            const auto dot = qualified.find('.');
            bool enum_variant_pattern = false;
            if (value.type.kind == Type::Kind::custom && dot != std::string::npos) {
                const auto type_name = trim(qualified.substr(0, dot));
                const auto variant_name = trim(qualified.substr(dot + 1));
                if (type_name == value.type.name && is_identifier(variant_name)) {
                    const auto variant = std::find_if(
                        enum_type->second.enum_variants.begin(),
                        enum_type->second.enum_variants.end(),
                        [&](const EnumVariant& candidate) {
                            return candidate.name == variant_name;
                        });
                    if (variant == enum_type->second.enum_variants.end()) {
                        throw ParseError(case_line.number, "unknown enum variant '" +
                                                               variant_name + "'");
                    }
                    const auto payload_enum = std::any_of(
                        enum_type->second.enum_variants.begin(),
                        enum_type->second.enum_variants.end(),
                        [](const EnumVariant& candidate) {
                            return !candidate.fields.empty();
                        });
                    if (payload_enum) {
                        enum_variant_pattern = true;
                        std::vector<std::string> bindings;
                        if (open != std::string::npos) {
                            if (pattern_text.back() != ')') {
                                throw ParseError(case_line.number,
                                                 "expected ')' after enum payload pattern");
                            }
                            bindings = split_arguments(
                                pattern_text.substr(open + 1,
                                                    pattern_text.size() - open - 2),
                                case_line.number);
                        } else if (!variant->fields.empty()) {
                            throw ParseError(case_line.number, "enum variant '" +
                                                                   variant_name +
                                                                   "' requires payload bindings");
                        }
                        if (bindings.size() != variant->fields.size()) {
                            throw ParseError(case_line.number, "enum variant '" +
                                                                   variant_name +
                                                                   "' pattern expects " +
                                                                   std::to_string(variant->fields.size()) +
                                                                   " binding(s)");
                        }
                        match_case.enum_variant_type =
                            "SunType_" + type_name + '_' + variant_name;
                        std::unordered_map<std::string, bool> seen_bindings;
                        for (std::size_t i = 0; i < bindings.size(); ++i) {
                            const auto binding = trim(bindings[i]);
                            if (binding == "_") continue;
                            if (!is_identifier(binding) ||
                                !seen_bindings.emplace(binding, true).second) {
                                throw ParseError(case_line.number,
                                                 "invalid or duplicate enum payload binding '" +
                                                     binding + "'");
                            }
                            if (symbols.find(binding) != symbols.end()) {
                                throw ParseError(case_line.number,
                                                 "match binding '" + binding +
                                                     "' shadows a value in this scope");
                            }
                            const auto cpp_name = "sun_match_binding_" +
                                std::to_string(line.number) + '_' +
                                std::to_string(case_index) + '_' + std::to_string(i);
                            branch_symbols.emplace(
                                binding, Symbol{variant->fields[i].type, cpp_name});
                            match_case.bindings.push_back(
                                {cpp_name, variant->fields[i].name});
                        }
                    }
                }
            }
            if (!enum_variant_pattern) {
                const auto pattern = expression(pattern_text, case_line.number,
                                                symbols, math_imported);
                if (pattern.type != value.type) {
                    throw ParseError(case_line.number,
                                     "case pattern type must match the match value");
                }
                const bool character_literal =
                    pattern.type == Type::character &&
                    pattern.ast.kind == Expression::Kind::call &&
                    pattern.ast.value == "sun_to_char" &&
                    pattern.ast.children.size() == 1 &&
                    pattern.ast.children.front().kind == Expression::Kind::string_literal;
                const bool integer_literal =
                    pattern.type == Type::integer &&
                    pattern.ast.kind == Expression::Kind::call &&
                    pattern.ast.value == "sun_to_int" &&
                    pattern.ast.children.size() == 1 &&
                    pattern.ast.children.front().kind == Expression::Kind::literal;
                if (pattern.ast.kind != Expression::Kind::literal &&
                    pattern.ast.kind != Expression::Kind::string_literal &&
                    !character_literal &&
                    !integer_literal &&
                    !(value.type.kind == Type::Kind::custom &&
                      pattern.ast.kind == Expression::Kind::variable)) {
                    throw ParseError(case_line.number,
                                     "case patterns must be literals or enum values");
                }
                match_case.pattern = pattern.ast;
            }
            ++index;
            bool branch_returns = false;
            match_case.body = parse_block(index, branch_symbols, math_imported,
                                          &branch_returns, true);
            all_cases_return = all_cases_return && branch_returns;
            statement.cases.push_back(std::move(match_case));
            has_case = true;
            ++case_index;
        }
        if (!has_case) {
            throw ParseError(line.number, "match must contain at least one case");
        }
        if (index == lines_.size() || lines_[index].text != "end") {
            const auto error_line = lines_.empty() ? 1 : lines_.back().number + 1;
            throw ParseError(error_line, "match block is missing 'end'");
        }
        ++index;
        if (definitely_returns) {
            *definitely_returns = has_else && all_cases_return;
        }
        return Statement{std::move(statement), line.number};
    }

    Statement parse_try(std::size_t& index, const Symbols& symbols, bool math_imported,
                        bool* definitely_returns) {
        const auto line = lines_[index];
        if (line.text != "try:") throw ParseError(line.number, "expected 'try:'");
        ++index;
        auto try_symbols = symbols;
        bool try_returns = false;
        const auto try_body = parse_block(index, try_symbols, math_imported, &try_returns);
        if (index == lines_.size() || !starts_word(lines_[index].text, "catch")) {
            throw ParseError(line.number, "try block must be followed by 'catch name:'");
        }
        const auto catch_line = lines_[index];
        const auto catch_header = trim(catch_line.text.substr(5));
        if (catch_header.size() < 2 || catch_header.back() != ':') {
            throw ParseError(catch_line.number, "expected 'catch name:'");
        }
        const auto name = trim(catch_header.substr(0, catch_header.size() - 1));
        if (!is_identifier(name)) {
            throw ParseError(catch_line.number, "invalid catch variable '" + name + "'");
        }
        if (symbols.find(name) != symbols.end()) {
            throw ParseError(catch_line.number, "catch variable '" + name +
                                                "' shadows a value in this scope");
        }
        const auto id = next_catch_id_++;
        const auto cpp_name = "sun_caught_error_" + std::to_string(id);
        ++index;
        auto catch_symbols = symbols;
        catch_symbols.emplace(name, Symbol{Type::string, "sun_catch_message_" +
                                                          std::to_string(id)});
        bool catch_returns = false;
        const auto catch_body = parse_block(index, catch_symbols, math_imported,
                                            &catch_returns);
        if (index == lines_.size() || lines_[index].text != "end") {
            const auto error_line = lines_.empty() ? 1 : lines_.back().number + 1;
            throw ParseError(error_line, "try block is missing 'end'");
        }
        ++index;
        if (definitely_returns) *definitely_returns = try_returns && catch_returns;
        return Statement{TryCatchStatement{cpp_name, "sun_catch_message_" +
                                                          std::to_string(id),
                                            std::move(try_body), std::move(catch_body)},
                         line.number};
    }

    Statement parse_while(std::size_t& index, Symbols& symbols, bool math_imported) {
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
        return Statement{WhileStatement{condition.cpp, std::move(body)}, line.number};
    }

    Statement parse_for(std::size_t& index, Symbols& symbols, bool math_imported) {
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
        if (range.rfind("range(", 0) != 0) {
            return parse_for_each(index, symbols, math_imported, name, range);
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
            if (value.type != Type::number && value.type != Type::integer) {
                throw ParseError(line.number, "range arguments must have type number or int");
            }
            if (value.type == Type::integer) {
                value.cpp = "static_cast<double>(" + value.cpp + ")";
                value.type = Type::number;
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

        return Statement{RangeForStatement{start_name, start_value, stop_name, stop_value,
                                           step_name, step_value, variable_name,
                                           std::move(body)},
                         line.number};
    }

    Statement parse_for_each(std::size_t& index, const Symbols& symbols,
                             bool math_imported, const std::string& name,
                             const std::string& collection_text) {
        const auto line = lines_[index];
        const auto collection = expression(collection_text, line.number, symbols,
                                           math_imported);
        const bool dictionary = is_dictionary_type(collection.type);
        if (!is_list_type(collection.type) && !dictionary) {
            throw ParseError(line.number, "for-in loops require a list, dictionary, or range(...)");
        }
        const auto id = next_loop_id_++;
        const auto variable_name = "sun_each_" + std::to_string(id) + "_" + name;
        auto loop_symbols = symbols;
        const auto item_type = dictionary
                                   ? dictionary_types(collection.type).first
                                   : list_item_type(collection.type);
        loop_symbols[name] = {item_type, variable_name};
        const auto entry_name = dictionary ? "sun_each_entry_" + std::to_string(id) : "";
        ++index;
        ++loop_depth_;
        auto body = parse_block(index, loop_symbols, math_imported);
        --loop_depth_;
        if (index == lines_.size() || lines_[index].text != "end") {
            const auto error_line = lines_.empty() ? 1 : lines_.back().number + 1;
            throw ParseError(error_line, "for block is missing 'end'");
        }
        ++index;
        return Statement{ForEachStatement{variable_name, collection.cpp, dictionary,
                                          entry_name, std::move(body)},
                         line.number};
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
        const auto type = parse_type(trim(typed_name.substr(colon + 1)), line.number,
                                     types_);
        if (type == Type::region && !memory_imported_) {
            throw ParseError(line.number, "the region type requires 'import memory'");
        }
        const auto value = expression(trim(declaration.substr(equals + 1)), line.number,
                                      symbols, math_imported);
        if (!can_assign(type, value.type)) {
            throw ParseError(line.number, "cannot initialize " + cpp_type(type) +
                                           " with a " + cpp_type(value.type) + " value");
        }
        symbols.emplace(name, Symbol{type, "sun_var_" + name});
        const auto initial_value = value.type == Type::empty_list ||
                                           value.type == Type::empty_dictionary
                                       ? "{}"
            : converted_value(type, value.type, value.cpp);
        return cpp_type(type) + " sun_var_" + name + " = " + initial_value + ";";
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
        if (!can_assign(found->second.type, value.type)) {
            throw ParseError(line.number, "cannot assign " + cpp_type(value.type) +
                                           " to " + cpp_type(found->second.type) + " value '" +
                                           name + "'");
        }
        const auto assigned = value.type == Type::empty_list ||
                                      value.type == Type::empty_dictionary
                                  ? "{}"
            : converted_value(found->second.type, value.type, value.cpp);
        return found->second.cpp_name + " = " + assigned + ";";
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
            if (!can_assign(*active_return_type_, value.type)) {
                throw ParseError(line.number, "return type mismatch: expected " +
                                               cpp_type(*active_return_type_) + ", got " +
                                               cpp_type(value.type));
            }
            if (returns) *returns = true;
            const auto returned = value.type == Type::empty_list ||
                                          value.type == Type::empty_dictionary
                                      ? "{}"
                : converted_value(*active_return_type_, value.type, value.cpp);
            return "return " + returned + ";";
        }
        if (!value_text.empty()) {
            throw ParseError(line.number, "void function cannot return a value");
        }
        if (returns) *returns = true;
        return "return;";
    }

    PrintStatement parse_print(const SourceLine& line, const std::string& arguments_text,
                               const Symbols& symbols, bool math_imported) const {
        const auto arguments = split_arguments(arguments_text, line.number);
        if (arguments.empty()) throw ParseError(line.number, "expected print(expression)");
        const auto first = expression(arguments.front(), line.number, symbols, math_imported);
        if (first.type == Type::region) {
            throw ParseError(line.number, "memory regions cannot be printed directly");
        }
        if (first.type.kind == Type::Kind::custom &&
            !custom_types_.at(first.type.name).is_enum) {
            throw ParseError(line.number, "class and struct values cannot be printed directly");
        }
        if (first.type == Type::empty_list || first.type == Type::empty_dictionary) {
            throw ParseError(line.number, "an empty collection needs a declared type before printing");
        }
        auto value = first.ast;
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
                if (values.back().type.kind == Type::Kind::custom &&
                    !custom_types_.at(values.back().type.name).is_enum) {
                    throw ParseError(line.number,
                                     "class and struct values cannot be formatted directly");
                }
            }
            std::vector<Expression> format_parts;
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
                        format_parts.push_back(make_expression(
                            Expression::Kind::string_literal, Type::string, literal));
                        literal.clear();
                    }
                    format_parts.push_back(make_expression(
                        Expression::Kind::call, Type::string, "sun_to_string",
                        {values[value_index].ast}));
                    ++value_index;
                    i += 2;
                } else {
                    throw ParseError(line.number, "unsupported formatted print placeholder; use %v or %%");
                }
            }
            if (!literal.empty() || format_parts.empty()) {
                format_parts.push_back(make_expression(
                    Expression::Kind::string_literal, Type::string, literal));
            }
            if (value_index != values.size()) {
                throw ParseError(line.number, "too many %v values for formatted print");
            }
            auto formatted_ast = format_parts.front();
            for (std::size_t i = 1; i < format_parts.size(); ++i) {
                formatted_ast = make_expression(
                    Expression::Kind::binary, Type::string, "+",
                    {std::move(formatted_ast), std::move(format_parts[i])});
            }
            value = std::move(formatted_ast);
        }
        return {std::move(value)};
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
        if (region.type != Type::region ||
            (offset.type != Type::number && offset.type != Type::integer) ||
            (value.type != Type::number && value.type != Type::integer)) {
            throw ParseError(line.number,
                             "memory.write_u8 expects (region, number or int offset, number or int value)");
        }
        const auto numeric = [](const ExprValue& value) {
            return value.type == Type::integer
                       ? "static_cast<double>(" + value.cpp + ")"
                       : value.cpp;
        };
        return "(" + region.cpp + ").write_u8(" + numeric(offset) + ", " +
               numeric(value) + ");";
    }

    std::string parse_io_statement(const SourceLine& line, const std::string& text,
                                   const Symbols& symbols, bool math_imported) const {
        if (!io_imported_) {
            throw ParseError(line.number, "io functions require 'import io'");
        }
        const auto call = parse_call(text.substr(3), line.number);
        if (call.first != "write_text" && call.first != "append_text") {
            throw ParseError(line.number,
                             "only io.write_text and io.append_text are standalone io statements");
        }
        const auto arguments = split_arguments(call.second, line.number);
        if (arguments.size() != 2) {
            throw ParseError(line.number, "io." + call.first + " expects (path, contents)");
        }
        const auto path = expression(arguments[0], line.number, symbols, math_imported);
        const auto contents = expression(arguments[1], line.number, symbols, math_imported);
        if (path.type != Type::string || contents.type != Type::string) {
            throw ParseError(line.number, "io." + call.first + " expects string path and contents");
        }
        return "sun_io_" + call.first + "(" + path.cpp + ", " + contents.cpp + ");";
    }

    std::string parse_list_append(const SourceLine& line, const std::string& text,
                                  const Symbols& symbols, bool math_imported) const {
        const auto method = text.find(".append(");
        const auto list_name = trim(text.substr(0, method));
        if (!is_identifier(list_name)) {
            throw ParseError(line.number, "append must be called on a list variable");
        }
        const auto list = symbols.find(list_name);
        if (list == symbols.end() || !is_list_type(list->second.type)) {
            throw ParseError(line.number, "append must be called on a declared list");
        }
        const auto call = parse_call(text.substr(method + 1), line.number);
        if (call.first != "append") {
            throw ParseError(line.number, "expected list.append(value)");
        }
        const auto arguments = split_arguments(call.second, line.number);
        if (arguments.size() != 1) {
            throw ParseError(line.number, "list.append expects one item");
        }
        const auto value = expression(arguments.front(), line.number, symbols,
                                      math_imported);
        const auto item_type = list_item_type(list->second.type);
        if (!can_assign(item_type, value.type)) {
            throw ParseError(line.number, "list.append item must have type " +
                                           cpp_type(item_type));
        }
        const auto item = converted_value(item_type, value.type, value.cpp);
        return "sun_list_append(" + list->second.cpp_name + ", " + item + ");";
    }

    Statement parse_index_assignment(const SourceLine& line, const std::string& text,
                                     const Symbols& symbols, bool math_imported) const {
        const auto equals = find_assignment_operator(text);
        const auto left = expression(trim(text.substr(0, equals)), line.number,
                                     symbols, math_imported);
        const bool dictionary_index =
            left.ast.kind == Expression::Kind::call && left.ast.value == "sun_dict_at";
        if (left.cpp.rfind("sun_list_at(", 0) != 0 && !dictionary_index) {
            throw ParseError(line.number, "indexed assignment requires list[index] or dictionary[key]");
        }
        const auto value = expression(trim(text.substr(equals + 1)), line.number,
                                      symbols, math_imported);
        if (!can_assign(left.type, value.type)) {
            throw ParseError(line.number, "cannot assign " + cpp_type(value.type) +
                                           " to indexed value of type " + cpp_type(left.type));
        }
        if (dictionary_index) {
            return Statement{
                DictionaryAssignmentStatement{
                    left.ast.children.at(0), left.ast.children.at(1),
                    converted_expression(left.type, value.type, value.ast)},
                line.number};
        }
        return left.cpp + " = " +
               converted_value(left.type, value.type, value.cpp) + ";";
    }

    std::string parse_member_assignment(const SourceLine& line, const std::string& text,
                                        const Symbols& symbols, bool math_imported) const {
        const auto equals = find_assignment_operator(text);
        const auto left = expression(trim(text.substr(0, equals)), line.number,
                                     symbols, math_imported);
        if (left.cpp.find(").") == std::string::npos) {
            throw ParseError(line.number, "member assignment requires object.field");
        }
        const auto value = expression(trim(text.substr(equals + 1)), line.number,
                                      symbols, math_imported);
        if (!can_assign(left.type, value.type)) {
            throw ParseError(line.number, "cannot assign " + cpp_type(value.type) +
                                           " to field of type " + cpp_type(left.type));
        }
        return left.cpp + " = " + converted_value(left.type, value.type, value.cpp) + ";";
    }

    std::string parse_method_statement(const SourceLine& line, const std::string& text,
                                      const Symbols& symbols, bool math_imported) const {
        const auto dot = text.find('.');
        const auto open = text.find('(', dot == std::string::npos ? 0 : dot);
        if (dot == std::string::npos || open == std::string::npos) {
            throw ParseError(line.number, "expected object.method(...)");
        }
        const auto object = expression(trim(text.substr(0, dot)), line.number,
                                       symbols, math_imported);
        if (object.type.kind != Type::Kind::custom) {
            throw ParseError(line.number, "method call requires a class or struct value");
        }
        const auto method = trim(text.substr(dot + 1, open - dot - 1));
        if (!is_identifier(method)) {
            throw ParseError(line.number, "invalid method name");
        }
        const auto call = parse_call(method + text.substr(open), line.number);
        const auto signature = signatures_.find(object.type.name + "." + method);
        if (signature == signatures_.end()) {
            throw ParseError(line.number, "unknown method '" + method +
                                           "' on '" + object.type.name + "'");
        }
        if (signature->second.return_type) {
            throw ParseError(line.number, "method '" + method +
                                           "' returns a value; use it in an expression");
        }
        std::vector<ExprValue> arguments;
        for (const auto& argument : split_arguments(call.second, line.number)) {
            arguments.push_back(expression(argument, line.number, symbols, math_imported));
        }
        validate_call_arity(method, signature->second, arguments.size(), line.number);
        std::ostringstream code;
        code << "(" << object.cpp << ")." << method << '(';
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (!can_assign(signature->second.parameters[i], arguments[i].type)) {
                throw ParseError(line.number, "argument " + std::to_string(i + 1) +
                                               " to '" + method + "' must have type " +
                                               cpp_type(signature->second.parameters[i]));
            }
            if (i) code << ", ";
            code << converted_value(signature->second.parameters[i], arguments[i].type,
                                    arguments[i].cpp);
        }
        for (std::size_t i = arguments.size();
             i < signature->second.parameters.size(); ++i) {
            if (i) code << ", ";
            code << *signature->second.default_values[i];
        }
        code << ");";
        return code.str();
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
        code << (signature->second.cpp_name.empty()
                     ? "sun_fn_" + call.first
                     : signature->second.cpp_name)
             << '(';
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!can_assign(signature->second.parameters[i], values[i].type)) {
                throw ParseError(line.number, "argument " + std::to_string(i + 1) +
                                               " of '" + call.first + "' must have type " +
                                               cpp_type(signature->second.parameters[i]));
            }
            if (i) code << ", ";
            code << converted_value(signature->second.parameters[i],
                                    values[i].type, values[i].cpp);
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

    Statement parse_statement(std::size_t& index, Symbols& symbols, bool math_imported,
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
        if (starts_word(text, "match")) {
            return parse_match(index, symbols, math_imported, definitely_returns);
        }
        if (starts_word(text, "try")) {
            auto result = parse_try(index, symbols, math_imported, definitely_returns);
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
        if (text.rfind("io.", 0) == 0) {
            auto result = parse_io_statement(line, text, symbols, math_imported);
            ++index;
            return result;
        }
        const auto append_method = text.find(".append(");
        if (append_method != std::string::npos &&
            is_identifier(trim(text.substr(0, append_method)))) {
            auto result = parse_list_append(line, text, symbols, math_imported);
            ++index;
            return result;
        }
        const auto assignment = find_assignment_operator(text);
        if (assignment != std::string::npos &&
            text.substr(0, assignment).find('[') != std::string::npos) {
            auto result = parse_index_assignment(line, text, symbols, math_imported);
            ++index;
            return result;
        }
        if (assignment != std::string::npos &&
            text.substr(0, assignment).find('.') != std::string::npos) {
            auto result = parse_member_assignment(line, text, symbols, math_imported);
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
            return Statement{std::move(result), line.number};
        }
        const auto method_dot = text.find('.');
        const auto method_object = method_dot == std::string::npos
            ? symbols.end() : symbols.find(trim(text.substr(0, method_dot)));
        if (method_object != symbols.end() &&
            method_object->second.type.kind == Type::Kind::custom &&
            text.find('(', method_dot) != std::string::npos) {
            auto result = parse_method_statement(line, text, symbols, math_imported);
            ++index;
            return result;
        }
        auto result = parse_user_call(line, text, symbols, math_imported);
        ++index;
        return result;
    }

    std::vector<SourceLine> lines_;
    Signatures signatures_;
    std::vector<std::size_t> global_function_lines_;
    std::vector<Function> functions_;
    std::vector<std::vector<std::string>> local_function_scopes_;
    std::optional<Type> active_return_type_;
    bool inside_function_ = false;
    std::size_t next_loop_id_ = 0;
    std::size_t next_catch_id_ = 0;
    std::size_t next_local_function_id_ = 0;
    std::size_t loop_depth_ = 0;
    bool memory_imported_ = false;
    bool io_imported_ = false;
    Types types_;
    CustomTypes custom_types_;
};

} // namespace

std::string cpp_type(Type type) {
    if (type.kind == Type::Kind::custom) return "SunType_" + type.name;
    switch (type.kind) {
    case Type::Kind::string: return "std::string";
    case Type::Kind::number: return "double";
    case Type::Kind::boolean: return "bool";
    case Type::Kind::region: return "SunMemoryRegion";
    case Type::Kind::integer: return "std::int64_t";
    case Type::Kind::character: return "std::string";
    case Type::Kind::list_number: return "std::vector<double>";
    case Type::Kind::list_integer: return "std::vector<std::int64_t>";
    case Type::Kind::list_string: return "std::vector<std::string>";
    case Type::Kind::list_boolean: return "std::vector<bool>";
    case Type::Kind::list_character: return "std::vector<std::string>";
    case Type::Kind::empty_list: return "std::vector<int>";
    case Type::Kind::dictionary: {
        const auto types = dictionary_types(type);
        return "std::map<" + cpp_type(types.first) + ", " + cpp_type(types.second) + ">";
    }
    case Type::Kind::empty_dictionary: return "std::map<int, int>";
    case Type::Kind::custom: break;
    }
    throw std::logic_error("invalid Sun type");
}

Program parse(std::istream& source) {
    return SunParser(source).parse();
}

} // namespace sun
