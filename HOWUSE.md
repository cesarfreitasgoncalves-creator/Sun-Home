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

As chaves duplas `{{` e `}}` representam chaves literais. No prototipo atual,
uma interpolacao aceita somente o nome de um parametro da funcao; expressoes
arbitrarias e variaveis locais ainda nao existem.

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
exatamente um argumento `%v=...`; todos os valores devem ser expressoes de
texto. Use `%%` para escrever um sinal de porcentagem literal quando estiver
usando argumentos formatados.

## 6. Comentarios e regras atuais

Linhas vazias e comentarios de linha inteira iniciados por `#` sao ignorados:

```sun
# Este e um comentario
print("Ola!")
```

Atualmente, Sun suporta declaracoes de funcao no nivel superior, parametros de
texto, chamadas, `print`, strings, concatenacao, f-strings e `%v`. Funcoes nao
retornam valores. Ainda nao ha variaveis locais, numeros, entrada interativa,
condicoes, loops, funcoes aninhadas ou comentarios no fim de uma linha.

## 7. Erros e acessibilidade

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
- **`missing 'end'`**: adicione `end` para fechar cada funcao.
- **`unknown string value`**: dentro da funcao, interpolacoes devem usar um
  parametro declarado no cabecalho.
- **`not enough %v values`**: adicione um argumento `%v=...` para cada marcador.
- **Erro ao compilar C++**: confirme se `g++`/`clang++` esta instalado ou informe
  o compilador com `--compiler`.

## 8. Executar os testes

Com Python 3 instalado:

```sh
make test
```
