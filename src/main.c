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
    srand(time(NULL));
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
        } else if (strcmp(argv[1], "-d") == 0) {
            r = modo_desenho();
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
