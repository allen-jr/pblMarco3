#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "driver.h"
#include "aplicacao.h"

extern uint8_t *base_virtual;

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
    if (base_virtual == NULL) {
        return 1;
    }
    limpar_tela();
    volatile uint32_t *vga_data_reg =
        (volatile uint32_t *)(base_virtual + 0x30);
    int vga_largura = 320;
    int vga_altura  = 240;
    int tamanho_quadrado = 240;
    int offset_x = (vga_largura - tamanho_quadrado) / 2;
    for (int y = 0; y < vga_altura; y++) {
        int orig_y = (y * 28) / tamanho_quadrado;
        if (orig_y > 27) orig_y = 27;
        for (int x = 0; x < vga_largura; x++) {
            uint32_t r = 0, g = 0, b = 0;
            if (x >= offset_x &&
                x < (offset_x + tamanho_quadrado)) {
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
    int digito_predito;
    if (carregar_e_inferir(pixels, &digito_predito) < 0) {
        return 1;
    }
    printf("===================================\n");
    printf("Digito previsto: %d\n", digito_predito);
    printf("===================================\n");
    return 0;
}