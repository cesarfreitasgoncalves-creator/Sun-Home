# Sun

Sun is an initial, small compiler prototype. Source files use the `.su`
extension; the compiler translates a supported Sun subset to C++17.

## Build and run

Requirements: a C++17 compiler (such as `g++` or `clang++`) and Python 3 for
the test suite. There are no third-party dependencies.

```sh
make
build/sun examples/hello.su
g++ -std=c++17 examples/hello.cpp -o examples/hello
./examples/hello
```

By default, the generated C++ file is written next to the source with a `.cpp`
extension. `-o` chooses a different output path. `--compile` also invokes a
C++ compiler and creates an executable next to the `.su` file; `--compiler`
selects its executable (otherwise `CXX`, then `g++`, is used).

```sh
build/sun examples/hello.su -o build/hello.cpp --compile
./examples/hello
make test
```

## Supported syntax

```sun
greet("world")

func greet(name)
    print("Hello, " + name + "!")
end
```

The subset supports top-level function declarations, calls, string parameters,
`print(string_expression)`, string literals, parameter references, `+` string
concatenation, and `end` to close a function. Strings support `\\`, `\"`,
`\n`, `\t`, and `\r` escapes. Function declarations may appear after their
calls. Blank lines and full-line `#` comments are ignored.

Every function returns no value and accepts only strings. Statements inside
functions are limited to `print` and function calls. There are no variables
other than parameters, control-flow constructs, numeric values, return
statements, nested functions, or inline comments yet. Errors include the
source line and a short explanation.
