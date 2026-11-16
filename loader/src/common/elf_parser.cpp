#include "elf_parser.hpp"
#include "omg_loader_core.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

namespace ElfParser {

// Helper to calculate a pointer to a location within the mapped ELF file.
template <typename T>
static T* pointer_at(void* map_base, ElfW(Off) offset) {
    return reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(map_base) + offset);
}

ElfImage::ElfImage(std::string_view library_name, void *base_address) {
    // An explicit load base is useful for images whose path is known but which
    // are not discoverable through dl_iterate_phdr (for example, custom-loaded
    // images). In that case parse the supplied file path directly.
    if (!findLoadedLibraryInfo(library_name)) {
        if (!base_address) return;
        library_path_.assign(library_name.data(), library_name.size());
    }

    if (base_address) base_address_ = base_address;

    int fd = open(library_path_.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }

    struct stat file_stat;
    if (fstat(fd, &file_stat) < 0) {
        close(fd);
        return;
    }
    if (file_stat.st_size < 64) {
        close(fd);
        return;
    }
    map_size_ = file_stat.st_size;

    map_base_ = mmap(nullptr, map_size_, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    if (map_base_ == MAP_FAILED) {
        map_base_ = nullptr;
        return;
    }

    if (!omg_loader_core_validate_elf64_aarch64(
            static_cast<const uint8_t*>(map_base_), static_cast<size_t>(map_size_))) {
        munmap(map_base_, map_size_);
        map_base_ = nullptr;
        map_size_ = 0;
        return;
    }

    header_ = static_cast<ElfW(Ehdr)*>(map_base_);

    const ElfW(Phdr)* const program_headers = pointer_at<ElfW(Phdr)>(map_base_, header_->e_phoff);
    for (int i = 0; i < header_->e_phnum; ++i) {
        const ElfW(Phdr)* phdr = &program_headers[i];
        if (phdr->p_type == PT_LOAD) {
            // Calculate the "bias" or "load bias". This is the difference between
            // the virtual address where the segment is loaded in memory and its
            // offset in the file. All file offsets must be adjusted by this bias
            // to get their corresponding address in memory.
            // We only need to calculate this once from the first LOAD segment.
            bias_ = phdr->p_vaddr - phdr->p_offset;
            break;
        }
    }

    const ElfW(Shdr)* const section_headers = pointer_at<ElfW(Shdr)>(map_base_, header_->e_shoff);
    const ElfW(Shdr)* const shstrtab_header = &section_headers[header_->e_shstrndx];
    const char* const shstrtab = pointer_at<char>(map_base_, shstrtab_header->sh_offset);

    // Iterate through section headers to find the ones we need for symbol lookup.
    for (int i = 0; i < header_->e_shnum; ++i) {
        const ElfW(Shdr)* shdr = &section_headers[i];
        switch (shdr->sh_type) {
        case SHT_DYNSYM:
            dynsym_ = pointer_at<ElfW(Sym)>(map_base_, shdr->sh_offset);
            dynsym_count_ = shdr->sh_entsize ? shdr->sh_size / shdr->sh_entsize : 0;
            break;
        case SHT_SYMTAB:
            symtab_ = pointer_at<ElfW(Sym)>(map_base_, shdr->sh_offset);
            symtab_count_ = shdr->sh_size / shdr->sh_entsize;
            break;
        case SHT_STRTAB:
            // There can be multiple string tables. Differentiate them by name.
            if (strcmp(&shstrtab[shdr->sh_name], ".strtab") == 0) {
                strtab_ = pointer_at<char>(map_base_, shdr->sh_offset);
            } else if (strcmp(&shstrtab[shdr->sh_name], ".dynstr") == 0) {
                dynstr_ = pointer_at<char>(map_base_, shdr->sh_offset);
            }
            break;
        case SHT_HASH: {
            const uint32_t* hash_data = pointer_at<uint32_t>(map_base_, shdr->sh_offset);
            nbucket_ = hash_data[0];
            bucket_ = const_cast<uint32_t*>(&hash_data[2]);
            chain_ = const_cast<uint32_t*>(&bucket_[nbucket_]);
            break;
        }
        case SHT_GNU_HASH: {
            const uint32_t* gnu_hash_data = pointer_at<uint32_t>(map_base_, shdr->sh_offset);
            gnu_nbucket_ = gnu_hash_data[0];
            gnu_symindx_ = gnu_hash_data[1];
            gnu_bloom_size_ = gnu_hash_data[2];
            gnu_shift2_ = gnu_hash_data[3];
            gnu_bloom_filter_ = pointer_at<ElfW(Addr)>(map_base_, shdr->sh_offset + 16);
            gnu_bucket_ = reinterpret_cast<uint32_t*>(&gnu_bloom_filter_[gnu_bloom_size_]);
            gnu_chain_ = &gnu_bucket_[gnu_nbucket_];
            break;
        }
        }
    }
}

ElfImage::~ElfImage() {
    if (map_base_) {
        munmap(map_base_, map_size_);
    }
}

ElfW(Addr) ElfImage::findSymbolAddress(std::string_view symbol_name) const {
    return findSymbolAddress(symbol_name, nullptr);
}

ElfW(Addr) ElfImage::findSymbolAddress(std::string_view symbol_name, size_t *size) const {
    if (size) *size = 0;
    const ElfW(Addr) offset = findSymbolOffset(symbol_name, calculateGnuHash(symbol_name),
                                               calculateSysvHash(symbol_name));
    if (offset == 0 || !base_address_) return 0;

    if (size && dynsym_ && dynstr_) {
        for (ElfW(Off) i = 0; i < dynsym_count_; ++i) {
            const ElfW(Sym) *symbol = &dynsym_[i];
            if (symbol->st_name != 0 && symbol_name == &dynstr_[symbol->st_name]) {
                *size = symbol->st_size;
                break;
            }
        }
    }
    if (size && *size == 0 && symtab_ && strtab_) {
        for (ElfW(Off) i = 0; i < symtab_count_; ++i) {
            const ElfW(Sym) *symbol = &symtab_[i];
            if (symbol->st_name != 0 && symbol_name == &strtab_[symbol->st_name]) {
                *size = symbol->st_size;
                break;
            }
        }
    }

    return static_cast<ElfW(Addr)>(reinterpret_cast<uintptr_t>(base_address_) +
                                   offset - bias_);
}

void ElfImage::forEachSymbol(SymbolCallback callback, void *data) const {
    if (!callback || !base_address_) return;
    const uintptr_t base = reinterpret_cast<uintptr_t>(base_address_);

    if (dynsym_ && dynstr_) {
        for (ElfW(Off) i = 0; i < dynsym_count_; ++i) {
            const ElfW(Sym) &symbol = dynsym_[i];
            if (symbol.st_shndx == SHN_UNDEF || symbol.st_name == 0) continue;
            const char *name = &dynstr_[symbol.st_name];
            if (!name[0]) continue;
            const uintptr_t address = base + symbol.st_value - bias_;
            if (!callback(name, reinterpret_cast<void *>(address), symbol.st_size, data)) return;
        }
    }

    if (symtab_ && strtab_) {
        for (ElfW(Off) i = 0; i < symtab_count_; ++i) {
            const ElfW(Sym) &symbol = symtab_[i];
            if (symbol.st_shndx == SHN_UNDEF || symbol.st_name == 0) continue;
            const char *name = &strtab_[symbol.st_name];
            if (!name[0]) continue;
            const uintptr_t address = base + symbol.st_value - bias_;
            if (!callback(name, reinterpret_cast<void *>(address), symbol.st_size, data)) return;
        }
    }
}

ElfW(Addr) ElfImage::findSymbolOffset(std::string_view symbol_name, uint32_t gnu_hash,
                                      uint32_t sysv_hash) const {
    // We try the lookup methods in order of efficiency:
    // 1. GNU Hash: Fastest, uses a Bloom filter.
    // 2. System V Hash: Slower, but still a hash table.
    // 3. Linear Scan: Slowest, searches the entire symbol table.

    if (auto offset = findSymbolByGnuHash(symbol_name, gnu_hash); offset > 0) {
        return offset;
    }
    if (auto offset = findSymbolBySysvHash(symbol_name, sysv_hash); offset > 0) {
        return offset;
    }
    // const_cast is safe here because findSymbolByLinearScan only modifies a mutable cache.
    if (auto offset = const_cast<ElfImage*>(this)->findSymbolByLinearScan(symbol_name);
        offset > 0) {
        return offset;
    }

    return 0;
}

/*
 * GNU Hash (.gnu.hash) Lookup Algorithm
 *
 * This is a highly efficient hash table for symbol lookups, designed to minimize cache misses.
 *
 * Structure Diagram:
 *
 * +-----------------+
 * | Header          | (nbucket, symindx, bloom_size, shift2)
 * +-----------------+
 * | Bloom Filter    | (bloom_size words)
 * +-----------------+
 * | Hash Buckets    | (nbucket integers)
 * +-----------------+
 * | Hash Chain      | (...)
 * +-----------------+
 *
 * Lookup Steps:
 * 1. Calculate the GNU hash of the symbol name.
 *
 * 2. Check the Bloom Filter:
 *    - The Bloom filter is a probabilistic data structure that can quickly tell us if a
 *      symbol is *definitely not* in the table.
 *    - Two hash values derived from the original hash are used to check bits in the filter.
 *    - If the check fails, the symbol is not in the .dynsym section, and we can stop early.
 *
 * 3. Locate the Hash Bucket:
 *    - If the Bloom filter passes, use `hash % nbucket` to find the starting symbol index
 *      in the `gnu_bucket_` array.
 *
 * 4. Traverse the Hash Chain:
 *    - Follow the chain starting from the index found in the bucket.
 *    - The chain entries are stored in `gnu_chain_`.
 *    - For each symbol in the chain, compare its full name with the target name.
 *    - The end of a chain is marked by the least significant bit (LSB) of the chain entry being 1.
 */
ElfW(Addr) ElfImage::findSymbolByGnuHash(std::string_view symbol_name, uint32_t gnu_hash) const {
    if (gnu_bloom_filter_ == nullptr) return 0;

    constexpr auto bloom_mask_bits = sizeof(ElfW(Addr)) * 8;
    const ElfW(Addr) bloom_word = gnu_bloom_filter_[(gnu_hash / bloom_mask_bits) % gnu_bloom_size_];
    const ElfW(Addr) mask = (1ULL << (gnu_hash % bloom_mask_bits)) |
                            (1ULL << ((gnu_hash >> gnu_shift2_) % bloom_mask_bits));

    // If the Bloom filter test fails, the symbol is definitely not present.
    if ((bloom_word & mask) != mask) {
        return 0;
    }

    uint32_t sym_index = gnu_bucket_[gnu_hash % gnu_nbucket_];
    if (sym_index < gnu_symindx_) {
        // This bucket is empty or points to symbols not in this chain.
        return 0;
    }

    // Traverse the chain for this hash bucket.
    const uint32_t* chain = &gnu_chain_[sym_index - gnu_symindx_];
    do {
        // The top 31 bits of the chain entry are a hash validation.
        if (((*chain ^ gnu_hash) >> 1) == 0) {
            const ElfW(Sym)* sym = &dynsym_[sym_index];
            if (symbol_name == &dynstr_[sym->st_name]) {
                return sym->st_value;  // Found the symbol
            }
        }
        sym_index++;
    } while ((*chain++ & 1) == 0);  // The LSB of 1 marks the end of the chain.

    return 0;
}

ElfW(Addr) ElfImage::findSymbolBySysvHash(std::string_view symbol_name, uint32_t sysv_hash) const {
    if (bucket_ == nullptr) return 0;

    // Use the hash to find the start of the chain in the bucket array.
    for (uint32_t n = bucket_[sysv_hash % nbucket_]; n != 0; n = chain_[n]) {
        const ElfW(Sym)* sym = &dynsym_[n];
        if (symbol_name == &dynstr_[sym->st_name]) {
            return sym->st_value;  // Found the symbol
        }
    }
    return 0;
}

void ElfImage::buildSymbolCache() {
    if (symbol_cache_.empty() && symtab_ != nullptr && strtab_ != nullptr) {
        symbol_cache_.reserve(symtab_count_);
        for (ElfW(Off) i = 0; i < symtab_count_; ++i) {
            const ElfW(Sym)* sym = &symtab_[i];
            const unsigned char type = ELF_ST_TYPE(sym->st_info);
            // Cache only function and object symbols that have a size.
            if ((type == STT_FUNC || type == STT_OBJECT) && sym->st_size > 0) {
                symbol_cache_.emplace(&strtab_[sym->st_name], sym);
            }
        }
    }
}

ElfW(Addr) ElfImage::findSymbolByLinearScan(std::string_view symbol_name) {
    buildSymbolCache();
    if (auto it = symbol_cache_.find(symbol_name); it != symbol_cache_.end()) {
        return it->second->st_value;
    }
    return 0;
}

std::string_view ElfImage::findSymbolNameByPrefix(std::string_view prefix) {
    if (dynsym_ && dynstr_) {
        for (ElfW(Off) i = 0; i < dynsym_count_; ++i) {
            const ElfW(Sym) *symbol = &dynsym_[i];
            if (symbol->st_shndx == SHN_UNDEF || symbol->st_name == 0) continue;
            const std::string_view name(&dynstr_[symbol->st_name]);
            if (name.starts_with(prefix)) return name;
        }
    }
    buildSymbolCache();
    for (const auto& pair : symbol_cache_) {
        std::string_view symbol_name = pair.first;
        if (symbol_name.size() >= prefix.size() && symbol_name.substr(0, prefix.size()) == prefix) {
            return symbol_name;
        }
    }
    return "";
}

bool ElfImage::findLoadedLibraryInfo(std::string_view library_name) {
    struct LibraryInfo {
        std::string_view name;
        std::string* path_out;
        void** base_addr_out;
        bool found;
    };

    LibraryInfo info = {library_name, &library_path_, &base_address_, false};

    dl_iterate_phdr(
        [](struct dl_phdr_info* phdr_info, size_t, void* data) -> int {
            auto* lib_info = static_cast<LibraryInfo*>(data);
            if (phdr_info->dlpi_name && strstr(phdr_info->dlpi_name, lib_info->name.data())) {
                *lib_info->path_out = phdr_info->dlpi_name;
                *lib_info->base_addr_out = reinterpret_cast<void*>(phdr_info->dlpi_addr);
                lib_info->found = true;
                return 1;  // Return non-zero to stop iteration
            }
            return 0;  // Continue iteration
        },
        &info);

    return info.found;
}

}  // namespace ElfParser
