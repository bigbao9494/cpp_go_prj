//这种方法在eclipse不会生效
#include "prefix.h"

#if defined(__x86__)
#include "x86/context.S"
#elif defined(__x86_64__)
#include "x64/context.S"
#elif defined(__arm64__)
#include "arm64/context.S"
#elif defined(__arm__)
#include "arm/context.S"
#elif defined(__mips__)
#include "mips/context.S"
#endif

