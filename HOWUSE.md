# Como usar a Sun

Este guia mostra como compilar o compilador, transpilar um arquivo `.su`,
gerar um executavel e escrever programas com a sintaxe atualmente suportada.

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

O core esta dividido em `src/main.cpp` (CLI), `src/lexer.cpp` (tokens),
`src/parser.cpp` (sintaxe e verificacao de tipos), `src/codegen.cpp` (geracao
de C++) e `src/compiler.hpp` (tipos e interface entre os modulos).

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

Coloque `f` antes das aspas e o nome de um parametro ou variavel local entre
chaves:

```sun
func mostrar(nome)
    print(f"Ola, {nome}!")
    print(f"Chaves literais: {{ e }}")
end
```

As chaves duplas `{{` e `}}` representam chaves literais. A interpolacao aceita
nomes de valores, mas nao expressoes arbitrarias dentro das chaves.

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

Parametros podem declarar `string`, `number`, `bool`, `int`, `char` e tipos de
lista como `list[number]` ou `list[int]`; `region` continua dependendo de
`import memory`. Parametros sem tipo continuam sendo `string` para manter
compatibilidade com programas anteriores. Variaveis locais usam `let` e exigem
tipo explicito:

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

As variaveis declaradas com `let` e os parametros podem ser reatribuidos com
`nome = expressao`; a expressao precisa ter o mesmo tipo:

```sun
func contar(): number
    let total: number = 0
    total = total + 1
    return total
end
```

Operadores aritmeticos: `+`, `-`, `*`, `/` e `%` (resto). `+` tambem concatena
duas strings. `int` e um inteiro assinado de 64 bits: sua divisao trunca em
direcao a zero e overflow ou divisao por zero geram erro em runtime.
`int(numero)` converte apenas valores inteiros dentro do intervalo de `int`;
`number(inteiro)` converte explicitamente para `number`, e `int` pode ser
promovido a `number` em expressoes. `char` representa um byte; `char("A")`
converte uma string de exatamente um byte.

Comparacoes disponiveis: `==`, `!=`, `<`, `<=`, `>` e `>=`. Comparacoes
ordenadas aceitam numeros; igualdade aceita valores do mesmo tipo e pares
`int`/`number`. Condicoes de `if`/`elif` precisam resultar em `bool`, por
exemplo uma comparacao, `true`/`false` ou um parametro booleano. Blocos `if`
podem ter varios `elif` e um `else`; todo o conjunto termina com `end`.

Operadores logicos `not`, `and` e `or` aceitam somente valores booleanos.
`not` tem precedencia acima de `and`, que tem precedencia acima de `or`.
`and` e `or` usam curto-circuito: a expressao da direita so e avaliada quando
necessario.

### Entrada tipada

`input(prompt, tipo)` mostra o prompt e le uma linha. Se o tipo for omitido,
`input(prompt)` retorna `string` (tambem aceita o alias `str`). Para conversao,
o tipo pode ser `number`, `int`, `bool` ou `char`. Valores invalidos geram erro
de runtime:

```sun
let nome: string = input("Nome: ")
let idade: int = input("Idade: ", int)
let ativo: bool = input("Ativo (true/false): ", bool)
let inicial: char = input("Inicial: ", char)
print(f"{nome}: {idade}, ativo={ativo}, inicial={inicial}")
```

`bool` aceita exatamente `true` ou `false`; `char` aceita exatamente um byte.
O prompt precisa ser uma expressao `string`.

### Listas

Listas sao homogeneas, de uma dimensao e devem declarar seu tipo:

```sun
let notas: list[number] = [8.5, 9.0]
let valores: list[int] = []
valores.append(int(12))
valores.append(int(7))
valores[0] = int(10)

for valor in valores:
    print(valor)
end

print(len(notas))
```

Os tipos de elemento aceitos sao `number`, `int`, `string`, `bool` e `char`.
Indices comecam em zero; leitura e escrita fora dos limites, ou com indice
negativo/fracionario, geram erro de runtime. `append` exige um item do mesmo
tipo. `len` aceita listas e strings. Listas podem ser passadas e retornadas de
funcoes e impressas. Listas vazias precisam de uma anotacao de tipo; listas
aninhadas e listas de `region` nao sao suportadas.

Loops usam condicoes booleanas, iteracao por lista e iteracao numerica por
intervalo:

```sun
let i: number = 0
while i < 3:
    print(i)
    i = i + 1
end

for n in range(1, 4):
    print(n)
end
```

`while` repete enquanto a condicao for verdadeira. `range(inicio, fim)` inclui
o inicio e exclui o fim. Pode-se passar um terceiro argumento para o passo;
passos negativos contam para tras e o passo zero gera um erro em tempo de
execucao. Um passo que nao consegue avancar o valor atual tambem gera erro,
evitando loop infinito por precisao de ponto flutuante. Os valores de `range`
sao `number` (`double`), nao inteiros.

Funcoes podem declarar retorno com `: tipo` no cabecalho, usando tipos escalares
ou listas suportados (e `region` com `import memory`). Sem anotacao, a funcao
retorna `void`. Todo caminho de uma funcao com retorno tipado deve retornar um
valor do tipo declarado:

```sun
func dobro(valor: number): number
    return valor * 2
end

let resultado: number = dobro(21)
```

Funcoes com retorno podem ser chamadas em expressoes. `return` sem valor so e
valido em funcoes sem tipo de retorno.

Parametros podem ter valores padrao no final da lista:

```sun
func cumprimentar(nome: string = "mundo", vezes: number = 1)
    for i in range(0, vezes):
        print(f"Ola, {nome}!")
    end
end

cumprimentar()
cumprimentar("Sun", 2)
```

Parametros com default precisam ser os ultimos. Os defaults aceitam expressoes
constantes do tipo declarado, mas nao podem depender de variaveis nem chamar
funcoes. Ao chamar a funcao, pode-se omitir qualquer sufixo de parametros que
tenha default.

`istype(valor, tipo)` compara o tipo estatico de um valor com `string`,
`number`, `bool`, `int`, `char` ou `region` (que requer `import memory`) e
devolve `bool`:

```sun
let idade: number = 20
print(istype(idade, number))
print(istype(idade, string))
```

Como Sun e estaticamente tipada, `istype` e decidido durante a compilacao e
gera `true` ou `false`; nao e uma verificacao dinamica de valores.

Dentro de `while` e `for`, `break` encerra o loop atual e `continue` avanca para
a proxima iteracao:

```sun
for n in range(0, 10):
    if n == 2:
        continue
    end
    if n == 5:
        break
    end
    print(n)
end
```

Essas instrucoes geram erro se forem usadas fora de um loop.

`import math` deve aparecer antes das declaracoes e instrucoes. Ele habilita as
funcoes `math.abs`, `math.ceil`, `math.cos`, `math.exp`, `math.floor`,
`math.log`, `math.pow`, `math.sin`, `math.sqrt` e `math.tan`. Essas funcoes
recebem numeros; todas, exceto `math.pow`, recebem um argumento. A biblioteca e
implementada usando `<cmath>` do C++.

## 7. Biblioteca `memory`

`import memory` habilita regioes de bytes gerenciadas, sem expor enderecos
arbitrarios nem ponteiros nativos. Cada alocacao e limitada a 64 MiB; offsets
precisam ser numeros inteiros dentro dos limites da regiao e os valores escritos
precisam estar entre 0 e 255:

```sun
import memory

let buffer: region = memory.alloc(4)
memory.write_u8(buffer, 0, 65)
let first_byte: number = memory.read_u8(buffer, 0)
print(first_byte)
print(memory.size(buffer))
```

As operacoes disponiveis sao `memory.alloc(tamanho)`, `memory.size(regiao)`,
`memory.read_u8(regiao, offset)` e a instrucao
`memory.write_u8(regiao, offset, valor)`. `alloc` aceita tamanhos inteiros de
0 a 67108864; regioes com tamanho zero nao tem offsets legiveis ou gravaveis.
Leitura e escrita fora dos limites, offsets fracionarios/negativos e valores
byte invalidos geram erro em tempo de execucao.

Uma regiao atribuida a outra variavel ou passada para uma funcao referencia a
mesma alocacao. A memoria e liberada automaticamente quando nao houver mais
regioes referenciando-a. O tipo `region` nao pode ser impresso, formatado ou
comparado diretamente.

## 8. C++ inline

Blocos C++ literais podem ser usados para interoperabilidade e funcionalidades
que ainda nao existem na linguagem. O terminador e `endcpp`:

```sun
inline cpp:
#include <iostream>
int helper_cpp() { return 42; }
endcpp

func mostrar(valor: number)
    let resposta: number = valor + 2
    inline cpp:
        std::cout << valor << " + 2 = " << resposta << '\n';
    endcpp
end

mostrar(40)
```

Um bloco no nivel superior e copiado para o escopo global do C++ gerado; use
esse local para `#include`, declaracoes e funcoes C++. Um bloco dentro de uma
funcao ou condicional e copiado para aquele ponto do corpo e pode ler ou alterar
os parametros e variaveis Sun visiveis ali, usando seus nomes originais.
Identificadores C++ reservados nao podem ser expostos como aliases.

O trecho inline precisa ser C++ valido e compativel com o compilador escolhido.
Ele e codigo nativo sem isolamento: pode afetar qualquer estado acessivel ao
programa. Sun nao valida nem torna seguro esse C++.

## 9. Comentarios e regras atuais

Linhas vazias e comentarios de linha inteira iniciados por `#` sao ignorados:

```sun
# Este e um comentario
print("Ola!")
```

Atualmente, Sun suporta declaracoes de funcao no nivel superior, parametros
tipados, variaveis locais tipadas e mutaveis, chamadas, `print`, strings,
numeros, `int`, `char`, listas homogeneas, entrada tipada com `input`, operadores
aritmeticos, de comparacao e logicos, f-strings, `%v`, `if`/`elif`/`else`,
loops `while`, `for` por `range` e `for` por lista, retornos tipados,
`try`/`catch` para erros de runtime Sun, as bibliotecas `math` e `memory`,
`istype`, parametros com valores padrao e C++ inline. Ainda nao ha structs,
enums, funcoes aninhadas ou comentarios no fim de uma linha.

## 10. Erros e acessibilidade

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

Use `try`/`catch nome:` para capturar erros de runtime produzidos por Sun
(por exemplo, conversao ou indice invalido). O nome do `catch` recebe a
mensagem como `string`:

```sun
try:
    let idade: int = input("Idade: ", int)
catch erro:
    print(f"Entrada invalida: {erro}")
end
```

`catch` nao captura erros de sintaxe/tipo da compilacao nem excecoes arbitrarias
de C++ inline.

Problemas comuns:

- **`source file must have the .su extension`**: confirme a extensao do arquivo.
- **`missing 'end'`**: adicione `end` para fechar cada funcao, condicional ou loop.
- **`missing 'endcpp'`**: feche cada bloco C++ com `endcpp`.
- **`range step cannot be zero`**: use um passo diferente de zero em `range`.
- **`unknown value`**: use um parametro declarado ou variavel local ja declarada.
- **`not enough %v values`**: adicione um argumento `%v=...` para cada marcador.
- **Erro ao compilar C++**: confirme se `g++`/`clang++` esta instalado ou informe
  o compilador com `--compiler`.

## 11. Executar os testes

Com Python 3 instalado:

```sh
make test
```
