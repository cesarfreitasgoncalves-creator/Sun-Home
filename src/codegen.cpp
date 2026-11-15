#include "compiler.hpp"
#include "stdlib_io.hpp"

#include <algorithm>
#include <sstream>

namespace sun {
namespace {

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

std::string emit_expression(const Expression& expression) {
    const auto child = [&](std::size_t index) {
        return emit_expression(expression.children.at(index));
    };
    const auto arguments = [&](std::size_t start) {
        std::ostringstream result;
        for (std::size_t i = start; i < expression.children.size(); ++i) {
            if (i != start) result << ", ";
            result << emit_expression(expression.children[i]);
        }
        return result.str();
    };
    switch (expression.kind) {
    case Expression::Kind::literal: return expression.value;
    case Expression::Kind::string_literal:
        return "std::string(" + cpp_string(expression.value) + ")";
    case Expression::Kind::variable: return expression.value;
    case Expression::Kind::unary:
        if (expression.value == "sun_int_neg") {
            return "sun_int_neg(" + child(0) + ")";
        }
        return "(" + expression.value + "(" + child(0) + "))";
    case Expression::Kind::binary:
        return "((" + child(0) + ") " + expression.value + " (" + child(1) + "))";
    case Expression::Kind::call:
        return expression.value + "(" + arguments(0) + ")";
    case Expression::Kind::member:
        return "(" + child(0) + ")." + expression.value;
    case Expression::Kind::member_call:
        return "(" + child(0) + ")." + expression.value + "(" + arguments(1) + ")";
    case Expression::Kind::list:
    case Expression::Kind::aggregate: {
        std::ostringstream result;
        if (expression.kind == Expression::Kind::list) {
            result << "std::vector<" << expression.value << ">{";
        } else {
            result << expression.value << '{';
        }
        result << arguments(0) << '}';
        return result.str();
    }
    case Expression::Kind::dictionary: {
        const auto separator = expression.value.find('|');
        if (separator == std::string::npos) {
            throw std::logic_error("dictionary expression is missing its type");
        }
        std::ostringstream result;
        result << "std::map<" << expression.value.substr(0, separator) << ", "
               << expression.value.substr(separator + 1) << ">{";
        for (std::size_t i = 0; i < expression.children.size(); i += 2) {
            if (i) result << ", ";
            result << '{' << emit_expression(expression.children[i]) << ", "
                   << emit_expression(expression.children[i + 1]) << '}';
        }
        result << '}';
        return result.str();
    }
    case Expression::Kind::cast:
        return "static_cast<" + expression.value + ">(" + child(0) + ")";
    case Expression::Kind::empty_list: return "{}";
    case Expression::Kind::empty_dictionary: return "{}";
    }
    throw std::logic_error("invalid expression node");
}

void emit_block(std::ostream& out, const Block& statements, std::size_t spaces);

void emit_raw(std::ostream& out, const std::string& code, std::size_t spaces) {
    std::istringstream lines(code);
    const std::string prefix(spaces, ' ');
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty()) out << prefix;
        out << line << '\n';
    }
}

void emit_statement(std::ostream& out, const Statement& statement, std::size_t spaces) {
    const std::string prefix(spaces, ' ');
    if (const auto* raw = std::get_if<RawStatement>(&statement.node)) {
        emit_raw(out, raw->cpp, spaces);
    } else if (const auto* print = std::get_if<PrintStatement>(&statement.node)) {
        out << prefix << "std::cout << sun_to_string(" << emit_expression(print->value)
            << ") << '\\n';\n";
    } else if (const auto* assignment =
                   std::get_if<DictionaryAssignmentStatement>(&statement.node)) {
        out << prefix << emit_expression(assignment->dictionary) << '['
            << emit_expression(assignment->key) << "] = "
            << emit_expression(assignment->value) << ";\n";
    } else if (const auto* conditional = std::get_if<IfStatement>(&statement.node)) {
        for (std::size_t i = 0; i < conditional->branches.size(); ++i) {
            const auto& branch = conditional->branches[i];
            if (i == 0) out << prefix << "if (";
            else out << "else if (";
            out << branch.condition << ") {\n";
            emit_block(out, branch.body, spaces + 4);
            out << prefix << '}';
            if (i + 1 < conditional->branches.size() || conditional->else_body) out << ' ';
        }
        if (conditional->else_body) {
            out << "else {\n";
            emit_block(out, *conditional->else_body, spaces + 4);
            out << prefix << '}';
        }
        out << '\n';
    } else if (const auto* matched = std::get_if<MatchStatement>(&statement.node)) {
        out << prefix << "{\n"
            << std::string(spaces + 4, ' ') << "auto "
            << matched->temporary_name << " = " << emit_expression(matched->value)
            << ";\n";
        for (std::size_t i = 0; i < matched->cases.size(); ++i) {
            const auto& match_case = matched->cases[i];
            const auto branch_prefix = std::string(spaces + 4, ' ');
            if (i == 0) out << branch_prefix;
            else out << "else ";
            if (!match_case.enum_variant_type.empty()) {
                const auto pointer = "sun_match_variant_" +
                    std::to_string(statement.line) + '_' + std::to_string(i);
                out << "if (auto* " << pointer << " = std::get_if<"
                    << match_case.enum_variant_type << ">(&"
                    << matched->temporary_name << ")) {\n";
                for (const auto& binding : match_case.bindings) {
                    out << std::string(spaces + 8, ' ') << "auto& "
                        << binding.cpp_name << " = " << pointer << "->"
                        << binding.field_name << ";\n";
                }
            } else {
                out << "if (" << matched->temporary_name << " == "
                    << emit_expression(match_case.pattern) << ") {\n";
            }
            emit_block(out, match_case.body, spaces + 8);
            out << branch_prefix << '}';
            if (i + 1 < matched->cases.size() || matched->else_body) out << ' ';
        }
        if (matched->else_body) {
            out << "else {\n";
            emit_block(out, *matched->else_body, spaces + 8);
            out << std::string(spaces + 4, ' ') << '}';
        }
        out << '\n' << prefix << "}\n";
    } else if (const auto* local_function =
                   std::get_if<LocalFunctionStatement>(&statement.node)) {
        const auto return_type = local_function->return_type
                                     ? cpp_type(*local_function->return_type)
                                     : "void";
        out << prefix << "std::function<" << return_type << '(';
        for (std::size_t i = 0; i < local_function->parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(local_function->parameters[i].type);
        }
        out << ")> " << local_function->cpp_name << ";\n"
            << prefix << local_function->cpp_name << " = [&]( ";
        for (std::size_t i = 0; i < local_function->parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(local_function->parameters[i].type) << " sun_arg_"
                << local_function->parameters[i].name;
        }
        out << ") -> " << return_type << " {\n";
        emit_block(out, local_function->body, spaces + 4);
        out << prefix << "};\n";
    } else if (const auto* loop = std::get_if<WhileStatement>(&statement.node)) {
        out << prefix << "while (" << loop->condition << ") {\n";
        emit_block(out, loop->body, spaces + 4);
        out << prefix << "}\n";
    } else if (const auto* loop = std::get_if<RangeForStatement>(&statement.node)) {
        const auto inner = std::string(spaces + 4, ' ');
        out << prefix << "{\n"
            << inner << "const double " << loop->start_name << " = "
            << loop->start_value << ";\n"
            << inner << "const double " << loop->stop_name << " = "
            << loop->stop_value << ";\n"
            << inner << "const double " << loop->step_name << " = "
            << loop->step_value << ";\n"
            << inner << "if (" << loop->step_name
            << " == 0.0) throw SunRuntimeError(\"range step cannot be zero\");\n"
            << inner << "for (double " << loop->variable_name << " = "
            << loop->start_name << "; (" << loop->step_name << " > 0.0 ? "
            << loop->variable_name << " < " << loop->stop_name << " : "
            << loop->variable_name << " > " << loop->stop_name << "); "
            << loop->variable_name << " = sun_range_next(" << loop->variable_name
            << ", " << loop->step_name << ")) {\n";
        emit_block(out, loop->body, spaces + 8);
        out << inner << "}\n" << prefix << "}\n";
    } else if (const auto* loop = std::get_if<ForEachStatement>(&statement.node)) {
        if (loop->dictionary) {
            out << prefix << "for (const auto& " << loop->entry_name << " : "
                << loop->collection << ") {\n"
                << std::string(spaces + 4, ' ') << "auto " << loop->variable_name
                << " = " << loop->entry_name << ".first;\n";
        } else {
            out << prefix << "for (auto " << loop->variable_name << " : "
                << loop->collection << ") {\n";
        }
        emit_block(out, loop->body, spaces + 4);
        out << prefix << "}\n";
    } else if (const auto* attempt = std::get_if<TryCatchStatement>(&statement.node)) {
        out << prefix << "try {\n";
        emit_block(out, attempt->try_body, spaces + 4);
        out << prefix << "} catch (const SunRuntimeError& "
            << attempt->caught_error_name << ") {\n"
            << std::string(spaces + 4, ' ') << "const std::string "
            << attempt->catch_message_name << " = "
            << attempt->caught_error_name << ".what();\n";
        emit_block(out, attempt->catch_body, spaces + 4);
        out << prefix << "}\n";
    }
}

void emit_block(std::ostream& out, const Block& statements, std::size_t spaces) {
    for (const auto& statement : statements) {
        emit_statement(out, statement, spaces);
    }
}

std::string render_block(const Block& statements, std::size_t spaces) {
    std::ostringstream result;
    emit_block(result, statements, spaces);
    return result.str();
}

} // namespace

std::string transpile(const Program& program) {
    std::ostringstream out;
    out << "#include <cmath>\n#include <cstdint>\n#include <iostream>\n#include <limits>\n"
           "#include <map>\n#include <memory>\n#include <sstream>\n#include <stdexcept>\n"
           "#include <functional>\n#include <string>\n#include <type_traits>\n#include <utility>\n"
           "#include <variant>\n#include <vector>\n";
    if (program.io_imported) {
        out << "#include <filesystem>\n#include <fstream>\n#include <iterator>\n";
    }
    out << R"cpp(
class SunRuntimeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

template <typename Map, typename Key>
decltype(auto) sun_dict_at(Map& values, const Key& key) {
    const auto found = values.find(key);
    if (found == values.end()) throw SunRuntimeError("dictionary key not found");
    return (found->second);
}

template <typename Map, typename Key>
bool sun_dict_contains(const Map& values, const Key& key) {
    return values.find(key) != values.end();
}

template <typename Map, typename Key>
bool sun_dict_remove(Map& values, const Key& key) {
    return values.erase(key) != 0;
}

std::string sun_input_string(const std::string& prompt) {
    std::cout << prompt << std::flush;
    std::string value;
    if (!std::getline(std::cin, value)) throw SunRuntimeError("input could not read a line");
    return value;
}

double sun_input_number(const std::string& prompt) {
    const auto text = sun_input_string(prompt);
    std::istringstream input(text);
    double value = 0.0;
    if (!(input >> value)) throw SunRuntimeError("input expected a number");
    input >> std::ws;
    if (!input.eof()) throw SunRuntimeError("input expected a number");
    return value;
}

std::int64_t sun_input_int(const std::string& prompt) {
    const auto text = sun_input_string(prompt);
    std::istringstream input(text);
    std::int64_t value = 0;
    if (!(input >> value)) throw SunRuntimeError("input expected an int");
    input >> std::ws;
    if (!input.eof()) throw SunRuntimeError("input expected an int");
    return value;
}

bool sun_input_bool(const std::string& prompt) {
    const auto value = sun_input_string(prompt);
    if (value == "true") return true;
    if (value == "false") return false;
    throw SunRuntimeError("input expected 'true' or 'false'");
}

bool sun_is_one_utf8_character(const std::string& value) {
    if (value.empty()) return false;
    const auto first = static_cast<unsigned char>(value[0]);
    std::size_t length = 0;
    if (first <= 0x7f) length = 1;
    else if (first >= 0xc2 && first <= 0xdf) length = 2;
    else if (first >= 0xe0 && first <= 0xef) length = 3;
    else if (first >= 0xf0 && first <= 0xf4) length = 4;
    else return false;
    if (value.size() != length) return false;
    for (std::size_t i = 1; i < length; ++i) {
        const auto byte = static_cast<unsigned char>(value[i]);
        if (byte < 0x80 || byte > 0xbf) return false;
    }
    if (length >= 3) {
        const auto second = static_cast<unsigned char>(value[1]);
        if ((first == 0xe0 && second < 0xa0) ||
            (first == 0xed && second > 0x9f) ||
            (first == 0xf0 && second < 0x90) ||
            (first == 0xf4 && second > 0x8f)) return false;
    }
    return true;
}

std::string sun_input_char(const std::string& prompt) {
    const auto value = sun_input_string(prompt);
    if (!sun_is_one_utf8_character(value))
        throw SunRuntimeError("input expected one UTF-8 character");
    return value;
}

std::string sun_to_char(const std::string& value) {
    if (!sun_is_one_utf8_character(value))
        throw SunRuntimeError("char() requires one UTF-8 character");
    return value;
}

std::int64_t sun_to_int(double value) {
    const auto wide = static_cast<long double>(value);
    if (!std::isfinite(value) || std::floor(value) != value ||
        wide < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        wide > static_cast<long double>(std::numeric_limits<std::int64_t>::max()))
        throw SunRuntimeError("int() requires an in-range whole number");
    return static_cast<std::int64_t>(value);
}

std::int64_t sun_int_add(std::int64_t left, std::int64_t right) {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right))
        throw SunRuntimeError("int arithmetic overflow");
    return left + right;
}

std::int64_t sun_int_sub(std::int64_t left, std::int64_t right) {
    if ((right < 0 && left > std::numeric_limits<std::int64_t>::max() + right) ||
        (right > 0 && left < std::numeric_limits<std::int64_t>::min() + right))
        throw SunRuntimeError("int arithmetic overflow");
    return left - right;
}

std::int64_t sun_int_mul(std::int64_t left, std::int64_t right) {
    if (left != 0 && right != 0) {
        const auto min = std::numeric_limits<std::int64_t>::min();
        const auto max = std::numeric_limits<std::int64_t>::max();
        if ((left == -1 && right == min) || (right == -1 && left == min) ||
            (left > 0 && ((right > 0 && left > max / right) ||
                          (right < 0 && right < min / left))) ||
            (left < 0 && ((right > 0 && left < min / right) ||
                          (right < 0 && left < max / right))))
            throw SunRuntimeError("int arithmetic overflow");
    }
    return left * right;
}

std::int64_t sun_int_div(std::int64_t left, std::int64_t right) {
    if (right == 0) throw SunRuntimeError("int division by zero");
    if (left == std::numeric_limits<std::int64_t>::min() && right == -1)
        throw SunRuntimeError("int arithmetic overflow");
    return left / right;
}

std::int64_t sun_int_mod(std::int64_t left, std::int64_t right) {
    if (right == 0) throw SunRuntimeError("int modulo by zero");
    if (left == std::numeric_limits<std::int64_t>::min() && right == -1) return 0;
    return left % right;
}

std::int64_t sun_int_neg(std::int64_t value) {
    if (value == std::numeric_limits<std::int64_t>::min())
        throw SunRuntimeError("int arithmetic overflow");
    return -value;
}

template <typename T>
decltype(auto) sun_list_at(std::vector<T>& values, double index) {
    if (!std::isfinite(index) || index < 0.0 || std::floor(index) != index ||
        index >= static_cast<double>(values.size()))
        throw SunRuntimeError("list index out of bounds or not a non-negative integer");
    return values[static_cast<std::size_t>(index)];
}

template <typename T>
void sun_list_append(std::vector<T>& values, T value) {
    values.push_back(std::move(value));
}
)cpp";
    if (program.io_imported) out << io_runtime_cpp();
    if (program.memory_imported) {
        out << R"cpp(
class SunMemoryRegion {
public:
    static SunMemoryRegion allocate(double requested_size) {
        if (!std::isfinite(requested_size) || requested_size < 0.0 ||
            std::floor(requested_size) != requested_size ||
            requested_size > static_cast<double>(max_region_size)) {
            throw SunRuntimeError("memory.alloc size must be an integer from 0 to 67108864");
        }
        return SunMemoryRegion(
            std::make_shared<std::vector<std::uint8_t>>(
                static_cast<std::size_t>(requested_size), 0));
    }

    double size() const {
        return static_cast<double>(bytes_->size());
    }

    double read_u8(double offset) const {
        return static_cast<double>((*bytes_)[checked_offset(offset)]);
    }

    void write_u8(double offset, double value) {
        if (!std::isfinite(value) || value < 0.0 || value > 255.0 ||
            std::floor(value) != value) {
            throw SunRuntimeError("memory.write_u8 value must be an integer from 0 to 255");
        }
        (*bytes_)[checked_offset(offset)] = static_cast<std::uint8_t>(value);
    }

private:
    static constexpr std::size_t max_region_size = 64 * 1024 * 1024;

    explicit SunMemoryRegion(std::shared_ptr<std::vector<std::uint8_t>> bytes)
        : bytes_(std::move(bytes)) {}

    std::size_t checked_offset(double offset) const {
        if (!std::isfinite(offset) || offset < 0.0 || std::floor(offset) != offset) {
            throw SunRuntimeError("memory offset must be a non-negative integer");
        }
        if (offset >= static_cast<double>(bytes_->size())) {
            throw SunRuntimeError("memory offset out of bounds");
        }
        return static_cast<std::size_t>(offset);
    }

    std::shared_ptr<std::vector<std::uint8_t>> bytes_;
};
)cpp";
    }
    for (const auto& type : program.custom_types) {
        if (type.is_enum) {
            const bool has_payload = std::any_of(
                type.enum_variants.begin(), type.enum_variants.end(),
                [](const EnumVariant& variant) { return !variant.fields.empty(); });
            if (!has_payload) {
                out << "\nenum class SunType_" << type.name << " { ";
                for (std::size_t i = 0; i < type.enum_values.size(); ++i) {
                    if (i) out << ", ";
                    out << type.enum_values[i];
                }
                out << " };\n";
                continue;
            }
            out << '\n';
            for (const auto& variant : type.enum_variants) {
                out << "struct SunType_" << type.name << '_' << variant.name << " {\n";
                for (const auto& field : variant.fields) {
                    out << "    " << cpp_type(field.type) << ' ' << field.name << "{};\n";
                }
                out << "    friend bool operator==(const SunType_" << type.name << '_'
                    << variant.name << "& left, const SunType_" << type.name << '_'
                    << variant.name << "& right) { return ";
                if (variant.fields.empty()) {
                    out << "true";
                } else {
                    for (std::size_t i = 0; i < variant.fields.size(); ++i) {
                        if (i) out << " && ";
                        out << "left." << variant.fields[i].name << " == right."
                            << variant.fields[i].name;
                    }
                }
                out << "; }\n"
                    << "    friend bool operator!=(const SunType_" << type.name << '_'
                    << variant.name << "& left, const SunType_" << type.name << '_'
                    << variant.name << "& right) { return !(left == right); }\n"
                    << "};\n";
            }
            out << "using SunType_" << type.name << " = std::variant<";
            for (std::size_t i = 0; i < type.enum_variants.size(); ++i) {
                if (i) out << ", ";
                out << "SunType_" << type.name << '_' << type.enum_variants[i].name;
            }
            out << ">;\n";
            continue;
        }
        out << "\nstruct SunType_" << type.name << " {\n";
        for (const auto& field : type.fields) {
            out << "    " << cpp_type(field.type) << ' ' << field.name << "{};\n";
        }
        for (const auto& method : type.methods) {
            out << "    " << (method.return_type ? cpp_type(*method.return_type) : "void")
                << ' ' << method.name << '(';
            for (std::size_t i = 0; i < method.parameters.size(); ++i) {
                if (i) out << ", ";
                out << cpp_type(method.parameters[i].type);
            }
            out << ");\n";
        }
        out << "};\n";
    }
    for (const auto& type : program.custom_types) {
        if (!type.is_class) continue;
        const auto init = std::find_if(type.methods.begin(), type.methods.end(),
                                       [](const Function& method) {
                                           return method.name == "init";
                                       });
        if (init == type.methods.end()) continue;
        out << "\nSunType_" << type.name << " sun_make_" << type.name << '(';
        for (std::size_t i = 0; i < init->parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(init->parameters[i].type);
        }
        out << ");\n";
    }
    for (const auto& code : program.global_cpp) {
        out << '\n' << code << '\n';
    }
    out << "\nstd::string sun_to_string(const std::string& value) {\n"
           "    return value;\n"
           "}\n\n"
           "template <typename T>\n"
           "std::string sun_to_string(const T& value) {\n"
           "    std::ostringstream stream;\n"
           "    stream << std::boolalpha << value;\n"
           "    return stream.str();\n"
           "}\n\n"
           "template <typename T>\n"
           "std::string sun_to_string(const std::vector<T>& values) {\n"
           "    std::ostringstream stream;\n"
           "    stream << '[';\n"
           "    for (std::size_t i = 0; i < values.size(); ++i) {\n"
           "        if (i) stream << \", \";\n"
           "        stream << std::boolalpha << values[i];\n"
           "    }\n"
           "    stream << ']';\n"
           "    return stream.str();\n"
           "}\n\n"
           "template <typename K, typename V>\n"
           "std::string sun_to_string(const std::map<K, V>& values) {\n"
           "    std::ostringstream stream;\n"
           "    stream << '{';\n"
           "    std::size_t index = 0;\n"
           "    for (const auto& entry : values) {\n"
           "        if (index++) stream << \", \";\n"
           "        stream << std::boolalpha << entry.first << \": \" << entry.second;\n"
           "    }\n"
           "    stream << '}';\n"
           "    return stream.str();\n"
           "}\n\n"
           "double sun_range_next(double value, double step) {\n"
           "    const double next = value + step;\n"
           "    if (next == value) {\n"
           "        throw SunRuntimeError(\"range step does not advance number\");\n"
           "    }\n"
           "    return next;\n"
           "}\n\n";
    for (const auto& type : program.custom_types) {
        if (!type.is_enum) continue;
        const bool has_payload = std::any_of(
            type.enum_variants.begin(), type.enum_variants.end(),
            [](const EnumVariant& variant) { return !variant.fields.empty(); });
        if (has_payload) {
            out << "std::string sun_to_string(const SunType_" << type.name
                << "& value) {\n"
                << "    return std::visit([](const auto& variant) -> std::string {\n";
            for (std::size_t i = 0; i < type.enum_variants.size(); ++i) {
                const auto& variant = type.enum_variants[i];
                out << (i ? "    else " : "    ")
                    << "if constexpr (std::is_same_v<std::decay_t<decltype(variant)>, "
                    << "SunType_" << type.name << '_' << variant.name << ">) return \""
                    << variant.name << "\";\n";
            }
            out << "    }, value);\n}\n\n";
            continue;
        }
        out << "std::string sun_to_string(SunType_" << type.name << " value) {\n"
            << "    switch (value) {\n";
        for (const auto& value : type.enum_values) {
            out << "    case SunType_" << type.name << "::" << value << ": return "
                << '"' << value << '"' << ";\n";
        }
        out << "    }\n    return \"<invalid " << type.name << ">\";\n}\n\n";
    }
    for (const auto& function : program.functions) {
        if (!function.owner_type.empty()) continue;
        out << (function.return_type ? cpp_type(*function.return_type) : "void")
            << " sun_fn_" << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(function.parameters[i].type);
        }
        out << ");\n";
    }
    if (!program.functions.empty()) out << '\n';
    for (const auto& function : program.functions) {
        if (function.owner_type.empty()) continue;
        out << (function.return_type ? cpp_type(*function.return_type) : "void")
            << " SunType_" << function.owner_type << "::" << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(function.parameters[i].type) << " sun_arg_"
                << function.parameters[i].name;
        }
        out << ") {\n" << render_block(function.body, 4) << "}\n\n";
    }
    for (const auto& type : program.custom_types) {
        if (!type.is_class) continue;
        const auto init = std::find_if(type.methods.begin(), type.methods.end(),
                                       [](const Function& method) {
                                           return method.name == "init";
                                       });
        if (init == type.methods.end()) continue;
        out << "SunType_" << type.name << " sun_make_" << type.name << '(';
        for (std::size_t i = 0; i < init->parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(init->parameters[i].type) << " sun_arg_"
                << init->parameters[i].name;
        }
        out << ") {\n    SunType_" << type.name << " sun_instance{};\n"
            << "    sun_instance.init(";
        for (std::size_t i = 0; i < init->parameters.size(); ++i) {
            if (i) out << ", ";
            out << "sun_arg_" << init->parameters[i].name;
        }
        out << ");\n    return sun_instance;\n}\n\n";
    }
    for (const auto& function : program.functions) {
        if (!function.owner_type.empty()) continue;
        out << (function.return_type ? cpp_type(*function.return_type) : "void")
            << " sun_fn_" << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << cpp_type(function.parameters[i].type) << " sun_arg_"
                << function.parameters[i].name;
        }
        out << ") {\n" << render_block(function.body, 4) << "}\n\n";
    }
    out << "int main() {\n"
        << "    try {\n" << render_block(program.main_body, 8)
        << "        return 0;\n"
        << "    } catch (const std::exception& error) {\n"
        << "        std::cerr << \"Sun runtime error: \" << error.what() << '\\n';\n"
        << "        return 1;\n"
        << "    }\n}\n";
    return out.str();
}

} // namespace sun
