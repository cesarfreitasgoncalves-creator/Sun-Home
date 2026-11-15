#include "compiler.hpp"

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
    out << "#include <cmath>\n#include <cstdint>\n#include <iostream>\n#include <sstream>\n"
           "#include <stdexcept>\n#include <string>\n";
    if (program.memory_imported) {
        out << "#include <memory>\n#include <utility>\n#include <vector>\n";
        out << R"cpp(
class SunMemoryRegion {
public:
    static SunMemoryRegion allocate(double requested_size) {
        if (!std::isfinite(requested_size) || requested_size < 0.0 ||
            std::floor(requested_size) != requested_size ||
            requested_size > static_cast<double>(max_region_size)) {
            throw std::runtime_error("memory.alloc size must be an integer from 0 to 67108864");
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
            throw std::runtime_error("memory.write_u8 value must be an integer from 0 to 255");
        }
        (*bytes_)[checked_offset(offset)] = static_cast<std::uint8_t>(value);
    }

private:
    static constexpr std::size_t max_region_size = 64 * 1024 * 1024;

    explicit SunMemoryRegion(std::shared_ptr<std::vector<std::uint8_t>> bytes)
        : bytes_(std::move(bytes)) {}

    std::size_t checked_offset(double offset) const {
        if (!std::isfinite(offset) || offset < 0.0 || std::floor(offset) != offset) {
            throw std::runtime_error("memory offset must be a non-negative integer");
        }
        if (offset >= static_cast<double>(bytes_->size())) {
            throw std::runtime_error("memory offset out of bounds");
        }
        return static_cast<std::size_t>(offset);
    }

    std::shared_ptr<std::vector<std::uint8_t>> bytes_;
};
)cpp";
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
           "double sun_range_next(double value, double step) {\n"
           "    const double next = value + step;\n"
           "    if (next == value) {\n"
           "        throw std::runtime_error(\"range step does not advance number\");\n"
           "    }\n"
           "    return next;\n"
           "}\n\n";
    for (const auto& function : program.functions) {
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
