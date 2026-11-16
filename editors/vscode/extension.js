const vscode = require("vscode");

const completions = [
  ["def", "Declare a function", "def ${1:name}(${2})${3:: number}\n    $0\nend"],
  ["if", "Conditional block", "if ${1:condition}:\n    $0\nend"],
  ["ifelse", "Conditional with else", "if ${1:condition}:\n    ${2}\nelse:\n    $0\nend"],
  ["for", "Iterate over a range", "for ${1:item} in range(${2:0}, ${3:10}):\n    $0\nend"],
  ["forin", "Iterate over a collection", "for ${1:item} in ${2:items}:\n    $0\nend"],
  ["while", "While loop", "while ${1:condition}:\n    $0\nend"],
  ["class", "Declare a class", "class ${1:Name}\n    ${2}\nend"],
  ["struct", "Declare a struct", "struct ${1:Name}\n    ${2:field}: ${3:string}\nend"],
  ["enum", "Declare an enum", "enum ${1:Name}\n    ${2:Value}\nend"],
  ["try", "Try/except block", "try:\n    ${1}\nexcept AnError as ${2:error}:\n    $0\nend"],
  ["tryfinally", "Try/finally block", "try:\n    ${1}\nfinally:\n    $0\nend"],
  ["match", "Match statement", "match ${1:value}:\n    case ${2:pattern}:\n        $0\nend"],
  ["print", "Print a value", "print(${1:value})$0"],
  ["fstr", "Formatted string", 'f"${1:value}"$0'],
  ["assert", "Assert a condition", "assert ${1:condition}, \"${2:message}\"$0"],
  ["inlinecpp", "Inline C++ block", "inline cpp:\n    $0\nendcpp"],
  ["named", "Call with named arguments", "${1:function}(${2:name}=${3:value})$0"],
  ["varargs", "Variadic function parameters", "def ${1:function}(*${2:values}: ${3:int}, **${4:options}: ${5:string})\n    $0\nend"]
];

const keywords = [
  "def", "class", "struct", "enum", "if", "elif", "else", "while", "for",
  "in", "match", "case", "try", "catch", "except", "finally", "export",
  "let", "const", "raise", "return", "break", "continue", "import", "end",
  "and", "or", "not", "as"
];

const types = ["string", "str", "number", "int", "bool", "char", "region",
  "list", "dict", "tuple", "AnError", "cppCatcher"];

const builtins = ["print", "input", "len", "sum", "all", "any", "min", "max",
  "round", "abs", "floor", "ceil", "trunc", "istype", "assert", "range"];

const mathFunctions = ["abs", "acos", "asin", "atan", "atan2", "ceil", "cos",
  "exp", "floor", "fmod", "hypot", "log", "log10", "log2", "pow", "sin",
  "sqrt", "tan", "trunc"];

const stringMethods = ["upper", "lower", "trim", "split", "replace", "contains",
  "starts_with", "ends_with"];

const moduleFunctions = {
  math: mathFunctions,
  io: ["write_text", "append_text", "read_text", "exists", "read_lines",
    "is_directory", "remove", "create_directories", "file_size"],
  sys: ["args", "platform", "cwd", "env", "env_exists", "exit"],
  memory: ["alloc", "size", "read_u8", "write_u8"]
};

const completionTriggers = [
  ..."abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ_ ."
];

function completion(label, detail, kind, insertText) {
  const item = new vscode.CompletionItem(label, kind);
  item.detail = detail;
  if (insertText) item.insertText = insertText;
  return item;
}

function provideCompletions(document, position) {
  const beforeCursor = position
    ? document.lineAt(position.line).text.slice(0, position.character)
    : "";
  const moduleContext = beforeCursor.match(/\b(math|io|sys|memory)\.[A-Za-z_]*$/);
  if (moduleContext) {
    const moduleName = moduleContext[1];
    return moduleFunctions[moduleName].map(name =>
      completion(name, `${moduleName} function`, vscode.CompletionItemKind.Function,
        new vscode.SnippetString(`${name}($0)`)));
  }
  if (/\.[A-Za-z_]*$/.test(beforeCursor)) {
    return stringMethods.map(name =>
      completion(name, "string method", vscode.CompletionItemKind.Method,
        new vscode.SnippetString(`${name}($0)`)));
  }

  const items = [];
  for (const word of keywords) {
    items.push(completion(word, "Sun keyword", vscode.CompletionItemKind.Keyword));
  }
  for (const type of types) {
    items.push(completion(type, "Sun type", vscode.CompletionItemKind.Class));
  }
  for (const name of builtins) {
    items.push(completion(name, "Built-in function", vscode.CompletionItemKind.Function,
      new vscode.SnippetString(`${name}($0)`)));
  }
  for (const name of mathFunctions) {
    items.push(completion(`math.${name}`, "math function", vscode.CompletionItemKind.Function,
      new vscode.SnippetString(`math.${name}($0)`)));
  }
  for (const name of stringMethods) {
    items.push(completion(`.${name}`, "string method", vscode.CompletionItemKind.Method,
      new vscode.SnippetString(`.${name}($0)`)));
  }
  for (const name of ["math", "io", "sys", "memory"]) {
    items.push(completion(name, "Sun standard module", vscode.CompletionItemKind.Module));
  }
  for (const [moduleName, names] of Object.entries(moduleFunctions)) {
    for (const name of names) {
      items.push(completion(`${moduleName}.${name}`, `${moduleName} function`,
        vscode.CompletionItemKind.Function,
        new vscode.SnippetString(`${moduleName}.${name}($0)`)));
    }
  }

  const source = document.getText();
  const declaration = /^\s*(?:export\s+)?(?:def|class|struct|enum)\s+([A-Za-z_]\w*)/gm;
  const variable = /^\s*(?:let|const)\s+([A-Za-z_]\w*)\s*:/gm;
  const parameter = /\bdef\s+[A-Za-z_]\w*\s*\(([^)]*)\)/gm;
  const seen = new Set();
  for (const pattern of [declaration, variable]) {
    let match;
    while ((match = pattern.exec(source)) !== null) {
      if (!seen.has(match[1])) {
        seen.add(match[1]);
        items.push(completion(match[1], "Declared in this file",
          vscode.CompletionItemKind.Variable));
      }
    }
  }
  let match;
  while ((match = parameter.exec(source)) !== null) {
    const names = match[1].matchAll(/\*{0,2}\s*([A-Za-z_]\w*)\s*:/g);
    for (const parameterMatch of names) {
      const name = parameterMatch[1];
      if (!seen.has(name)) {
        seen.add(name);
        items.push(completion(name, "Function parameter in this file",
          vscode.CompletionItemKind.Variable));
      }
    }
  }
  return items;
}

function getIndentLevel(line) {
  const text = line.trim();
  if (/^endcpp\s*(?:#.*)?$/.test(text)) return "closeCpp";
  if (/^end\s*(?:#.*)?$/.test(text)) return "close";
  if (/^inline\s+cpp:\s*$/.test(text)) return "cpp";
  if (/^(?:elif|else|except|finally|case)\b/.test(text)) return "branch";
  if (/^(?:def|class|struct|enum|if|while|for|match|try)\b/.test(text)) {
    return "open";
  }
  return "normal";
}

function formatDocument(document, indentSize) {
  const edits = [];
  const indent = " ".repeat(indentSize);
  const blocks = [];
  let inCpp = false;

  for (let index = 0; index < document.lineCount; index += 1) {
    const line = document.lineAt(index);
    const text = line.text;
    const trimmed = text.trim();
    if (inCpp && !/^endcpp(?:\s*(?:#.*)?)?$/.test(trimmed)) continue;

    const level = getIndentLevel(text);
    let lineDepth = blocks.length;
    if (level === "branch") {
      const branchName = trimmed.match(/^[A-Za-z]+/)[0];
      if (blocks[blocks.length - 1] === "case") blocks.pop();
      const enclosing = blocks[blocks.length - 1];
      if (branchName === "case" || (branchName === "else" && enclosing === "match")) {
        lineDepth = blocks.length;
        blocks.push("case");
      } else {
        lineDepth = Math.max(0, blocks.length - 1);
      }
    } else if (level === "close" || level === "closeCpp") {
      if (level === "close" && blocks[blocks.length - 1] === "case") {
        blocks.pop();
        if (blocks[blocks.length - 1] === "match") blocks.pop();
      } else if (blocks.length > 0) {
        blocks.pop();
      }
      if (level === "closeCpp") inCpp = false;
      lineDepth = blocks.length;
    }

    if (trimmed.length > 0) {
      const desired = indent.repeat(lineDepth);
      const leading = text.match(/^\s*/)[0];
      if (leading !== desired) {
        edits.push(vscode.TextEdit.replace(
          new vscode.Range(index, 0, index, leading.length), desired));
      }
    } else if (text.length > 0) {
      edits.push(vscode.TextEdit.replace(
        new vscode.Range(index, 0, index, text.length), ""));
    }

    if (level === "open") {
      blocks.push(trimmed.match(/^[A-Za-z]+/)[0]);
    } else if (level === "cpp") {
      blocks.push("cpp");
      if (level === "cpp") inCpp = true;
    }
  }
  return edits;
}

function activate(context) {
  context.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      { language: "sun" },
      {
        provideCompletionItems(document, position) {
          return provideCompletions(document, position);
        }
      },
      ...completionTriggers
    ),
    vscode.languages.registerDocumentFormattingEditProvider(
      { language: "sun" },
      {
        provideDocumentFormattingEdits(document) {
          const indentSize = vscode.workspace
            .getConfiguration("sun.format", document.uri)
            .get("indentSize", 4);
          const safeIndentSize = Number.isInteger(indentSize)
            ? Math.max(1, Math.min(8, indentSize))
            : 4;
          return formatDocument(document, safeIndentSize);
        }
      }
    )
  );
}

function deactivate() {}

module.exports = { activate, deactivate };
