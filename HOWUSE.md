# Como usar a Sun

Este guia mostra como compilar o compilador, transpilar um arquivo `.su`,
gerar um executável e escrever programas com a sintaxe atualmente suportada.

## 1. Requisitos

- Um compilador C++17: `g++` ou `clang++`.
- `make` para usar os comandos simplificados do projeto.
- Python 3 somente para executar os testes.

Nao sao necessarias bibliotecas de terceiros.

## 2. Compilar o compilador Sun

Na pasta raiz do projeto:

```sh
make
```

O executavel do compilador fica em `build/sun`. Para ver as opcoes:

```sh
build/sun --help
```

## 3. Transpilar e compilar um programa

O exemplo incluido e `examples/hello.su`. Primeiro, gere o arquivo C++:

```sh
build/sun examples/hello.su
```

Isso cria `examples/hello.cpp`. Compile e execute o C++ manualmente:

```sh
g++ -std=c++17 examples/hello.cpp -o examples/hello
./examples/hello
```

Ou faca a transpilaçao e a compilacao em uma unica etapa:

```sh
build/sun examples/hello.su --compile
./examples/hello
```

O executavel gerado fica ao lado do arquivo `.su`, sem a extensao. Para
escolher o caminho do C++ gerado, use `-o`:

```sh
build/sun examples/hello.su -o build/hello.cpp
```

`--compiler clang++` escolhe outro compilador C++. Se essa opcao nao for
informada, Sun usa a variavel de ambiente `CXX` quando configurada e, caso
contrario, tenta usar `g++`.

Para escrever o C++ gerado na saida padrao:

```sh
build/sun examples/hello.su -o -
```

## 4. Primeiro programa

Crie um arquivo chamado `ola.su`:

```sun
func cumprimentar(nome)
    print(f"Ola, {nome}!")
end

cumprimentar("mundo")
```

Compile e execute:

```sh
build/sun ola.su --compile
./ola
```

Saida:

```text
Ola, mundo!
```

`func` inicia uma funcao e `end` encerra o bloco. A indentacao e recomendada
para facilitar a leitura, mas atualmente e `end` que determina onde o bloco
termina. Funcoes aceitam parametros de texto; chamadas podem aparecer antes ou
depois da declaracao da funcao.

## 5. Strings e interpolacao

Strings usam aspas duplas. Os escapes aceitos sao `\\`, `\"`, `\n`, `\t` e
`\r`. O operador `+` concatena texto:

```sun
func mostrar(nome)
    print("Ola, " + nome + "!")
end
```

### F-strings

Coloque `f` antes das aspas e o nome de um parametro entre chaves:

```sun
func mostrar(nome)
    print(f"Ola, {nome}!")
    print(f"Chaves literais: {{ e }}")
end
```

As chaves duplas `{{` e `}}` representam chaves literais. A interpolacao aceita
nomes de parametros e variaveis locais, mas nao expressoes arbitrarias dentro
das chaves.

### Marcador `%v`

Para passar valores junto com uma string de formato, associe cada `%v` a um
argumento `%v=valor`:

```sun
func mostrar(nome)
    print("Seu nome e %v.", %v=nome)
    print("%v gosta de %v.", %v=nome, %v="Sun")
    print("Progresso: 100%%, %v esta online.", %v=nome)
end
```

Os marcadores sao substituidos da esquerda para a direita. Cada `%v` exige
exatamente um argumento `%v=...`. Os valores podem ser dos tipos suportados e
sao convertidos para texto. Use `%%` para escrever um sinal de porcentagem
literal quando estiver usando argumentos formatados.

## 6. Numeros, operadores, condicoes e matematica

Parametros podem declarar o tipo `string`, `number` ou `bool`. Parametros sem
tipo continuam sendo `string` para manter compatibilidade com programas
anteriores. Variaveis locais usam `let` e exigem tipo explicito:

```sun
import math

func classificar(x: number)
    let magnitude: number = math.sqrt(x * x)
    let pontuacao: number = magnitude + 2 * 3

    if pontuacao < 7:
        print("pequeno")
    elif pontuacao == 7:
        print("sete")
    else:
        print(f"grande: {pontuacao}")
    end
end

classificar(2)
```

Operadores aritmeticos: `+`, `-`, `*`, `/` e `%` (resto). `+` tambem concatena
duas strings. Comparacoes disponiveis: `==`, `!=`, `<`, `<=`, `>` e `>=`.
Comparacoes ordenadas aceitam numeros; igualdade aceita dois valores do mesmo
tipo. Condicoes de `if`/`elif` precisam resultar em `bool`, por exemplo uma
comparacao, `true`/`false` ou um parametro booleano. Blocos `if` podem ter
varios `elif` e um `else`; todo o conjunto termina com `end`.

`import math` deve aparecer antes das declaracoes e instrucoes. Ele habilita as
funcoes `math.abs`, `math.ceil`, `math.cos`, `math.exp`, `math.floor`,
`math.log`, `math.pow`, `math.sin`, `math.sqrt` e `math.tan`. Essas funcoes
recebem numeros; todas, exceto `math.pow`, recebem um argumento. A biblioteca e
implementada usando `<cmath>` do C++.

## 7. Comentarios e regras atuais

Linhas vazias e comentarios de linha inteira iniciados por `#` sao ignorados:

```sun
# Este e um comentario
print("Ola!")
```

Atualmente, Sun suporta declaracoes de funcao no nivel superior, parametros
tipados, variaveis locais tipadas, chamadas, `print`, strings, numeros,
booleanos, operadores aritmeticos e de comparacao, f-strings, `%v`, `if`,
`elif`, `else` e a biblioteca `math`. Funcoes nao retornam valores. Ainda nao
ha entrada interativa, loops, funcoes aninhadas, atribuicao apos declaracao ou
comentarios no fim de uma linha. Operadores logicos `and`/`or`/`not` tambem nao
sao suportados nesta etapa.

## 8. Erros e acessibilidade

Mensagens de erro incluem o numero da linha para ajudar a localizar o problema.
Por exemplo:

```text
sun: line 3: error: unknown function 'desconhecida'
```

As mensagens sao texto simples, sem depender de cores, e os erros vao para
`stderr`; isso permite ler a saida em terminais simples e encaminha-la para
ferramentas assistivas. Use `-h` ou `--help` para consultar as opcoes sem
precisar memoriza-las. A indentacao consistente e nomes descritivos ajudam a
manter os exemplos faceis de navegar.

Problemas comuns:

- **`source file must have the .su extension`**: confirme a extensao do arquivo.
- **`missing 'end'`**: adicione `end` para fechar cada funcao ou bloco `if`.
- **`unknown value`**: dentro da funcao, interpolacoes devem usar um
  parametro declarado no cabecalho ou uma variavel local ja declarada.
- **`not enough %v values`**: adicione um argumento `%v=...` para cada marcador.
- **Erro ao compilar C++**: confirme se `g++`/`clang++` esta instalado ou informe
  o compilador com `--compiler`.

## 9. Executar os testes

Com Python 3 instalado:

```sh
make test
```
