; Exercises the richer assembler: a .MACRO with a \1 argument, .INCLUDE of a
; second source, and an expression operand (617+617). Assemble with the ASM
; cartridge (RUN ASM -> "FEAT.ASM FEATOUT") then RUN FEATOUT. It prints the
; included message and the value of the expression (1234).

.MACRO PRINT
LA A0, \1
LD T0, 8(S0)
JALR T0
.ENDM

MV S0, A0
ADDI SP, SP, -16
SD RA, 0(SP)
PRINT MSG
LA A0, VAL
LW A0, 0(A0)
LD T0, 24(S0)
JALR T0
LD RA, 0(SP)
ADDI SP, SP, 16
LI A0, 0
RET

VAL: .WORD 617+617
.INCLUDE "FMSG.ASM"
