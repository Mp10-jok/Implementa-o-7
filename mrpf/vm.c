#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>

#define PAGE_SIZE 256 
#define NUM_PAGES 256 
#define NUM_FRAMES 128
#define TLB_SIZE 16
#define MAX_ADDRESS 65535
#define BACKING_STORE "BACKING_STORE.bin"
#define OUTPUT_FILE "correct.txt"
#define LINE_BUF 256

typedef enum{ 
    ALG_FIFO, ALG_LRU 
} Algorithm;

static int page_table[NUM_PAGES];

static signed char memory[NUM_FRAMES][PAGE_SIZE];
static int frame_page[NUM_FRAMES];          
static long frame_loaded[NUM_FRAMES];       
static long frame_used[NUM_FRAMES];
static int frames_in_use = 0;

typedef struct{
    int valid;
    int page;
    int frame;
    long loaded;
    long used;
} TLBEntry;

static TLBEntry tlb[TLB_SIZE];

static long clock_tick = 0; 
static int page_faults = 0;
static int tlb_hits = 0;

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
        for(int i = 0; ok && i < len; i++){
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
    if(n_addresses == 0){
        fatal("o arquivo de endereços está vazio", path);
    }
}

typedef struct{
    int index;      
    int page;       
    int *found;     
} TLBSearchArg;

static void *tlb_search_entry(void *arg){
    TLBSearchArg *a = (TLBSearchArg *)arg;
    if(tlb[a->index].valid && tlb[a->index].page == a->page){
        *a->found = a->index;
    }   
    return NULL;
}

static int tlb_lookup(int page){
    pthread_t threads[TLB_SIZE];
    TLBSearchArg args[TLB_SIZE];
    int found = -1;

    for(int i = 0; i < TLB_SIZE; i++){
        args[i].index = i;
        args[i].page = page;
        args[i].found = &found;
        if(pthread_create(&threads[i], NULL, tlb_search_entry, &args[i]) != 0){
            fatal("não foi possível criar a thread de busca na TLB", NULL);
        }       
    }
    for(int i = 0; i < TLB_SIZE; i++){
        pthread_join(threads[i], NULL);
    }
    return found;
}

static void tlb_insert(int page, int frame, Algorithm tlb_alg){
    int victim = -1;

    for(int i = 0; i < TLB_SIZE; i++){
        if(!tlb[i].valid){ 
            victim = i; break; 
        }
    }
        
    if(victim == -1){
        victim = 0;
        for(int i = 1; i < TLB_SIZE; i++){
            long a = (tlb_alg == ALG_FIFO) ? tlb[i].loaded : tlb[i].used;
            long b = (tlb_alg == ALG_FIFO) ? tlb[victim].loaded : tlb[victim].used;
            if (a < b) victim = i;
        }
    }

    tlb[victim].valid = 1;
    tlb[victim].page = page;
    tlb[victim].frame = frame;
    tlb[victim].loaded = clock_tick;
    tlb[victim].used = clock_tick;
}

static void tlb_invalidate(int page){
    for(int i = 0; i < TLB_SIZE; i++){
        if(tlb[i].valid && tlb[i].page == page){
            tlb[i].valid = 0;
        } 
    }       
}

static int choose_victim_frame(Algorithm page_alg){
    int victim = 0;
    for(int i = 1; i < NUM_FRAMES; i++){
        long a = (page_alg == ALG_FIFO) ? frame_loaded[i] : frame_used[i];
        long b = (page_alg == ALG_FIFO) ? frame_loaded[victim] : frame_used[victim];
        if(a < b){
            victim = i;
        }
    }
    return victim;
}

static int handle_page_fault(int page, FILE *store, Algorithm page_alg){
    int frame;

    if(frames_in_use < NUM_FRAMES){
        frame = frames_in_use++;            
    } else{
        frame = choose_victim_frame(page_alg);
        int old_page = frame_page[frame];
        page_table[old_page] = -1;          
        tlb_invalidate(old_page);           
    }

    if(fseek(store, (long)page * PAGE_SIZE, SEEK_SET) != 0 || fread(memory[frame], 1, PAGE_SIZE, store) != PAGE_SIZE){
        fatal("falha ao ler a página do " BACKING_STORE, NULL);
    }

    page_table[page] = frame;
    frame_page[frame] = page;
    frame_loaded[frame] = clock_tick;
    page_faults++;
    return frame;
}

int main(int argc, char *argv[]){
    Algorithm page_alg, tlb_alg;

    if(argc != 4){
        fprintf(stderr, "Erro: número de argumentos inválido (recebido %d, esperado 3)\n", argc - 1);
        usage();
        return EXIT_FAILURE;
    }
    if(!parse_algorithm(argv[2], &page_alg)){
        fprintf(stderr, "Erro: algoritmo de substituição de página inválido: '%s' (use fifo ou lru)\n", argv[2]);
        usage();
        return EXIT_FAILURE;
    }
    if(!parse_algorithm(argv[3], &tlb_alg)){
        fprintf(stderr, "Erro: algoritmo de substituição da TLB inválido: '%s' (use fifo ou lru)\n", argv[3]);
        usage();
        return EXIT_FAILURE;
    }

    load_addresses(argv[1]);

    FILE *store = fopen(BACKING_STORE, "rb");
    if(!store){
        perror("Erro ao abrir o " BACKING_STORE);
        free(addresses);
        return EXIT_FAILURE;
    }

    FILE *out = fopen(OUTPUT_FILE, "w");
    if(!out){
        perror("Erro ao criar o " OUTPUT_FILE);
        fclose(store);
        free(addresses);
        return EXIT_FAILURE;
    }

    for(int i = 0; i < NUM_PAGES; i++){
        page_table[i] = -1;
    }
    memset(tlb, 0, sizeof(tlb));

    for(int i = 0; i < n_addresses; i++){
        clock_tick++;
        int logical = addresses[i];
        int page = (logical >> 8) & 0xFF;
        int offset = logical & 0xFF;
        int frame;

        int t = tlb_lookup(page);
        if(t != -1){
            tlb_hits++;
            frame = tlb[t].frame;
            tlb[t].used = clock_tick;
        } else{
            frame = page_table[page];
            if (frame == -1){
                frame = handle_page_fault(page, store, page_alg);  
            }
            tlb_insert(page, frame, tlb_alg);
        }
        frame_used[frame] = clock_tick;

        int physical = frame * PAGE_SIZE + offset;
        signed char value = memory[frame][offset];
        fprintf(out, "Virtual address: %d Physical address: %d Value: %d\n", logical, physical, value);
    }

    fprintf(out, "Number of Translated Addresses = %d\n", n_addresses);
    fprintf(out, "Page Faults = %d\n", page_faults);
    fprintf(out, "Page Fault Rate = %.3f\n", (double)page_faults / n_addresses);
    fprintf(out, "TLB Hits = %d\n", tlb_hits);
    fprintf(out, "TLB Hit Rate = %.3f\n", (double)tlb_hits / n_addresses);

    fclose(out);
    fclose(store);
    free(addresses);
    return EXIT_SUCCESS;
}