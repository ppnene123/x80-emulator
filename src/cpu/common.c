
// common definitions for all frontends

PUBLIC void PREFIXED(disable_interrupts)(x80_state_t * cpu, uint8_t bits)
{
	disable_interrupts(cpu, bits);
}

PUBLIC void PREFIXED(enable_interrupts)(x80_state_t * cpu, uint8_t bits)
{
	enable_interrupts(cpu, bits);
}

/* those bits that should not be affected by arithmetic operations (such as the VM1 MF flag) */
#ifndef AF_KEEP
# define AF_KEEP 0x0000
#endif
/* bits that are copied from the result (without the sign flag) */
#ifndef AF_COPY
# define AF_COPY 0x0000
#endif

/* these flags occur regularly in calculations, so they must be defined */
#ifndef F_S
# define F_S 0 /* sm83 does not define this */
#endif
#ifndef F_P
# define F_P 0 /* sm83 does not define this */
#endif
#ifndef F_V
# define F_V 0 /* i80 does not define this, but i85 and vm1 do */
#endif
#ifndef F_N
# define F_N 0 /* i80, i85, vm1 do not define this */
#endif
#ifndef F_UI
# define F_UI 0 /* only i85 defines this */
#endif
#ifndef F_H
# define F_H 0 /* pre-i80 do not define this */
#endif

#ifndef wordsize
INLINE size_t wordsize(x80_state_t * cpu)
{
	return 2;
}
#endif

#ifndef get_address
INLINE address_t get_address(x80_state_t * cpu, address_t address, x80_access_type_t access)
{
# if !defined ADDRESS_MASK && !defined IOADDRESS_MASK
	return address;
# elif !defined IOADDRESS_MASK || ADDRESS_MASK == IOADDRESS_MASK
	return address & ADDRESS_MASK;
# else
	if((access & X80_ACCESS_MODE_PORT) != 0)
		return address & IOADDRESS_MASK;
	else
		return address & ADDRESS_MASK;
# endif
}
#endif

#ifndef GETSHORT
# define GETSHORT(a) ((a)&0xFFFF)
#endif
#ifndef GETLONG
# define GETLONG(a) ((a)&0xFFFF)
#endif
#ifndef SETSHORT
# define SETSHORT(a, b) ((a) = GETSHORT(b))
#endif
#ifndef SETLONG
# define SETLONG(a, b) ((a) = GETLONG(b))
#endif

/* handles addresses at the current address size
	z380: 16-bit in native mode, 32-bit in enhanced mode
	ez80: 16-bit in Z80 mode, 24-bit in ADL mode
	others: 16-bit
*/
#ifndef GETADDR
# define GETADDR(a) (GETLONG(a))
#endif
#ifndef SETADDR
# define SETADDR(a, b) (SETLONG(a, b))
#endif
#ifndef ADDADDR
# define ADDADDR(a, d) (GETLONG((a) + (d)))
#endif

