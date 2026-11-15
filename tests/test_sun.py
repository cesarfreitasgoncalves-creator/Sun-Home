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

    def test_rejects_math_calls_without_import(self):
        result = self.invoke(
            "missing-math-import.su",
            "func root(x: number)\n"
            "    let value: number = sqrt(x)\n"
            "end\n",
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("math functions require 'import math'", result.stderr)

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
