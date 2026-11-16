# Sun

![Sun language logo](sunimg.jpg)

Sun e uma linguagem compilada experimental com arquivos `.su`. O compilador,
escrito em C++, traduz o subconjunto atual para C++17.

## Inicio rapido

```sh
make
build/sun examples/hello.su --compile
./examples/hello
```

O guia completo de instalacao, compilacao, sintaxe, exemplos e diagnostico esta
em [HOWUSE.md](HOWUSE.md).

## Recursos atuais

- Funcoes com parametros tipados (`string`, `number`, `bool`, `int`, `char`,
  listas e `region`) e blocos `end`.
- Chamadas de funcao e `print(...)`.
- Variaveis locais tipadas com `let`, constantes imutaveis com `const`,
  operadores aritmeticos e comparacoes.
- Operadores logicos com curto-circuito; loops `while` e `for ... in range(...)`.
- Listas homogeneas tipadas com indexacao, `append`, `insert`, `remove`,
  `pop`, `contains`, `clear`, `len` e iteracao.
- Dicionarios homogeneos com `keys`, `values`, `get`, `remove`, `contains`,
  `clear`, indexacao, `len` e iteracao.
- Tuplas heterogeneas, fixas e imutaveis com acesso por indice literal.
- `match` para valores escalares e variantes de enum; enums podem carregar campos tipados.
- Funcoes aninhadas com captura de variaveis externas por referencia.
- `input(prompt, tipo)`, `try`/`catch` legado e `except` tipado, com erros
  personalizados baseados em `AnError`, `raise` e `cppCatcher` para excecoes
  de blocos `inline cpp`; `finally` para executar limpeza ao sair do bloco.
- Inteiros assinados de 64 bits com conversoes verificadas e `char` Unicode.
- Funcoes com retornos tipados e atribuicao mutavel.
- `break`/`continue`, `istype(valor, tipo)` e parametros com valores padrao.
- `sum`, `all`, `any`, `min`, `max` e `round` como funcoes integradas tipadas.
- Condicoes `if`/`elif`/`else` e `import math` para funcoes matematicas.
- `import memory` com regioes de bytes gerenciadas, limites de alocacao e
  verificacao de bounds para leitura e escrita.
- `import io` para leitura de texto/linhas, escrita, diretorios e metadados de
  arquivos.
- `import sys` para argumentos, ambiente, diretorio atual, plataforma e saida.
- Imports de modulos locais com alias, structs, enums e classes simples com
  campos, metodos e `init`; `export def` controla a API publica de modulos, e
  classes de erro podem herdar de `AnError`.
- Blocos de interoperabilidade `inline cpp:` ... `endcpp`.
- Strings concatenadas com `+` e f-strings como `f"Ola, {nome}!"`.
- Interpolacao `%v` com argumentos nomeados, como
  `print("Ola, %v", %v=nome)`.
- Erros de compilacao com arquivo, linha, coluna, trecho destacado e sugestoes.
- `--check` para validar sintaxe e tipos sem gerar C++.

Comentarios com `#` podem aparecer no fim de uma linha fora de strings. Sun
ainda esta em prototipo; heranca entre classes comuns nao esta disponivel,
exceto para classes de erro que herdam de `AnError`.

`inline cpp` executa codigo C++ nativo sem isolamento ou validacao de seguranca.
Use somente com codigo confiavel; nao e uma sandbox.

O core esta dividido em lexer, parser, gerador C++ e CLI em `src/`. Veja
[HOWUSE.md](HOWUSE.md) para detalhes, exemplos e regras do C++ inline.

## Visual Studio Code

Uma extensao basica para reconhecer arquivos `.su`, realcar a sintaxe e mostrar
um icone Sun esta em [`editors/vscode`](editors/vscode). No VS Code, abra essa
pasta com **Developer: Install Extension from Location...**. Para ver o icone
dos arquivos `.su`, selecione **Sun File Icons** em **File Icon Theme**.
