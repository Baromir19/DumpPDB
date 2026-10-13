#include <Zydis/Zydis.h>
#include <Windows.h>
#include <cstdio>
#include <string>
#include <vector>

// Throwaway disasm tool: dumps instructions for a function RVA range.
// usage: disasm.exe <file.exe> <startRvaHex> <endRvaHex>

static uint64_t g_imageBase = 0;
static std::vector<uint8_t> g_buf;
static uint32_t g_textRva = 0;
static size_t g_textOff = 0;
static bool g_is64 = false;

static bool loadPe(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD sz = GetFileSize(h, nullptr);
    g_buf.resize(sz);
    DWORD rd = 0;
    ReadFile(h, g_buf.data(), sz, &rd, nullptr);
    CloseHandle(h);

    auto rd16 = [&](size_t o) { return *(uint16_t*)&g_buf[o]; };
    auto rd32 = [&](size_t o) { return *(uint32_t*)&g_buf[o]; };
    auto rd64 = [&](size_t o) { return *(uint64_t*)&g_buf[o]; };
    uint32_t pe = rd32(0x3C);
    uint16_t magic = rd16(pe + 24);
    if (magic == 0x10B) { g_imageBase = rd32(pe + 24 + 28); g_is64 = false; }
    else { g_imageBase = rd64(pe + 24 + 24); g_is64 = true; }
    uint16_t nsec = rd16(pe + 6);
    uint16_t optSize = rd16(pe + 20);
    size_t secOff = pe + 24 + optSize;
    for (uint16_t i = 0; i < nsec; ++i) {
        size_t o = secOff + (size_t)i * 40;
        char name[9] = {};
        memcpy(name, &g_buf[o], 8);
        if (strcmp(name, ".text") == 0) {
            g_textRva = rd32(o + 12);
            g_textOff = rd32(o + 20);
        }
    }
    return true;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 4) { printf("usage: disasm <exe> <startRva> <endRva>\n"); return 1; }
    if (!loadPe(argv[1])) { printf("load fail\n"); return 1; }
    uint64_t start = wcstoull(argv[2], nullptr, 16);
    uint64_t end = wcstoull(argv[3], nullptr, 16);

    ZydisDecoder dec;
    if (g_is64)
        ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    else
        ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_STACK_WIDTH_32);

    uint64_t rva = start;
    uint64_t fileOff = g_textOff + (rva - g_textRva);
    while (rva < end && fileOff + 16 < g_buf.size()) {
        ZydisDecodedInstruction ins;
        ZydisDecodedOperand ops[10];
        if (ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, g_buf.data() + fileOff,
                g_buf.size() - fileOff, &ins, ops))) {
            char buf[256];
            ZydisFormatter fmt;
            ZydisFormatterInit(&fmt, ZYDIS_FORMATTER_STYLE_INTEL);
            ZydisFormatterFormatInstruction(&fmt, &ins, ops, ins.operand_count_visible,
                buf, sizeof(buf), g_imageBase + rva, nullptr);
            printf("%08llX  %s\n", rva, buf);
            rva += ins.length;
            fileOff += ins.length;
        } else {
            break;
        }
    }
    return 0;
}