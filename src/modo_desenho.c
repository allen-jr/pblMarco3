#define _POSIX_C_SOURCE 199309L
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#include "stb_image_write.h"

#include "driver.h"
#include "aplicacao.h"

extern uint8_t *base_virtual;

static void desenhar_celula(int grid_x, int grid_y, int r, int g, int b, volatile uint32_t *vga_data_reg) {
    if (vga_data_reg == NULL) return;
    int tamanho_quadrado = 240;
    int offset_x = (320 - tamanho_quadrado) / 2;
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
    int fd_mouse = open("/dev/input/mice", O_RDONLY | O_NONBLOCK);
    if (fd_mouse < 0) {
        return 1;
    }
    int flags_stdin = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags_stdin | O_NONBLOCK);
    struct termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    uint8_t conteudo[28][28] = {0};
    uint8_t conteudo_vga[28][28] = {0};
    int tamanho_quadrado = 240;
    int offset_x = (320 - tamanho_quadrado) / 2; // = 40
    int mouse_x = offset_x + (tamanho_quadrado / 2);
    int mouse_y = tamanho_quadrado / 2;
    int cursor_gx = 14;
    int cursor_gy = 14;
    int rodando = 1;
    limpar_tela();
    volatile uint32_t *vga_data_reg = NULL;
    if (base_virtual != NULL) {
        vga_data_reg = (volatile uint32_t *)(base_virtual + 0x30);
        desenhar_celula(cursor_gx, cursor_gy, 7, 0, 0, vga_data_reg);
    }
    int kernel[3][3] = {
        {1, 2, 1},
        {2, 4, 2},
        {1, 2, 1}
    };
    while (rodando) {
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
            mouse_y -= dy;
            if (mouse_x < offset_x) mouse_x = offset_x;
            if (mouse_x > offset_x + tamanho_quadrado - 1) mouse_x = offset_x + tamanho_quadrado - 1;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y > tamanho_quadrado - 1) mouse_y = tamanho_quadrado - 1;
            int novo_gx = ((mouse_x - offset_x) * 28) / tamanho_quadrado;
            int novo_gy = (mouse_y * 28) / tamanho_quadrado;
            if (novo_gx > 27) novo_gx = 27;
            if (novo_gy > 27) novo_gy = 27;
            int clicou = left_click || right_click;
            int moveu_grade = (novo_gx != cursor_gx || novo_gy != cursor_gy);
            if (clicou) {
                if (left_click) {
                    conteudo[novo_gy][novo_gx] = 255;
                    if (novo_gy > 0) conteudo[novo_gy - 1][novo_gx] = (conteudo[novo_gy - 1][novo_gx] < 220) ? 220 : conteudo[novo_gy - 1][novo_gx];
                    if (novo_gy < 27) conteudo[novo_gy + 1][novo_gx] = (conteudo[novo_gy + 1][novo_gx] < 220) ? 220 : conteudo[novo_gy + 1][novo_gx];
                    if (novo_gx > 0) conteudo[novo_gy][novo_gx - 1] = (conteudo[novo_gy][novo_gx - 1] < 220) ? 220 : conteudo[novo_gy][novo_gx - 1];
                    if (novo_gx < 27) conteudo[novo_gy][novo_gx + 1] = (conteudo[novo_gy][novo_gx + 1] < 220) ? 220 : conteudo[novo_gy][novo_gx + 1];
                } 
                else if (right_click) {
                    conteudo[novo_gy][novo_gx] = 0;
                    if (novo_gy > 0) conteudo[novo_gy - 1][novo_gx] = 0;
                    if (novo_gy < 27) conteudo[novo_gy + 1][novo_gx] = 0;
                    if (novo_gx > 0) conteudo[novo_gy][novo_gx - 1] = 0;
                    if (novo_gx < 27) conteudo[novo_gy][novo_gx + 1] = 0;
                }
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
            if (vga_data_reg != NULL && (moveu_grade || clicou)) {
                if (moveu_grade) {
                    uint8_t tom_cinza = conteudo_vga[cursor_gy][cursor_gx];
                    int vga_v = tom_cinza >> 5; // Escala 0 a 7
                    desenhar_celula(cursor_gx, cursor_gy, vga_v, vga_v, vga_v, vga_data_reg);
                }
                if (clicou) {
                    int start_y = (novo_gy - 2 < 0) ? 0 : novo_gy - 2;
                    int end_y = (novo_gy + 2 > 27) ? 27 : novo_gy + 2;
                    int start_x = (novo_gx - 2 < 0) ? 0 : novo_gx - 2;
                    int end_x = (novo_gx + 2 > 27) ? 27 : novo_gx + 2;
                    for (int y = start_y; y <= end_y; y++) {
                        for (int x = start_x; x <= end_x; x++) {
                            if (x == novo_gx && y == novo_gy) continue; 
                            uint8_t tom_cinza = conteudo_vga[y][x];
                            int vga_v = tom_cinza >> 5;
                            desenhar_celula(x, y, vga_v, vga_v, vga_v, vga_data_reg);
                        }
                    }
                }
                desenhar_celula(novo_gx, novo_gy, 7, 0, 0, vga_data_reg);
                cursor_gx = novo_gx;
                cursor_gy = novo_gy;
            }
        }
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
                if (stbi_write_png("desenho.png", 28, 28, 1, pixels, 28) != 0) {
                    printf("===================================\n");
                    printf("Imagem salva como desenho.png\n");
                    printf("===================================\n");
                }
            }
            else if (ch == '\n' || ch == '\r') {
                uint8_t pixels[784];
                for(int i = 0; i < 28; i++) {
                    for(int j = 0; j < 28; j++) {
                        pixels[i * 28 + j] = conteudo_vga[i][j];
                    }
                }
                int digito_predito;
                if (carregar_e_inferir(pixels, &digito_predito) == 0) {
                    printf("===================================\n");
                    printf("Digito previsto: %d\n", digito_predito);
                    printf("===================================\n");
                }
            }
        }
        usleep(2000);
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    fcntl(STDIN_FILENO, F_SETFL, flags_stdin);
    close(fd_mouse);
    limpar_tela();
    return 0;
}