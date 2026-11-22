# Implementação 7 – Projetando uma Memória Virtual

**Aluno:** Márcio Rodriguez Perez de Farias

Implementação em C do *Designing a Virtual Memory Manager* (Silberschatz et al., *Operating System Concepts*, 10ª ed., P-51), com as modificações do enunciado:

- memória física com **128 frames**, preenchidos do 0 ao 127;
- substituição de página **fifo** ou **lru**;
- substituição da TLB (16 entradas) **fifo** ou **lru**;
- busca na TLB com **uma thread para cada entrada**;
- saída no arquivo `correct.txt`.

## Arquivos

| Arquivo | Descrição |
|---|---|
| `mrpf/vm.c` | Código-fonte do gerenciador de memória virtual |
| `mrpf/Makefile` | Compilação (`make`) e limpeza (`make clean`) |
| `evidencias.log` | Registro do terminal com a compilação, as execuções e os testes de erro |
| `mrpf.pdf` | Relatório |

## Compilação

```bash
cd mrpf
make        
make clean   
```

## Execução

```bash
./vm <arquivo_de_enderecos> <fifo|lru> <fifo|lru>
```

- 1º argumento: arquivo de endereços lógicos (um número de 0 a 65535 por linha)
- 2º argumento: algoritmo de substituição de **página**
- 3º argumento: algoritmo de substituição da **TLB**

Exemplo (página com LRU e TLB com FIFO):

```bash
./vm addresses.txt lru fifo
cat correct.txt
```

O arquivo `BACKING_STORE.bin` precisa estar na mesma pasta do executável. A saída é gravada em `correct.txt`.

## Tratamento de erros

O programa mostra uma mensagem e encerra quando:

- o número de argumentos é diferente de 3;
- o 2º ou o 3º argumento não é `fifo` nem `lru`;
- o arquivo de endereços não existe ou não pode ser aberto;
- o arquivo de endereços está vazio ou tem uma linha fora do formato (texto, número negativo ou maior que 65535);
- o `BACKING_STORE.bin` não existe.

## Evidências

O arquivo `evidencias.log` foi gerado com o comando `script`, que grava tudo o que
acontece no terminal. Ele mostra a compilação, as execuções com os algoritmos e os testes de erro.
