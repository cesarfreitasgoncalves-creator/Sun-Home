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
    out << "#include <cmath>\n#include <iostream>\n#include <sstream>\n#include <string>\n";
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

} // namespace sun
