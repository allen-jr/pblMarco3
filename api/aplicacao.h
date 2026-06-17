#ifndef APLICACAO_H
#define APLICACAO_H

#include <stdint.h>

int modo_arquivo(const char *caminho_arquivo);
int modo_desenho(void);
int modo_benchmark(const char *dir_raiz, int n_imagens, const char *csv_saida);

int png_carregar(const char *caminho, uint8_t pixels[784]);
int png_carregar_bin(const char *caminho, uint8_t pixels[784]);

int carregar_e_inferir(const char *modo, const char *origem_imagem, const uint8_t *pixels, int *digito_out);

void limpar_tela(void);

#endif