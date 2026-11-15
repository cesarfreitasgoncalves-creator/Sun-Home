#pragma once

#include <cstddef>
#include <istream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace sun {

enum class Type {
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
    empty_list
};

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
    std::vector<Parameter> parameters;
    std::optional<Type> return_type;
    std::vector<std::string> body;
    std::size_t line;
};

struct Program {
    std::vector<Function> functions;
    std::vector<std::string> main_body;
    std::vector<std::string> global_cpp;
    bool math_imported = false;
    bool memory_imported = false;
};

Program parse(std::istream& source);
std::string transpile(const Program& program);
std::string cpp_type(Type type);

} // namespace sun
