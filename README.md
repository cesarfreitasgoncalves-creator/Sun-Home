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

- Funcoes com parametros tipados (`string`, `number`, `bool`, `int`, `char`,
  listas e `region`) e blocos `end`.
- Chamadas de funcao e `print(...)`.
- Variaveis locais tipadas com `let`, operadores aritmeticos e comparacoes.
- Operadores logicos com curto-circuito; loops `while` e `for ... in range(...)`.
- Listas homogeneas tipadas, indexacao, `append`, `len` e `for` sobre listas.
- `input(prompt, tipo)` para leitura tipada e `try`/`catch` para erros de runtime Sun.
- Inteiros assinados de 64 bits com conversoes verificadas e tipo `char` de um byte.
- Funcoes com retornos tipados e atribuicao mutavel.
- `break`/`continue`, `istype(valor, tipo)` e parametros com valores padrao.
- Condicoes `if`/`elif`/`else` e `import math` para funcoes matematicas.
- `import memory` com regioes de bytes gerenciadas, limites de alocacao e
  verificacao de bounds para leitura e escrita.
- Blocos de interoperabilidade `inline cpp:` ... `endcpp`.
- Strings concatenadas com `+` e f-strings como `f"Ola, {nome}!"`.
- Interpolacao `%v` com argumentos nomeados, como
  `print("Ola, %v", %v=nome)`.
- Erros de compilacao com numero da linha, sem saida colorida obrigatoria.

Sun ainda esta em prototipo: structs, enums, funcoes aninhadas e comentarios no
fim de uma linha ainda nao sao suportados.

O core esta dividido em lexer, parser, gerador C++ e CLI em `src/`. Veja
[HOWUSE.md](HOWUSE.md) para detalhes, exemplos e regras do C++ inline.
