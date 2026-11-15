#include "compiler.hpp"

#include <algorithm>
#include <sstream>

namespace sun {
namespace {

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

} // namespace

std::string transpile(const Program& program) {
    std::ostringstream out;
    out << "#include <cmath>\n#include <cstdint>\n#include <iostream>\n#include <limits>\n"
           "#include <memory>\n#include <sstream>\n#include <stdexcept>\n"
           "#include <string>\n#include <utility>\n#include <vector>\n";
    out << R"cpp(
class SunRuntimeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

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
            out << "\nenum class SunType_" << type.name << " { ";
            for (std::size_t i = 0; i < type.enum_values.size(); ++i) {
                if (i) out << ", ";
                out << type.enum_values[i];
            }
            out << " };\n";
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
           "double sun_range_next(double value, double step) {\n"
           "    const double next = value + step;\n"
           "    if (next == value) {\n"
           "        throw SunRuntimeError(\"range step does not advance number\");\n"
           "    }\n"
           "    return next;\n"
           "}\n\n";
    for (const auto& type : program.custom_types) {
        if (!type.is_enum) continue;
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
        out << ") {\n" << indent_statements(function.body, 4) << "}\n\n";
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
        out << ") {\n" << indent_statements(function.body, 4) << "}\n\n";
    }
    out << "int main() {\n"
        << "    try {\n" << indent_statements(program.main_body, 8)
        << "        return 0;\n"
        << "    } catch (const std::exception& error) {\n"
        << "        std::cerr << \"Sun runtime error: \" << error.what() << '\\n';\n"
        << "        return 1;\n"
        << "    }\n}\n";
    return out.str();
}

} // namespace sun
