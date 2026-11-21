#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include <Core/PE/RcExporter.hpp>

using namespace DumpPDB;

// ============================================================================
// RcExporter on hand-built resources: every type that the sample executables do
// not contain (dialogs, menus, accelerators, cursors, bitmaps, user data) plus the
// ones they do (icons, strings, version, manifest).
// ============================================================================

namespace
{
using Bytes = std::vector<uint8_t>;

void u16(Bytes& b, uint16_t v) { b.push_back(static_cast<uint8_t>(v)); b.push_back(static_cast<uint8_t>(v >> 8)); }
void u32(Bytes& b, uint32_t v) { u16(b, static_cast<uint16_t>(v)); u16(b, static_cast<uint16_t>(v >> 16)); }
void wz(Bytes& b, const std::string& s) { for (unsigned char c : s) u16(b, c); u16(b, 0); }
void align4(Bytes& b) { while (b.size() % 4) b.push_back(0); }

PEResourceItem item(uint32_t type, uint32_t id, Bytes data, uint16_t lang = 0x0409)
{
    PEResourceItem i;
    i.type.id = type;
    i.name.id = id;
    i.language = lang;
    i.data = std::move(data);
    return i;
}

const RcFile* file(const RcExportResult& r, const std::string& path)
{
    for (const auto& f : r.files)
        if (f.path == path)
            return &f;
    return nullptr;
}

bool has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }
} // namespace

TEST(RcExporter, StringTableUsesIdsFromTheBlockNumber)
{
    // Block 7 holds ids 96..111; put strings at slots 0 and 3.
    Bytes b;
    for (int k = 0; k < 16; ++k)
    {
        if (k == 0 || k == 3)
        {
            const std::string s = k == 0 ? "Hello \"world\"\n" : "Tab\there";
            u16(b, static_cast<uint16_t>(s.size()));
            for (unsigned char c : s) u16(b, c);
        }
        else
            u16(b, 0);
    }
    const auto r = RcExporter::build({item(RT_STRING_, 7, b)});
    EXPECT_TRUE(has(r.rc, "STRINGTABLE"));
    EXPECT_TRUE(has(r.rc, "IDS_STRING_96, \"Hello \"\"world\"\"\\n\""));
    EXPECT_TRUE(has(r.rc, "IDS_STRING_99, \"Tab\\there\""));
    EXPECT_TRUE(has(r.header, "#define IDS_STRING_99 99"));
}

TEST(RcExporter, IconGroupBecomesAValidIcoFile)
{
    Bytes group;
    u16(group, 0); u16(group, 1); u16(group, 2);
    for (uint16_t id : {1, 2})
    {
        group.push_back(id == 1 ? 16 : 32); group.push_back(id == 1 ? 16 : 32);
        group.push_back(0); group.push_back(0);
        u16(group, 1); u16(group, 32); u32(group, 4); u16(group, id);
    }
    const auto r = RcExporter::build({item(RT_GROUP_ICON_, 100, group),
        item(RT_ICON_, 1, {1, 2, 3, 4}), item(RT_ICON_, 2, {5, 6, 7, 8})});
    EXPECT_TRUE(has(r.rc, "IDI_100 ICON \"icons/icon_100_0409.ico\""));
    const RcFile* ico = file(r, "icons/icon_100_0409.ico");
    ASSERT_TRUE(ico != nullptr);
    ASSERT_EQ(ico->bytes.size(), 6u + 2 * 16 + 8);
    EXPECT_EQ(ico->bytes[2], 1);                 // type: icon
    EXPECT_EQ(ico->bytes[4], 2);                 // two images
    EXPECT_EQ(ico->bytes[6], 16);                // first width
    EXPECT_EQ(ico->bytes[6 + 16 + 0], 32);       // second width
    EXPECT_EQ(ico->bytes[6 + 12], 6 + 32);       // first image offset
    EXPECT_EQ(ico->bytes[6 + 32], 1);            // image payload follows the directory
    EXPECT_TRUE(r.notes.empty());
}

TEST(RcExporter, CursorKeepsItsHotspot)
{
    Bytes group;
    u16(group, 0); u16(group, 2); u16(group, 1);
    u16(group, 32); u16(group, 64); u16(group, 1); u16(group, 1); u32(group, 8); u16(group, 5);
    Bytes img;
    u16(img, 7); u16(img, 9);                    // hotspot x, y
    img.insert(img.end(), {0xAA, 0xBB, 0xCC, 0xDD});
    const auto r = RcExporter::build({item(RT_GROUP_CURSOR_, 3, group), item(RT_CURSOR_, 5, img)});
    const RcFile* cur = file(r, "cursors/cursor_3_0409.cur");
    ASSERT_TRUE(cur != nullptr);
    EXPECT_EQ(cur->bytes[2], 2);                 // type: cursor
    EXPECT_EQ(cur->bytes[10], 7);                // hotspot x
    EXPECT_EQ(cur->bytes[12], 9);                // hotspot y
    EXPECT_EQ(cur->bytes.back(), 0xDD);
}

TEST(RcExporter, BitmapGetsAFileHeader)
{
    Bytes dib;
    u32(dib, 40); u32(dib, 2); u32(dib, 2); u16(dib, 1); u16(dib, 8);
    u32(dib, 0); u32(dib, 0); u32(dib, 0); u32(dib, 0); u32(dib, 0); u32(dib, 0);
    for (int i = 0; i < 256; ++i) u32(dib, 0);   // palette
    dib.insert(dib.end(), {1, 2, 3, 4});
    const auto r = RcExporter::build({item(RT_BITMAP_, 9, dib)});
    const RcFile* bmp = file(r, "bitmaps/bitmap_9_0409.bmp");
    ASSERT_TRUE(bmp != nullptr);
    EXPECT_EQ(bmp->bytes[0], 'B');
    const uint32_t offBits = bmp->bytes[10] | (bmp->bytes[11] << 8) | (bmp->bytes[12] << 16) | (bmp->bytes[13] << 24);
    EXPECT_EQ(offBits, 14u + 40u + 1024u);       // pixel data offset = file header + info header + palette
    EXPECT_EQ(bmp->bytes.size(), dib.size() + 14);
}

TEST(RcExporter, MenuIsDecompiledWithPopupsAndSeparators)
{
    Bytes m;
    u16(m, 0); u16(m, 0);                        // MENUHEADER
    u16(m, 0x10); wz(m, "&File");                // POPUP
    u16(m, 0); u16(m, 100); wz(m, "&Open\tCtrl+O");
    u16(m, 0); u16(m, 0); wz(m, "");             // separator
    u16(m, 0x80 | 0x08); u16(m, 101); wz(m, "E&xit");   // last item, CHECKED
    u16(m, 0x80); u16(m, 200); wz(m, "Help");    // last top-level item
    const auto r = RcExporter::build({item(RT_MENU_, 40, m)});
    EXPECT_TRUE(has(r.rc, "IDR_MENU_40 MENU"));
    EXPECT_TRUE(has(r.rc, "POPUP \"&File\""));
    EXPECT_TRUE(has(r.rc, "MENUITEM \"&Open\\tCtrl+O\", ID_MENU_100"));
    EXPECT_TRUE(has(r.rc, "MENUITEM SEPARATOR"));
    EXPECT_TRUE(has(r.rc, "MENUITEM \"E&xit\", ID_MENU_101, CHECKED"));
    EXPECT_TRUE(has(r.rc, "MENUITEM \"Help\", ID_MENU_200"));
    EXPECT_TRUE(r.notes.empty());
}

TEST(RcExporter, AcceleratorsKeepKeysAndModifiers)
{
    Bytes a;
    u16(a, 0x01 | 0x08); u16(a, 'S'); u16(a, 300); u16(a, 0);          // Ctrl+S
    u16(a, 0x01 | 0x80); u16(a, 0x70); u16(a, 301); u16(a, 0);         // F1 (last)
    const auto r = RcExporter::build({item(RT_ACCELERATOR_, 1, a)});
    EXPECT_TRUE(has(r.rc, "\"S\", ID_ACCEL_300, VIRTKEY, CONTROL"));
    EXPECT_TRUE(has(r.rc, "VK_F1, ID_ACCEL_301, VIRTKEY"));
}

TEST(RcExporter, DialogExIsDecompiledWithControls)
{
    Bytes d;
    u16(d, 1); u16(d, 0xFFFF);                   // DLGTEMPLATEEX
    u32(d, 0);                                   // helpID
    u32(d, 0x00000000);                          // exStyle
    u32(d, 0x80000000u | 0x00C00000u | 0x40 | 0x80);   // POPUP | CAPTION | SETFONT | MODALFRAME
    u16(d, 2);                                   // controls
    u16(d, 10); u16(d, 20); u16(d, 200); u16(d, 100);
    u16(d, 0);                                   // menu
    u16(d, 0);                                   // class
    wz(d, "About");
    u16(d, 9); u16(d, 400); d.push_back(0); d.push_back(1); wz(d, "MS Shell Dlg");
    // control 1: OK button
    align4(d);
    u32(d, 0); u32(d, 0); u32(d, 0x50010001u);   // help, exStyle, style: VISIBLE|CHILD|TABSTOP|DEFPUSHBUTTON
    u16(d, 10); u16(d, 80); u16(d, 50); u16(d, 14);
    u32(d, 1);
    u16(d, 0xFFFF); u16(d, 0x80);                // Button
    wz(d, "OK");
    u16(d, 0);
    // control 2: static text with id -1
    align4(d);
    u32(d, 0); u32(d, 0); u32(d, 0x50000000u);
    u16(d, 10); u16(d, 10); u16(d, 100); u16(d, 8);
    u32(d, 0xFFFFFFFFu);
    u16(d, 0xFFFF); u16(d, 0x82);                // Static
    wz(d, "Hello");
    u16(d, 0);

    const auto r = RcExporter::build({item(RT_DIALOG_, 101, d)});
    EXPECT_TRUE(has(r.rc, "IDD_DIALOG_101 DIALOGEX 10, 20, 200, 100"));
    EXPECT_TRUE(has(r.rc, "STYLE WS_POPUP | WS_CAPTION"));
    EXPECT_TRUE(has(r.rc, "DS_MODALFRAME"));
    EXPECT_TRUE(has(r.rc, "CAPTION \"About\""));
    EXPECT_TRUE(has(r.rc, "FONT 9, \"MS Shell Dlg\", 400, 0, 0x1"));
    EXPECT_TRUE(has(r.rc, "CONTROL         \"OK\", IDC_CTRL_1, \"Button\", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON"));
    EXPECT_TRUE(has(r.rc, "CONTROL         \"Hello\", -1, \"Static\""));
    EXPECT_TRUE(r.notes.empty());
}

TEST(RcExporter, VersionInfoRoundTripsToScript)
{
    auto block = [](const std::string& key, uint16_t type, const Bytes& value, uint16_t valueLen, const Bytes& kids)
    {
        Bytes b;
        u16(b, 0); u16(b, valueLen); u16(b, type);
        wz(b, key);
        align4(b);
        b.insert(b.end(), value.begin(), value.end());
        align4(b);
        b.insert(b.end(), kids.begin(), kids.end());
        b[0] = static_cast<uint8_t>(b.size());
        b[1] = static_cast<uint8_t>(b.size() >> 8);
        align4(b);
        return b;
    };
    Bytes fixed;
    u32(fixed, 0xFEEF04BDu); u32(fixed, 0x10000);
    u32(fixed, (1u << 16) | 2); u32(fixed, (3u << 16) | 4);
    u32(fixed, (1u << 16) | 2); u32(fixed, (3u << 16) | 4);
    u32(fixed, 0x3F); u32(fixed, 0); u32(fixed, 0x40004); u32(fixed, 1); u32(fixed, 0); u32(fixed, 0); u32(fixed, 0);
    Bytes text; wz(text, "Acme");
    const Bytes str = block("CompanyName", 1, text, static_cast<uint16_t>(text.size() / 2), {});
    const Bytes tbl = block("040904b0", 1, {}, 0, str);
    const Bytes sfi = block("StringFileInfo", 1, {}, 0, tbl);
    Bytes trans; u32(trans, (1200u << 16) | 0x409);
    const Bytes var = block("Translation", 0, trans, 4, {});
    const Bytes vfi = block("VarFileInfo", 1, {}, 0, var);
    Bytes kids = sfi; kids.insert(kids.end(), vfi.begin(), vfi.end());
    const Bytes root = block("VS_VERSION_INFO", 0, fixed, static_cast<uint16_t>(fixed.size()), kids);

    const auto r = RcExporter::build({item(RT_VERSION_, 1, root)});
    EXPECT_TRUE(has(r.rc, "FILEVERSION 1,2,3,4"));
    EXPECT_TRUE(has(r.rc, "FILEOS 0x40004L"));
    EXPECT_TRUE(has(r.rc, "BLOCK \"StringFileInfo\""));
    EXPECT_TRUE(has(r.rc, "VALUE \"CompanyName\", \"Acme\""));
    EXPECT_TRUE(has(r.rc, "VALUE \"Translation\", 0x409, 1200"));
}

TEST(RcExporter, UserDataManifestAndUnknownTypesAreExtracted)
{
    PEResourceItem custom;
    custom.type.isString = true;
    custom.type.name = "MYDATA";
    custom.name.isString = true;
    custom.name.name = "CONFIG";
    custom.language = 0x0409;
    custom.data = {9, 9, 9};

    const auto r = RcExporter::build({item(RT_RCDATA_, 5, {1, 2, 3}), item(RT_MANIFEST_, 1, {'<', 'a', '/', '>'}),
        item(777, 2, {7}), custom});
    EXPECT_TRUE(has(r.rc, "IDR_RES_5 RCDATA \"binary/5_0409.bin\""));
    EXPECT_TRUE(has(r.rc, "IDR_MANIFEST_1 24 \"manifest/1_0409.manifest\""));
    EXPECT_TRUE(has(r.rc, "IDR_RES_2 777 \"binary/type777_2_0409.bin\""));
    EXPECT_TRUE(has(r.rc, "CONFIG \"MYDATA\" \"binary/MYDATA_CONFIG_0409.bin\""));
    ASSERT_TRUE(file(r, "manifest/1_0409.manifest") != nullptr);
}

TEST(RcExporter, ResFileContainsEveryResourceAndIsWellFormed)
{
    const auto r = RcExporter::build({item(RT_RCDATA_, 5, {1, 2, 3}), item(RT_ICON_, 1, {1, 2, 3, 4, 5})});
    size_t pos = 0, records = 0;
    while (pos + 8 <= r.res.size())
    {
        const uint32_t size = r.res[pos] | (r.res[pos + 1] << 8) | (r.res[pos + 2] << 16) | (r.res[pos + 3] << 24);
        const uint32_t hdr = r.res[pos + 4] | (r.res[pos + 5] << 8) | (r.res[pos + 6] << 16) | (r.res[pos + 7] << 24);
        ASSERT_TRUE(hdr >= 16);
        ASSERT_TRUE(pos + hdr + size <= r.res.size());
        pos = (pos + hdr + size + 3) & ~size_t(3);
        ++records;
    }
    EXPECT_EQ(records, 3u);                      // marker + both resources (even the ungrouped icon)
    EXPECT_EQ(pos, r.res.size());
}

TEST(RcExporter, MalformedResourcesFallBackToRawFilesAndNeverThrow)
{
    const auto r = RcExporter::build({item(RT_DIALOG_, 1, {1, 2, 3}), item(RT_MENU_, 2, {0, 0, 0x10}),
        item(RT_VERSION_, 3, {1, 2}), item(RT_GROUP_ICON_, 4, {0, 0, 1})});
    EXPECT_FALSE(r.notes.empty());
    EXPECT_TRUE(has(r.rc, "// RT_DIALOG #1"));
    EXPECT_GE(r.res.size(), 32u + 4 * 28u);
}
