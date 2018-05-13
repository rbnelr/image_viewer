#pragma once

#define RZ_COMP_GCC				1
#define RZ_COMP_LLVM			2
#define RZ_COMP_MSVC			3
// Determining the compiler
#if !defined RZ_COMP
	#if _MSC_VER && !__INTELRZ_COMPILER && !__clan_
		#define RZ_COMP RZ_COMP_MSVC
	#elif __GNUC__ && !__clan_
		#define RZ_COMP RZ_COMP_GCC
	#elif __clan_
		#define RZ_COMP RZ_COMP_LLVM
	#else
		#warning Cannot determine compiler!.
	#endif
#endif

#undef FORCEINLINE

#if RZ_COMP == RZ_COMP_MSVC
	#define FORCEINLINE						__forceinline
	#define NOINLINE						__declspec(noinline)
	#define DBGBREAK						__debugbreak()
	
#include <limits>

	#define F32_INF							((float)(1e+300 * 1e+300))
	#define F64_INF							(1e+300 * 1e+300)
	#define F32_QNAN						std::numeric_limits<float>::quiet_NaN()
	#define F64_QNAN						std::numeric_limits<double>::quiet_NaN()
	
#elif RZ_COMP == RZ_COMP_LLVM
	#define FORCEINLINE						__attribute__((always_inline)) inline
	#define NOINLINE						__attribute__((noinline))
	#define DBGBREAK						do { asm volatile ("int3"); } while(0)
		
	#define F32_INF							(__builtin_inff())
	#define F64_INF							(__builtin_inf())
	#define F32_QNAN						((float)__builtin_nan("0"))
	#define F64_QNAN						(__builtin_nan("0"))
	
#elif RZ_COMP == RZ_COMP_GCC
	#define FORCEINLINE						__attribute__((always_inline)) inline
	#define NOINLINE						__attribute__((noinline))
	#define DBGBREAK						do { __debugbreak(); } while(0)
	
	#define F32_INF							(__builtin_inff())
	#define F64_INF							(__builtin_inf())
	#define F32_QNAN						((float)__builtin_nan("0"))
	#define F64_QNAN						(__builtin_nan("0"))
	
#endif
