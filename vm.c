#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_ADDRESS 65535
#define LINE_BUF 256

typedef enum{ 
    ALG_FIFO, ALG_LRU 
} Algorithm;

typedef struct{
    int valid;
    int page;
    int frame;
    long loaded;
    long used;
} TLBEntry;

static int *addresses = NULL;
static int n_addresses = 0;

static void fatal(const char *msg, const char *detail){
    if(detail){
        fprintf(stderr, "Erro: %s: %s\n", msg, detail);
    } else{
        fprintf(stderr, "Erro: %s\n", msg);
    }
    free(addresses);
    exit(EXIT_FAILURE);
}

static void usage(void){
    fprintf(stderr, "Uso: ./vm <arquivo_de_enderecos> <fifo|lru> <fifo|lru>\n");
    fprintf(stderr, "  2o argumento: algoritmo de substituição de página\n");
    fprintf(stderr, "  3o argumento: algoritmo de substituição da TLB\n");
    fprintf(stderr, "Exemplo: ./vm address.txt lru fifo\n");
}

static int parse_algorithm(const char *s, Algorithm *alg){
    if(strcmp(s, "fifo") == 0){
        *alg = ALG_FIFO;
        return 1;
    }
    if(strcmp(s, "lru") == 0){
        *alg = ALG_LRU;
        return 1;
    }
    return 0;
}

static void load_addresses(const char *path){
    FILE *f = fopen(path, "r");
    if(!f){
        perror("Erro ao abrir o arquivo de endereços");
        fprintf(stderr, "Arquivo: %s\n", path);
        exit(EXIT_FAILURE);
    }

    char line[LINE_BUF];
    int capacity = 1024, line_no = 0;
    addresses = malloc(capacity * sizeof(int));
    if(!addresses){
        fclose(f);
        fatal("memória insuficiente", NULL);
    }

    while(fgets(line, sizeof(line), f)){
        line_no++;

        if(!strchr(line, '\n') && !feof(f)){
            fclose(f);
            fprintf(stderr, "Erro: linha %d do arquivo de endereços é longa demais\n", line_no);
            free(addresses);
            exit(EXIT_FAILURE);
        }

        line[strcspn(line, "\r\n")] = '\0';

        int len = (int)strlen(line);
        int ok = len > 0 && len <= 5;
        for (int i = 0; ok && i < len; i++){
            if(!isdigit((unsigned char)line[i])) ok = 0;
        }

        long value = ok ? strtol(line, NULL, 10) : -1;
        if(!ok || value < 0 || value > MAX_ADDRESS){
            fclose(f);
            fprintf(stderr, "Erro: linha %d do arquivo de endereços inválida: '%s'\n", line_no, line);
            fprintf(stderr, "Formato esperado: um número inteiro de 0 a %d por linha\n", MAX_ADDRESS);
            free(addresses);
            exit(EXIT_FAILURE);
        }

        if(n_addresses == capacity){
            capacity *= 2;
            int *tmp = realloc(addresses, capacity * sizeof(int));
            if(!tmp){ 
                fclose(f); fatal("memória insuficiente", NULL); 
            }
            addresses = tmp;
        }
        addresses[n_addresses++] = (int)value;
    }
    fclose(f);
    if (n_addresses == 0){
        fatal("o arquivo de endereços está vazio", path);
    }
}