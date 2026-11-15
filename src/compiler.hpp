#pragma once

#include <cstddef>
#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
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

struct Function {
    std::string name;
    std::string owner_type;
    std::vector<Parameter> parameters;
    std::optional<Type> return_type;
    std::vector<std::string> body;
    std::size_t line;
};

struct Field {
    std::string name;
    Type type;
};

struct CustomTypeDefinition {
    std::string name;
    bool is_class = false;
    bool is_enum = false;
    std::vector<Field> fields;
    std::vector<std::string> enum_values;
    std::vector<Function> methods;
};

struct Program {
    std::vector<Function> functions;
    std::vector<CustomTypeDefinition> custom_types;
    std::vector<std::string> main_body;
    std::vector<std::string> global_cpp;
    bool math_imported = false;
    bool memory_imported = false;
};

Program parse(std::istream& source);
std::string transpile(const Program& program);
std::string cpp_type(Type type);

} // namespace sun
