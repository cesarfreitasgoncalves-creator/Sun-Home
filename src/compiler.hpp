#pragma once

#include <cstddef>
#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace sun {

struct Type {
    enum class Kind {
        string,
        number,
        boolean,
        region,
        integer,
        character,
        list_number,
        list_integer,
        list_string,
        list_boolean,
        list_character,
        empty_list,
        dictionary,
        empty_dictionary,
        custom
    };

    Kind kind;
    std::string name;

    Type() : kind(Kind::string) {}
    Type(Kind kind) : kind(kind) {}
    Type(Kind kind, std::string name) : kind(kind), name(std::move(name)) {}

    bool operator==(const Type& other) const {
        return kind == other.kind && name == other.name;
    }

    bool operator!=(const Type& other) const { return !(*this == other); }

    static const Type string;
    static const Type number;
    static const Type boolean;
    static const Type region;
    static const Type integer;
    static const Type character;
    static const Type list_number;
    static const Type list_integer;
    static const Type list_string;
    static const Type list_boolean;
    static const Type list_character;
    static const Type empty_list;
    static const Type empty_dictionary;
};

inline const Type Type::string{Type::Kind::string};
inline const Type Type::number{Type::Kind::number};
inline const Type Type::boolean{Type::Kind::boolean};
inline const Type Type::region{Type::Kind::region};
inline const Type Type::integer{Type::Kind::integer};
inline const Type Type::character{Type::Kind::character};
inline const Type Type::list_number{Type::Kind::list_number};
inline const Type Type::list_integer{Type::Kind::list_integer};
inline const Type Type::list_string{Type::Kind::list_string};
inline const Type Type::list_boolean{Type::Kind::list_boolean};
inline const Type Type::list_character{Type::Kind::list_character};
inline const Type Type::empty_list{Type::Kind::empty_list};
inline const Type Type::empty_dictionary{Type::Kind::empty_dictionary};

struct ParseError : std::runtime_error {
    ParseError(std::size_t line, const std::string& message)
        : std::runtime_error(message), line(line) {}
    std::size_t line;
};

struct Parameter {
    std::string name;
    Type type;
    std::optional<std::string> default_value = std::nullopt;
};

struct Statement;
using Block = std::vector<Statement>;

struct RawStatement {
    std::string cpp;
};

struct Expression {
    enum class Kind {
        literal,
        string_literal,
        variable,
        unary,
        binary,
        call,
        member,
        member_call,
        list,
        aggregate,
        cast,
        empty_list,
        dictionary,
        empty_dictionary
    };

    Kind kind = Kind::literal;
    Type type;
    std::string value;
    std::vector<Expression> children;
};

struct PrintStatement {
    Expression value;
};

struct DictionaryAssignmentStatement {
    Expression dictionary;
    Expression key;
    Expression value;
};

struct IfBranch {
    std::string condition;
    Block body;
};

struct IfStatement {
    std::vector<IfBranch> branches;
    std::optional<Block> else_body;
};

struct MatchBinding {
    std::string cpp_name;
    std::string field_name;
};

struct MatchCase {
    Expression pattern;
    std::string enum_variant_type;
    std::vector<MatchBinding> bindings;
    Block body;
};

struct MatchStatement {
    Expression value;
    std::string temporary_name;
    std::vector<MatchCase> cases;
    std::optional<Block> else_body;
};

struct LocalFunctionStatement {
    std::string cpp_name;
    std::vector<Parameter> parameters;
    std::optional<Type> return_type;
    Block body;
};

struct WhileStatement {
    std::string condition;
    Block body;
};

struct RangeForStatement {
    std::string start_name;
    std::string start_value;
    std::string stop_name;
    std::string stop_value;
    std::string step_name;
    std::string step_value;
    std::string variable_name;
    Block body;
};

struct ForEachStatement {
    std::string variable_name;
    std::string collection;
    bool dictionary = false;
    std::string entry_name;
    Block body;
};

struct TryCatchStatement {
    std::string caught_error_name;
    std::string catch_message_name;
    Block try_body;
    Block catch_body;
};

struct Statement {
    using Node = std::variant<RawStatement, PrintStatement, DictionaryAssignmentStatement,
                              IfStatement, MatchStatement, LocalFunctionStatement,
                              WhileStatement, RangeForStatement,
                              ForEachStatement, TryCatchStatement>;

    Node node;
    std::size_t line;

    Statement(std::string cpp) : node(RawStatement{std::move(cpp)}), line(0) {}
    Statement(Node node, std::size_t line) : node(std::move(node)), line(line) {}
};

struct Function {
    std::string name;
    std::string owner_type;
    std::vector<Parameter> parameters;
    std::optional<Type> return_type;
    Block body;
    std::size_t line;
};

struct Field {
    std::string name;
    Type type;
};

struct EnumVariant {
    std::string name;
    std::vector<Field> fields;
};

struct CustomTypeDefinition {
    std::string name;
    bool is_class = false;
    bool is_enum = false;
    std::vector<Field> fields;
    std::vector<std::string> enum_values;
    std::vector<EnumVariant> enum_variants;
    std::vector<Function> methods;
};

struct Program {
    std::vector<Function> functions;
    std::vector<CustomTypeDefinition> custom_types;
    Block main_body;
    std::vector<std::string> global_cpp;
    bool math_imported = false;
    bool memory_imported = false;
    bool io_imported = false;
};

Program parse(std::istream& source);
std::string transpile(const Program& program);
std::string cpp_type(Type type);

} // namespace sun
