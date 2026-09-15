; ============================================================================
; AWEDUMP.COM - EMU8000 sample-memory (ROM) dumper for real-mode DOS
;
; Reads the low 1 MB of the EMU8000's unified sample-memory address space
; (the region physically populated by the onboard EMU8011 GM/GS ROM on a
; Sound Blaster AWE32) via direct I/O port access, and writes it to
; AWE32ROM.BIN in the current directory.
;
; Register-level behaviour (port layout, command encoding, chip-init
; sequence, and the SMALR/SMLD sample-memory read protocol) is translated
; from two public, freely reusable references:
;   - "AWE32/EMU8000 Programmer's Guide", Rev 1.00, Dave Rossum (E-mu/
;     Creative Technology) - a hardware programming reference, not the
;     card's ROM content.
;   - Linux kernel ALSA driver sound/isa/sb/emu8000.c and
;     include/sound/emu8000_reg.h (GPL-2.0-or-later, Steve Ratcliffe /
;     Takashi Iwai) - including the published ADIP initialisation tables.
; No proprietary sample data is embedded in this program; it only contains
; initialisation constants and register-access logic.
;
; Build:   nasm -f bin awedump.asm -o AWEDUMP.COM
; Usage:   AWEDUMP [hexbase]
;          hexbase = EMU8000 base I/O port in hex (e.g. 620). If omitted,
;          0x620 is used (the common non-PnP default). Find your card's
;          real base from the "Ex" field of the BLASTER env var, or from
;          CTCM.EXE / DIAGNOSE.EXE.
; Output:  AWE32ROM.BIN, exactly 1,048,576 bytes, in the current directory.
;
; NOTE: this талks directly to hardware I/O ports. Run it from real DOS
; (or a DOS box with genuine ISA/EMU8000 port passthrough) on the machine
; that physically has the AWE32 installed - not in a plain Windows/Linux
; console, and not in an emulator that doesn't forward these exact ports.
; ============================================================================

BITS 16
ORG 0x100

start:
    mov     si, 0x81            ; PSP command tail
    call    skip_spaces
    cmp     byte [si], 0x0D
    je      .use_default
    call    parse_hex
    jmp     .got_base
.use_default:
    mov     ax, 0x0620
.got_base:
    mov     [awebase], ax

    ; ---- derive the four data ports + pointer port ----
    mov     ax, [awebase]
    mov     [p_data0], ax
    add     ax, 0x400
    mov     [p_data1], ax
    add     ax, 2
    mov     [p_data2], ax
    mov     ax, [awebase]
    add     ax, 0x800
    mov     [p_data3], ax
    add     ax, 2
    mov     [p_ptr], ax

    ; ---- banner ----
    mov     dx, msg_banner
    call    print_str
    mov     ax, [awebase]
    call    print_hex16
    mov     dx, msg_banner2
    call    print_str

    ; ---- quick chip presence check (informational only) ----
    call    detect_chip
    cmp     ax, 1
    je      .detected
    mov     dx, msg_notdetected
    call    print_str
    jmp     .ask
.detected:
    mov     dx, msg_detected
    call    print_str
.ask:
    mov     dx, msg_continue
    call    print_str
    mov     ah, 0x01
    int     0x21                ; wait for keypress
    cmp     al, 27              ; ESC aborts
    jne     .go
    mov     dx, msg_abort
    call    print_str
    mov     ax, 0x4C01
    int     0x21
.go:
    mov     dx, msg_nl
    call    print_str

    ; ---- create output file ----
    mov     ah, 0x3C
    xor     cx, cx
    mov     dx, fname
    int     0x21
    jc      .file_err
    mov     [filehandle], ax

    ; ---- full chip init sequence ----
    call    chip_init

    ; ---- put channel 1 into RAM-read mode ----
    call    dma_chan_read

    mov     dx, msg_dumping
    call    print_str

    ; ---- dump 1 MB ----
    call    do_dump

    mov     ah, 0x3E
    mov     bx, [filehandle]
    int     0x21

    mov     dx, msg_done
    call    print_str
    mov     ax, 0x4C00
    int     0x21

.file_err:
    mov     dx, msg_fileerr
    call    print_str
    mov     ax, 0x4C01
    int     0x21


; ----------------------------------------------------------------------
; low-level helpers
; ----------------------------------------------------------------------

skip_spaces:
.loop:
    mov     al, [si]
    cmp     al, ' '
    jne     .done
    inc     si
    jmp     .loop
.done:
    ret

; parse hex digits at DS:SI into AX
parse_hex:
    xor     ax, ax
.loop:
    mov     bl, [si]
    cmp     bl, '0'
    jl      .done
    cmp     bl, '9'
    jle     .digit
    and     bl, 0xDF
    cmp     bl, 'A'
    jl      .done
    cmp     bl, 'F'
    jg      .done
    sub     bl, 'A' - 10
    jmp     .accum
.digit:
    sub     bl, '0'
.accum:
    shl     ax, 4
    or      al, bl
    inc     si
    jmp     .loop
.done:
    ret

print_str:                     ; DX -> '$'-terminated string
    mov     ah, 9
    int     0x21
    ret

print_hex16:                   ; AX = value to print in hex
    push    ax
    push    bx
    push    cx
    push    dx
    mov     cx, 4
    mov     bx, ax
.loop:
    rol     bx, 4
    mov     al, bl
    and     al, 0x0F
    add     al, '0'
    cmp     al, '9'
    jle     .out
    add     al, 7
.out:
    mov     dl, al
    mov     ah, 2
    int     0x21
    loop    .loop
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    ret

; poke16: [parm_port], [parm_cmd], [parm_val_lo]
poke16:
    push    ax
    push    dx
    mov     dx, [p_ptr]
    mov     ax, [parm_cmd]
    out     dx, ax
    mov     dx, [parm_port]
    mov     ax, [parm_val_lo]
    out     dx, ax
    pop     dx
    pop     ax
    ret

; poke32: [parm_port], [parm_cmd], [parm_val_lo]/[parm_val_hi]
poke32:
    push    ax
    push    dx
    mov     dx, [p_ptr]
    mov     ax, [parm_cmd]
    out     dx, ax
    mov     dx, [parm_port]
    mov     ax, [parm_val_lo]
    out     dx, ax
    add     dx, 2
    mov     ax, [parm_val_hi]
    out     dx, ax
    pop     dx
    pop     ax
    ret

; peek16 -> AX ; params: [parm_port], [parm_cmd]
peek16:
    push    dx
    mov     dx, [p_ptr]
    mov     ax, [parm_cmd]
    out     dx, ax
    mov     dx, [parm_port]
    in      ax, dx
    pop     dx
    ret

; peek32 -> [result_lo]/[result_hi] ; params: [parm_port], [parm_cmd]
peek32:
    push    ax
    push    dx
    mov     dx, [p_ptr]
    mov     ax, [parm_cmd]
    out     dx, ax
    mov     dx, [parm_port]
    in      ax, dx
    mov     [result_lo], ax
    add     dx, 2
    in      ax, dx
    mov     [result_hi], ax
    pop     dx
    pop     ax
    ret

; ~110ms busy-wait via BIOS tick counter (int 1Ah)
short_delay:
    push    ax
    push    bx
    push    cx
    push    dx
    xor     ax, ax
    int     0x1A
    mov     bx, dx
.wait:
    xor     ax, ax
    int     0x1A
    sub     dx, bx
    cmp     dx, 2
    jl      .wait
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    ret

; informational presence check a la snd_emu8000_detect(); AX=1 if plausible
detect_chip:
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 0x3D      ; HWCF1
    mov     word [parm_val_lo], 0x0059
    call    poke16
    mov     word [parm_cmd], 0x3E      ; HWCF2
    mov     word [parm_val_lo], 0x0020
    call    poke16
    mov     word [parm_cmd], 0x3F      ; HWCF3
    mov     word [parm_val_lo], 0x0000
    call    poke16

    mov     word [parm_cmd], 0x3D
    call    peek16
    and     ax, 0x007E
    cmp     ax, 0x0058
    jne     .no
    mov     word [parm_cmd], 0x3E
    call    peek16
    and     ax, 0x0003
    cmp     ax, 0x0003
    jne     .no
    mov     ax, 1
    ret
.no:
    xor     ax, ax
    ret


; ----------------------------------------------------------------------
; macros for the repetitive per-channel init
; ----------------------------------------------------------------------
%macro POKE16_ZERO_CH 2
    mov     ax, [%1]
    mov     [parm_port], ax
    mov     ax, %2
    add     ax, si
    mov     [parm_cmd], ax
    mov     word [parm_val_lo], 0
    call    poke16
%endmacro

%macro POKE32_ZERO_CH 2
    mov     ax, [%1]
    mov     [parm_port], ax
    mov     ax, %2
    add     ax, si
    mov     [parm_cmd], ax
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0
    call    poke32
%endmacro


; ----------------------------------------------------------------------
; chip initialisation (mirrors snd_emu8000_init_hw())
; ----------------------------------------------------------------------
chip_init:
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 0x3D
    mov     word [parm_val_lo], 0x0059
    call    poke16
    mov     word [parm_cmd], 0x3E
    mov     word [parm_val_lo], 0x0020
    call    poke16
    mov     word [parm_cmd], 0x3F
    mov     word [parm_val_lo], 0x0000
    call    poke16

    call    init_audio
    call    init_dma
    call    init_arrays
    call    init_fm

    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 0x3F
    mov     word [parm_val_lo], 0x0004  ; re-enable audio
    call    poke16
    ret

init_audio:
    mov     si, 0
.phase1:
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     ax, 160
    add     ax, si
    mov     [parm_cmd], ax
    mov     word [parm_val_lo], 0x80
    call    poke16
    inc     si
    cmp     si, 32
    jl      .phase1

    mov     si, 0
.phase2:
    POKE16_ZERO_CH p_data1, 128     ; ENVVOL
    POKE16_ZERO_CH p_data1, 192     ; ENVVAL
    POKE16_ZERO_CH p_data1, 224     ; DCYSUS
    POKE16_ZERO_CH p_data2, 128     ; ATKHLDV
    POKE16_ZERO_CH p_data2, 160     ; LFO1VAL
    POKE16_ZERO_CH p_data2, 192     ; ATKHLD
    POKE16_ZERO_CH p_data2, 224     ; LFO2VAL
    POKE16_ZERO_CH p_data3, 0       ; IP
    POKE16_ZERO_CH p_data3, 32      ; IFATN
    POKE16_ZERO_CH p_data3, 64      ; PEFE
    POKE16_ZERO_CH p_data3, 96      ; FMMOD
    POKE16_ZERO_CH p_data3, 128     ; TREMFRQ
    POKE16_ZERO_CH p_data3, 160     ; FM2FRQ2
    POKE32_ZERO_CH p_data0, 32      ; PTRX
    POKE32_ZERO_CH p_data0, 96      ; VTFT
    POKE32_ZERO_CH p_data0, 192     ; PSST
    POKE32_ZERO_CH p_data0, 224     ; CSL
    POKE32_ZERO_CH p_data1, 0       ; CCCA
    inc     si
    cmp     si, 32
    jl      .phase2

    mov     si, 0
.phase3:
    POKE32_ZERO_CH p_data0, 0       ; CPF
    POKE32_ZERO_CH p_data0, 64      ; CVCF
    inc     si
    cmp     si, 32
    jl      .phase3
    ret

init_dma:
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0
    mov     word [parm_cmd], 0x34   ; SMALR
    call    poke32
    mov     word [parm_cmd], 0x35   ; SMARR
    call    poke32
    mov     word [parm_cmd], 0x36   ; SMALW
    call    poke32
    mov     word [parm_cmd], 0x37   ; SMARW
    call    poke32
    ret

; DS:SI -> 128-word table; splits into 4x32 sent to INIT1..INIT4
send_array:
    mov     cx, 0
.sa1:
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     ax, 64
    add     ax, cx
    mov     [parm_cmd], ax
    mov     ax, [si]
    mov     [parm_val_lo], ax
    call    poke16
    add     si, 2
    inc     cx
    cmp     cx, 32
    jl      .sa1

    mov     cx, 0
.sa2:
    mov     ax, [p_data2]
    mov     [parm_port], ax
    mov     ax, 64
    add     ax, cx
    mov     [parm_cmd], ax
    mov     ax, [si]
    mov     [parm_val_lo], ax
    call    poke16
    add     si, 2
    inc     cx
    cmp     cx, 32
    jl      .sa2

    mov     cx, 0
.sa3:
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     ax, 96
    add     ax, cx
    mov     [parm_cmd], ax
    mov     ax, [si]
    mov     [parm_val_lo], ax
    call    poke16
    add     si, 2
    inc     cx
    cmp     cx, 32
    jl      .sa3

    mov     cx, 0
.sa4:
    mov     ax, [p_data2]
    mov     [parm_port], ax
    mov     ax, 96
    add     ax, cx
    mov     [parm_cmd], ax
    mov     ax, [si]
    mov     [parm_val_lo], ax
    call    poke16
    add     si, 2
    inc     cx
    cmp     cx, 32
    jl      .sa4
    ret

init_arrays:
    mov     si, init1_tbl
    call    send_array
    call    short_delay
    mov     si, init2_tbl
    call    send_array
    mov     si, init3_tbl
    call    send_array

    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 0x29   ; HWCF4
    mov     word [parm_val_lo], 0x0000
    call    poke16
    mov     word [parm_cmd], 0x2A   ; HWCF5
    mov     word [parm_val_lo], 0x0083
    call    poke16
    mov     word [parm_cmd], 0x2D   ; HWCF6
    mov     word [parm_val_lo], 0x8000
    call    poke16

    mov     si, init4_tbl
    call    send_array
    ret

init_fm:
    ; --- channel 30 (left) ---
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 190        ; DCYSUSV ch30
    mov     word [parm_val_lo], 0x80
    call    poke16

    mov     ax, [p_data0]
    mov     [parm_port], ax
    mov     word [parm_cmd], 222        ; PSST ch30
    mov     word [parm_val_lo], 0xFFE0
    mov     word [parm_val_hi], 0xFFFF
    call    poke32
    mov     word [parm_cmd], 254        ; CSL ch30
    mov     word [parm_val_lo], 0xFFE8
    mov     word [parm_val_hi], 0x00FF
    call    poke32
    mov     word [parm_cmd], 62         ; PTRX ch30
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0
    call    poke32
    mov     word [parm_cmd], 30         ; CPF ch30
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0
    call    poke32

    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 30         ; CCCA ch30
    mov     word [parm_val_lo], 0xFFE3
    mov     word [parm_val_hi], 0x00FF
    call    poke32

    ; --- channel 31 (right) ---
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 191        ; DCYSUSV ch31
    mov     word [parm_val_lo], 0x80
    call    poke16

    mov     ax, [p_data0]
    mov     [parm_port], ax
    mov     word [parm_cmd], 223        ; PSST ch31
    mov     word [parm_val_lo], 0xFFF0
    mov     word [parm_val_hi], 0x00FF
    call    poke32
    mov     word [parm_cmd], 255        ; CSL ch31
    mov     word [parm_val_lo], 0xFFF8
    mov     word [parm_val_hi], 0x00FF
    call    poke32
    mov     word [parm_cmd], 63         ; PTRX ch31
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0
    call    poke32
    mov     word [parm_cmd], 31         ; CPF ch31
    mov     word [parm_val_lo], 0x8000
    mov     word [parm_val_hi], 0
    call    poke32

    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 31         ; CCCA ch31
    mov     word [parm_val_lo], 0xFFF3
    mov     word [parm_val_hi], 0x00FF
    call    poke32

    ; raw single-word poke (PTRX cmd) = 0
    mov     ax, [p_data0]
    mov     [parm_port], ax
    mov     word [parm_cmd], 62
    mov     word [parm_val_lo], 0
    call    poke16

    ; busy-wait on Pointer port bit 0x1000 (set then clear), bounded so a
    ; missing/wrong-base card can't hang the machine forever
    mov     dx, [p_ptr]
    mov     cx, 0
.wset:
    in      ax, dx
    test    ax, 0x1000
    jnz     .wset_done
    loop    .wset
.wset_done:
    mov     cx, 0
.wclr:
    in      ax, dx
    test    ax, 0x1000
    jz      .wclr_done
    loop    .wclr
.wclr_done:

    mov     ax, [p_data0]
    mov     [parm_port], ax
    mov     word [parm_cmd], 62
    mov     word [parm_val_lo], 0x4828
    call    poke16

    ; "odd part" - undocumented direct register write
    mov     dx, [p_ptr]
    mov     ax, 0x003C
    out     dx, ax
    mov     dx, [p_data1]
    xor     ax, ax
    out     dx, ax

    mov     ax, [p_data0]
    mov     [parm_port], ax
    mov     word [parm_cmd], 126        ; VTFT ch30
    mov     word [parm_val_lo], 0xFFFF
    mov     word [parm_val_hi], 0x8000
    call    poke32
    mov     word [parm_cmd], 127        ; VTFT ch31
    mov     word [parm_val_lo], 0xFFFF
    mov     word [parm_val_hi], 0x8000
    call    poke32
    ret

; put channel 1 into "RAM read" mode (mirrors snd_emu8000_dma_chan RAM_READ)
dma_chan_read:
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 161        ; DCYSUSV ch1
    mov     word [parm_val_lo], 0x80
    call    poke16

    mov     ax, [p_data0]
    mov     [parm_port], ax
    mov     word [parm_cmd], 97         ; VTFT ch1
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0
    call    poke32
    mov     word [parm_cmd], 65         ; CVCF ch1
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0
    call    poke32
    mov     word [parm_cmd], 33         ; PTRX ch1
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0x4000
    call    poke32
    mov     word [parm_cmd], 1          ; CPF ch1
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0x4000
    call    poke32
    mov     word [parm_cmd], 193        ; PSST ch1
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0
    call    poke32
    mov     word [parm_cmd], 225        ; CSL ch1
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0
    call    poke32

    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 1          ; CCCA ch1 = 0x04000000 (RAM read, no right bit)
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0x0400
    call    poke32
    ret

; ----------------------------------------------------------------------
; the actual dump: 32 blocks x 16384 words x 2 bytes = 1,048,576 bytes
; ----------------------------------------------------------------------
do_dump:
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 0x34       ; SMALR = 0
    mov     word [parm_val_lo], 0
    mov     word [parm_val_hi], 0
    call    poke32

    mov     word [parm_cmd], 0x3A       ; discard first SMLD read
    call    peek16

    mov     word [blocks_left], 32
.block_loop:
    mov     word [bufpos], 0
    mov     cx, 16384
.word_loop:
    push    cx
    mov     cx, 0
.busywait:
    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 0x34
    call    peek32
    mov     ax, [result_hi]
    test    ax, 0x8000
    jz      .busywait_done
    loop    .busywait
    mov     dx, msg_timeout
    call    print_str
    pop     cx
    jmp     .abort_dump
.busywait_done:
    pop     cx

    mov     ax, [p_data1]
    mov     [parm_port], ax
    mov     word [parm_cmd], 0x3A
    call    peek16

    mov     bx, [bufpos]
    mov     [buffer + bx], ax
    add     bx, 2
    mov     [bufpos], bx

    loop    .word_loop

    mov     ah, 0x40
    mov     bx, [filehandle]
    mov     cx, 32768
    mov     dx, buffer
    int     0x21

    mov     dl, '.'
    mov     ah, 2
    int     0x21

    dec     word [blocks_left]
    jnz     .block_loop
    ret

.abort_dump:
    mov     ah, 0x3E
    mov     bx, [filehandle]
    int     0x21
    mov     ax, 0x4C02
    int     0x21


; ----------------------------------------------------------------------
; data
; ----------------------------------------------------------------------
msg_banner      db  'AWEDUMP - EMU8000 ROM dumper.  Base port: 0x', '$'
msg_banner2     db  0x0D, 0x0A, '$'
msg_detected    db  'EMU8000-like chip responded at this base.', 0x0D, 0x0A, '$'
msg_notdetected db  'WARNING: no EMU8000 response at this base (wrong port?).', 0x0D, 0x0A, '$'
msg_continue    db  'Press a key to dump ROM to AWE32ROM.BIN (ESC to abort)...$'
msg_abort       db  0x0D, 0x0A, 'Aborted.', 0x0D, 0x0A, '$'
msg_nl          db  0x0D, 0x0A, '$'
msg_dumping     db  'Dumping 1 MB ', '$'
msg_done        db  0x0D, 0x0A, 'Done: AWE32ROM.BIN written (1,048,576 bytes).', 0x0D, 0x0A, '$'
msg_fileerr     db  'ERROR: could not create AWE32ROM.BIN in current directory.', 0x0D, 0x0A, '$'
msg_timeout     db  0x0D, 0x0A, 'ERROR: SMALR busy-bit never cleared (wrong base port, or no card?). Aborting - partial file kept.', 0x0D, 0x0A, '$'

fname           db  'AWE32ROM.BIN', 0

awebase         dw  0
p_data0         dw  0
p_data1         dw  0
p_data2         dw  0
p_data3         dw  0
p_ptr           dw  0

parm_port       dw  0
parm_cmd        dw  0
parm_val_lo     dw  0
parm_val_hi     dw  0
result_lo       dw  0
result_hi       dw  0

filehandle      dw  0
bufpos          dw  0
blocks_left     dw  0

; ---- ADIP initialisation tables (from Linux GPL emu8000.c, public) ----
init1_tbl:
    dw 0x03ff, 0x0030, 0x07ff, 0x0130, 0x0bff, 0x0230, 0x0fff, 0x0330
    dw 0x13ff, 0x0430, 0x17ff, 0x0530, 0x1bff, 0x0630, 0x1fff, 0x0730
    dw 0x23ff, 0x0830, 0x27ff, 0x0930, 0x2bff, 0x0a30, 0x2fff, 0x0b30
    dw 0x33ff, 0x0c30, 0x37ff, 0x0d30, 0x3bff, 0x0e30, 0x3fff, 0x0f30
    dw 0x43ff, 0x0030, 0x47ff, 0x0130, 0x4bff, 0x0230, 0x4fff, 0x0330
    dw 0x53ff, 0x0430, 0x57ff, 0x0530, 0x5bff, 0x0630, 0x5fff, 0x0730
    dw 0x63ff, 0x0830, 0x67ff, 0x0930, 0x6bff, 0x0a30, 0x6fff, 0x0b30
    dw 0x73ff, 0x0c30, 0x77ff, 0x0d30, 0x7bff, 0x0e30, 0x7fff, 0x0f30
    dw 0x83ff, 0x0030, 0x87ff, 0x0130, 0x8bff, 0x0230, 0x8fff, 0x0330
    dw 0x93ff, 0x0430, 0x97ff, 0x0530, 0x9bff, 0x0630, 0x9fff, 0x0730
    dw 0xa3ff, 0x0830, 0xa7ff, 0x0930, 0xabff, 0x0a30, 0xafff, 0x0b30
    dw 0xb3ff, 0x0c30, 0xb7ff, 0x0d30, 0xbbff, 0x0e30, 0xbfff, 0x0f30
    dw 0xc3ff, 0x0030, 0xc7ff, 0x0130, 0xcbff, 0x0230, 0xcfff, 0x0330
    dw 0xd3ff, 0x0430, 0xd7ff, 0x0530, 0xdbff, 0x0630, 0xdfff, 0x0730
    dw 0xe3ff, 0x0830, 0xe7ff, 0x0930, 0xebff, 0x0a30, 0xefff, 0x0b30
    dw 0xf3ff, 0x0c30, 0xf7ff, 0x0d30, 0xfbff, 0x0e30, 0xffff, 0x0f30

init2_tbl:
    dw 0x03ff, 0x8030, 0x07ff, 0x8130, 0x0bff, 0x8230, 0x0fff, 0x8330
    dw 0x13ff, 0x8430, 0x17ff, 0x8530, 0x1bff, 0x8630, 0x1fff, 0x8730
    dw 0x23ff, 0x8830, 0x27ff, 0x8930, 0x2bff, 0x8a30, 0x2fff, 0x8b30
    dw 0x33ff, 0x8c30, 0x37ff, 0x8d30, 0x3bff, 0x8e30, 0x3fff, 0x8f30
    dw 0x43ff, 0x8030, 0x47ff, 0x8130, 0x4bff, 0x8230, 0x4fff, 0x8330
    dw 0x53ff, 0x8430, 0x57ff, 0x8530, 0x5bff, 0x8630, 0x5fff, 0x8730
    dw 0x63ff, 0x8830, 0x67ff, 0x8930, 0x6bff, 0x8a30, 0x6fff, 0x8b30
    dw 0x73ff, 0x8c30, 0x77ff, 0x8d30, 0x7bff, 0x8e30, 0x7fff, 0x8f30
    dw 0x83ff, 0x8030, 0x87ff, 0x8130, 0x8bff, 0x8230, 0x8fff, 0x8330
    dw 0x93ff, 0x8430, 0x97ff, 0x8530, 0x9bff, 0x8630, 0x9fff, 0x8730
    dw 0xa3ff, 0x8830, 0xa7ff, 0x8930, 0xabff, 0x8a30, 0xafff, 0x8b30
    dw 0xb3ff, 0x8c30, 0xb7ff, 0x8d30, 0xbbff, 0x8e30, 0xbfff, 0x8f30
    dw 0xc3ff, 0x8030, 0xc7ff, 0x8130, 0xcbff, 0x8230, 0xcfff, 0x8330
    dw 0xd3ff, 0x8430, 0xd7ff, 0x8530, 0xdbff, 0x8630, 0xdfff, 0x8730
    dw 0xe3ff, 0x8830, 0xe7ff, 0x8930, 0xebff, 0x8a30, 0xefff, 0x8b30
    dw 0xf3ff, 0x8c30, 0xf7ff, 0x8d30, 0xfbff, 0x8e30, 0xffff, 0x8f30

init3_tbl:
    dw 0x0C10, 0x8470, 0x14FE, 0xB488, 0x167F, 0xA470, 0x18E7, 0x84B5
    dw 0x1B6E, 0x842A, 0x1F1D, 0x852A, 0x0DA3, 0x8F7C, 0x167E, 0xF254
    dw 0x0000, 0x842A, 0x0001, 0x852A, 0x18E6, 0x8BAA, 0x1B6D, 0xF234
    dw 0x229F, 0x8429, 0x2746, 0x8529, 0x1F1C, 0x86E7, 0x229E, 0xF224
    dw 0x0DA4, 0x8429, 0x2C29, 0x8529, 0x2745, 0x87F6, 0x2C28, 0xF254
    dw 0x383B, 0x8428, 0x320F, 0x8528, 0x320E, 0x8F02, 0x1341, 0xF264
    dw 0x3EB6, 0x8428, 0x3EB9, 0x8528, 0x383A, 0x8FA9, 0x3EB5, 0xF294
    dw 0x3EB7, 0x8474, 0x3EBA, 0x8575, 0x3EB8, 0xC4C3, 0x3EBB, 0xC5C3
    dw 0x0000, 0xA404, 0x0001, 0xA504, 0x141F, 0x8671, 0x14FD, 0x8287
    dw 0x3EBC, 0xE610, 0x3EC8, 0x8C7B, 0x031A, 0x87E6, 0x3EC8, 0x86F7
    dw 0x3EC0, 0x821E, 0x3EBE, 0xD208, 0x3EBD, 0x821F, 0x3ECA, 0x8386
    dw 0x3EC1, 0x8C03, 0x3EC9, 0x831E, 0x3ECA, 0x8C4C, 0x3EBF, 0x8C55
    dw 0x3EC9, 0xC208, 0x3EC4, 0xBC84, 0x3EC8, 0x8EAD, 0x3EC8, 0xD308
    dw 0x3EC2, 0x8F7E, 0x3ECB, 0x8219, 0x3ECB, 0xD26E, 0x3EC5, 0x831F
    dw 0x3EC6, 0xC308, 0x3EC3, 0xB2FF, 0x3EC9, 0x8265, 0x3EC9, 0x8319
    dw 0x1342, 0xD36E, 0x3EC7, 0xB3FF, 0x0000, 0x8365, 0x1420, 0x9570

init4_tbl:
    dw 0x0C10, 0x8470, 0x14FE, 0xB488, 0x167F, 0xA470, 0x18E7, 0x84B5
    dw 0x1B6E, 0x842A, 0x1F1D, 0x852A, 0x0DA3, 0x0F7C, 0x167E, 0x7254
    dw 0x0000, 0x842A, 0x0001, 0x852A, 0x18E6, 0x0BAA, 0x1B6D, 0x7234
    dw 0x229F, 0x8429, 0x2746, 0x8529, 0x1F1C, 0x06E7, 0x229E, 0x7224
    dw 0x0DA4, 0x8429, 0x2C29, 0x8529, 0x2745, 0x07F6, 0x2C28, 0x7254
    dw 0x383B, 0x8428, 0x320F, 0x8528, 0x320E, 0x0F02, 0x1341, 0x7264
    dw 0x3EB6, 0x8428, 0x3EB9, 0x8528, 0x383A, 0x0FA9, 0x3EB5, 0x7294
    dw 0x3EB7, 0x8474, 0x3EBA, 0x8575, 0x3EB8, 0x44C3, 0x3EBB, 0x45C3
    dw 0x0000, 0xA404, 0x0001, 0xA504, 0x141F, 0x0671, 0x14FD, 0x0287
    dw 0x3EBC, 0xE610, 0x3EC8, 0x0C7B, 0x031A, 0x07E6, 0x3EC8, 0x86F7
    dw 0x3EC0, 0x821E, 0x3EBE, 0xD208, 0x3EBD, 0x021F, 0x3ECA, 0x0386
    dw 0x3EC1, 0x0C03, 0x3EC9, 0x031E, 0x3ECA, 0x8C4C, 0x3EBF, 0x0C55
    dw 0x3EC9, 0xC208, 0x3EC4, 0xBC84, 0x3EC8, 0x0EAD, 0x3EC8, 0xD308
    dw 0x3EC2, 0x8F7E, 0x3ECB, 0x0219, 0x3ECB, 0xD26E, 0x3EC5, 0x031F
    dw 0x3EC6, 0xC308, 0x3EC3, 0x32FF, 0x3EC9, 0x0265, 0x3EC9, 0x8319
    dw 0x1342, 0xD36E, 0x3EC7, 0x33FF, 0x0000, 0x8365, 0x1420, 0x9570

buffer: times 32768 db 0
