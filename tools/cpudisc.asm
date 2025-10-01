
	.org	0x0100
	.z80

start:
	ld	de, cpm_message
	ld	c, 9
	call	5

	xor	a
	dec	a
	; We must start with the Sharp SM83, because it replaces many instructions
	; The following instruction is interpreted as: ld a, (not_sm83)
	jp	m, not_sm83
	ld	de, msg_sm83
	jr	print_end

not_sm83:
	; Zilog z80 and derivatives use the P flag as an overflow flag for certain instructions such as ADD, ADC, SUB, SBC, INC, DEC
	; Since A-A=0, the parity flag will be on for non-Zilog CPUs but off for Zilog CPUs
	sub	a
	jp	pe, parity_flag_only

overflow_flag:
	; The Z180 DAA instruction is broken, it tests only the CF after a subtraction, which is not set for DEC
	; The other processors are *probably* correct
	xor	a
	dec	a
	daa
	jr	nc, z180

	; The eZ80 overloads the LD B, B; LD C, C; LD D, D; LD E, E instructions to be prefixes
	; The LD E, E becomes the .LIL prefix which forces the following LD HL, 0 instruction to take another byte of data
	; which is the "INC A" instruction
	; Since other CPUs will clear the zero bit after INC A, but the eZ80 will keep it set after the XOR A instruction,
	; the eZ80 can be distinguished
	; Note that the eZ80 should diverge here, because the next test would cause an illegal instruction trap, terminating
	; the program
	xor	a
	ld	e, e
	ld	hl, 0
	inc	a
	jr	z, ez80

	; This undefined opcode has different functionality on various CPUs
	; On the Z80, the undocumented SL1 will shift the value 0x40 to the left by one with an extra 1 shifted in,
	; causing it to have a negative sign
	; The Z280 does TSET A that will check the sign of 0x40 and copy it to the SF
	; The Z380 does an EX A, A' which leaves the flags intact, the last instruction to modify them being INC A that cleared SF
	ld	a, 0x40
	db	0xCB, 0x37
		; z80: sl1 a
		; z280: tset a
		; z380: ex a, a'
		; ez80: illegal
		; r800: sla a
	jp	m, z80_r800

	; In the second round, only the Z280 and Z380 are running
	; The Z280 tests the result of the previous TSET A, which is 0xFF, and copies the sign bit to SF, which is now set
	; Whereas the Z380 issues another EX A, A', preserving the cleared SF
	db	0xCB, 0x37
	jp	m, z280
	jr	z380

z80_r800:
	jp	po, r800
z80:
	ld	de, msg_z80
	jr	print_end

r800:
	ld	de, msg_r800
	jr	print_end

z180:
	ld	de, msg_z180
	jr	print_end

z280:
	ld	de, msg_z280
	jr	print_end

z380:
	ld	de, msg_z380
	jr	print_end

ez80:
	ld	de, msg_ez80
	jr	print_end

parity_flag_only:
	; Intel 8085 sets the half flag to 1 after and AND operation, while Intel 8080 will set it to the OR of bits 3 of the two operands
	; Since the previous instruction cleared A, we are left with 0
	; The KR580VM1 clears it
	and	a
	daa
	jp	nz, i85

	; As a final test, we AND two operands in a way that the OR of bit 3 should be 1, so Intel 8080 will have a half-carry flag set
	ld	b, a
	ld	a, 8
	and	b
	daa
	jp	z, vm1

i80:
	ld	de, msg_i80
	jp	print_end

i85:
	ld	de, msg_i85
	jp	print_end

vm1:
	ld	de, msg_vm1
	jp	print_end

print_end:
	ld	c, 9
	call	5

	ld	c, 0
	call	5

cpm_message:
	db	"Your CPU: ", '$'

msg_i80:
	db	"Intel 8080", 13, 10, '$'
msg_i85:
	db	"Intel 8085", 13, 10, '$'
msg_vm1:
	db	"KR580VM1", 13, 10, '$'
msg_z80:
	db	"Zilog Z80", 13, 10, '$'
msg_z180:
	db	"Zilog Z180", 13, 10, '$'
msg_z280:
	db	"Zilog Z280", 13, 10, '$'
msg_z380:
	db	"Zilog Z380", 13, 10, '$'
msg_ez80:
	db	"Zilog eZ80", 13, 10, '$'
msg_sm83:
	db	"Sharp SM83", 13, 10, '$'
msg_r800:
	db	"ASCII R800", 13, 10, '$'

