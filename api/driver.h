#ifndef DRIVER_H
#define DRIVER_H

/* =============================================================================
 * driver.h
 * Interface pública do driver de comunicação com a FPGA via MMIO.
 *
 * As funções abaixo são implementadas em assembly (rotinas.s e instrucoes.s)
 * e chamadas diretamente pelo main.c em C.
 * ============================================================================= */

/* -----------------------------------------------------------------------------
 * Funções de ciclo de vida  (rotinas.s)
 *
 *   inicializar_fpga  — abre /dev/mem, mapeia a Lightweight HPS-to-FPGA Bridge
 *                       e armazena o file descriptor e o ponteiro base em
 *                       variáveis globais compartilhadas com instrucoes.s.
 *                       Retorna o ponteiro base virtual (não-nulo) em sucesso,
 *                       ou NULL em falha.
 *
 *   finalizar_fpga    — desfaz o mapeamento de memória e fecha /dev/mem.
 *                       Zera as variáveis globais para evitar uso acidental.
 *
 *   reset_clean_fpga  — pulsa os sinais RESET e CLEAR no registrador PIO_CTRL,
 *                       com delays de ~1000 ciclos entre cada transição para
 *                       garantir estabilização do hardware.
 * ----------------------------------------------------------------------------- */
void *inicializar_fpga(void);
void  finalizar_fpga(void);
void  reset_clean_fpga(void);

/* -----------------------------------------------------------------------------
 * Funções de comunicação MMIO  (instrucoes.s)
 *
 *   enviar_bias   — envia os 128 valores de bias (Q4.12) via OP_STORE_BIAS.
 *   enviar_beta   — envia os 1280 valores de beta (Q4.12) via OP_STORE_BETA.
 *   enviar_pesos  — envia os 100352 pesos (Q4.12) via OP_STORE_WEIGHT_ADDR
 *                   + OP_STORE_WEIGHT_VALUE (duas instruções por peso).
 *   enviar_imagem — envia os 784 pixels (0–255) da imagem via OP_STORE_IMAGE.
 *   inferencia    — pulsa CLEAR, envia OP_START e aguarda o sinal DONE da FPGA.
 *   ler_resultado — lê os bits [3:0] de PIO_DATA_OUT e retorna o dígito (0–9).
 *
 * Retorno de enviar_*  e inferencia: 0 em sucesso, valor negativo em erro.
 *   -2  : timeout aguardando DONE (somente inferencia)
 *   -3  : erro interno sinalizado pela FPGA (STATUS_ERROR)
 *   -99 : timeout de handshake em enviar_instrucao
 * ----------------------------------------------------------------------------- */
int  enviar_bias(void);
int  enviar_beta(void);
int  enviar_pesos(void);
int  enviar_imagem(void);
int  inferencia(void);
int  ler_resultado(void);

#endif /* DRIVER_H */