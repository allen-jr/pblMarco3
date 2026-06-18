#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <sys/types.h>
#include <dirent.h>

#include "driver.h"
#include "aplicacao.h"

extern uint8_t *base_virtual;

static void log_evento(const char *formato, ...) {
    FILE *fp = fopen("historico_usuario.csv", "a");
    if (!fp) return;
    time_t t = time(NULL);
    struct tm tm_info = *localtime(&t);
    char buffer_tempo[26];
    strftime(buffer_tempo, sizeof(buffer_tempo), "%Y-%m-%d %H:%M:%S", &tm_info);
    fprintf(fp, "%s,", buffer_tempo);
    va_list args;
    va_start(args, formato);
    vfprintf(fp, formato, args);
    va_end(args);
    fprintf(fp, "\n");
    fclose(fp);
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

int main(void) {
    srand(time(NULL));
    FILE *fp_reset = fopen("historico_usuario.csv", "w");
    if (fp_reset) {
        fprintf(fp_reset, "timestamp,mensagem\n");
        fclose(fp_reset);
    }
    if (!inicializar_fpga()) {
        log_evento("ERRO: Falha ao inicializar FPGA");
        printf("ERRO: Falha ao inicializar FPGA.\n");
        return 1;
    }
    log_evento("SUCESSO: FPGA mapeada na memoria");
    reset_clean_fpga();
    if (enviar_bias() < 0 || enviar_beta() < 0 || enviar_pesos() < 0) {
        log_evento("ERRO: Falha ao carregar parametros na FPGA");
        printf("ERRO: Falha ao carregar parametros na FPGA.\n");
        finalizar_fpga();
        return 1;
    }
    log_evento("SUCESSO: Parametros carregados na FPGA");
    int rodando = 1;
    int op;
    while (rodando) {
        exibir_menu();       
        if (scanf("%d", &op) != 1) {
            while (getchar() != '\n');
            log_evento("ERRO: Entrada invalida no menu principal");
            continue;
        }
        while (getchar() != '\n');
        switch (op) {
            case 1: {
                log_evento("INFO: Selecionou Modo Arquivo");
                char caminho_arquivo[256];
                printf("Digite o caminho da imagem (data/imagens/...): ");
                if (scanf("%255s", caminho_arquivo) == 1) {
                    while (getchar() != '\n');
                    FILE *arquivo_teste = fopen(caminho_arquivo, "rb");
                    if (arquivo_teste == NULL) {
                        log_evento("ERRO: Arquivo nao encontrado: '%s'", caminho_arquivo);
                        printf("ERRO: arquivo nao encontrado: '%s'\n", caminho_arquivo);
                    } else {
                        fclose(arquivo_teste);
                        log_evento("SUCESSO: Arquivo '%s' aberto", caminho_arquivo);
                        if (modo_arquivo(caminho_arquivo) == 0) {
                            log_evento("SUCESSO: Modo arquivo finalizado");
                        } else {
                            log_evento("ERRO: Falha ao carregar imagem no VGA");
                            printf("ERRO: erro ao carregar imagem no VGA\n");
                        }
                    }
                } else {
                    while (getchar() != '\n');
                    log_evento("ERRO: Falha ao ler o caminho digitado no terminal");
                    printf("ERRO: Entrada de texto invalida.\n");
                }
                break;
            }
            case 2: {
                log_evento("INFO: Selecionou Modo Desenho");
                if (modo_desenho() == 0) {
                    log_evento("SUCESSO: Modo Desenho finalizado");
                } else {
                    log_evento("ERRO: Falha na execucao do modo desenho");
                    printf("ERRO: erro na execucao do modo desenho\n");
                }
                break;
            }
            case 3: {
                log_evento("INFO: Selecionou Modo Benchmark");
                char dir_raiz[256];
                int n_imagens;
                printf("Digite o caminho das imagens (data/imagens): ");
                if (scanf("%255s", dir_raiz) == 1) {
                    while (getchar() != '\n');
                    DIR *dir_teste = opendir(dir_raiz);
                    if (dir_teste == NULL) {
                        log_evento("ERRO: Caminho '%s' nao encontrado", dir_raiz);
                        printf("ERRO: caminho '%s' nao encontrado\n", dir_raiz);
                        break;
                    }
                    closedir(dir_teste);
                    printf("Digite a quantidade de imagens: ");
                    if (scanf("%d", &n_imagens) == 1) {
                        while (getchar() != '\n');
                        if (n_imagens <= 0) {
                            log_evento("ERRO: Quantidade de imagens invalida: '%d'", n_imagens);
                            printf("ERRO: Quantidade de imagens invalida\n");
                            break;
                        }
                        log_evento("INFO: Iniciando benchmark em '%s' com '%d' imagens", dir_raiz, n_imagens);
                        if (modo_benchmark(dir_raiz, n_imagens, "benchmark.csv") == 0) {
                            log_evento("SUCESSO: Modo benchmark finalizado");
                        } else {
                            log_evento("ERRO: O benchmark falhou durante a execucao");
                            printf("ERRO: o benchmark falhou durante a execucao\n");
                        }
                    }
                }
                break;
            }
            case 0: {
                rodando = 0;
                break;
            }
            default: {
                log_evento("ERRO: Opcao invalida digitada: '%d'", op);
                printf("ERRO: opcao invalida\n");
                break;
            }
        }
    }
    if (base_virtual != NULL) {
        limpar_tela();
    }
    finalizar_fpga();
    log_evento("SUCESSO: Programa encerrado");
    return 0;
}
