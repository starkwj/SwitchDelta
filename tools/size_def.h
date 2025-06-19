#pragma once

#define forceinline __inline__ __attribute__((always_inline))

#define KB (1ULL << 10)
#define MB (1ULL << 20)
#define GB (1ull << 30)
#define SDIVNS (1e9)

#define ADD_ROUND(x, n) ((x) = ((x) + 1) % (n))
#define GET_ROUND_M(x, n, m) ((x + n + m) % (n))
#define ADD_ROUND_M(x, n, m) ((x) = ((x) + (m)) % (n))
#define GET_NEXT_ROUND(x, n) (((x) + 1) % (n))
#define ALIGNMENT_XB(x, b) (((x) + ((b)-1))/(b) * (b))  