# Sun Language VS Code Extension

The extension adds `.su` syntax highlighting, snippets, completion suggestions,
a basic document formatter, and the Sun file icon theme.

## Install from source

In VS Code, run **Developer: Install Extension from Location...** and select
this `editors/vscode` directory. To use the file icon, select **Sun File Icons**
under **File Icon Theme**.

To install the packaged version, run **Extensions: Install from VSIX...** and
select `sun-language-0.2.0.vsix`, or run `code --install-extension
sun-language-0.2.0.vsix` from this directory.

The formatter uses four spaces by default. Change
`sun.format.indentSize` to use a different indent width. Inline C++ contents are
preserved verbatim.

The completion list includes Sun keywords, types, built-ins, standard library
functions, and declarations found in the current file. Snippets include common
function, control-flow, class, exception, and inline C++ blocks.
