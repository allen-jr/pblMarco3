#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver.h"
#include "aplicacao.h"

extern uint8_t *base_virtual;

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
    system("ntpdate -u a.ntp.br 2>/dev/null || chronyc -a makestep 2>/dev/null");
    srand(time(NULL));
    if (!inicializar_fpga()) {
        return 1;
    }
    FILE *fp_reset = fopen("historico_inferencias.csv", "w");
    if (fp_reset) {
        fprintf(fp_reset, "timestamp,modo,caminho_arquivo,digito_predito,resultado,latencia_ms,status\n");
        fclose(fp_reset);
    }
    reset_clean_fpga();
    if (enviar_bias() < 0 || enviar_beta() < 0 || enviar_pesos() < 0) {
        finalizar_fpga();
        return 1;
    }
    int rodando = 1;
    int op;
    while (rodando) {
        exibir_menu();       
        scanf("%d", &op);
        while (getchar() != '\n');
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
            case 2: {
                modo_desenho();
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
