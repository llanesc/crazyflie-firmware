#pragma once

#if defined(__MACH__)
    // Mach-O: Don't use the name of the section, Mach-O is limited to 16 chars
    #define SECTION_LOG(name)   __attribute__((section("__DATA,.log"), used))
    #define SECTION_PARAM(name) __attribute__((section("__DATA,.param"), used))
    #define SECTION_EVENTTRIGGER(name) __attribute__((section("__DATA,.eventtrigger"), used))
    #define SECTION(name)       __attribute__((section("__DATA," name), used))
#else
    // ELF: keep original scheme
    #define SECTION_LOG(name)   __attribute__((section(".log."   name), used))
    #define SECTION_PARAM(name) __attribute__((section(".param." name), used))
    #define SECTION_EVENTTRIGGER(name) __attribute__((section(".eventtrigger." name), used))
    #define SECTION(name)       __attribute__((section(name), used))
#endif