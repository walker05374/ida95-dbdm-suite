// ============================================================================
// File          : visibility.hpp
// Architecture  : ELF64 AArch64 (arm64-v8a)
// Standard      : C++20
// Purpose       : Symbol visibility macros for ELF dynamic symbol table hygiene
// ============================================================================
#pragma once

#if defined(__GNUC__) || defined(__clang__)
    #define INSTRUMENT_LOCAL    __attribute__((visibility("hidden")))
    #define INSTRUMENT_EXPORT   __attribute__((visibility("default")))
#else
    #define INSTRUMENT_LOCAL
    #define INSTRUMENT_EXPORT
#endif

#ifdef __cplusplus
    #define INSTRUMENT_C_EXPORT extern "C" INSTRUMENT_EXPORT
#else
    #define INSTRUMENT_C_EXPORT INSTRUMENT_EXPORT
#endif

