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

- Funcoes com parametros de texto e blocos terminados por `end`.
- Chamadas de funcao e `print(...)`.
- Strings concatenadas com `+` e f-strings como `f"Ola, {nome}!"`.
- Interpolacao `%v` com argumentos nomeados, como
  `print("Ola, %v", %v=nome)`.
- Erros de compilacao com numero da linha, sem saida colorida obrigatoria.

Sun ainda esta em prototipo: nao oferece variaveis locais, numeros, condicoes,
loops ou valores de retorno.
