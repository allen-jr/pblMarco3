@ =============================================================================
@ instrucoes.s
@ Protocolo MMIO de comunicação com a FPGA e envio dos dados da rede neural.
@
@ Funções públicas exportadas:
@   enviar_bias   — envia os 128 valores de bias
@   enviar_beta   — envia os 1280 valores de beta
@   enviar_pesos  — envia os 100352 pesos
@   enviar_imagem — envia os 784 pixels da imagem
@   inferencia    — pulsa CLEAR, envia OP_START e aguarda DONE
@   ler_resultado — lê os bits [3:0] de PIO_DATA_OUT (dígito 0–9)
@
@ Função interna (não exportada):
@   enviar_instrucao — primitiva de handshake MMIO usada por todas acima
@ =============================================================================

.extern base_virtual

@ --- Offsets dos registradores PIO na Lightweight Bridge ---
.equ PIO_DATA_IN,    0x00
.equ PIO_DATA_OUT,   0x10
.equ PIO_CTRL,       0x20

@ --- Bits do registrador PIO_CTRL ---
.equ CTRL_ENABLE,    1
.equ CTRL_CLEAR,     2

@ --- Bits de status lidos em PIO_DATA_OUT ---
.equ STATUS_DONE,    0x10
.equ STATUS_BUSY,    0x20
.equ STATUS_ERROR,   0x40
.equ RESULT_MASK,    0x0F

@ --- Opcodes de instrução (bits [2:0] da palavra de 32 bits) ---
.equ OP_STORE_IMAGE,        0
.equ OP_STORE_WEIGHT_ADDR,  1
.equ OP_STORE_WEIGHT_VALUE, 2
.equ OP_STORE_BIAS,         3
.equ OP_STORE_BETA,         4
.equ OP_START,              5

@ --- Tamanhos dos vetores da rede neural ---
.equ TAM_IMAGEM,    784
.equ TAM_BIAS,      128
.equ TAM_BETA,      1280
.equ TAM_PESOS,     100352

@ --- Timeouts (em iterações de polling) ---
.equ TIMEOUT_INSTR,   200000
.equ TIMEOUT_START,   2000000


.section .text

@ =============================================================================
@ enviar_instrucao  [interna — sem .global]
@
@ Envia uma palavra de 32 bits para a FPGA e aguarda confirmação via handshake:
@   1. Escreve instrução em PIO_DATA_IN
@   2. Ativa CTRL_ENABLE → FPGA lê e levanta BUSY
@   3. Aguarda BUSY subir  (FPGA confirmou recebimento)
@   4. Desativa CTRL_ENABLE
@   5. Aguarda BUSY descer (FPGA terminou de processar)
@   6. Verifica STATUS_ERROR
@
@ Entrada:  R0 = palavra de instrução de 32 bits
@ Retorno:  R0 = 0 (sucesso) | -99 (timeout) | -3 (erro interno da FPGA)
@ =============================================================================
.type enviar_instrucao, %function
enviar_instrucao:
    PUSH {R1-R6, LR}

    LDR R6, =base_virtual
    LDR R6, [R6]

    STR R0, [R6, #PIO_DATA_IN]
    MOV R1, #CTRL_ENABLE
    STR R1, [R6, #PIO_CTRL]

    LDR R5, =TIMEOUT_INSTR

enviar_instrucao_aguarda_busy_alto:
    LDR R2, [R6, #PIO_DATA_OUT]
    TST R2, #STATUS_BUSY
    BNE enviar_instrucao_busy_detectado
    SUBS R5, R5, #1
    BNE enviar_instrucao_aguarda_busy_alto
    B   enviar_instrucao_timeout

enviar_instrucao_busy_detectado:
    MOV R1, #0
    STR R1, [R6, #PIO_CTRL]        @ desativa ENABLE — FPGA leu a instrução
    LDR R5, =TIMEOUT_INSTR

enviar_instrucao_aguarda_busy_baixo:
    LDR R2, [R6, #PIO_DATA_OUT]
    TST R2, #STATUS_ERROR
    BNE enviar_instrucao_erro_fpga
    TST R2, #STATUS_BUSY
    BEQ enviar_instrucao_ok
    SUBS R5, R5, #1
    BNE enviar_instrucao_aguarda_busy_baixo

enviar_instrucao_timeout:
    MOV R1, #0
    STR R1, [R6, #PIO_CTRL]
    MOV R0, #-99
    POP {R1-R6, LR}
    BX  LR

enviar_instrucao_erro_fpga:
    MOV R1, #0
    STR R1, [R6, #PIO_CTRL]
    MOV R0, #-3
    POP {R1-R6, LR}
    BX  LR

enviar_instrucao_ok:
    MOV R0, #0
    POP {R1-R6, LR}
    BX  LR


@ =============================================================================
@ enviar_bias
@
@ Envia os 128 valores de bias para a FPGA via OP_STORE_BIAS.
@ Formato da instrução: [25:10] = valor Q4.12 | [9:3] = índice | [2:0] = opcode
@
@ Retorno: R0 = 0 (sucesso) | código negativo em erro
@ =============================================================================
.global enviar_bias
.type enviar_bias, %function
enviar_bias:
    PUSH {R4-R7, LR}

    LDR R4, =bias_bin
    MOV R5, #0
    LDR R6, =TAM_BIAS

enviar_bias_loop:
    CMP R5, R6
    BEQ enviar_bias_ok

    LDR  R7, [R4], #4
    REV  R7, R7                     @ big-endian → little-endian
    ASR  R7, R7, #16                @ extrai 16 bits superiores (Q4.12)
    UXTH R7, R7

    LSL R0, R7, #10                 @ valor em [25:10]
    LSL R1, R5, #3                  @ índice em [9:3]
    ORR R0, R0, R1
    ORR R0, R0, #OP_STORE_BIAS
    BL  enviar_instrucao
    CMP R0, #0
    BLT enviar_bias_fim

    ADD R5, R5, #1
    B   enviar_bias_loop

enviar_bias_ok:
    MOV R0, #0
enviar_bias_fim:
    POP {R4-R7, LR}
    BX  LR


@ =============================================================================
@ enviar_beta
@
@ Envia os 1280 valores de beta para a FPGA via OP_STORE_BETA.
@ Formato da instrução: [29:14] = valor Q4.12 | [13:3] = índice | [2:0] = opcode
@
@ Retorno: R0 = 0 (sucesso) | código negativo em erro
@ =============================================================================
.global enviar_beta
.type enviar_beta, %function
enviar_beta:
    PUSH {R4-R7, LR}

    LDR R4, =beta_bin
    MOV R5, #0
    LDR R6, =TAM_BETA

enviar_beta_loop:
    CMP R5, R6
    BEQ enviar_beta_ok

    LDR  R7, [R4], #4
    REV  R7, R7
    ASR  R7, R7, #16
    UXTH R7, R7

    LSL R0, R7, #14                 @ valor em [29:14]
    LSL R1, R5, #3                  @ índice em [13:3]
    ORR R0, R0, R1
    ORR R0, R0, #OP_STORE_BETA
    BL  enviar_instrucao
    CMP R0, #0
    BLT enviar_beta_fim

    ADD R5, R5, #1
    B   enviar_beta_loop

enviar_beta_ok:
    MOV R0, #0
enviar_beta_fim:
    POP {R4-R7, LR}
    BX  LR


@ =============================================================================
@ enviar_pesos
@
@ Envia os 100352 pesos para a FPGA usando dois opcodes por peso:
@   Instrução A: [19:3] = índice      | [2:0] = OP_STORE_WEIGHT_ADDR
@   Instrução B: [18:3] = valor Q4.12 | [2:0] = OP_STORE_WEIGHT_VALUE
@
@ Retorno: R0 = 0 (sucesso) | código negativo em erro
@ =============================================================================
.global enviar_pesos
.type enviar_pesos, %function
enviar_pesos:
    PUSH {R4-R7, LR}

    LDR R4, =pesos_bin
    MOV R5, #0
    LDR R6, =TAM_PESOS

enviar_pesos_loop:
    CMP R5, R6
    BEQ enviar_pesos_ok

    LSL R0, R5, #3
    ORR R0, R0, #OP_STORE_WEIGHT_ADDR  @ instrução A: endereço
    BL  enviar_instrucao
    CMP R0, #0
    BLT enviar_pesos_fim

    LDR  R7, [R4], #4
    REV  R7, R7
    ASR  R7, R7, #16
    UXTH R7, R7

    LSL R0, R7, #3
    ORR R0, R0, #OP_STORE_WEIGHT_VALUE  @ instrução B: valor
    BL  enviar_instrucao
    CMP R0, #0
    BLT enviar_pesos_fim

    ADD R5, R5, #1
    B   enviar_pesos_loop

enviar_pesos_ok:
    MOV R0, #0
enviar_pesos_fim:
    POP {R4-R7, LR}
    BX  LR


@ =============================================================================
@ enviar_imagem
@
@ Envia os 784 pixels da imagem para a FPGA via OP_STORE_IMAGE.
@ Formato da instrução: [20:13] = pixel (0–255) | [12:3] = índice | [2:0] = opcode
@
@ Retorno: R0 = 0 (sucesso) | código negativo em erro
@ =============================================================================
.global enviar_imagem
.type enviar_imagem, %function
enviar_imagem:
    PUSH {R4-R7, LR}

    LDR R4, =imagem_ptr          @ carrega endereço do ponteiro externo
    LDR R4, [R4]                 @ desreferencia: R4 = buffer real da imagem
    MOV R5, #0
    LDR R6, =TAM_IMAGEM

enviar_imagem_loop:
    CMP R5, R6
    BEQ enviar_imagem_ok

    LDRB R7, [R4], #1               @ lê 1 byte (pixel 0–255) e avança ponteiro

    LSL R0, R7, #13                 @ pixel em [20:13]
    LSL R1, R5, #3                  @ índice em [12:3]
    ORR R0, R0, R1
    ORR R0, R0, #OP_STORE_IMAGE
    BL  enviar_instrucao
    CMP R0, #0
    BLT enviar_imagem_fim

    ADD R5, R5, #1
    B   enviar_imagem_loop

enviar_imagem_ok:
    MOV R0, #0
enviar_imagem_fim:
    POP {R4-R7, LR}
    BX  LR


@ =============================================================================
@ inferencia
@
@ Pulsa CLEAR para limpar flags residuais, envia OP_START e aguarda DONE.
@ O timeout aqui é TIMEOUT_START (10× maior que TIMEOUT_INSTR) porque a
@ rede precisa processar todos os pesos antes de produzir o resultado.
@
@ Retorno: R0 = 0 (sucesso) | -2 (timeout) | -3 (erro interno da FPGA)
@ =============================================================================
.global inferencia
.type inferencia, %function
inferencia:
    PUSH {R4-R6, LR}

    LDR R6, =base_virtual
    LDR R6, [R6]

    @ Pulso de CLEAR para zerar flags de rodadas anteriores
    MOV R1, #CTRL_CLEAR
    STR R1, [R6, #PIO_CTRL]
    LDR R4, =1000
inferencia_delay_clear:
    SUBS R4, R4, #1
    BNE  inferencia_delay_clear
    MOV R1, #0
    STR R1, [R6, #PIO_CTRL]

    @ Escreve OP_START e ativa ENABLE
    MOV R0, #OP_START
    STR R0, [R6, #PIO_DATA_IN]
    MOV R1, #CTRL_ENABLE
    STR R1, [R6, #PIO_CTRL]

    @ Aguarda BUSY subir (FPGA recebeu o START)
    LDR R4, =TIMEOUT_INSTR
inferencia_busy_alto:
    LDR R2, [R6, #PIO_DATA_OUT]
    TST R2, #STATUS_BUSY
    BNE inferencia_busy_detectado
    SUBS R4, R4, #1
    BNE  inferencia_busy_alto
    B    inferencia_timeout

inferencia_busy_detectado:
    MOV R1, #0
    STR R1, [R6, #PIO_CTRL]        @ desativa ENABLE — FPGA está processando

    @ Aguarda DONE com timeout longo
    LDR R4, =TIMEOUT_START
inferencia_aguarda_done:
    LDR R2, [R6, #PIO_DATA_OUT]
    TST R2, #STATUS_ERROR
    BNE inferencia_erro_fpga
    TST R2, #STATUS_DONE
    BNE inferencia_ok
    SUBS R4, R4, #1
    BNE  inferencia_aguarda_done

inferencia_timeout:
    MOV R1, #0
    STR R1, [R6, #PIO_CTRL]
    MOV R0, #-2
    POP {R4-R6, LR}
    BX  LR

inferencia_erro_fpga:
    MOV R1, #0
    STR R1, [R6, #PIO_CTRL]
    MOV R0, #-3
    POP {R4-R6, LR}
    BX  LR

inferencia_ok:
    MOV R0, #0
    POP {R4-R6, LR}
    BX  LR


@ =============================================================================
@ ler_resultado
@
@ Lê PIO_DATA_OUT e retorna o dígito classificado nos bits [3:0] (0–9).
@ Deve ser chamada após inferencia() ter retornado com sucesso.
@
@ Retorno: R0 = dígito classificado (0–9)
@ =============================================================================
.global ler_resultado
.type ler_resultado, %function
ler_resultado:
    PUSH {R6, LR}

    LDR R6, =base_virtual
    LDR R6, [R6]

    LDR R0, [R6, #PIO_DATA_OUT]
    AND R0, R0, #RESULT_MASK        @ isola bits [3:0]

    POP {R6, LR}
    BX  LR


@ =============================================================================
@ Seção de dados — binários embutidos em tempo de compilação
@ =============================================================================
.section .data

.align 4
@ imagem_ptr: ponteiro para o buffer de 784 bytes preenchido pelo main.c em runtime.
@ O C declara: extern uint8_t *imagem_ptr;  e faz  imagem_ptr = buffer;
.global imagem_ptr
imagem_ptr:
    .word 0                                 @ inicializado pelo main.c antes de enviar_imagem()

.align 4
pesos_bin:
    .incbin "data/W_in_q.bin"       @ 100352 × 4 bytes: pesos em Q4.12 big-endian

.align 4
bias_bin:
    .incbin "data/b_q.bin"          @ 128 × 4 bytes: bias em Q4.12 big-endian

.align 4
beta_bin:
    .incbin "data/beta_q.bin"       @ 1280 × 4 bytes: beta em Q4.12 big-endian
