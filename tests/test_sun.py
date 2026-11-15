import pathlib
import shutil
import subprocess
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
COMPILER = ROOT / "build" / "sun"
WORK = ROOT / "build" / "test-sun"


class SunCompilerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        WORK.mkdir(parents=True, exist_ok=True)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(WORK, ignore_errors=True)

    def invoke(self, source_name, source, *arguments):
        source_path = WORK / source_name
        source_path.write_text(source, encoding="utf-8")
        return subprocess.run(
            [str(COMPILER), str(source_path), *arguments],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_help_option(self):
        result = subprocess.run(
            [str(COMPILER), "--help"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("Usage: sun <source.su>", result.stdout)

    def test_transpiles_and_compiles_function_calls_and_strings(self):
        result = self.invoke(
            "greeting.su",
            'greet("Sun")\n'
            '\n'
            'func greet(name)\n'
            '    print("Hello, " + name + "!\\n")\n'
            'end\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        generated = (WORK / "greeting.cpp").read_text(encoding="utf-8")
        self.assertIn("void sun_fn_greet(std::string sun_arg_name)", generated)
        self.assertIn('std::string("Hello, ")', generated)
        self.assertIn("sun_arg_name", generated)

        executable = subprocess.run(
            [str(WORK / "greeting")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "Hello, Sun!\n\n")

    def test_f_strings_interpolate_parameters_and_escape_braces(self):
        result = self.invoke(
            "f-string.su",
            'func greet(name)\n'
            '    print(f"Hello, {name}! {{welcome}}" + " again")\n'
            'end\n'
            'greet("Sun")\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "f-string")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "Hello, Sun! {welcome} again\n")

    def test_formatted_print_substitutes_values_and_escaped_percent(self):
        result = self.invoke(
            "formatted-print.su",
            'func greet(name)\n'
            '    print("Seu nome é %v (100%% Sun).", %v=name)\n'
            '    print("%v and %v.", %v=name, %v="Sun")\n'
            'end\n'
            'greet("Sol")\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "formatted-print")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "Seu nome é Sol (100% Sun).\nSol and Sun.\n",
        )

    def test_formatted_print_reports_missing_values(self):
        result = self.invoke("missing-format-value.su", 'print("Hello, %v")\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not enough %v values", result.stderr)

    def test_empty_print_has_a_source_error(self):
        result = self.invoke("empty-print.su", "print()\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("line 1: error: expected print(expression)", result.stderr)

    def test_numeric_operators_math_and_if_elif_else(self):
        result = self.invoke(
            "math-branches.su",
            "import math\n"
            "func classify(x: number)\n"
            "    let magnitude: number = math.sqrt(math.pow(x, 2))\n"
            "    let score: number = magnitude + 2 * 3\n"
            "    if score < 7:\n"
            '        print("small")\n'
            "    elif score == 7:\n"
            '        print("seven")\n'
            "    else:\n"
            '        print(f"large {score}")\n'
            "    end\n"
            "end\n"
            "classify(1)\n"
            "classify(2)\n"
            "classify(0)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "math-branches")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "seven\nlarge 8\nsmall\n")

    def test_numeric_precedence_fractional_division_and_nested_conditions(self):
        result = self.invoke(
            "operators.su",
            "func check()\n"
            "    let value: number = -9 / 2 + 3 % 2 - 0\n"
            "    if value <= -3:\n"
            "        if value >= -3.5:\n"
            "            if value < 0:\n"
            "                if value > -4:\n"
            "                    if value != 0:\n"
            '                        print(f"{value}")\n'
            "                    end\n"
            "                end\n"
            "            end\n"
            "        end\n"
            "    end\n"
            "    if 1 / 2 == 0.5:\n"
            '        print("fractional")\n'
            "    end\n"
            "end\n"
            "check()\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "operators")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "-3.5\nfractional\n")

    def test_boolean_parameters_conditions_and_interpolation(self):
        result = self.invoke(
            "booleans.su",
            "func show(enabled: bool)\n"
            "    if enabled:\n"
            '        print(f"enabled={enabled}")\n'
            "    else:\n"
            '        print("enabled=%v", %v=enabled)\n'
            "    end\n"
            "end\n"
            "show(true)\n"
            "show(false)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "booleans")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "enabled=true\nenabled=false\n")

    def test_inline_cpp_global_and_function_blocks(self):
        result = self.invoke(
            "inline-cpp.su",
            "inline cpp:\n"
            "#include <cstdio>\n"
            "static int sun_global_value = 40;\n"
            "endcpp\n"
            "func show(value: number)\n"
            "    let next: number = value + 2\n"
            "    if next == 42:\n"
            "        inline cpp:\n"
            '            std::cout << value << ":" << next << ":" << sun_global_value << "\\n";\n'
            "        endcpp\n"
            "    end\n"
            "end\n"
            "show(40)\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "inline-cpp")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "40:42:40\n")

    def test_inline_cpp_reports_missing_endcpp(self):
        result = self.invoke("open-inline-cpp.su", 'inline cpp:\nstd::cout << "hi";\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("inline C++ block is missing 'endcpp'", result.stderr)

    def test_logical_operators_short_circuit_loops_assignments_and_returns(self):
        result = self.invoke(
            "control-flow.su",
            "func add(a: number, b: number): number\n"
            "    let result: number = a + b\n"
            "    return result\n"
            "end\n"
            "func label(): string\n"
            '    return "total"\n'
            "end\n"
            "func side_effect(): bool\n"
            '    print("called")\n'
            "    return true\n"
            "end\n"
            "func short_circuit(value: bool): bool\n"
            "    return value and side_effect()\n"
            "end\n"
            "func short_or(value: bool): bool\n"
            "    return value or side_effect()\n"
            "end\n"
            "func calculate(): number\n"
            "    let count: number = 0\n"
            "    while count < 3:\n"
            "        count = count + 1\n"
            "    end\n"
            "    let total: number = 0\n"
            "    for i in range(1, 4):\n"
            "        total = total + i\n"
            "    end\n"
            "    for j in range(3, 0, -1):\n"
            "        total = total + j\n"
            "    end\n"
            "    if not false and (count == 3 or total == 0):\n"
            "        return add(total, count)\n"
            "    else:\n"
            "        return -1\n"
            "    end\n"
            "end\n"
            'print("%v=%v", %v=label(), %v=calculate())\n'
            "print(short_circuit(false))\n"
            "print(short_circuit(true))\n"
            "print(short_or(true))\n"
            "print(short_or(false))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "control-flow")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "total=15\nfalse\ncalled\ntrue\ntrue\ncalled\ntrue\n",
        )

    def test_non_void_function_requires_return_on_all_paths(self):
        result = self.invoke(
            "missing-return.su",
            "func incomplete(enabled: bool): number\n"
            "    if enabled:\n"
            "        return 1\n"
            "    end\n"
            "end\n",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("must return a value on every path", result.stderr)

    def test_rejects_assignment_type_mismatch_and_void_value_use(self):
        assignment = self.invoke(
            "bad-assignment.su",
            'func invalid()\n'
            '    let value: number = 1\n'
            '    value = "text"\n'
            "end\n",
        )
        self.assertNotEqual(assignment.returncode, 0)
        self.assertIn("cannot assign std::string to double", assignment.stderr)

        void_value = self.invoke(
            "void-value.su",
            "func act()\n"
            "end\n"
            "func invalid(): number\n"
            "    return act()\n"
            "end\n",
        )
        self.assertNotEqual(void_value.returncode, 0)
        self.assertIn("does not return a value", void_value.stderr)

    def test_range_rejects_zero_step_at_runtime(self):
        result = self.invoke(
            "zero-range.su",
            "func start()\n"
            "    for i in range(0, 3, 0):\n"
            '        print("unreachable")\n'
            "    end\n"
            "end\n"
            "start()\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "zero-range")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 1)
        self.assertIn("Sun runtime error: range step cannot be zero", executable.stderr)

    def test_break_continue_and_istype(self):
        result = self.invoke(
            "break-continue-istype.su",
            "func total(limit: number): number\n"
            "    let result: number = 0\n"
            "    for i in range(0, limit):\n"
            "        if i == 1:\n"
            "            continue\n"
            "        end\n"
            "        if i == 4:\n"
            "            break\n"
            "        end\n"
            "        result = result + i\n"
            "    end\n"
            "    while true:\n"
            "        result = result + 10\n"
            "        break\n"
            "    end\n"
            "    if istype(result, number) and istype(limit, number) and not istype(result, string):\n"
            "        return result\n"
            "    else:\n"
            "        return 0\n"
            "    end\n"
            "end\n"
            'print("total=%v", %v=total(10))\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "break-continue-istype")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "total=15\n")

    def test_function_parameters_accept_trailing_default_values(self):
        result = self.invoke(
            "default-parameters.su",
            "func describe(name: string = \"world\", times: number = 2): string\n"
            '    return f"Hello {name} {times}"\n'
            "end\n"
            'print(describe())\n'
            'print(describe("Sun"))\n'
            'print(describe("Earth", 3))\n',
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "default-parameters")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(
            executable.stdout,
            "Hello world 2\nHello Sun 2\nHello Earth 3\n",
        )

    def test_default_parameters_work_in_statement_calls(self):
        result = self.invoke(
            "default-call.su",
            'func greet(name: string = "world")\n'
            '    print(f"Hello {name}")\n'
            "end\n"
            "greet()\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "default-call")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "Hello world\n")

    def test_default_values_must_be_typed_constant_expressions(self):
        wrong_type = self.invoke(
            "default-wrong-type.su",
            'func invalid(value: number = "text")\n'
            "end\n",
        )
        self.assertNotEqual(wrong_type.returncode, 0)
        self.assertIn("default for parameter 'value' must have type double",
                      wrong_type.stderr)

        non_constant = self.invoke(
            "default-variable.su",
            "func invalid(value: number = other)\n"
            "end\n",
        )
        self.assertNotEqual(non_constant.returncode, 0)
        self.assertIn("unknown value 'other'", non_constant.stderr)

    def test_default_parameters_must_be_trailing_and_break_must_be_in_loop(self):
        invalid_defaults = self.invoke(
            "default-order.su",
            "func invalid(first: number = 1, second: number)\n"
            "end\n",
        )
        self.assertNotEqual(invalid_defaults.returncode, 0)
        self.assertIn("parameters without defaults cannot follow", invalid_defaults.stderr)

        invalid_break = self.invoke(
            "break-outside-loop.su",
            "func invalid()\n"
            "    break\n"
            "end\n",
        )
        self.assertNotEqual(invalid_break.returncode, 0)
        self.assertIn("'break' is only valid inside a loop", invalid_break.stderr)

    def test_rejects_math_calls_without_import(self):
        result = self.invoke(
            "missing-math-import.su",
            "func root(x: number)\n"
            "    let value: number = sqrt(x)\n"
            "end\n",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("math functions require 'import math'", result.stderr)

    def test_memory_regions_are_bounded_shared_and_typed(self):
        result = self.invoke(
            "memory.su",
            "import memory\n"
            "func store(buffer: region)\n"
            "    memory.write_u8(buffer, 1, 66)\n"
            "end\n"
            "let buffer: region = memory.alloc(4)\n"
            "let alias: region = buffer\n"
            "memory.write_u8(buffer, 0, 65)\n"
            "store(alias)\n"
            'print("size=%v", %v=memory.size(buffer))\n'
            "print(memory.read_u8(alias, 0))\n"
            "print(memory.read_u8(buffer, 1))\n"
            "print(istype(buffer, region))\n"
            "print(istype(buffer, number))\n",
            "--compile",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        executable = subprocess.run(
            [str(WORK / "memory")],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(executable.returncode, 0, executable.stderr)
        self.assertEqual(executable.stdout, "size=4\n65\n66\ntrue\nfalse\n")

    def test_memory_requires_import_and_rejects_regions_as_text(self):
        missing_import = self.invoke(
            "memory-missing-import.su",
            "let size: number = memory.size(1)\n",
        )
        self.assertNotEqual(missing_import.returncode, 0)
        self.assertIn("memory functions require 'import memory'", missing_import.stderr)

        print_region = self.invoke(
            "memory-print-region.su",
            "import memory\n"
            "let buffer: region = memory.alloc(1)\n"
            "print(buffer)\n",
        )
        self.assertNotEqual(print_region.returncode, 0)
        self.assertIn("memory regions cannot be printed directly", print_region.stderr)

    def test_memory_runtime_checks_allocation_offsets_and_byte_values(self):
        cases = [
            (
                "memory-bounds.su",
                "import memory\n"
                "let buffer: region = memory.alloc(1)\n"
                "print(memory.read_u8(buffer, 1))\n",
                "memory offset out of bounds",
            ),
            (
                "memory-byte-value.su",
                "import memory\n"
                "let buffer: region = memory.alloc(1)\n"
                "memory.write_u8(buffer, 0, 256)\n",
                "memory.write_u8 value must be an integer from 0 to 255",
            ),
            (
                "memory-allocation-limit.su",
                "import memory\n"
                "let buffer: region = memory.alloc(67108865)\n",
                "memory.alloc size must be an integer from 0 to 67108864",
            ),
        ]
        for source_name, source, expected_error in cases:
            with self.subTest(source=source_name):
                result = self.invoke(source_name, source, "--compile")
                self.assertEqual(result.returncode, 0, result.stderr)
                executable = subprocess.run(
                    [str(WORK / source_name[:-3])],
                    cwd=ROOT,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                self.assertEqual(executable.returncode, 1)
                self.assertIn(expected_error, executable.stderr)

    def test_rejects_incompatible_static_types(self):
        result = self.invoke(
            "wrong-type.su",
            'func invalid()\n'
            '    let count: number = "three"\n'
            "end\n",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("cannot initialize double with a std::string value", result.stderr)

    def test_reports_unknown_function_with_source_line(self):
        result = self.invoke("bad-call.su", 'print("start")\nmissing()\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("line 2: error: unknown function 'missing'", result.stderr)
        self.assertFalse((WORK / "bad-call.cpp").exists())

    def test_reports_missing_end(self):
        result = self.invoke("open-block.su", "func hello()\nprint(\"hi\")\n")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("line 3: error: function 'hello' is missing 'end'", result.stderr)

    def test_rejects_wrong_argument_count(self):
        result = self.invoke("wrong-args.su", 'func greet(name)\nend\ngreet()\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("line 3: error: function 'greet' expects 1 argument(s), got 0",
                      result.stderr)

    def test_rejects_nul_in_string_literal(self):
        result = self.invoke("nul-string.su", 'print("before\x00after")\n')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("line 1: error: NUL byte is not allowed in string literal", result.stderr)
        self.assertFalse((WORK / "nul-string.cpp").exists())


if __name__ == "__main__":
    unittest.main()
