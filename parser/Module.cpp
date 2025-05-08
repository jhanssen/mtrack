#include "Module.h"
#include "Parser.h"
#include "Creatable.h"
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <backtrace.h>
#include <functional>
extern "C" {
#include <internal.h>
}

std::vector<Module*> Module::sModules;
namespace {
void backtrace_moduleErrorCallback(void* /*data*/, const char* msg, int errnum)
{
    printf("pc frame bad %s %d\n", msg, errnum);
}

static inline uint64_t read_uleb(int fd) {
    uint64_t result = 0;
    for(size_t shift = 0; true; shift += 7) {
        uint8_t byte;
        read(fd, &byte, sizeof(byte));
        result |= uint64_t(byte & 0x7F) << shift;
        if (!(byte & 0x80))
            break;
    }
    return result;
}
}

Module::Module(ApplicationType type, Indexer<std::string>& indexer, const std::string& filename, uint64_t addr)
    : mIndexer(indexer), mFileName(filename), mAddr(addr)
{
    auto state = backtrace_create_state(mFileName.c_str(), false, btErrorHandler, this);
    if (!state)
        return;

    const int descriptor = backtrace_open(mFileName.c_str(), btErrorHandler, this, nullptr);
    if (descriptor >= 1) {
        if(type == ApplicationType::ELF) {
            int foundSym = 0;
            int foundDwarf = 0;
            auto ret = elf_add(state, mFileName.c_str(), descriptor, NULL, 0, addr, btErrorHandler, this,
                               &state->fileline_fn, &foundSym, &foundDwarf, nullptr, false, false, nullptr, 0);
            if (ret && foundSym)
                state->syminfo_fn = &elf_syminfo;
            else
                state->syminfo_fn = &elf_nosyms;
        } else if(type == ApplicationType::WASM) {
            struct dwarf_sections sections;
            memset(&sections, 0, sizeof(sections));
            int wasm = open(mFileName.c_str(), O_RDONLY);
            uint64_t codeAddr = addr;
            if(wasm != -1) {
                uint32_t magic, version;
                read(wasm, &magic, sizeof(magic));
                read(wasm, &version, sizeof(version));
                if(magic == 0x6d736100) {
                    const off_t file_len = lseek(wasm, 0, SEEK_END);
                    for(off_t offset = 8; offset < file_len; ) {
                        if(lseek(wasm, offset, SEEK_SET) == -1)
                            break;
                        uint8_t section_type;
                        read(wasm, &section_type, sizeof(type));
                        const uint64_t section_size = read_uleb(wasm);
                        const uint64_t section_offset = lseek(wasm, 0, SEEK_CUR);
                        switch(section_type) {
                        case 10: { //CODE
                            codeAddr += section_offset;
                            //printf("Found code %d\n", section_offset);
                            break; }
                        case 0: { //CUSTOM
                            char section_name[256] = "";
                            const uint64_t name_size = read_uleb(wasm);
                            read(wasm, section_name, name_size);
                            section_name[name_size+1] = 0;
                            //printf("got section %ld %ld %s\n", section_offset, section_size, section_name);
                            dwarf_section section = DEBUG_MAX;
                            if(!strcmp(section_name, ".debug_info"))
                                section = DEBUG_INFO;
                            else if(!strcmp(section_name, ".debug_ranges"))
                                section = DEBUG_RANGES;
                            else if(!strcmp(section_name, ".debug_abbrev"))
                                section = DEBUG_ABBREV;
                            else if(!strcmp(section_name, ".debug_line"))
                                section = DEBUG_LINE;
                            else if(!strcmp(section_name, ".debug_loc"))
                                section = DEBUG_LINE_STR;
                            else if(!strcmp(section_name, ".debug_str"))
                                section = DEBUG_STR;
                            if(section != DEBUG_MAX) {
                                sections.size[section] = size_t(section_size) - (lseek(wasm, 0, SEEK_CUR) - section_offset);
                                unsigned char *data = (unsigned char *)malloc(sections.size[section]);
                                read(wasm, data, sections.size[section]);
                                sections.data[section] = data;
                            }
                            break; }
                        }
                        offset = section_offset + section_size;
                    }
                }
                close(wasm);
            }
            auto ret = backtrace_dwarf_add(state, codeAddr, &sections, false, nullptr, btErrorHandler, this,
                                           &state->fileline_fn, nullptr);
            if(ret)
                state->syminfo_fn = &elf_syminfo;
            else
                state->syminfo_fn = &elf_nosyms;
        }
        fileline_initialize(state, backtrace_moduleErrorCallback, this);
    }

    mState = state;
}

void Module::btErrorHandler(void* data, const char* msg, int errnum)
{
    auto module = reinterpret_cast<const Module*>(data);
    fprintf(stderr, "libbacktrace error %s: %s - %s(%d)\n", module->mFileName.c_str(), msg, strerror(errnum), errnum);
};

std::shared_ptr<Module> Module::create(ApplicationType type, Indexer<std::string>& indexer, std::string&& filename, uint64_t addr)
{
    // assume we'll never have a hash collision?
    const auto [ idx, inserted ] = indexer.index(filename);
    if (static_cast<size_t>(idx) < sModules.size() && sModules[idx] != nullptr)
        return sModules[idx]->shared_from_this();
    auto mod = Creatable<Module>::create(type, indexer, std::move(filename), addr);
    if (static_cast<size_t>(idx) >= sModules.size()) {
        const auto num = idx - sModules.size() + 1;
        sModules.reserve(sModules.size() + num);
        for (size_t i = 0; i < num; ++i) {
            sModules.push_back(nullptr);
        }
    }
    assert(static_cast<size_t>(idx) < sModules.size() && sModules[idx] == nullptr);
    sModules[idx] = mod.get();
    return mod;
}

void Module::addHeader(uint64_t addr, uint64_t len)
{
    mRanges.push_back(std::make_pair(mAddr + addr, mAddr + addr + len));
}
