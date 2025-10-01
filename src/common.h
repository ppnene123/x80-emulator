#ifndef _COMMON_H
#define _COMMON_H

// common declarations for all frontends

#define PUBLIC
#define PRIVATE static
#define INLINE  static inline

#if !CPU_EZ80 && !CPU_SM83
PRIVATE void interpret_input(x80_state_t * cpu, int count, void * data);
#endif
PRIVATE void do_jump(x80_state_t * cpu, address_t target);
PRIVATE void do_call(x80_state_t * cpu, address_t target);

INLINE address_t popword(x80_state_t * cpu, size_t bytes);
INLINE void pushword(x80_state_t * cpu, size_t bytes, address_t value);

INLINE uint8_t readbyte(x80_state_t * cpu, address_t address);
INLINE address_t readword(x80_state_t * cpu, size_t bytes, address_t address);
INLINE address_t readword_access(x80_state_t * cpu, size_t bytes, address_t address, int access);

INLINE void writebyte(x80_state_t * cpu, address_t address, uint8_t value);
INLINE void writeword_access(x80_state_t * cpu, size_t bytes, address_t address, address_t value, int access);

#if CPU_Z280 || CPU_Z380 || CPU_EZ80
INLINE void exception_trap(x80_state_t *, int number, ...);
#endif

#endif /* _COMMON_H */
