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

int carregar_e_inferir(const uint8_t *pixels, int *digito_out) {
    reset_clean_fpga();
    imagem_ptr = (uint8_t *)pixels;
    if (enviar_imagem() < 0) return -1;
    if (inferencia() < 0) return -1;
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