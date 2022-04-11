#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include "lib/logger/logger.h"

static void _getVdsoBounds(void** start, void** end) {
    assert(start);
    *start = NULL;
    assert(end);
    *end = NULL;

    FILE* maps = fopen("/proc/self/maps", "r");
    size_t n = 100;
    char* line = malloc(n);
    while (true) {
        ssize_t rv = getline(&line, &n, maps);
        if (rv < 0) {
            break;
        }
        const char* name = "[vdso]\n";
        size_t name_len = strlen(name);
        if (rv < name_len) {
            // Can't be [vdso].
            continue;
        }
        if (strcmp(&line[rv - name_len], name) != 0) {
            // Isn't [vdso].
            continue;
        }
        // *is* [vdso]. Parse the line.
        if (sscanf(line, "%p-%p", start, end) != 2) {
            warning("Couldn't parse maps line: %s", line);
            // Ensure both are still NULL.
            *start = NULL;
            *end = NULL;
            // Might as well keep going and see if another line matches and parses.
            continue;
        }
        // Success
        break;
    }

    free(line);
    fclose(maps);
}

static void _checkIdentByte(const unsigned char ident[EI_NIDENT], size_t idx, char expected) {
    if (ident[idx] != expected) {
        panic("Expected byte %zu of elf header to be %x; got %x", idx, expected, ident[idx]);
    }
}

static const Elf64_Shdr* _findSection(const Elf64_Ehdr* elfHdr, const char* sectionName) {
    if (elfHdr->e_shoff == 0) {
        panic("No section headers");
    }
    const Elf64_Shdr* sections = ((void*)elfHdr) + elfHdr->e_shoff;

    if (elfHdr->e_shstrndx == SHN_UNDEF) {
        panic("No section header names");
    }
    if (elfHdr->e_shstrndx == SHN_XINDEX) {
        panic("SHN_XINDEX unimplemented");
    }
    const Elf64_Shdr* sectionNameSection = &sections[elfHdr->e_shstrndx];
    const char* sectionNames = (void*)elfHdr + sectionNameSection->sh_offset;

    for (int i = 0; i < elfHdr->e_shnum; ++i) {
        const char* thisSectionName = &sectionNames[sections[i].sh_name];
        if (strcmp(sectionName, thisSectionName) == 0) {
            return &sections[i];
        }
    }
    return NULL;
}

struct ParsedElf {
    const void* mapStart;
    const void* mapEnd;
    const Elf64_Ehdr* hdr;
    const Elf64_Shdr* dynSymSectionHdr;
    const Elf64_Shdr* dynSymNameSectionHdr;
};

static struct ParsedElf _parseElf(const void* base) {
    const Elf64_Ehdr* elfHdr = base;

    void* mapStart;
    void* mapEnd;
    _getVdsoBounds(&mapStart, &mapEnd);
    if (mapStart == NULL || mapEnd == NULL) {
        panic("Couldn't find VDSO bounds");
    }
    if (base < mapStart || base > mapEnd) {
        panic("vdso base %p not within mapping bounds %p-%p", base, mapStart, mapEnd);
    }

    _checkIdentByte(elfHdr->e_ident, EI_MAG0, ELFMAG0);
    _checkIdentByte(elfHdr->e_ident, EI_MAG1, ELFMAG1);
    _checkIdentByte(elfHdr->e_ident, EI_MAG2, ELFMAG2);
    _checkIdentByte(elfHdr->e_ident, EI_MAG3, ELFMAG3);
    _checkIdentByte(elfHdr->e_ident, EI_CLASS, ELFCLASS64);
    _checkIdentByte(elfHdr->e_ident, EI_DATA, ELFDATA2LSB);
    _checkIdentByte(elfHdr->e_ident, EI_VERSION, EV_CURRENT);
    _checkIdentByte(elfHdr->e_ident, EI_OSABI, ELFOSABI_NONE);
    _checkIdentByte(elfHdr->e_ident, EI_ABIVERSION, 0);

    const Elf64_Shdr* dynSymSectionHdr = _findSection(elfHdr, ".dynsym");
    if (!dynSymSectionHdr) {
        panic("Couldn't find dynamic symbols");
    }
    const Elf64_Shdr* dynSymNameSectionHdr = _findSection(elfHdr, ".dynstr");
    if (!dynSymNameSectionHdr) {
        panic("Couldn't find dynamic symbol names");
    }

    return (struct ParsedElf){
        .mapStart = mapStart,
        .mapEnd = mapEnd,
        .hdr = elfHdr,
        .dynSymSectionHdr = dynSymSectionHdr,
        .dynSymNameSectionHdr = dynSymNameSectionHdr,
    };
}

static const Elf64_Sym* _findSymbol(const struct ParsedElf* parsedElf, const char* symbolName) {
    size_t numEntries =
        parsedElf->dynSymSectionHdr->sh_size / parsedElf->dynSymSectionHdr->sh_entsize;
    const Elf64_Sym* symbols = (void*)parsedElf->hdr + parsedElf->dynSymSectionHdr->sh_offset;
    const char* symbolNames = (void*)parsedElf->hdr + parsedElf->dynSymNameSectionHdr->sh_offset;
    for (int i = 0; i < numEntries; ++i) {
        const char* thisSymbolName = &symbolNames[symbols[i].st_name];
        if (strcmp(thisSymbolName, symbolName) == 0) {
            return &symbols[i];
        }
    }
    return NULL;
}

static int _replacement_gettimeofday(void* arg1, void* arg2) {
    return (int)syscall(SYS_gettimeofday, arg1, arg2);
}

static int _replacement_time(void* arg1) { return (int)syscall(SYS_time, arg1); }

static int _replacement_clock_gettime(void* arg1, void* arg2) {
    return (int)syscall(SYS_clock_gettime, arg1, arg2);
}

static int _replacement_getcpu(void* arg1, void* arg2, void* arg3) {
    return (int)syscall(SYS_getcpu, arg1, arg2, arg3);
}

static void _inject_trampoline(struct ParsedElf* parsedElf, const char* vdsoFnName,
                               void* replacementFn) {
    const Elf64_Sym* symbol = _findSymbol(parsedElf, vdsoFnName);
    if (symbol == NULL) {
        warning("Couldn't find symbol '%s' to override", vdsoFnName);
        return;
    }

    // Manually calculated trampoline size. Later validated in assertion.
    const size_t trampolineSize = 13;

    if (symbol->st_size < trampolineSize) {
        warning("Symbol '%s' not large enough to inject trampoline", vdsoFnName);
        return;
    }

    uint8_t* start = (void*)parsedElf->hdr + symbol->st_value;
    uint8_t* current = start;

    // movabs $...,%r10
    *(current++) = 0x49;
    *(current++) = 0xba;
    *(void**)current = replacementFn;
    current += sizeof(void*);

    // jmpq *%r10
    *(current++) = 0x41;
    *(current++) = 0xff;
    *(current++) = 0xe2;

    // Validate trampolineSize.
    const size_t actualTrampolineSize = (size_t)current - (size_t)start;
    // In debug builds require equality.
    assert(actualTrampolineSize == trampolineSize);
    // Otherwise just ensure we didn't actually clobber another symbol.
    if (symbol->st_size < actualTrampolineSize) {
        panic("Accidentally wrote %zd byte trampoline into %zd byte symbol %s",
              actualTrampolineSize, symbol->st_size, vdsoFnName);
    }
}

void patch_vdso(void* vdsoBase) {
    struct ParsedElf parsedElf = _parseElf(vdsoBase);
    size_t regionSize = (size_t)parsedElf.mapEnd - (size_t)parsedElf.mapStart;

    if (mprotect((void*)parsedElf.mapStart, regionSize, PROT_READ | PROT_WRITE | PROT_EXEC)) {
        panic("mprotect: %s", strerror(errno));
    }

    _inject_trampoline(&parsedElf, "__vdso_gettimeofday", _replacement_gettimeofday);
    _inject_trampoline(&parsedElf, "__vdso_time", _replacement_time);
    _inject_trampoline(&parsedElf, "__vdso_clock_gettime", _replacement_clock_gettime);
    _inject_trampoline(&parsedElf, "__vdso_getcpu", _replacement_getcpu);

    if (mprotect((void*)parsedElf.mapStart, regionSize, PROT_READ | PROT_EXEC)) {
        panic("mprotect: %s", strerror(errno));
    }
}