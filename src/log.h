#pragma once

#include <furi.h>

/**
 * Compile-time log level for Boot2Flipper
 *
 * Logs above B2F_LOG_LEVEL are compiled out. Per-sector I/O logs use their own
 * level above debug, so they stay out of normal debug builds. When enabled they
 * print at the Info runtime level, so they can be seen without turning on
 * Trace (which floods the log with firmware internals).
 *
 * Set it in application.fam, e.g. cdefines=["B2F_LOG_LEVEL=5"]
 */
#define B2F_LOG_LEVEL_NONE  0
#define B2F_LOG_LEVEL_ERROR 1
#define B2F_LOG_LEVEL_WARN  2
#define B2F_LOG_LEVEL_INFO  3
#define B2F_LOG_LEVEL_DEBUG 4
#define B2F_LOG_LEVEL_IO    5 // Per-sector I/O, very noisy and slows transfers down

#ifndef B2F_LOG_LEVEL
#define B2F_LOG_LEVEL B2F_LOG_LEVEL_DEBUG
#endif

// Disabled logs still type-check their arguments, so variables only used in
// logs don't trigger unused warnings; the compiler drops the code.
#define B2F_LOG_OFF(log, tag, ...)   \
    do {                             \
        if(0) log(tag, __VA_ARGS__); \
    } while(0)

#if B2F_LOG_LEVEL >= B2F_LOG_LEVEL_ERROR
#define B2F_LOG_E(tag, ...) FURI_LOG_E(tag, __VA_ARGS__)
#else
#define B2F_LOG_E(tag, ...) B2F_LOG_OFF(FURI_LOG_E, tag, __VA_ARGS__)
#endif

#if B2F_LOG_LEVEL >= B2F_LOG_LEVEL_WARN
#define B2F_LOG_W(tag, ...) FURI_LOG_W(tag, __VA_ARGS__)
#else
#define B2F_LOG_W(tag, ...) B2F_LOG_OFF(FURI_LOG_W, tag, __VA_ARGS__)
#endif

#if B2F_LOG_LEVEL >= B2F_LOG_LEVEL_INFO
#define B2F_LOG_I(tag, ...) FURI_LOG_I(tag, __VA_ARGS__)
#else
#define B2F_LOG_I(tag, ...) B2F_LOG_OFF(FURI_LOG_I, tag, __VA_ARGS__)
#endif

#if B2F_LOG_LEVEL >= B2F_LOG_LEVEL_DEBUG
#define B2F_LOG_D(tag, ...) FURI_LOG_D(tag, __VA_ARGS__)
#else
#define B2F_LOG_D(tag, ...) B2F_LOG_OFF(FURI_LOG_D, tag, __VA_ARGS__)
#endif

#if B2F_LOG_LEVEL >= B2F_LOG_LEVEL_IO
#define B2F_LOG_IO(tag, ...) FURI_LOG_I(tag, __VA_ARGS__)
#else
#define B2F_LOG_IO(tag, ...) B2F_LOG_OFF(FURI_LOG_I, tag, __VA_ARGS__)
#endif
