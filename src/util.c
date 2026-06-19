#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "driver.h"
#include "aplicacao.h"

extern uint8_t *imagem_ptr;
extern uint8_t *base_virtual;

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

static int obter_rotulo_real(const char *modo, const char *origem_imagem) {
    if (strcmp(modo, "Desenho") == 0) {
        return -1;
    }
    for (int d = 0; d <= 9; d++) {
        char padrao[8];
        snprintf(padrao, sizeof(padrao), "/%d/", d);
        if (strstr(origem_imagem, padrao) != NULL) {
            return d;
        }
    }
    return -1;
}

void registrar_log_csv(const char *modo, const char *origem_imagem, int digito_predito, double latencia_ms, const char *status) {
    const char *nome_arquivo_log = "historico_inferencias.csv";
    static int benchmark_inicializado = 0;
    FILE *fp = NULL;
    if (strcmp(modo, "Benchmark") == 0 && benchmark_inicializado == 0) {
        fp = fopen(nome_arquivo_log, "w");
        if (fp) {
            fprintf(fp, "Data_Hora,Modo,Caminho_Imagem,Predicao,Resultado,Latencia_ms,Status\n");
        }
        benchmark_inicializado = 1;
        fp = fopen(nome_arquivo_log, "a");
    }
    if (!fp) {
        printf("Erro: Nao foi possivel salvar o log\n");
        return;
    }
    time_t t = time(NULL);
    struct tm tm_info = *localtime(&t);
    char buffer_tempo[26];
    strftime(buffer_tempo, sizeof(buffer_tempo), "%Y-%m-%d %H:%M:%S", &tm_info);

    char resultado_acerto[16] = "N/A";
    if (strcmp(status, "SUCESSO") == 0) {
        int rotulo_real = obter_rotulo_real(modo, origem_imagem);
        if (rotulo_real != -1) {
            if (digito_predito == rotulo_real) {
                strcpy(resultado_acerto, "ACERTO");
            } else {
                strcpy(resultado_acerto, "ERRO");
            }
        }
    }
    fprintf(fp, "%s,%s,%s,%d,%s,%.3f,%s\n", 
            buffer_tempo, modo, origem_imagem, digito_predito, resultado_acerto, latencia_ms, status);
    fclose(fp);
}

int carregar_e_inferir(const char *modo, const char *origem_imagem, const uint8_t *pixels, int *digito_out) {
    struct timespec t0, t1;
    double latencia_ms = 0.0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    reset_clean_fpga();
    imagem_ptr = (uint8_t *)pixels;
    if (enviar_imagem() < 0) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        latencia_ms = ((t1.tv_sec - t0.tv_sec) * 1000.0) + ((t1.tv_nsec - t0.tv_nsec) / 1000000.0);
        registrar_log_csv(modo, origem_imagem, -1, latencia_ms, "ERRO_ENVIO_IMAGEM");
        return -1;
    }
    if (inferencia() < 0) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        latencia_ms = ((t1.tv_sec - t0.tv_sec) * 1000.0) + ((t1.tv_nsec - t0.tv_nsec) / 1000000.0);
        registrar_log_csv(modo, origem_imagem, -1, latencia_ms, "ERRO_PROCESSAMENTO");
        return -1;
    }
    int d = ler_resultado();
    if (d < 0) {
        clock_gettime(CLOCK_MONOTONIC, &t1);
        latencia_ms = ((t1.tv_sec - t0.tv_sec) * 1000.0) + ((t1.tv_nsec - t0.tv_nsec) / 1000000.0);
        registrar_log_csv(modo, origem_imagem, -1, latencia_ms, "ERRO_LEITURA");
        return -1;
    }
    *digito_out = d;  
    clock_gettime(CLOCK_MONOTONIC, &t1);
    latencia_ms = ((t1.tv_sec - t0.tv_sec) * 1000.0) + ((t1.tv_nsec - t0.tv_nsec) / 1000000.0);
    registrar_log_csv(modo, origem_imagem, d, latencia_ms, "SUCESSO");
    reset_clean_fpga();
    return 0;
}
