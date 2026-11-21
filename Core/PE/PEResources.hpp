#pragma once

#include <Core/PE/PEImage.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace DumpPDB
{

/// Standard resource types (RT_*).
enum PEResType : uint32_t
{
    RT_CURSOR_ = 1,
    RT_BITMAP_ = 2,
    RT_ICON_ = 3,
    RT_MENU_ = 4,
    RT_DIALOG_ = 5,
    RT_STRING_ = 6,
    RT_FONTDIR_ = 7,
    RT_FONT_ = 8,
    RT_ACCELERATOR_ = 9,
    RT_RCDATA_ = 10,
    RT_MESSAGETABLE_ = 11,
    RT_GROUP_CURSOR_ = 12,
    RT_GROUP_ICON_ = 14,
    RT_VERSION_ = 16,
    RT_DLGINCLUDE_ = 17,
    RT_PLUGPLAY_ = 19,
    RT_VXD_ = 20,
    RT_ANICURSOR_ = 21,
    RT_ANIICON_ = 22,
    RT_HTML_ = 23,
    RT_MANIFEST_ = 24,
};

/// A resource type or name: either a numeric id or a string.
struct PEResId
{
    bool isString = false;
    uint32_t id = 0;
    std::string name; ///< UTF-8, valid when isString.

    [[nodiscard]] bool operator==(const PEResId& o) const noexcept
    {
        return isString == o.isString && (isString ? name == o.name : id == o.id);
    }
    [[nodiscard]] bool operator<(const PEResId& o) const noexcept
    {
        if (isString != o.isString)
            return !isString; // numeric ids first
        return isString ? name < o.name : id < o.id;
    }
    [[nodiscard]] std::string text() const { return isString ? name : "#" + std::to_string(id); }
};

/// One leaf of the resource tree: type / name / language -> data.
struct PEResourceItem
{
    PEResId type;
    PEResId name;
    uint16_t language = 0;
    uint32_t codePage = 0;
    uint32_t rva = 0;
    std::vector<uint8_t> data; ///< Owned copy so items can be built without a PE (tests).
};

/// Reads the complete resource directory (every type, name and language).
class PEResources
{
public:

    [[nodiscard]] static std::vector<PEResourceItem> parse(const PEImage& a_pe)
    {
        std::vector<PEResourceItem> items;
        const PEDataDirectory& dir = a_pe.directory(PE_DIR_RESOURCE);
        if (dir.rva == 0 || dir.size == 0)
            return items;

        walk(a_pe, dir.rva, dir.rva, 0, {}, {}, items, 0);

        std::stable_sort(items.begin(),
            items.end(),
            [](const PEResourceItem& a, const PEResourceItem& b)
            {
                if (!(a.type == b.type))
                    return a.type < b.type;
                if (!(a.name == b.name))
                    return a.name < b.name;
                return a.language < b.language;
            });
        return items;
    }

    [[nodiscard]] static std::string typeName(const PEResId& a_type)
    {
        if (a_type.isString)
            return a_type.name;
        switch (a_type.id)
        {
        case RT_CURSOR_:
            return "RT_CURSOR";
        case RT_BITMAP_:
            return "RT_BITMAP";
        case RT_ICON_:
            return "RT_ICON";
        case RT_MENU_:
            return "RT_MENU";
        case RT_DIALOG_:
            return "RT_DIALOG";
        case RT_STRING_:
            return "RT_STRING";
        case RT_FONTDIR_:
            return "RT_FONTDIR";
        case RT_FONT_:
            return "RT_FONT";
        case RT_ACCELERATOR_:
            return "RT_ACCELERATOR";
        case RT_RCDATA_:
            return "RT_RCDATA";
        case RT_MESSAGETABLE_:
            return "RT_MESSAGETABLE";
        case RT_GROUP_CURSOR_:
            return "RT_GROUP_CURSOR";
        case RT_GROUP_ICON_:
            return "RT_GROUP_ICON";
        case RT_VERSION_:
            return "RT_VERSION";
        case RT_DLGINCLUDE_:
            return "RT_DLGINCLUDE";
        case RT_PLUGPLAY_:
            return "RT_PLUGPLAY";
        case RT_VXD_:
            return "RT_VXD";
        case RT_ANICURSOR_:
            return "RT_ANICURSOR";
        case RT_ANIICON_:
            return "RT_ANIICON";
        case RT_HTML_:
            return "RT_HTML";
        case RT_MANIFEST_:
            return "RT_MANIFEST";
        default:
            return "#" + std::to_string(a_type.id);
        }
    }

private:

    static std::string utf16ToUtf8(const std::vector<uint16_t>& a_w)
    {
        std::string out;
        for (size_t i = 0; i < a_w.size(); ++i)
        {
            uint32_t cp = a_w[i];
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < a_w.size() && a_w[i + 1] >= 0xDC00
                && a_w[i + 1] < 0xE000)
            {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (a_w[i + 1] - 0xDC00);
                ++i;
            }
            if (cp < 0x80)
                out.push_back(static_cast<char>(cp));
            else if (cp < 0x800)
            {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else if (cp < 0x10000)
            {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            else
            {
                out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
        return out;
    }

    static PEResId readId(const PEImage& a_pe, uint64_t a_base, uint32_t a_field)
    {
        PEResId id;
        if ((a_field & 0x80000000u) == 0)
        {
            id.id = a_field & 0xFFFF;
            return id;
        }
        id.isString = true;
        const uint64_t strRva = a_base + (a_field & 0x7FFFFFFFu);
        uint16_t len = 0;
        if (!a_pe.readU16(strRva, len) || len > 512)
            return id;
        std::vector<uint16_t> w;
        for (uint16_t i = 0; i < len; ++i)
        {
            uint16_t c = 0;
            if (!a_pe.readU16(strRva + 2 + static_cast<uint64_t>(i) * 2, c))
                break;
            w.push_back(c);
        }
        id.name = utf16ToUtf8(w);
        return id;
    }

    static void walk(const PEImage& a_pe,
        uint64_t a_base,
        uint64_t a_dir,
        int a_level,
        PEResId a_type,
        PEResId a_name,
        std::vector<PEResourceItem>& a_out,
        int a_guard)
    {
        if (a_level > 2 || a_guard > 100000)
            return;
        uint16_t named = 0, ids = 0;
        if (!a_pe.readU16(a_dir + 12, named) || !a_pe.readU16(a_dir + 14, ids))
            return;
        const uint32_t total = std::min<uint32_t>(static_cast<uint32_t>(named) + ids, 65536);

        for (uint32_t i = 0; i < total; ++i)
        {
            const uint64_t e = a_dir + 16 + static_cast<uint64_t>(i) * 8;
            uint32_t nameField = 0, offField = 0;
            if (!a_pe.readU32(e, nameField) || !a_pe.readU32(e + 4, offField))
                break;

            const PEResId id = readId(a_pe, a_base, nameField);
            if (offField & 0x80000000u)
            {
                const uint64_t sub = a_base + (offField & 0x7FFFFFFFu);
                if (a_level == 0)
                    walk(a_pe, a_base, sub, 1, id, a_name, a_out, a_guard + 1);
                else if (a_level == 1)
                    walk(a_pe, a_base, sub, 2, a_type, id, a_out, a_guard + 1);
                continue;
            }

            // Leaf: IMAGE_RESOURCE_DATA_ENTRY
            uint32_t dataRva = 0, size = 0, codePage = 0;
            if (!a_pe.readU32(a_base + offField, dataRva) || !a_pe.readU32(a_base + offField + 4, size)
                || !a_pe.readU32(a_base + offField + 8, codePage))
                continue;

            PEResourceItem item;
            item.type = a_type;
            item.name = a_name;
            item.language = static_cast<uint16_t>(id.id);
            item.codePage = codePage;
            item.rva = dataRva;
            const BinaryView bytes = a_pe.bytesAtRva(dataRva, size);
            item.data.assign(bytes.data(), bytes.data() + bytes.size());
            a_out.push_back(std::move(item));
        }
    }
};

} // namespace DumpPDB
