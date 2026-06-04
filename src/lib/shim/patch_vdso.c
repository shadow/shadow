#include <assert.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "lib/logger/logger.h"

// Read `filename` into an mmap'd buffer. `*buffer` is set to the mmap'd buffer. `*buflen` is set to
// the size of the buffer.  `*filelen` is set to the number of bytes read from the file into the
// buffer.
//
// Doesn't attempt to mmap or stat the file, making it suitable for `/proc` files.
//
// Avoids known-not-signal-safe libc functions (e.g. anything that allocates other than mmap).
//
// Caller is responsible for deallocating `*buffer`, by `munmap(*buffer, *buflen)`.
static void _read_file_into_mmapd(const char* filename, char** buffer, size_t* buflen,
                                  size_t* filelen) {
    *buflen = 4096;
    *buffer = mmap(NULL, *buflen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
    if (*buffer == MAP_FAILED) {
        panic("mmap: %s", strerror(errno));
    }
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        panic("open: %s", strerror(errno));
    }
    *filelen = 0;
    while (true) {
        ssize_t remaining_capacity = *buflen - *filelen;
        if (remaining_capacity <= 0) {
            // We ran out of space. Reallocate a larger buffer.
            size_t old_region_size = *buflen;
            *buflen *= 2;
            *buffer = mremap(*buffer, old_region_size, *buflen, MREMAP_MAYMOVE);
            if (*buffer == MAP_FAILED) {
                panic("mremap: %s", strerror(errno));
            }
            continue;
        }
        ssize_t this_read = read(fd, &(*buffer)[*filelen], remaining_capacity);
        if (this_read < 0) {
            panic("read: %s", strerror(errno));
        }
        if (this_read == 0) {
            // EOF
            break;
        }
        *filelen += this_read;
    }
    if (close(fd) == -1) {
        panic("close: %s", strerror(errno));
    }
}

static void _getVdsoBounds(void** start, void** end) {
    assert(start);
    *start = NULL;
    assert(end);
    *end = NULL;

    // First read all of /proc/self/maps into an mmap'd buffer.
    char* maps_buffer = NULL;
    size_t maps_buffer_size = 0;
    size_t maps_file_size = 0;
    _read_file_into_mmapd("/proc/self/maps", &maps_buffer, &maps_buffer_size, &maps_file_size);

    const char* label = "[vdso]\n";
    char* line_end = memmem(maps_buffer, maps_file_size, label, strlen(label));
    if (line_end == NULL) {
        panic("Couldn't find [vdso] line");
    }
    // terminate the string. this overwrites the first bracket of "[vdso]", but
    // that's ok.
    *line_end = '\0';
    // Look backwards for the preceding newline.
    const char* last_newline = rindex(maps_buffer, '\n');
    const char* line = NULL;
    if (last_newline != NULL) {
        // the [vdso] line starts just after the newline.
        line = last_newline + 1;
    } else {
        // the [vdso] line is the first line in the file
        line = maps_buffer;
    }
    // Parse the line.
    if (sscanf(line, "%p-%p", start, end) != 2) {
        panic("Couldn't parse maps line: %s", line);
    }
    if (munmap(maps_buffer, maps_buffer_size) == -1) {
        panic("munmap: %s", strerror(errno));
    };
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

// Inject a trampoline that uses a relative jump. Only needs 5 bytes, but requires
// that the offset fits in an i32.
//
// Returns injected trampoline size on success, or 0 if the trampoline couldn't be injected.
static size_t _inject_trampoline_relative(void* start, size_t symbolSize, void* replacementFn) {
    intptr_t jmp_offset = (intptr_t)replacementFn - ((intptr_t)start + 5);

    if (jmp_offset > INT32_MAX || jmp_offset < INT32_MIN) {
        trace("Offset from %p to %p doesn't fit in i32", start, replacementFn);
        return 0;
    }

    const size_t trampolineSize = 5;
    if (symbolSize < trampolineSize) {
        trace("Can't inject %zd byte trampoline into %zd byte symbol", trampolineSize, symbolSize);
        return 0;
    }

    uint8_t* current = start;
    // opcode for jmp
    *(current++) = 0xe9;
    // jmp offset
    *(int32_t*)current = (int32_t)jmp_offset;
    current += sizeof(int32_t);

    size_t actualTrampolineSize = (size_t)current - (size_t)start;
    assert(actualTrampolineSize == trampolineSize);
    return actualTrampolineSize;
}

// Inject a trampoline using an absolute jump. Less efficient at runtime and
// needs 13 bytes, but can jump to any target address.
//
// Returns injected trampoline size on success, or 0 if the trampoline couldn't be injected.
static size_t _inject_trampoline_absolute(void* start, size_t symbolSize, void* replacementFn) {
    const size_t trampolineSize = 13;

    if (symbolSize < trampolineSize) {
        trace("Can't inject %zd byte trampoline into %zd byte symbol", trampolineSize, symbolSize);
        return 0;
    }

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

    size_t actualTrampolineSize = (size_t)current - (size_t)start;
    assert(actualTrampolineSize == trampolineSize);
    return actualTrampolineSize;
}

static void _inject_trampoline(struct ParsedElf* parsedElf, const char* vdsoFnName,
                               void* replacementFn) {
    const Elf64_Sym* symbol = _findSymbol(parsedElf, vdsoFnName);
    if (symbol == NULL) {
        // This could happen e.g. if vdso is disabled at the system level.
        warning("Couldn't find symbol '%s' to override", vdsoFnName);
        return;
    }

    uint8_t* start = (void*)parsedElf->hdr + symbol->st_value;

    size_t actualTrampolineSize =
        _inject_trampoline_relative(start, symbol->st_size, replacementFn);
    if (actualTrampolineSize == 0) {
        actualTrampolineSize = _inject_trampoline_absolute(start, symbol->st_size, replacementFn);
    }
    // TODO: Some other trampoline strategies if neither of the above work:
    //
    // * In case the replacementFn is more than a 32-bit offset away, create a secondary
    //   trampoline that *is* < 32-bit-offset away, e.g. using `mmap` with a supplied hint.
    //
    // * Inject `ud2; ret`, or even just `ud2`. `ud2` is only 2 bytes and raises
    //   SIGILL; we could get control in the SIGILL signal handler and figure out
    //   which patched function we're trying to execute by inspecting the
    //   instruction pointer in the siginfo_t.

    if (actualTrampolineSize == 0) {
        // TODO: Make make this a warning or error when shim-side logs are more visible.
        panic("Couldn't patch symbol '%s'", vdsoFnName);
    }

    // Validate that we didn't actually clobber another symbol.
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
