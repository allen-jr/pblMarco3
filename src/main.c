#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <termios.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "driver.h"
#include "main.h"

extern uint8_t *imagem_ptr;
extern uint8_t *base_virtual;

typedef struct {
    char *caminho_completo;
    char *nome_ficheiro;
    int rotulo_esperado;
} ItemBenchmark;

int png_carregar(const char *caminho, uint8_t pixels[784]) {
    int w, h, canais;
    uint8_t *dados = stbi_load(caminho, &w, &h, &canais, STBI_grey);
    if (!dados) return -1;
    if (w != 28 || h != 28) { stbi_image_free(dados); return -1; }
    memcpy(pixels, dados, 784);
    stbi_image_free(dados);
    return 0;
}

int png_carregar_bin(const char *caminho, uint8_t pixels[784]) {
    FILE *fp = fopen(caminho, "rb");
    if (!fp) return -1;
    size_t lido = fread(pixels, 1, 784, fp);
    fclose(fp);
    return (lido == 784) ? 0 : -1;
}

typedef struct {
    int total;
    int acertos;
    int falhas;
    double soma_lat;
    double soma_lat2;
    double lat_min;
    double lat_max;
} MetricasLocais;

static void metricas_inicializar(MetricasLocais *m) {
    memset(m, 0, sizeof(*m));
    m->lat_min = 1e18;
    m->lat_max = 0.0;
}

static void metricas_registrar(MetricasLocais *m, double lat_ms, int acerto) {
    m->total++;
    m->soma_lat  += lat_ms;
    m->soma_lat2 += lat_ms * lat_ms;
    if (lat_ms < m->lat_min) m->lat_min = lat_ms;
    if (lat_ms > m->lat_max) m->lat_max = lat_ms;
    if (acerto) m->acertos++;
}

static double metricas_diff_ms(struct timespec t0, struct timespec t1) {
    double seg  = (double)(t1.tv_sec  - t0.tv_sec)  * 1000.0;
    double nano = (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    return seg + nano;
}

static void imprimir_metricas(const MetricasLocais *m, double tempo_total_ms) {
    int validas = m->total;
    double lat_media = (validas > 0) ? m->soma_lat / validas : 0.0;
    double variancia = (validas > 1) ? (m->soma_lat2 - (m->soma_lat * m->soma_lat) / validas) / (validas - 1) : 0.0;
    double desvio     = (variancia > 0.0) ? sqrt(variancia) : 0.0;
    double throughput = (tempo_total_ms > 0.0) ? validas / (tempo_total_ms / 1000.0) : 0.0;
    double acuracia   = (validas > 0) ? (double)m->acertos / validas * 100.0 : 0.0;
    printf("===================================\n");
    printf("RESULTADOS DO BENCHMARK\n");
    printf("===================================\n");
    printf("Acuracia      : %.2f%%\n", acuracia);
    printf("Latencia media: %.3f ms\n", lat_media);
    printf("Desvio padrao : %.3f ms\n", desvio);
    printf("Throughput    : %.2f imagens/s\n", throughput);
    printf("===================================\n");
}

static int carregar_e_inferir(const uint8_t *pixels, int *digito_out) {
    reset_clean_fpga();
    imagem_ptr = (uint8_t *)pixels;
    if (enviar_imagem() < 0)  return -1;
    if (inferencia()    < 0)  return -1;
    int d = ler_resultado();
    if (d < 0) return -1;
    *digito_out = d;
    reset_clean_fpga();
    return 0;
}

void limpar_tela(void) {
    if (base_virtual == NULL) {
        return;
    }
    volatile uint32_t *vga_data_reg = (volatile uint32_t *)(base_virtual + 0x30);
    int max_x = 320; 
    int max_y = 240;
    uint32_t r = 0, g = 0, b = 0;
    for (int y = 0; y < max_y; y++) {
        for (int x = 0; x < max_x; x++) {
            uint32_t pacote_vga = (x & 0x1FF) | ((y & 0xFF) << 9) | ((r & 0x7) << 17) | ((g & 0x7) << 20) | ((b & 0x7) << 23) | (1 << 26); 
            *vga_data_reg = pacote_vga;
            for (volatile int delay = 0; delay < 10; delay++);
        }
    }
}

int modo_arquivo(const char *caminho_arquivo) {
    uint8_t pixels[784];
    int ok_carga = -1;
    size_t len = strlen(caminho_arquivo);
    if (len > 4 && strcmp(caminho_arquivo + len - 4, ".bin") == 0) {
        ok_carga = png_carregar_bin(caminho_arquivo, pixels);
    } else {
        ok_carga = png_carregar(caminho_arquivo, pixels);
    }
    if (ok_carga < 0) {
        printf("ERRO: arquivo nao encontrado\n");
        return 1;
    }
    if (base_virtual != NULL) {
        limpar_tela();
        volatile uint32_t *vga_data_reg = (volatile uint32_t *)(base_virtual + 0x30);
        int vga_largura = 320;
        int vga_altura  = 240;
        int tamanho_quadrado = 240; 
        int offset_x = (vga_largura - tamanho_quadrado) / 2; // Exatamente 40
        for (int y = 0; y < vga_altura; y++) {
            int orig_y = (y * 28) / tamanho_quadrado;
            if (orig_y > 27) orig_y = 27;
            for (int x = 0; x < vga_largura; x++) {
                uint32_t r = 0, g = 0, b = 0;
                if (x >= offset_x && x < (offset_x + tamanho_quadrado)) {
                    int quadrado_x = x - offset_x;
                    int orig_x = (quadrado_x * 28) / tamanho_quadrado;
                    if (orig_x > 27) orig_x = 27;
                    uint8_t tom_de_cinza = pixels[orig_y * 28 + orig_x];
                    r = (tom_de_cinza >> 5) & 0x7;
                    g = (tom_de_cinza >> 5) & 0x7;
                    b = (tom_de_cinza >> 5) & 0x7;
                }
                uint32_t pacote_escreve = (x & 0x1FF) | ((y & 0xFF) << 9) | ((r & 0x7) << 17) | ((g & 0x7) << 20) | ((b & 0x7) << 23) | (1 << 26);
                uint32_t pacote_desliga = pacote_escreve & ~(1 << 26); // Enable = 0
                *vga_data_reg = pacote_escreve;
                for (volatile int d = 0; d < 40; d++); // Espera a FPGA capturar o estado WRITE
                *vga_data_reg = pacote_desliga;
                for (volatile int d = 0; d < 40; d++); // Janela de segurança para o próximo pixel
            }
        }
    }
    int digito_predito;
    if (carregar_e_inferir(pixels, &digito_predito) < 0) {
        return 1;
    }
    printf("===================================\n");
    printf("Digito Previsto: %d\n", digito_predito);
    printf("===================================\n");
    return 0;
}

int modo_desenho(void) {
   return 0;
}

int modo_benchmark(const char *dir_raiz, int n_imagens, const char *csv_saida) {
    ItemBenchmark *lista = NULL;
    int total_encontrado = 0;
    int capacidade = 128;
    lista = malloc(capacidade * sizeof(ItemBenchmark));
    for (int digito = 0; digito <= 9; digito++) {
        char caminho_subpasta[512];
        snprintf(caminho_subpasta, sizeof(caminho_subpasta), "%s/%d", dir_raiz, digito);
        DIR *d = opendir(caminho_subpasta);
        if (!d) continue;
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            const char *nome = ent->d_name;
            size_t len = strlen(nome);
            if (!(len > 4 && (strcmp(nome + len - 4, ".png") == 0 || strcmp(nome + len - 4, ".bin") == 0))) {
                continue;
            }
            if (total_encontrado == capacidade) { 
                capacidade *= 2;
                lista = realloc(lista, capacidade * sizeof(ItemBenchmark));
            }
            size_t tam_caminho = strlen(caminho_subpasta) + 1 + len + 1;
            lista[total_encontrado].caminho_completo = malloc(tam_caminho);
            snprintf(lista[total_encontrado].caminho_completo, tam_caminho, "%s/%s", caminho_subpasta, nome);
            lista[total_encontrado].nome_ficheiro = strdup(nome);
            lista[total_encontrado].rotulo_esperado = digito;
            total_encontrado++;
        }
        closedir(d);
    }
    if (total_encontrado == 0) {
        printf("ERRO: nenhum arquivo encontrado\n");
        free(lista);
        return 1;
    }
    int executar = (n_imagens < total_encontrado) ? n_imagens : total_encontrado;
    FILE *csv = fopen(csv_saida, "w");
    if (csv) fprintf(csv, "arquivo,subpasta_rotulo,digito_predito,acerto,latencia_ms\n");
    MetricasLocais m;
    metricas_inicializar(&m);
    struct timespec t0_total, t1_total;
    clock_gettime(CLOCK_MONOTONIC, &t0_total);
    for (int i = 0; i < executar; i++) {
        ItemBenchmark item = lista[i];
        uint8_t pixels[784];
        int ok_carga = (strcmp(item.caminho_completo + strlen(item.caminho_completo) - 4, ".bin") == 0) ? png_carregar_bin(item.caminho_completo, pixels) : png_carregar(item.caminho_completo, pixels);
        if (ok_carga < 0) {
            m.falhas++;
            if (csv) fprintf(csv, "%s,%d,-1,0,0.0\n", item.nome_ficheiro, item.rotulo_esperado);
            continue;
        }
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int digito_predito;
        int ok_inf = carregar_e_inferir(pixels, &digito_predito);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double lat_ms = metricas_diff_ms(t0, t1);
        if (ok_inf < 0) {
            m.falhas++;
            if (csv) fprintf(csv, "%s,%d,-1,0,%.3f\n", item.nome_ficheiro, item.rotulo_esperado, lat_ms);
            continue;
        }
        int acerto = (digito_predito == item.rotulo_esperado) ? 1 : 0;
        metricas_registrar(&m, lat_ms, acerto);
        printf("[%d/%d]\tsubpasta=%d\tarquivo=%s\tpredito=%d\t%s\t(%.2f ms)\n", i+1, executar, item.rotulo_esperado, item.nome_ficheiro, digito_predito, acerto ? "OK" : "ERRO", lat_ms);
        if (csv) fprintf(csv, "%s,%d,%d,%d,%.3f\n", item.nome_ficheiro, item.rotulo_esperado, digito_predito, acerto, lat_ms);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1_total);
    double tempo_total_ms = metricas_diff_ms(t0_total, t1_total);
    if (csv) {
        fclose(csv);
    }
    for (int i = 0; i < total_encontrado; i++) {
        free(lista[i].caminho_completo);
        free(lista[i].nome_ficheiro);
    }
    free(lista);
    imprimir_metricas(&m, tempo_total_ms);
    return 0;
}

static void exibir_menu(void) {
    printf("===================================\n");
    printf("MENU\n");
    printf("===================================\n");
    printf("1. Modo Arquivo\n");
    printf("2. Modo Desenho\n");
    printf("3. Modo Benchmark\n");
    printf("0. Sair do programa\n");
    printf("===================================\n");
    printf("Digite uma opcao: ");
}

int main(int argc, char *argv[]) {
    if (!inicializar_fpga()) {
        return 1;
    }
    reset_clean_fpga();
    if (enviar_bias() < 0 || enviar_beta() < 0 || enviar_pesos() < 0) {
        finalizar_fpga();
        return 1;
    }
    if (argc > 1) {
        int r = 0;
        if (strcmp(argv[1], "-a") == 0 && argc >= 3) {
            r = modo_arquivo(argv[2]);
        } else if (strcmp(argv[1], "-b") == 0 && argc >= 4) {
            r = modo_benchmark(argv[2], atoi(argv[3]), "benchmark.csv");
        }
        if (base_virtual != NULL) {
            limpar_tela();
        }
        finalizar_fpga();
        return r;
    }
    int rodando = 1;
    int op;
    while (rodando) {
        exibir_menu();       
        scanf("%d", &op);
        while (getchar() != '\n'); // Limpa o \n do menu        
        switch (op) {
            case 1: {
                char caminho_arquivo[256];
                printf("Digite o caminho da imagem (data/imagens/...): ");
                if (scanf("%255s", caminho_arquivo) == 1) {
                    while (getchar() != '\n');
                    modo_arquivo(caminho_arquivo);
                }
                break;
            }
            case 3: {
                char dir_raiz[256];
                int n_imagens;
                printf("Digite o caminho das imagens (data/imagens): ");
                if (scanf("%255s", dir_raiz) == 1) {
                    while (getchar() != '\n');
                    printf("Digite a quantidade de imagens: ");
                    if (scanf("%d", &n_imagens) == 1) {
                        while (getchar() != '\n');
                        modo_benchmark(dir_raiz, n_imagens, "benchmark.csv");
                    }
                }
                break;
            }
            case 0:
                rodando = 0;
                break;

            default:
                printf("ERRO: opcao invalida\n");
                break;
        }
    }
    if (base_virtual != NULL) {
        limpar_tela();
    }
    finalizar_fpga();
    return 0;
}