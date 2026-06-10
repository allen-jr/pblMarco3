#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <dirent.h>

#include "driver.h"
#include "aplicacao.h"

typedef struct {
    char *caminho_completo;
    char *nome_ficheiro;
    int rotulo_esperado;
} ItemBenchmark;

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
    m->soma_lat += lat_ms;
    m->soma_lat2 += lat_ms * lat_ms;
    if (lat_ms < m->lat_min) m->lat_min = lat_ms;
    if (lat_ms > m->lat_max) m->lat_max = lat_ms;
    if (acerto) m->acertos++;
}

static double metricas_diff_ms(struct timespec t0, struct timespec t1) {
    double seg = (double)(t1.tv_sec - t0.tv_sec) * 1000.0;
    double nano = (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    return seg + nano;
}

static void imprimir_metricas(const MetricasLocais *m, double tempo_total_ms) {
    int validas = m->total;
    double lat_media = (validas > 0) ? m->soma_lat / validas : 0.0;
    double variancia = (validas > 1) ? (m->soma_lat2 - (m->soma_lat * m->soma_lat) / validas) / (validas - 1) : 0.0;
    double desvio = (variancia > 0.0) ? sqrt(variancia) : 0.0;
    double throughput = (tempo_total_ms > 0.0) ? validas / (tempo_total_ms / 1000.0) : 0.0;
    double acuracia = (validas > 0) ? (double)m->acertos / validas * 100.0 : 0.0;
    printf("===================================\n");
    printf("RESULTADOS DO BENCHMARK\n");
    printf("===================================\n");
    printf("Total de imagens: %d\n", validas + m->falhas);
    printf("Acertos         : %d\n", m->acertos);
    printf("Acuracia        : %.2f%%\n", acuracia);
    printf("Latencia media  : %.3f ms\n", lat_media);
    printf("Desvio padrao   : %.3f ms\n", desvio);
    printf("Throughput      : %.2f imagens/s\n", throughput);
    printf("===================================\n");
}

int modo_benchmark(const char *dir_raiz, int n_imagens_total, const char *csv_saida) {
    ItemBenchmark *lista = NULL;
    int total_encontrado = 0;
    int capacidade = 128;
    lista = malloc(capacidade * sizeof(ItemBenchmark));
    if (!lista) {
        printf("ERRO: Falha ao alocar memória inicial.\n");
        return 1;
    }
    int limite_por_subpasta = n_imagens_total / 10;
    if (limite_por_subpasta < 1) limite_por_subpasta = 1;
    for (int digito = 0; digito <= 9; digito++) {
        char caminho_subpasta[512];
        snprintf(caminho_subpasta, sizeof(caminho_subpasta), "%s/%d", dir_raiz, digito);
        DIR *d = opendir(caminho_subpasta);
        if (!d) continue;
        char **arquivos_subpasta = NULL;
        int qtd_arquivos = 0;
        int cap_arquivos = 32;
        arquivos_subpasta = malloc(cap_arquivos * sizeof(char *));
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            const char *nome = ent->d_name;
            if (strcmp(nome, ".") == 0 || strcmp(nome, "..") == 0) continue;
            size_t len = strlen(nome);
            if (!(len > 4 && (strcmp(nome + len - 4, ".png") == 0 || strcmp(nome + len - 4, ".bin") == 0))) {
                continue;
            }
            if (qtd_arquivos == cap_arquivos) {
                cap_arquivos *= 2;
                arquivos_subpasta = realloc(arquivos_subpasta, cap_arquivos * sizeof(char *));
            }
            arquivos_subpasta[qtd_arquivos++] = strdup(nome);
        }
        closedir(d);
        if (qtd_arquivos > 1) {
            for (int i = qtd_arquivos - 1; i > 0; i--) {
                int j = rand() % (i + 1);
                char *temp = arquivos_subpasta[j];
                arquivos_subpasta[j] = arquivos_subpasta[i];
                arquivos_subpasta[i] = temp;
            }
        }
        int pegar_quantos = (qtd_arquivos < limite_por_subpasta) ? qtd_arquivos : limite_por_subpasta;
        for (int i = 0; i < pegar_quantos; i++) {
            char *nome = arquivos_subpasta[i];
            if (total_encontrado == capacidade) { 
                capacidade *= 2;
                ItemBenchmark *temp = realloc(lista, capacidade * sizeof(ItemBenchmark));
                if (!temp) {
                    printf("ERRO: Falha ao realocar memória.\n");
                    return 1;
                }
                lista = temp;
            }
            size_t len = strlen(nome);
            size_t tam_caminho = strlen(caminho_subpasta) + 1 + len + 1;
            lista[total_encontrado].caminho_completo = malloc(tam_caminho);
            snprintf(lista[total_encontrado].caminho_completo, tam_caminho, "%s/%s", caminho_subpasta, nome);
            lista[total_encontrado].nome_ficheiro = strdup(nome);
            lista[total_encontrado].rotulo_esperado = digito;
            total_encontrado++;
        }
        for (int i = 0; i < qtd_arquivos; i++) {
            free(arquivos_subpasta[i]);
        }
        free(arquivos_subpasta);
    }
    if (total_encontrado == 0) {
        printf("ERRO: nenhum arquivo encontrado nas subpastas\n");
        free(lista);
        return 1;
    }
    int executar = total_encontrado; 
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
    if (csv) fclose(csv);
    for (int i = 0; i < total_encontrado; i++) {
        free(lista[i].caminho_completo);
        free(lista[i].nome_ficheiro);
    }
    free(lista);
    imprimir_metricas(&m, tempo_total_ms);
    return 0;
}