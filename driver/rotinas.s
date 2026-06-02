@ =============================================================================
@ rotinas.s
@ Gerenciamento do ciclo de vida da conexão com a FPGA via /dev/mem.
@
@ Expõe três funções públicas (declaradas em driver.h):
@   inicializar_fpga  — abre /dev/mem e mapeia a bridge no espaço virtual
@   finalizar_fpga    — desfaz o mapeamento e fecha /dev/mem
@   reset_clean_fpga  — pulsa RESET e CLEAR no hardware
@
@ Variáveis globais compartilhadas com instrucoes.s:
@   fd_devmem   — file descriptor de /dev/mem
@   base_virtual — ponteiro virtual para a Lightweight HPS-to-FPGA Bridge
@ =============================================================================

.extern open
.extern mmap
.extern munmap
.extern close

@ --- Constantes de abertura de arquivo e mmap ---
.equ FLAG_RDWR,          2           @ O_RDWR: abre /dev/mem para leitura e escrita
.equ FLAG_SYNC,          0x101000    @ O_SYNC: escritas chegam ao hardware imediatamente
.equ MMAP_COMPARTILHADO, 1           @ MAP_SHARED: alterações refletem no hardware
.equ MMAP_PROT_RDWR,     3           @ PROT_READ | PROT_WRITE

@ --- Endereço físico e tamanho da Lightweight HPS-to-FPGA Bridge ---
.equ BRIDGE_ENDERECO_FISICO, 0xFF200000
.equ BRIDGE_TAMANHO,         0x00005000

@ --- Offsets dos registradores PIO na bridge ---
.equ PIO_CTRL,   0x20   @ registrador de controle (ENABLE, CLEAR, RESET)

@ --- Bits do registrador de controle ---
.equ BIT_CLEAR,  2      @ zera flags de status na FPGA
.equ BIT_RESET,  4      @ reinicia a lógica interna da FPGA


.section .text

@ =============================================================================
@ inicializar_fpga
@
@ Abre /dev/mem e mapeia a Lightweight Bridge no espaço de endereço virtual
@ do processo. Salva o file descriptor em `fd_devmem` e o ponteiro base em
@ `base_virtual` para uso pelas rotinas de instrucoes.s.
@
@ Retorno: R0 = ponteiro base virtual (não-nulo) em sucesso, 0 em erro.
@ =============================================================================
.global inicializar_fpga
.type inicializar_fpga, %function
inicializar_fpga:
    PUSH {R4-R7, LR}

    @ Abre /dev/mem com O_RDWR | O_SYNC
    LDR R0, =caminho_devmem
    LDR R1, =FLAG_SYNC
    ORR R1, R1, #FLAG_RDWR
    BL  open

    @ Se fd < 0, /dev/mem não pôde ser aberto (sem permissão ou ausente)
    CMP R0, #0
    BLT inicializar_erro_open
    MOV R4, R0                      @ R4 = fd salvo

    @ Mapeia a bridge: mmap(NULL, TAMANHO, PROT, MAP_SHARED, fd, ENDERECO_FISICO)
    MOV R0, #0                      @ addr = NULL (kernel escolhe o endereço)
    LDR R1, =BRIDGE_TAMANHO
    MOV R2, #MMAP_PROT_RDWR
    MOV R3, #MMAP_COMPARTILHADO
    SUB SP, SP, #8                  @ argumentos 5 e 6 vão na pilha (ABI ARM)
    STR R4, [SP]                    @ arg5 = fd
    LDR R5, =BRIDGE_ENDERECO_FISICO
    STR R5, [SP, #4]                @ arg6 = offset físico
    BL  mmap
    ADD SP, SP, #8

    @ mmap retorna -1 em erro
    CMP R0, #-1
    BEQ inicializar_erro_mmap

    MOV R6, R0                      @ R6 = base virtual obtida

    @ Persiste fd e base para uso em instrucoes.s
    LDR R7, =fd_devmem
    STR R4, [R7]
    LDR R7, =base_virtual
    STR R6, [R7]

    MOV R0, R6                      @ retorna base virtual
    POP {R4-R7, LR}
    BX  LR

inicializar_erro_mmap:
    @ mmap falhou: fecha o fd antes de retornar
    MOV R0, R4
    BL  close
    MOV R0, #0
    POP {R4-R7, LR}
    BX  LR

inicializar_erro_open:
    MOV R0, #0
    POP {R4-R7, LR}
    BX  LR


@ =============================================================================
@ finalizar_fpga
@
@ Desfaz o mapeamento de memória e fecha /dev/mem.
@ Zera `fd_devmem` e `base_virtual` para evitar uso acidental após a chamada.
@ =============================================================================
.global finalizar_fpga
.type finalizar_fpga, %function
finalizar_fpga:
    PUSH {R4-R5, LR}

    @ Lê fd; se já for -1, não há nada a fechar
    LDR R4, =fd_devmem
    LDR R4, [R4]
    CMP R4, #-1
    BEQ finalizar_zeragem

    @ Desfaz mmap se base_virtual não for nula
    LDR R0, =base_virtual
    LDR R0, [R0]
    CMP R0, #0
    BEQ finalizar_fechar_fd
    LDR R1, =BRIDGE_TAMANHO
    BL  munmap

finalizar_fechar_fd:
    MOV R0, R4
    BL  close

finalizar_zeragem:
    @ Marca fd e base como inválidos
    LDR R5, =fd_devmem
    MOV R1, #-1
    STR R1, [R5]
    LDR R5, =base_virtual
    MOV R1, #0
    STR R1, [R5]

    MOV R0, #0
    POP {R4-R5, LR}
    BX  LR


@ =============================================================================
@ reset_clean_fpga
@
@ Pulsa os sinais RESET e CLEAR no registrador PIO_CTRL da FPGA.
@ Cada passo tem um delay de ~1000 ciclos para garantir estabilização.
@
@ Sequência:
@   1. Ativa   RESET  → aguarda → desativa RESET  → aguarda
@   2. Ativa   CLEAR  → aguarda → desativa CLEAR  → aguarda
@ =============================================================================
.global reset_clean_fpga
.type reset_clean_fpga, %function
reset_clean_fpga:
    PUSH {R6, R12, LR}

    LDR R6, =base_virtual
    LDR R6, [R6]                    @ ponteiro para os registradores da bridge

    @ --- Pulso de RESET ---
    MOV R0, #BIT_RESET
    STR R0, [R6, #PIO_CTRL]         @ ativa RESET
    LDR R12, =1000
reset_delay_ativo:
    SUBS R12, R12, #1
    BNE  reset_delay_ativo

    MOV R0, #0
    STR R0, [R6, #PIO_CTRL]         @ desativa RESET
    LDR R12, =1000
reset_delay_pos:
    SUBS R12, R12, #1
    BNE  reset_delay_pos

    @ --- Pulso de CLEAR ---
    MOV R0, #BIT_CLEAR
    STR R0, [R6, #PIO_CTRL]         @ ativa CLEAR
    LDR R12, =1000
clear_delay_ativo:
    SUBS R12, R12, #1
    BNE  clear_delay_ativo

    MOV R0, #0
    STR R0, [R6, #PIO_CTRL]         @ desativa CLEAR
    LDR R12, =1000
clear_delay_pos:
    SUBS R12, R12, #1
    BNE  clear_delay_pos

    POP {R6, R12, LR}
    BX  LR


@ =============================================================================
@ Seção de dados
@ =============================================================================
.section .data

@ String de caminho para open()
caminho_devmem:
    .asciz "/dev/mem"

@ File descriptor de /dev/mem; -1 indica "não aberto"
.global fd_devmem
fd_devmem:
    .word -1

@ Ponteiro base da Lightweight Bridge após mmap; 0 indica "não mapeado"
.global base_virtual
base_virtual:
    .word 0
