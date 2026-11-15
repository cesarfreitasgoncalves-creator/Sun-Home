#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct ExpressionPart {
    bool is_string;
    std::string value;
};

struct Expression {
    std::vector<ExpressionPart> parts;
};

struct Statement {
    enum class Kind { print, call };
    Kind kind;
    std::string name;
    std::vector<Expression> arguments;
    std::size_t line;
};

struct Function {
    std::string name;
    std::vector<std::string> parameters;
    std::vector<Statement> body;
    std::size_t line;
};

struct Program {
    std::vector<Function> functions;
    std::vector<Statement> top_level;
};

struct ParseError : std::runtime_error {
    ParseError(std::size_t line, const std::string& message)
        : std::runtime_error(message), line(line) {}
    std::size_t line;
};

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

std::vector<std::string> split_arguments(const std::string& text, std::size_t line) {
    std::vector<std::string> result;
    if (trim(text).empty()) return result;

    std::size_t start = 0;
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
        } else if (c == ',') {
            const auto part = trim(text.substr(start, i - start));
            if (part.empty()) throw ParseError(line, "empty argument");
            result.push_back(part);
            start = i + 1;
        }
    }
    if (in_string) throw ParseError(line, "unterminated string literal");
    const auto last = trim(text.substr(start));
    if (last.empty()) throw ParseError(line, "empty argument");
    result.push_back(last);
    return result;
}

Expression parse_expression(const std::string& text, std::size_t line,
                            const std::unordered_set<std::string>& parameters) {
    Expression expression;
    std::size_t i = 0;
    bool expect_part = true;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (i == text.size()) break;
        if (!expect_part) {
            if (text[i] != '+') throw ParseError(line, "expected '+' between string values");
            ++i;
            expect_part = true;
            continue;
        }

        if (text[i] == '"') {
            ++i;
            std::string value;
            bool closed = false;
            while (i < text.size()) {
                char c = text[i++];
                if (c == '"') {
                    closed = true;
                    break;
                }
                if (c == '\\') {
                    if (i == text.size()) throw ParseError(line, "unterminated escape sequence");
                    c = text[i++];
                    switch (c) {
                    case 'n': value.push_back('\n'); break;
                    case 't': value.push_back('\t'); break;
                    case 'r': value.push_back('\r'); break;
                    case '\\': value.push_back('\\'); break;
                    case '"': value.push_back('"'); break;
                    default: throw ParseError(line, "unsupported escape sequence");
                    }
                } else {
                    if (c == '\0') throw ParseError(line, "NUL byte is not allowed in string literal");
                    value.push_back(c);
                }
            }
            if (!closed) throw ParseError(line, "unterminated string literal");
            expression.parts.push_back({true, value});
        } else if (is_identifier_start(text[i])) {
            const std::size_t start = i++;
            while (i < text.size() && is_identifier_char(text[i])) ++i;
            const auto name = text.substr(start, i - start);
            if (parameters.find(name) == parameters.end()) {
                throw ParseError(line, "unknown string value '" + name + "'");
            }
            expression.parts.push_back({false, name});
        } else {
            throw ParseError(line, "expected a string literal or parameter name");
        }
        expect_part = false;
    }
    if (expression.parts.empty()) throw ParseError(line, "expected a string expression");
    if (expect_part) throw ParseError(line, "expected a string value after '+'");
    return expression;
}

std::pair<std::string, std::string> parse_call(const std::string& text, std::size_t line) {
    const auto open = text.find('(');
    if (open == std::string::npos || text.back() != ')') {
        throw ParseError(line, "expected a function call in the form name(...)");
    }
    const auto name = trim(text.substr(0, open));
    if (!is_identifier(name)) throw ParseError(line, "invalid function name in call");

    bool in_string = false;
    bool escaped = false;
    int depth = 0;
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
        } else if (c == ')' && --depth == 0 && i + 1 != text.size()) {
            throw ParseError(line, "unexpected text after function call");
        }
    }
    if (in_string || depth != 0) throw ParseError(line, "malformed function call");
    return {name, text.substr(open + 1, text.size() - open - 2)};
}

Statement parse_statement(const std::string& text, std::size_t line,
                          const std::unordered_set<std::string>& parameters) {
    if (text.rfind("print", 0) == 0 &&
        (text.size() == 5 || text[5] == ' ' || text[5] == '\t' || text[5] == '(')) {
        const auto rest = trim(text.substr(5));
        if (rest.size() < 2 || rest.front() != '(' || rest.back() != ')') {
            throw ParseError(line, "expected print(string_expression)");
        }
        return {Statement::Kind::print, "",
                {parse_expression(rest.substr(1, rest.size() - 2), line, parameters)}, line};
    }

    const auto call = parse_call(text, line);
    Statement statement{Statement::Kind::call, call.first, {}, line};
    for (const auto& argument : split_arguments(call.second, line)) {
        statement.arguments.push_back(parse_expression(argument, line, parameters));
    }
    return statement;
}

Function parse_function_header(const std::string& text, std::size_t line) {
    const auto call = parse_call(text, line);
    if (call.first == "print") {
        throw ParseError(line, "function name 'print' is reserved");
    }
    Function function{call.first, {}, {}, line};
    std::unordered_set<std::string> seen;
    for (const auto& parameter : split_arguments(call.second, line)) {
        if (!is_identifier(parameter)) throw ParseError(line, "invalid parameter name '" + parameter + "'");
        if (!seen.insert(parameter).second) {
            throw ParseError(line, "duplicate parameter '" + parameter + "'");
        }
        function.parameters.push_back(parameter);
    }
    return function;
}

Program parse_program(std::istream& source) {
    Program program;
    std::optional<Function> current;
    std::string line_text;
    std::size_t line_number = 0;
    while (std::getline(source, line_text)) {
        ++line_number;
        const auto text = trim(line_text);
        if (text.empty() || text[0] == '#') continue;
        if (text == "end") {
            if (!current) throw ParseError(line_number, "'end' without an open function");
            program.functions.push_back(std::move(*current));
            current.reset();
            continue;
        }
        if (text.rfind("func", 0) == 0 &&
            (text.size() == 4 || text[4] == ' ' || text[4] == '\t')) {
            if (current) throw ParseError(line_number, "nested function declarations are not supported");
            const auto header = trim(text.substr(4));
            if (header.empty()) throw ParseError(line_number, "expected 'func name(parameters)'");
            current = parse_function_header(header, line_number);
            continue;
        }

        const std::unordered_set<std::string> no_parameters;
        auto statement = parse_statement(text, line_number,
                                         current ? std::unordered_set<std::string>(
                                                       current->parameters.begin(),
                                                       current->parameters.end())
                                                 : no_parameters);
        if (current) current->body.push_back(std::move(statement));
        else program.top_level.push_back(std::move(statement));
    }
    if (current) throw ParseError(line_number + 1, "function '" + current->name + "' is missing 'end'");

    std::unordered_map<std::string, std::size_t> signatures;
    for (const auto& function : program.functions) {
        if (!signatures.emplace(function.name, function.parameters.size()).second) {
            throw ParseError(function.line, "function '" + function.name + "' is already defined");
        }
    }
    auto validate = [&](const Statement& statement) {
        if (statement.kind == Statement::Kind::print) return;
        const auto found = signatures.find(statement.name);
        if (found == signatures.end()) {
            throw ParseError(statement.line, "unknown function '" + statement.name + "'");
        }
        if (statement.arguments.size() != found->second) {
            throw ParseError(statement.line, "function '" + statement.name + "' expects " +
                                       std::to_string(found->second) + " argument(s), got " +
                                       std::to_string(statement.arguments.size()));
        }
    };
    for (const auto& statement : program.top_level) validate(statement);
    for (const auto& function : program.functions) {
        for (const auto& statement : function.body) validate(statement);
    }
    return program;
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

std::string emit_expression(const Expression& expression) {
    std::ostringstream result;
    for (std::size_t i = 0; i < expression.parts.size(); ++i) {
        if (i) result << " + ";
        const auto& part = expression.parts[i];
        if (part.is_string) result << "std::string(" << cpp_string(part.value) << ')';
        else result << "sun_arg_" << part.value;
    }
    return result.str();
}

std::string emit_call(const Statement& statement) {
    std::ostringstream result;
    result << "sun_fn_" << statement.name << '(';
    for (std::size_t i = 0; i < statement.arguments.size(); ++i) {
        if (i) result << ", ";
        result << emit_expression(statement.arguments[i]);
    }
    result << ");";
    return result.str();
}

void emit_statements(std::ostream& out, const std::vector<Statement>& statements) {
    for (const auto& statement : statements) {
        if (statement.kind == Statement::Kind::print) {
            out << "    std::cout << " << emit_expression(statement.arguments.front())
                << " << '\\n';\n";
        } else {
            out << "    " << emit_call(statement) << '\n';
        }
    }
}

std::string transpile(const Program& program) {
    std::ostringstream out;
    out << "#include <iostream>\n#include <string>\n\n";
    for (const auto& function : program.functions) {
        out << "void sun_fn_" << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << "std::string";
        }
        out << ");\n";
    }
    if (!program.functions.empty()) out << '\n';
    for (const auto& function : program.functions) {
        out << "void sun_fn_" << function.name << '(';
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i) out << ", ";
            out << "std::string sun_arg_" << function.parameters[i];
        }
        out << ") {\n";
        emit_statements(out, function.body);
        out << "}\n\n";
    }
    out << "int main() {\n";
    emit_statements(out, program.top_level);
    out << "    return 0;\n}\n";
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
        if (!input) throw std::runtime_error("cannot open source file '" + options.source.string() + "'");
        const auto generated = transpile(parse_program(input));

        if (options.output == "-") {
            std::cout << generated;
        } else {
            std::ofstream output(options.output);
            if (!output) throw std::runtime_error("cannot write C++ output '" + options.output.string() + "'");
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
