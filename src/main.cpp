#include "compiler.hpp"
#include "modules.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

namespace fs = std::filesystem;

struct Options {
    fs::path source;
    fs::path output;
    bool compile = false;
    bool compiler_explicit = false;
    std::string compiler = "g++";
};

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += '\'';
    return quoted;
}

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
        if (options.compile) {
            throw std::runtime_error("cannot compile when C++ output is sent to stdout");
        }
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
        std::istringstream input(sun::load_project_source(options.source));
        const auto generated = sun::transpile(sun::parse(input));
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
            const std::string command = shell_quote(compiler) +
                                        " -std=c++17 -Wall -Wextra -pedantic " +
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
    } catch (const sun::ParseError& error) {
        std::cerr << "sun: line " << error.line << ": error: " << error.what() << '\n';
    } catch (const std::exception& error) {
        std::cerr << "sun: error: " << error.what() << '\n';
        if (argc < 2) print_usage(std::cerr);
    }
    return 1;
}
