#ifndef APLICACAO_H
#define APLICACAO_H

#include <stdint.h>

/* ============================================================================
 * MODOS DE EXECUÇÃO
 * ========================================================================== */

int modo_arquivo(
    const char *caminho_arquivo);

int modo_desenho(void);

int modo_benchmark(
    const char *dir_raiz,
    int n_imagens,
    const char *csv_saida);

/* ============================================================================
 * UTILITÁRIOS DE IMAGEM
 * ========================================================================== */

int png_carregar(
    const char *caminho,
    uint8_t pixels[784]);

int png_carregar_bin(
    const char *caminho,
    uint8_t pixels[784]);

/* ============================================================================
 * INFERÊNCIA
 * ========================================================================== */

int carregar_e_inferir(
    const uint8_t *pixels,
    int *digito_out);

/* ============================================================================
 * VGA
 * ========================================================================== */

void limpar_tela(void);

#endif /* APLICACAO_H */
