#ifndef __PREFIX__
#define __PREFIX__

#define __x86_64__
//__x86__,__arm__,__arm64__,__mips__,
#define TB_ARCH_ELF

// check 64-bits
#if defined(__LP64__) \
    || defined(__64BIT__) \
    || defined(_LP64) \
    || defined(__x86_64) \
    || defined(__x86_64__) \
    || defined(__amd64) \
    || defined(__amd64__) \
    || defined(__arm64) \
    || defined(__arm64__) \
    || defined(__sparc64__) \
    || defined(__PPC64__) \
    || defined(__powerpc64__) \
    || defined(_M_X64) \
    || defined(_M_AMD64) \
    || defined(_M_IA64) \
    || (defined(__WORDSIZE) && (__WORDSIZE == 64)) \
    || defined(TCC_TARGET_X86_64)
#   define TB_CPU_BITSIZE       (64)
#   define TB_CPU_BITBYTE       (8)
#   define TB_CPU_BITALIGN      (7)
#   define TB_CPU_BIT32         (0)
#   define TB_CPU_BIT64         (1)
#   define TB_CPU_SHIFT         (6)
#else
#   define TB_CPU_BITSIZE       (32)
#   define TB_CPU_BITBYTE       (4)
#   define TB_CPU_BITALIGN      (3)
#   define TB_CPU_BIT32         (1)
#   define TB_CPU_BIT64         (0)
#   define TB_CPU_SHIFT         (5)
#endif

//----------------------------------------------------------------------------------------------------------------------------------

/*! function
 * 
 * @code
    function(func_xxxx)
        ...
    endfunc
   @endcode
 */
#if defined(__x86_64__) || defined(__x86__)

#ifdef TB_ARCH_ELF
#   define function(name) \
    .text ;\
    .align TB_CPU_BITBYTE ; \
    .global name ;\
    .type   name, %function; \
    .hidden name; \
name:
#else
#   define function(name) \
    .text ;\
    .align TB_CPU_BITBYTE ; \
    .global _##name ;\
_##name:
#endif

// end function
#define endfunc

#endif

//----------------------------------------------------------------------------------------------------------------------------------

/*! function
 * 
 * @code
    function func_xxxx, export=1
        ...
    endfunc
   @endcode
 */
#ifdef __mips__

.macro function name, export=0
    .macro endfunc
        .end \name
        .size \name, . - \name
        .purgem endfunc
    .endm

        .text
        .align TB_CPU_BITBYTE
    .if \export
        .global \name
        .type   \name, @function
        .hidden \name
        .ent \name
\name:
    .else
        .type   \name, @function
        .hidden \name
        .ent \name
\name:
    .endif
.endm

#endif

//----------------------------------------------------------------------------------------------------------------------------------

#ifdef __arm__

#ifdef TB_ARCH_ELF
#   define ELF
#   define EXTERN_ASM
#else
#   define ELF          @
#   define EXTERN_ASM   _
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * arch
 */

#if defined(TB_ARCH_ARM_v8)
        .arch armv8-a
#elif defined(TB_ARCH_ARM_v7A)
        .arch armv7-a
#elif defined(TB_ARCH_ARM_v7)
        .arch armv7
#elif defined(TB_ARCH_ARM_v6) 
        .arch armv6
#elif defined(TB_ARCH_ARM_v5te) 
        .arch armv5te
#elif defined(TB_ARCH_ARM_v5) 
        .arch armv5
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * fpu
 */

#if defined(TB_ARCH_ARM_NEON) && !defined(TB_ARCH_ARM64)
        .fpu neon
#elif defined(TB_ARCH_VFP)
        .fpu vfp
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * syntax
 */
#if defined(TB_ARCH_ARM) && !defined(TB_ARCH_ARM64)
        .syntax unified
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * eabi
 */
#if defined(TB_ARCH_ELF) && defined(TB_ARCH_ARM) && !defined(TB_ARCH_ARM64)
        .eabi_attribute 25, 1 
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * macros
 */

/*! function
 * 
 * @code
    function func_xxxx, export=1
        ...
    endfunc
   @endcode
 */
.macro function name, export=0
    .macro endfunc
ELF     .size \name, . - \name
        .purgem endfunc
    .endm

        .text
        .align TB_CPU_BITBYTE
    .if \export
        .global EXTERN_ASM\name
ELF     .type   EXTERN_ASM\name, %function
ELF     .hidden EXTERN_ASM\name
EXTERN_ASM\name:
    .else
ELF     .type   \name, %function
ELF     .hidden \name
\name:
    .endif
.endm

/*! label
 * 
 * @code
    label name
        ...
   @endcode
 */
.macro label name
        .align TB_CPU_BITBYTE
\name:
.endm

#endif

//----------------------------------------------------------------------------------------------------------------------------------

#ifdef __arm64__

#ifdef TB_ARCH_ELF
#   define ELF
#   define EXTERN_ASM
#else
#   define ELF          //
#   define EXTERN_ASM   _
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * arch
 */

#if defined(TB_ARCH_ARM_v8)
        .arch armv8-a
#elif defined(TB_ARCH_ARM_v7A)
        .arch armv7-a
#elif defined(TB_ARCH_ARM_v7)
        .arch armv7
#elif defined(TB_ARCH_ARM_v6) 
        .arch armv6
#elif defined(TB_ARCH_ARM_v5te) 
        .arch armv5te
#elif defined(TB_ARCH_ARM_v5) 
        .arch armv5
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * fpu
 */

#if defined(TB_ARCH_ARM_NEON) && !defined(TB_ARCH_ARM64)
        .fpu neon
#elif defined(TB_ARCH_VFP)
        .fpu vfp
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * syntax
 */
#if defined(TB_ARCH_ARM) && !defined(TB_ARCH_ARM64)
        .syntax unified
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * eabi
 */
#if defined(TB_ARCH_ELF) && defined(TB_ARCH_ARM) && !defined(TB_ARCH_ARM64)
        .eabi_attribute 25, 1 
#endif

/* //////////////////////////////////////////////////////////////////////////////////////
 * macros
 */

/*! function
 * 
 * @code
    function func_xxxx, export=1
        ...
    endfunc
   @endcode
 */
.macro function name, export=0
    .macro endfunc
ELF     .size \name, . - \name
        .purgem endfunc
    .endm

        .text
        .align TB_CPU_BITBYTE
    .if \export
        .global EXTERN_ASM\name
ELF     .type   EXTERN_ASM\name, %function
ELF     .hidden EXTERN_ASM\name
EXTERN_ASM\name:
    .else
ELF     .type   \name, %function
ELF     .hidden \name
\name:
    .endif
.endm

/*! label
 * 
 * @code
    label name
        ...
   @endcode
 */
.macro label name
        .align TB_CPU_BITBYTE
\name:
.endm
#endif
//----------------------------------------------------------------------------------------------------------------------------------
#endif