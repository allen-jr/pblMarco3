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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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
        int offset_x = (vga_largura - tamanho_quadrado) / 2;
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
                uint32_t pacote_desliga = pacote_escreve & ~(1 << 26);
                *vga_data_reg = pacote_escreve;
                for (volatile int d = 0; d < 40; d++);
                *vga_data_reg = pacote_desliga;
                for (volatile int d = 0; d < 40; d++);
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

static void desenhar_celula(int grid_x, int grid_y, int r, int g, int b, volatile uint32_t *vga_data_reg) {
    if (vga_data_reg == NULL) return;
    int tamanho_quadrado = 240;
    int offset_x = (320 - tamanho_quadrado) / 2; // = 40
    int start_y = (grid_y * tamanho_quadrado) / 28;
    int end_y   = ((grid_y + 1) * tamanho_quadrado) / 28;
    int start_x = offset_x + (grid_x * tamanho_quadrado) / 28;
    int end_x   = offset_x + ((grid_x + 1) * tamanho_quadrado) / 28;
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            uint32_t pkt = (x & 0x1FF) | ((y & 0xFF) << 9) | ((r & 0x7) << 17) | ((g & 0x7) << 20) | ((b & 0x7) << 23) | (1 << 26);
            *vga_data_reg = pkt;
            for (volatile int d = 0; d < 10; d++); 
            *vga_data_reg = pkt & ~(1 << 26);
        }
    }
}

int modo_desenho(void) {
    // 1. Abre o driver de mouse do Linux
    int fd_mouse = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
    if (fd_mouse < 0) {
        printf("ERRO: Nao foi possivel abrir o mouse em /dev/input/mice.\n");
        printf("Dica: Verifique as permissoes ou rode com sudo.\n");
        return 1;
    }

    // 2. Configura o terminal para ler teclas sem precisar de "Enter" e sem bloquear
    int flags_stdin = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags_stdin | O_NONBLOCK);
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    uint8_t conteudo[28][28] = {0};
    uint8_t conteudo_vga[28][28] = {0}; // Matriz auxiliar para armazenar o que vai para a VGA
    
    // Configurações do canvas (240x240 centralizado)
    int tamanho_quadrado = 240;
    int offset_x = (320 - tamanho_quadrado) / 2; // = 40
    
    // O mouse_x e mouse_y reais (começam no centro)
    int mouse_x = offset_x + (tamanho_quadrado / 2);
    int mouse_y = tamanho_quadrado / 2;
    
    // O bloco 28x28 atual do cursor
    int cursor_gx = 14;
    int cursor_gy = 14;
    
    int rodando = 1;

    printf("===================================\n");
    printf("MODO DESENHO NA GRADE (MOUSE)\n");
    printf("===================================\n");
    printf("Mouse Esquerdo : Desenhar (Blur em Tempo Real)\n");
    printf("Mouse Direito  : Apagar\n");
    printf("Teclado Enter  : Fazer Inferencia\n");
    printf("Teclado C      : Limpar Tela\n");
    printf("Teclado P      : Salvar como PNG (desenho_debug.png)\n");
    printf("Teclado Q      : Sair do modo\n");
    printf("===================================\n");

    limpar_tela();
    volatile uint32_t *vga_data_reg = NULL;
    if (base_virtual != NULL) {
        vga_data_reg = (volatile uint32_t *)(base_virtual + 0x30);
        // Pinta o primeiro cursor na tela
        desenhar_celula(cursor_gx, cursor_gy, 7, 0, 0, vga_data_reg);
    }

    // Matriz do Kernel Gaussiano 3x3
    int kernel[3][3] = {
        {1, 2, 1},
        {2, 4, 2},
        {1, 2, 1}
    };

    while (rodando) {
        // --- LEITURA DO MOUSE ---
        unsigned char dados_mouse[3];
        int bytes = read(fd_mouse, dados_mouse, sizeof(dados_mouse));
        
        if (bytes > 0) {
            int left_click  = dados_mouse[0] & 0x1;
            int right_click = dados_mouse[0] & 0x2;
            int dx = dados_mouse[1];
            int dy = dados_mouse[2];

            if (dados_mouse[0] & 0x10) dx -= 256;
            if (dados_mouse[0] & 0x20) dy -= 256;

            mouse_x += dx;
            mouse_y -= dy; // Inverte o eixo Y

            // Mantém o ponteiro estritamente dentro da área de desenho
            if (mouse_x < offset_x) mouse_x = offset_x;
            if (mouse_x > offset_x + tamanho_quadrado - 1) mouse_x = offset_x + tamanho_quadrado - 1;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y > tamanho_quadrado - 1) mouse_y = tamanho_quadrado - 1;

            // Mapeia para a grade 28x28 na mesma escala do Modo 1
            int novo_gx = ((mouse_x - offset_x) * 28) / tamanho_quadrado;
            int novo_gy = (mouse_y * 28) / tamanho_quadrado;
            
            // Garantia de limite
            if (novo_gx > 27) novo_gx = 27;
            if (novo_gy > 27) novo_gy = 27;

            int clicou = left_click || right_click;
            int moveu_grade = (novo_gx != cursor_gx || novo_gy != cursor_gy);

            // --- ATUALIZAÇÃO DA MATRIZ BASE ---
            if (clicou) {
                if (left_click) {
                    conteudo[novo_gy][novo_gx] = 255;
                    if (novo_gy > 0)  conteudo[novo_gy - 1][novo_gx] = (conteudo[novo_gy - 1][novo_gx] < 220) ? 220 : conteudo[novo_gy - 1][novo_gx];
                    if (novo_gy < 27) conteudo[novo_gy + 1][novo_gx] = (conteudo[novo_gy + 1][novo_gx] < 220) ? 220 : conteudo[novo_gy + 1][novo_gx];
                    if (novo_gx > 0)  conteudo[novo_gy][novo_gx - 1] = (conteudo[novo_gy][novo_gx - 1] < 220) ? 220 : conteudo[novo_gy][novo_gx - 1];
                    if (novo_gx < 27) conteudo[novo_gy][novo_gx + 1] = (conteudo[novo_gy][novo_gx + 1] < 220) ? 220 : conteudo[novo_gy][novo_gx + 1];
                } 
                else if (right_click) {
                    conteudo[novo_gy][novo_gx] = 0;
                    if (novo_gy > 0)  conteudo[novo_gy - 1][novo_gx] = 0;
                    if (novo_gy < 27) conteudo[novo_gy + 1][novo_gx] = 0;
                    if (novo_gx > 0)  conteudo[novo_gy][novo_gx - 1] = 0;
                    if (novo_gx < 27) conteudo[novo_gy][novo_gx + 1] = 0;
                }

                // --- CALCULA O BLUR EM TEMPO REAL APENAS NA ÁREA MODIFICADA (Janela 5x5 para performance) ---
                int start_y = (novo_gy - 2 < 0) ? 0 : novo_gy - 2;
                int end_y = (novo_gy + 2 > 27) ? 27 : novo_gy + 2;
                int start_x = (novo_gx - 2 < 0) ? 0 : novo_gx - 2;
                int end_x = (novo_gx + 2 > 27) ? 27 : novo_gx + 2;

                for (int y = start_y; y <= end_y; y++) {
                    for (int x = start_x; x <= end_x; x++) {
                        int soma_ponderada = 0, soma_pesos = 0;
                        for (int ky = -1; ky <= 1; ky++) {
                            for (int kx = -1; kx <= 1; kx++) {
                                int py = y + ky;
                                int px = x + kx;
                                if (py >= 0 && py < 28 && px >= 0 && px < 28) {
                                    int peso = kernel[ky + 1][kx + 1];
                                    soma_ponderada += conteudo[py][px] * peso;
                                    soma_pesos += peso;
                                }
                            }
                        }
                        int res = soma_ponderada / soma_pesos;
                        if (res > 0 && conteudo[y][x] > 0) {
                            res = (res * 1.3 > 255) ? 255 : (int)(res * 1.3);
                        }
                        conteudo_vga[y][x] = (uint8_t)res;
                    }
                }
            }

            // Se mudou de bloco na grade ou se clicou, atualiza a VGA
            if (vga_data_reg != NULL && (moveu_grade || clicou)) {
                
                // 1. Restaura a cor real (com blur) do bloco onde o cursor estava
                if (moveu_grade) {
                    uint8_t tom_cinza = conteudo_vga[cursor_gy][cursor_gx];
                    int vga_v = tom_cinza >> 5; // Escala 0 a 7
                    desenhar_celula(cursor_gx, cursor_gy, vga_v, vga_v, vga_v, vga_data_reg);
                }

                // 2. Atualiza os blocos vizinhos modificados pelo pincel e pelo blur na tela
                if (clicou) {
                    int start_y = (novo_gy - 2 < 0) ? 0 : novo_gy - 2;
                    int end_y = (novo_gy + 2 > 27) ? 27 : novo_gy + 2;
                    int start_x = (novo_gx - 2 < 0) ? 0 : novo_gx - 2;
                    int end_x = (novo_gx + 2 > 27) ? 27 : novo_gx + 2;

                    for (int y = start_y; y <= end_y; y++) {
                        for (int x = start_x; x <= end_x; x++) {
                            // Ignora a célula onde o cursor vermelho está exatamente em cima agora para não piscar
                            if (x == novo_gx && y == novo_gy) continue; 
                            
                            uint8_t tom_cinza = conteudo_vga[y][x];
                            int vga_v = tom_cinza >> 5;
                            desenhar_celula(x, y, vga_v, vga_v, vga_v, vga_data_reg);
                        }
                    }
                }

                // 3. Pinta o cursor atual (sempre vermelho) na nova posição
                desenhar_celula(novo_gx, novo_gy, 7, 0, 0, vga_data_reg);
                
                cursor_gx = novo_gx;
                cursor_gy = novo_gy;
            }
        }

        // --- LEITURA DO TECLADO ---
        int ch = getchar();
        if (ch != EOF) {
            if (ch == 'q' || ch == 'Q') {
                rodando = 0;
            } 
            else if (ch == 'c' || ch == 'C') {
                memset(conteudo, 0, sizeof(conteudo));
                memset(conteudo_vga, 0, sizeof(conteudo_vga));
                limpar_tela();
                desenhar_celula(cursor_gx, cursor_gy, 7, 0, 0, vga_data_reg);
            } 
            else if (ch == 'p' || ch == 'P') {
                uint8_t pixels[784];
                for(int i = 0; i < 28; i++) {
                    for(int j = 0; j < 28; j++) {
                        pixels[i * 28 + j] = conteudo_vga[i][j];
                    }
                }
                if (stbi_write_png("desenho_debug.png", 28, 28, 1, pixels, 28) != 0) {
                    printf("\r\n===================================\r\n");
                    printf("Sucesso! Imagem salva como 'desenho_debug.png'\r\n");
                    printf("===================================\r\n");
                }
            }
            else if (ch == '\n' || ch == '\r') {
                uint8_t pixels[784];
                for(int i = 0; i < 28; i++) {
                    for(int j = 0; j < 28; j++) {
                        // Passa a matriz que já está com o filtro gaussiano aplicado
                        pixels[i * 28 + j] = conteudo_vga[i][j];
                    }
                }

                // Executa a inferência enviando a imagem perfeitamente idêntica à tela
                int digito_predito;
                if (carregar_e_inferir(pixels, &digito_predito) == 0) {
                    printf("\r\n===================================\r\n");
                    printf("Digito Previsto pelo FPGA: %d\r\n", digito_predito);
                    printf("===================================\r\n");
                }
            }
        }
        
        usleep(2000); 
    }

    // --- RESTAURAÇÃO DO SISTEMA ---
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, flags_stdin);
    close(fd_mouse);

    limpar_tela();
    return 0;
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
                // Troca os ponteiros de strings de posição
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
