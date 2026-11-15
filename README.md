# Sun

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

- Funcoes com parametros tipados (`string`, `number`, `bool`) e blocos `end`.
- Chamadas de funcao e `print(...)`.
- Variaveis locais tipadas com `let`, operadores aritmeticos e comparacoes.
- Operadores logicos com curto-circuito; loops `while` e `for ... in range(...)`.
- Funcoes com retornos tipados e atribuicao mutavel.
- Condicoes `if`/`elif`/`else` e `import math` para funcoes matematicas.
- Blocos de interoperabilidade `inline cpp:` ... `endcpp`.
- Strings concatenadas com `+` e f-strings como `f"Ola, {nome}!"`.
- Interpolacao `%v` com argumentos nomeados, como
  `print("Ola, %v", %v=nome)`.
- Erros de compilacao com numero da linha, sem saida colorida obrigatoria.

Sun ainda esta em prototipo: nao oferece colecoes, entrada interativa,
`break`/`continue` ou comentarios no fim de uma linha.

O core esta dividido em lexer, parser, gerador C++ e CLI em `src/`. Veja
[HOWUSE.md](HOWUSE.md) para detalhes, exemplos e regras do C++ inline.
