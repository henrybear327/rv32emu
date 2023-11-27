/*
 * rv32emu is freely redistributable under the MIT License. See the file
 * "LICENSE" for information on usage and redistribution of this file.
 */

#include <stdlib.h>
#include <string.h>

#include "elf.h"
#include "io.h"
#include "utils.h"

#if defined(_WIN32)
/* fallback to standard I/O text stream */
#include <stdio.h>
#else
/* Assume POSIX-compatible runtime */
#define USE_MMAP 1
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

enum {
    EM_RISCV = 243,
};

enum {
    ELFCLASS32 = 1,
};

enum {
    PT_NULL = 0,
    PT_LOAD = 1,
    PT_DYNAMIC = 2,
    PT_INTERP = 3,
    PT_NOTE = 4,
    PT_SHLIB = 5,
    PT_PHDR = 6,
    PT_TLS = 7,
};

enum {
    STT_NOTYPE = 0,
    STT_OBJECT = 1,
    STT_FUNC = 2,
    STT_SECTION = 3,
    STT_FILE = 4,
    STT_COMMON = 5,
    STT_TLS = 6,
};

#define ELF_ST_TYPE(x) (((unsigned int) x) & 0xf)

struct elf_internal {
    const struct Elf32_Ehdr *hdr;
    uint32_t raw_size;
    uint8_t *raw_data;

    /* symbol table map: uint32_t -> (const char *) */
    map_t symbols;
};

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

elf_t *elf_new()
{
    elf_t *e = malloc(sizeof(elf_t));
    e->hdr = NULL;
    e->raw_size = 0;
    e->symbols = map_init(int, char *, map_cmp_uint);
    e->raw_data = NULL;
    return e;
}

void elf_delete(elf_t *e)
{
    if (!e)
        return;

    map_delete(e->symbols);
#if defined(USE_MMAP)
    if (e->raw_data)
        munmap(e->raw_data, e->raw_size);
#else
    free(e->raw_data);
#endif
    free(e);
}

/* release a loaded ELF file */
static void release(elf_t *e)
{
#if !defined(USE_MMAP) && !defined(FUZZER)
    free(e->raw_data);
#endif

    e->raw_data = NULL;
    e->raw_size = 0;
    e->hdr = NULL;
}

/* check if the ELF file header is valid */
static bool is_valid(elf_t *e)
{
    /* check for ELF magic */
    if (memcmp(e->hdr->e_ident, "\177ELF", 4))
        return false;

    /* must be 32bit ELF */
    if (e->hdr->e_ident[EI_CLASS] != ELFCLASS32)
        return false;

    /* check if machine type is RISC-V */
    if (e->hdr->e_machine != EM_RISCV)
        return false;

    return true;
}

/* get section header string table */
static const char *get_sh_string(elf_t *e, int index)
{
    uint32_t offset =
        e->hdr->e_shoff + e->hdr->e_shstrndx * e->hdr->e_shentsize;
    const struct Elf32_Shdr *shdr =
        (const struct Elf32_Shdr *) (e->raw_data + offset);
    return (const char *) (e->raw_data + shdr->sh_offset + index);
}

/* get a section header */
static const struct Elf32_Shdr *get_section_header(elf_t *e, const char *name)
{
    for (int s = 0; s < e->hdr->e_shnum; ++s) {
        uint32_t offset = e->hdr->e_shoff + s * e->hdr->e_shentsize;
        const struct Elf32_Shdr *shdr =
            (const struct Elf32_Shdr *) (e->raw_data + offset);
        const char *sname = get_sh_string(e, shdr->sh_name);
        if (!strcmp(name, sname))
            return shdr;
    }
    return NULL;
}

/* get the ELF string table */
static const char *get_strtab(elf_t *e)
{
    const struct Elf32_Shdr *shdr = get_section_header(e, ".strtab");
    if (!shdr)
        return NULL;

    return (const char *) (e->raw_data + shdr->sh_offset);
}

/* find a symbol entry */
const struct Elf32_Sym *elf_get_symbol(elf_t *e, const char *name)
{
    const char *strtab = get_strtab(e); /* get the string table */
    if (!strtab)
        return NULL;

    /* get the symbol table */
    const struct Elf32_Shdr *shdr = get_section_header(e, ".symtab");
    if (!shdr)
        return NULL;

    /* find symbol table range */
    const struct Elf32_Sym *sym =
        (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset);
    const struct Elf32_Sym *end =
        (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset +
                                    shdr->sh_size);

    for (; sym < end; ++sym) { /* try to find the symbol */
        const char *sym_name = strtab + sym->st_name;
        if (!strcmp(name, sym_name))
            return sym;
    }

    /* no symbol found */
    return NULL;
}

static void fill_symbols(elf_t *e)
{
    /* initialize the symbol table */
    map_clear(e->symbols);
    map_insert(e->symbols, &(int){0}, &(char *){NULL});

    /* get the string table */
    const char *strtab = get_strtab(e);
    if (!strtab)
        return;

    /* get the symbol table */
    const struct Elf32_Shdr *shdr = get_section_header(e, ".symtab");
    if (!shdr)
        return;

    /* find symbol table range */
    const struct Elf32_Sym *sym =
        (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset);
    const struct Elf32_Sym *end =
        (const struct Elf32_Sym *) (e->raw_data + shdr->sh_offset +
                                    shdr->sh_size);

    for (; sym < end; ++sym) { /* try to find the symbol */
        const char *sym_name = strtab + sym->st_name;
        switch (ELF_ST_TYPE(sym->st_info)) { /* add to the symbol table */
        case STT_NOTYPE:
        case STT_OBJECT:
        case STT_FUNC:
            map_insert(e->symbols, (void *) &(sym->st_value), &sym_name);
        }
    }
}

const char *elf_find_symbol(elf_t *e, uint32_t addr)
{
    if (map_empty(e->symbols))
        fill_symbols(e);
    map_iter_t it;
    map_find(e->symbols, &it, &addr);
    return map_at_end(e->symbols, &it) ? NULL : map_iter_value(&it, char *);
}

bool elf_get_data_section_range(elf_t *e, uint32_t *start, uint32_t *end)
{
    const struct Elf32_Shdr *shdr = get_section_header(e, ".data");
    if (!shdr || shdr->sh_type == SHT_NOBITS)
        return false;

    *start = shdr->sh_addr;
    *end = *start + shdr->sh_size;
    return true;
}

/* ported from librisvc */

// template <int W>
// RISCV_INTERNAL void Memory<W>::binary_load_ph(const MachineOptions<W>
// &options,
//                                               const Phdr *hdr)
// {
//     const auto *src = m_binary.data() + hdr->p_offset;
//     const size_t len = hdr->p_filesz;
//     if (m_binary.size() <= hdr->p_offset ||
//         hdr->p_offset + len < hdr->p_offset) {
//         throw MachineException(INVALID_PROGRAM,
//                                "Bogus ELF program segment offset");
//     }
//     if (m_binary.size() < hdr->p_offset + len) {
//         throw MachineException(INVALID_PROGRAM,
//                                "Not enough room for ELF program segment");
//     }
//     if (hdr->p_vaddr + len < hdr->p_vaddr) {
//         throw MachineException(INVALID_PROGRAM,
//                                "Bogus ELF segment virtual base");
//     }

//     if (options.verbose_loader) {
//         printf("* Loading program of size %zu from %p to virtual %p\n", len,
//                src, (void *) (uintptr_t) hdr->p_vaddr);
//     }
//     // Serialize pages cannot be called with len == 0,
//     // and there is nothing further to do.
//     if (unlikely(len == 0))
//         return;

//     // segment permissions
//     const PageAttributes attr{.read = (hdr->p_flags & PF_R) != 0,
//                               .write = (hdr->p_flags & PF_W) != 0,
//                               .exec = (hdr->p_flags & PF_X) != 0};
//     if (options.verbose_loader) {
//         printf("* Program segment readable: %d writable: %d  executable:
//         %d\n",
//                attr.read, attr.write, attr.exec);
//     }

//     if (attr.exec && this->cached_execute_segments() == 0) {
//         serialize_execute_segment(options, hdr);
//         // Nothing more to do here, if execute-only
//         if (!attr.read)
//             return;
//     }
//     // We would normally never allow this
//     if (attr.exec && attr.write) {
//         if (!options.allow_write_exec_segment) {
//             throw MachineException(INVALID_PROGRAM,
//                                    "Insecure ELF has writable executable
//                                    code");
//         }
//     }
//     // In some cases we want to enforce execute-only
//     if (attr.exec && (attr.read || attr.write)) {
//         if (options.enforce_exec_only) {
//             throw MachineException(INVALID_PROGRAM,
//                                    "Execute segment must be execute-only");
//         }
//     }
//     if (attr.write) {
//         if (this->m_initial_rodata_end == RWREAD_BEGIN)
//             this->m_initial_rodata_end = hdr->p_vaddr;
//         else
//             this->m_initial_rodata_end =
//                 std::min(m_initial_rodata_end, hdr->p_vaddr);
//     }

//     // Load into virtual memory
//     this->memcpy(hdr->p_vaddr, src, len);

//     if (options.protect_segments) {
//         this->set_page_attr(hdr->p_vaddr, len, attr);
//     } else {
//         // this might help execute simplistic barebones programs
//         this->set_page_attr(hdr->p_vaddr, len,
//                             {.read = true, .write = true, .exec = true});
//     }
// }

bool validate_header(elf_t *e)
{
    if (e->hdr->e_ident[EI_MAG0] != 0x7F || e->hdr->e_ident[EI_MAG1] != 'E' ||
        e->hdr->e_ident[EI_MAG2] != 'L' || e->hdr->e_ident[EI_MAG3] != 'F')
        return false;
    return e->hdr->e_ident[EI_CLASS] == ELFCLASS32;
}

static bool verify_elf(elf_t *e)
{
    /* check if the ELF program is too short */
    if (sizeof(struct Elf32_Ehdr) > e->raw_size) {
        release(e);
        return false;
    }

    /* check if the ELF header is valid */
    if (unlikely(!validate_header(e))) {
        return false;
    }

    /* check if the ELF program is an executable type */
    if (unlikely(e->hdr->e_type != ET_EXEC)) {
        return false;
    }

    /* check if the ELF program is a RISC-V executable */
    if (unlikely(e->hdr->e_machine != EM_RISCV)) {
        return false;
    }

    /* check if program headers exist */
    Elf32_Half program_headers = e->hdr->e_phnum;
    if (unlikely(program_headers <= 0)) {
        return false;
    }

    /* check if the total amount of program headers is less than 16 */
    if (unlikely(program_headers >= 16)) {
        return false;
    }

    /* check if the program headers have valid offset */
    if (unlikely(e->hdr->e_phoff > 0x4000)) {
        return false;
    }

    /* check if the program headers are within the binary */
    if (unlikely(e->hdr->e_phoff + program_headers * sizeof(struct Elf32_Phdr) >
                 e->raw_size)) {
        return false;
    }

    /* Load program segments */
    struct Elf32_Phdr *phdr =
        (struct Elf32_Phdr *) (e->raw_data + e->hdr->e_phoff);
    // int m_start_address = e->hdr->e_entry;
    // int m_heap_address = 0;

    for (struct Elf32_Phdr *hdr = phdr; hdr < phdr + program_headers; hdr++) {
        /* Detect overlapping segments */
        for (struct Elf32_Phdr *ph = phdr; ph < hdr; ph++) {
            if (hdr->p_type == PT_LOAD && ph->p_type == PT_LOAD)
                if (ph->p_vaddr < hdr->p_vaddr + hdr->p_filesz &&
                    ph->p_vaddr + ph->p_filesz > hdr->p_vaddr) {
                    /* normal ELF should not have overlapping segments */
                    return false;
                }
        }

        // switch (hdr->p_type) {
        // case PT_LOAD:
        //     // loadable program segments
        //     if (options.load_program) {
        //         binary_load_ph(options, hdr);
        //     }
        //     break;
        // case PT_GNU_STACK:
        //     // This seems to be a mark for executable stack. Big NO!
        //     break;
        // case PT_GNU_RELRO:
        //     // throw std::runtime_error(
        //     //	"Dynamically linked ELF binaries are not supported");
        //     break;
        // }

        // Elf32_Word endm = hdr->p_vaddr + hdr->p_memsz;
        // endm += Page::size() - 1;
        // endm &= ~address_t(Page::size() - 1);
        // if (this->m_heap_address < endm)
        //     this->m_heap_address = endm;
    }

    return true;
}

/* A quick ELF briefer:
 *    +--------------------------------+
 *    | ELF Header                     |--+
 *    +--------------------------------+  |
 *    | Program Header                 |  |
 *    +--------------------------------+  |
 * +->| Sections: .text, .strtab, etc. |  |
 * |  +--------------------------------+  |
 * +--| Section Headers                |<-+
 *    +--------------------------------+
 *
 * Finding the section header table (SHT):
 *   File start + ELF_header.shoff -> section_header table
 * Finding the string table for section header names:
 *   section_header table[ELF_header.shstrndx] -> section header for name table
 * Finding data for section headers:
 *   File start + section_header.offset -> section Data
 */
bool elf_load(elf_t *e, riscv_t *rv, memory_t *mem)
{
    if (!verify_elf(e)) {
        return false;
    }

    /* set the entry point */
    if (!rv_set_pc(rv, e->hdr->e_entry))
        return false;

    /* loop over all of the program headers */
    for (int p = 0; p < e->hdr->e_phnum; ++p) {
        /* find next program header */
        uint32_t offset = e->hdr->e_phoff + (p * e->hdr->e_phentsize);
        const struct Elf32_Phdr *phdr =
            (const struct Elf32_Phdr *) (e->raw_data + offset);

        /* check this section should be loaded */
        if (phdr->p_type != PT_LOAD)
            continue;

        /* memcpy required range */
        const int to_copy = min(phdr->p_memsz, phdr->p_filesz);
        if (to_copy)
            memory_write(mem, phdr->p_vaddr, e->raw_data + phdr->p_offset,
                         to_copy);

        /* zero fill required range */
        const int to_zero = max(phdr->p_memsz, phdr->p_filesz) - to_copy;
        if (to_zero)
            memory_fill(mem, phdr->p_vaddr + to_copy, to_zero, 0);
    }

    return true;
}

#ifdef FUZZER
bool elf_open(elf_t *e, uint8_t *data, size_t len)
#else
bool elf_open(elf_t *e, const char *input)
#endif
{
    /* free previous memory */
    if (e->raw_data)
        release(e);

#ifndef FUZZER
    char *path = sanitize_path(input);
    if (!path) {
        return false;
    }
#endif

#if defined(FUZZER)
    if (!data || !len) {
        /* if the fuzzer sent in an empty buffer, we don't proceed further */
        return false;
    }

    /* get file size */
    e->raw_size = len;

    /* allocate memory */
    free(e->raw_data);
    e->raw_data = (uint8_t *) data;
#elif defined(USE_MMAP)
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        free(path);
        return false;
    }

    /* get file size */
    struct stat st;
    fstat(fd, &st);
    e->raw_size = st.st_size;

    /* map or unmap files or devices into memory.
     * The beginning of the file is ELF header.
     */
    e->raw_data = mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (e->raw_data == MAP_FAILED) {
        release(e);
        free(path);
        return false;
    }
    close(fd);
#else  /* fallback to standard I/O text stream */
    FILE *f = fopen(path, "rb");
    if (!f) {
        free(path);
        return false;
    }

    /* get file size */
    fseek(f, 0, SEEK_END);
    e->raw_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (e->raw_size == 0) {
        fclose(f);
        free(path);
        return false;
    }

    /* allocate memory */
    free(e->raw_data);
    e->raw_data = malloc(e->raw_size);

    /* read data into memory */
    const size_t r = fread(e->raw_data, 1, e->raw_size, f);
    fclose(f);
    if (r != e->raw_size) {
        release(e);
        free(path);
        return false;
    }
#endif /* USE_MMAP */

    /* point to the header */
    if (sizeof(struct Elf32_Ehdr) > e->raw_size) {
        release(e);
        return false;
    }
    e->hdr = (const struct Elf32_Ehdr *) e->raw_data;

    /* check it is a valid ELF file */
    if (!is_valid(e)) {
        release(e);
#ifndef FUZZER
        free(path);
#endif
        return false;
    }

#ifndef FUZZER
    free(path);
#endif
    return true;
}

struct Elf32_Ehdr *get_elf_header(elf_t *e)
{
    return (struct Elf32_Ehdr *) e->hdr;
}

uint8_t *get_elf_first_byte(elf_t *e)
{
    return (uint8_t *) e->raw_data;
}
