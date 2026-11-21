#pragma once

#include <Core/PE/PEResources.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace DumpPDB
{

/// A file produced next to the .rc (icon, bitmap, manifest, binary blob ...).
struct RcFile
{
    std::string path; ///< Relative, forward slashes.
    std::vector<uint8_t> bytes;
};

struct RcExportResult
{
    std::string rc;                 ///< The resource script.
    std::string header;             ///< resource.h with the numeric ids.
    std::vector<RcFile> files;      ///< Extracted payloads referenced by the script.
    std::vector<uint8_t> res;       ///< Binary .res: every resource, byte exact, ready for the linker.
    std::string index;              ///< CSV listing of every resource.
    std::vector<std::string> notes; ///< Things that could not be decompiled (kept as raw files).
    size_t resourceCount = 0;
};

/// ============================================================================
/// RcExporter — turns a PE's resources back into an editable resource script.
///
/// Produces, for the whole resource tree:
///   * <name>.rc      resource script (UTF-8, `#pragma code_page(65001)`)
///   * resource.h     #defines for the numeric ids
///   * icons/*.ico, cursors/*.cur, bitmaps/*.bmp, manifest/*.manifest,
///     binary/*   the payload files the script refers to
///   * <name>.res     a byte-exact compiled copy of ALL resources (use it when the
///     decompiled script is not good enough, e.g. `link ... resources.res`)
///   * resources.csv  index of everything
///
/// Decompiled to readable script: ICON / CURSOR groups, BITMAP, STRINGTABLE,
/// VERSIONINFO, MANIFEST, DIALOG / DIALOGEX, MENU / MENUEX, ACCELERATORS.
/// Everything else (RCDATA, HTML, FONT, MESSAGETABLE, user types) is extracted
/// as a file and included by reference. A resource that fails to decompile is
/// written as a raw file, listed in `notes`, and is still present in the .res.
/// ============================================================================
class RcExporter
{
public:

    [[nodiscard]] static RcExportResult build(
        const std::vector<PEResourceItem>& a_items, const std::string& a_baseName = "resources")
    {
        RcExporter e(a_items, a_baseName);
        return e.run();
    }

    /// Writes the result below a_dir. Returns the list of written relative paths.
    static std::vector<std::string> writeTo(const RcExportResult& a_result,
        const std::filesystem::path& a_dir,
        const std::string& a_baseName,
        std::vector<std::string>& a_errors)
    {
        std::vector<std::string> written;
        std::error_code ec;
        std::filesystem::create_directories(a_dir, ec);
        auto put = [&](const std::string& a_rel, const void* a_data, size_t a_size)
        {
            const std::filesystem::path p = a_dir / a_rel;
            std::filesystem::create_directories(p.parent_path(), ec);
            std::ofstream f(p, std::ios::binary);
            if (!f)
            {
                a_errors.push_back("cannot write " + p.string());
                return;
            }
            f.write(static_cast<const char*>(a_data), static_cast<std::streamsize>(a_size));
            written.push_back(a_rel);
        };

        put(a_baseName + ".rc", a_result.rc.data(), a_result.rc.size());
        put("resource.h", a_result.header.data(), a_result.header.size());
        put(a_baseName + ".res", a_result.res.data(), a_result.res.size());
        put("resources.csv", a_result.index.data(), a_result.index.size());
        for (const auto& f : a_result.files)
            put(f.path, f.bytes.data(), f.bytes.size());
        return written;
    }

private:

    // ─────────────────────────────────────────────────────────────────────
    // Little-endian reader with bounds checking
    // ─────────────────────────────────────────────────────────────────────

    struct Reader
    {
        const uint8_t* p;
        size_t n;
        size_t pos = 0;
        bool ok = true;

        Reader(const std::vector<uint8_t>& a_v, size_t a_start = 0)
            : p(a_v.data())
            , n(a_v.size())
            , pos(a_start)
        {
        }

        bool has(size_t a_bytes) const { return ok && pos + a_bytes <= n; }
        uint8_t u8()
        {
            if (!has(1))
            {
                ok = false;
                return 0;
            }
            return p[pos++];
        }
        uint16_t u16()
        {
            if (!has(2))
            {
                ok = false;
                return 0;
            }
            const uint16_t v = static_cast<uint16_t>(p[pos] | (p[pos + 1] << 8));
            pos += 2;
            return v;
        }
        int16_t i16() { return static_cast<int16_t>(u16()); }
        uint32_t u32()
        {
            if (!has(4))
            {
                ok = false;
                return 0;
            }
            const uint32_t v = static_cast<uint32_t>(p[pos]) | (static_cast<uint32_t>(p[pos + 1]) << 8)
                               | (static_cast<uint32_t>(p[pos + 2]) << 16)
                               | (static_cast<uint32_t>(p[pos + 3]) << 24);
            pos += 4;
            return v;
        }
        void align(size_t a_to)
        {
            pos = (pos + a_to - 1) / a_to * a_to;
            if (pos > n)
                ok = false;
        }
        void skip(size_t a_bytes)
        {
            if (!has(a_bytes))
            {
                ok = false;
                return;
            }
            pos += a_bytes;
        }
        /// NUL-terminated UTF-16 string -> UTF-8.
        std::string wstr()
        {
            std::vector<uint16_t> w;
            while (ok)
            {
                const uint16_t c = u16();
                if (!ok || c == 0)
                    break;
                w.push_back(c);
            }
            return utf8(w);
        }
        static std::string utf8(const std::vector<uint16_t>& a_w)
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
    };

    /// sz_Or_Ord: 0x0000 = none, 0xFFFF + ordinal, otherwise a string.
    struct SzOrOrd
    {
        bool isOrdinal = false;
        uint16_t ordinal = 0;
        std::string text;
        bool empty() const { return !isOrdinal && text.empty(); }
    };

    static SzOrOrd readSzOrOrd(Reader& r)
    {
        SzOrOrd v;
        const uint16_t first = r.u16();
        if (first == 0)
            return v;
        if (first == 0xFFFF)
        {
            v.isOrdinal = true;
            v.ordinal = r.u16();
            return v;
        }
        std::vector<uint16_t> w{first};
        while (r.ok)
        {
            const uint16_t c = r.u16();
            if (!r.ok || c == 0)
                break;
            w.push_back(c);
        }
        v.text = Reader::utf8(w);
        return v;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Text helpers
    // ─────────────────────────────────────────────────────────────────────

    static std::string hex(uint64_t v, int width = 0)
    {
        char b[24];
        std::snprintf(b, sizeof(b), "0x%0*llX", width, static_cast<unsigned long long>(v));
        return b;
    }

    /// Quoted RC string: `"` -> `""`, `\` -> `\\`, control characters as escapes.
    static std::string q(const std::string& a_s)
    {
        std::string o = "\"";
        for (unsigned char c : a_s)
        {
            switch (c)
            {
            case '"':
                o += "\"\"";
                break;
            case '\\':
                o += "\\\\";
                break;
            case '\n':
                o += "\\n";
                break;
            case '\r':
                o += "\\r";
                break;
            case '\t':
                o += "\\t";
                break;
            default:
                if (c < 0x20)
                {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\%03o", c);
                    o += b;
                }
                else
                    o.push_back(static_cast<char>(c));
            }
        }
        return o + "\"";
    }

    static bool isIdentifier(const std::string& a_s)
    {
        if (a_s.empty() || std::isdigit(static_cast<unsigned char>(a_s[0])))
            return false;
        for (char c : a_s)
            if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_')
                return false;
        return true;
    }

    static std::string sanitize(const std::string& a_s)
    {
        std::string o;
        for (char c : a_s)
            o.push_back(std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        return o.empty() ? "_" : o;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Style decoding (symbolic names from winuser.h; unknown bits stay hex)
    // ─────────────────────────────────────────────────────────────────────

    struct Flag
    {
        uint32_t mask;
        uint32_t value;
        const char* name;
    };

    static std::string decode(uint32_t a_style, const std::vector<const std::vector<Flag>*>& a_tables)
    {
        std::vector<std::string> parts;
        uint32_t rest = a_style;
        for (const auto* t : a_tables)
        {
            for (const Flag& f : *t)
            {
                if ((rest & f.mask) == f.value && f.value != 0 && (a_style & f.mask) == f.value)
                {
                    parts.push_back(f.name);
                    rest &= ~f.mask;
                }
            }
        }
        if (parts.empty() || rest)
            parts.push_back(rest || parts.empty() ? hex(rest) : std::string());
        std::string out;
        for (const auto& p : parts)
        {
            if (p.empty())
                continue;
            if (!out.empty())
                out += " | ";
            out += p;
        }
        return out;
    }

    static const std::vector<Flag>& wsCommon()
    {
        static const std::vector<Flag> t = {
            {0x80000000u, 0x80000000u, "WS_POPUP"},      {0x40000000u, 0x40000000u, "WS_CHILD"},
            {0x20000000u, 0x20000000u, "WS_MINIMIZE"},   {0x10000000u, 0x10000000u, "WS_VISIBLE"},
            {0x08000000u, 0x08000000u, "WS_DISABLED"},   {0x04000000u, 0x04000000u, "WS_CLIPSIBLINGS"},
            {0x02000000u, 0x02000000u, "WS_CLIPCHILDREN"}, {0x01000000u, 0x01000000u, "WS_MAXIMIZE"},
            {0x00C00000u, 0x00C00000u, "WS_CAPTION"},    {0x00800000u, 0x00800000u, "WS_BORDER"},
            {0x00400000u, 0x00400000u, "WS_DLGFRAME"},   {0x00200000u, 0x00200000u, "WS_VSCROLL"},
            {0x00100000u, 0x00100000u, "WS_HSCROLL"},    {0x00080000u, 0x00080000u, "WS_SYSMENU"},
            {0x00040000u, 0x00040000u, "WS_THICKFRAME"}};
        return t;
    }
    static const std::vector<Flag>& wsDialogBoxes()
    {
        static const std::vector<Flag> t = {{0x00020000u, 0x00020000u, "WS_MINIMIZEBOX"},
            {0x00010000u, 0x00010000u, "WS_MAXIMIZEBOX"}};
        return t;
    }
    static const std::vector<Flag>& wsControlBoxes()
    {
        static const std::vector<Flag> t = {
            {0x00020000u, 0x00020000u, "WS_GROUP"}, {0x00010000u, 0x00010000u, "WS_TABSTOP"}};
        return t;
    }
    static const std::vector<Flag>& dsFlags()
    {
        static const std::vector<Flag> t = {
            {0x48, 0x48, "DS_SHELLFONT"}, {0x1, 0x1, "DS_ABSALIGN"},   {0x2, 0x2, "DS_SYSMODAL"},
            {0x4, 0x4, "DS_3DLOOK"},      {0x8, 0x8, "DS_FIXEDSYS"},   {0x10, 0x10, "DS_NOFAILCREATE"},
            {0x20, 0x20, "DS_LOCALEDIT"}, {0x40, 0x40, "DS_SETFONT"},  {0x80, 0x80, "DS_MODALFRAME"},
            {0x100, 0x100, "DS_NOIDLEMSG"}, {0x200, 0x200, "DS_SETFOREGROUND"},
            {0x400, 0x400, "DS_CONTROL"}, {0x800, 0x800, "DS_CENTER"}, {0x1000, 0x1000, "DS_CENTERMOUSE"},
            {0x2000, 0x2000, "DS_CONTEXTHELP"}};
        return t;
    }
    static const std::vector<Flag>& buttonFlags()
    {
        static const std::vector<Flag> t = {
            {0xF, 0x1, "BS_DEFPUSHBUTTON"}, {0xF, 0x2, "BS_CHECKBOX"}, {0xF, 0x3, "BS_AUTOCHECKBOX"},
            {0xF, 0x4, "BS_RADIOBUTTON"}, {0xF, 0x5, "BS_3STATE"}, {0xF, 0x6, "BS_AUTO3STATE"},
            {0xF, 0x7, "BS_GROUPBOX"}, {0xF, 0x8, "BS_USERBUTTON"}, {0xF, 0x9, "BS_AUTORADIOBUTTON"},
            {0xF, 0xB, "BS_OWNERDRAW"}, {0x20, 0x20, "BS_LEFTTEXT"}, {0x40, 0x40, "BS_ICON"},
            {0x80, 0x80, "BS_BITMAP"}, {0xF00, 0x300, "BS_CENTER"}, {0xF00, 0x100, "BS_LEFT"},
            {0xF00, 0x200, "BS_RIGHT"}, {0xF00, 0xC00, "BS_VCENTER"}, {0xF00, 0x400, "BS_TOP"},
            {0xF00, 0x800, "BS_BOTTOM"}, {0x1000, 0x1000, "BS_PUSHLIKE"}, {0x2000, 0x2000, "BS_MULTILINE"},
            {0x4000, 0x4000, "BS_NOTIFY"}, {0x8000, 0x8000, "BS_FLAT"}};
        return t;
    }
    static const std::vector<Flag>& staticFlags()
    {
        static const std::vector<Flag> t = {
            {0x1F, 0x1, "SS_CENTER"}, {0x1F, 0x2, "SS_RIGHT"}, {0x1F, 0x3, "SS_ICON"},
            {0x1F, 0x4, "SS_BLACKRECT"}, {0x1F, 0x5, "SS_GRAYRECT"}, {0x1F, 0x6, "SS_WHITERECT"},
            {0x1F, 0x7, "SS_BLACKFRAME"}, {0x1F, 0x8, "SS_GRAYFRAME"}, {0x1F, 0x9, "SS_WHITEFRAME"},
            {0x1F, 0xB, "SS_SIMPLE"}, {0x1F, 0xC, "SS_LEFTNOWORDWRAP"}, {0x1F, 0xD, "SS_OWNERDRAW"},
            {0x1F, 0xE, "SS_BITMAP"}, {0x1F, 0x10, "SS_ETCHEDHORZ"}, {0x1F, 0x11, "SS_ETCHEDVERT"},
            {0x1F, 0x12, "SS_ETCHEDFRAME"}, {0x80, 0x80, "SS_NOPREFIX"}, {0x100, 0x100, "SS_NOTIFY"},
            {0x200, 0x200, "SS_CENTERIMAGE"}, {0x400, 0x400, "SS_RIGHTJUST"},
            {0x800, 0x800, "SS_REALSIZEIMAGE"}, {0x1000, 0x1000, "SS_SUNKEN"},
            {0xC000, 0xC000, "SS_WORDELLIPSIS"}, {0xC000, 0x8000, "SS_PATHELLIPSIS"},
            {0xC000, 0x4000, "SS_ENDELLIPSIS"}};
        return t;
    }
    static const std::vector<Flag>& editFlags()
    {
        static const std::vector<Flag> t = {
            {0x3, 0x1, "ES_CENTER"}, {0x3, 0x2, "ES_RIGHT"}, {0x4, 0x4, "ES_MULTILINE"},
            {0x8, 0x8, "ES_UPPERCASE"}, {0x10, 0x10, "ES_LOWERCASE"}, {0x20, 0x20, "ES_PASSWORD"},
            {0x40, 0x40, "ES_AUTOVSCROLL"}, {0x80, 0x80, "ES_AUTOHSCROLL"}, {0x100, 0x100, "ES_NOHIDESEL"},
            {0x400, 0x400, "ES_OEMCONVERT"}, {0x800, 0x800, "ES_READONLY"}, {0x1000, 0x1000, "ES_WANTRETURN"},
            {0x2000, 0x2000, "ES_NUMBER"}};
        return t;
    }
    static const std::vector<Flag>& listFlags()
    {
        static const std::vector<Flag> t = {
            {0x1, 0x1, "LBS_NOTIFY"}, {0x2, 0x2, "LBS_SORT"}, {0x4, 0x4, "LBS_NOREDRAW"},
            {0x8, 0x8, "LBS_MULTIPLESEL"}, {0x10, 0x10, "LBS_OWNERDRAWFIXED"},
            {0x20, 0x20, "LBS_OWNERDRAWVARIABLE"}, {0x40, 0x40, "LBS_HASSTRINGS"},
            {0x80, 0x80, "LBS_USETABSTOPS"}, {0x100, 0x100, "LBS_NOINTEGRALHEIGHT"},
            {0x200, 0x200, "LBS_MULTICOLUMN"}, {0x400, 0x400, "LBS_WANTKEYBOARDINPUT"},
            {0x800, 0x800, "LBS_EXTENDEDSEL"}, {0x1000, 0x1000, "LBS_DISABLENOSCROLL"},
            {0x2000, 0x2000, "LBS_NODATA"}, {0x4000, 0x4000, "LBS_NOSEL"}};
        return t;
    }
    static const std::vector<Flag>& comboFlags()
    {
        static const std::vector<Flag> t = {
            {0x3, 0x1, "CBS_SIMPLE"}, {0x3, 0x2, "CBS_DROPDOWN"}, {0x3, 0x3, "CBS_DROPDOWNLIST"},
            {0x10, 0x10, "CBS_OWNERDRAWFIXED"}, {0x20, 0x20, "CBS_OWNERDRAWVARIABLE"},
            {0x40, 0x40, "CBS_AUTOHSCROLL"}, {0x80, 0x80, "CBS_OEMCONVERT"}, {0x100, 0x100, "CBS_SORT"},
            {0x200, 0x200, "CBS_HASSTRINGS"}, {0x400, 0x400, "CBS_NOINTEGRALHEIGHT"},
            {0x800, 0x800, "CBS_DISABLENOSCROLL"}, {0x2000, 0x2000, "CBS_UPPERCASE"},
            {0x4000, 0x4000, "CBS_LOWERCASE"}};
        return t;
    }
    static const std::vector<Flag>& scrollFlags()
    {
        static const std::vector<Flag> t = {{0x1, 0x1, "SBS_VERT"}, {0x2, 0x2, "SBS_TOPALIGN"},
            {0x4, 0x4, "SBS_BOTTOMALIGN"}, {0x8, 0x8, "SBS_SIZEBOX"}};
        return t;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Construction / bookkeeping
    // ─────────────────────────────────────────────────────────────────────

    RcExporter(const std::vector<PEResourceItem>& a_items, std::string a_base)
        : m_items(a_items)
        , m_base(std::move(a_base))
    {
    }

    const std::vector<PEResourceItem>& m_items;
    std::string m_base;
    RcExportResult m_out;
    std::map<std::string, uint32_t> m_defines;
    std::set<std::string> m_usedPaths;
    std::set<const PEResourceItem*> m_consumed;

    static bool isType(const PEResourceItem& i, uint32_t t) { return !i.type.isString && i.type.id == t; }

    /// Macro (or quoted/plain name) used for a resource id in the script.
    std::string idText(const PEResId& a_id, const char* a_prefix)
    {
        if (a_id.isString)
            return isIdentifier(a_id.name) ? a_id.name : q(a_id.name);
        std::string name = std::string(a_prefix) + std::to_string(a_id.id);
        m_defines.emplace(name, a_id.id);
        return name;
    }

    std::string uniquePath(const std::string& a_dir, const std::string& a_stem, const char* a_ext)
    {
        std::string p = a_dir + "/" + a_stem + a_ext;
        for (int i = 2; m_usedPaths.count(p); ++i)
            p = a_dir + "/" + a_stem + "_" + std::to_string(i) + a_ext;
        m_usedPaths.insert(p);
        return p;
    }

    std::string stem(const PEResourceItem& a_i, const std::string& a_prefix) const
    {
        std::string s = a_prefix;
        s += a_i.name.isString ? sanitize(a_i.name.name) : std::to_string(a_i.name.id);
        if (a_i.language)
            s += "_" + hex(a_i.language, 4).substr(2);
        return s;
    }

    std::string addFile(const std::string& a_dir, const std::string& a_stem, const char* a_ext,
        std::vector<uint8_t> a_bytes)
    {
        RcFile f;
        f.path = uniquePath(a_dir, a_stem, a_ext);
        f.bytes = std::move(a_bytes);
        const std::string p = f.path;
        m_out.files.push_back(std::move(f));
        return p;
    }

    static void put16(std::vector<uint8_t>& v, uint16_t x)
    {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
    }
    static void put32(std::vector<uint8_t>& v, uint32_t x)
    {
        put16(v, static_cast<uint16_t>(x));
        put16(v, static_cast<uint16_t>(x >> 16));
    }

    const PEResourceItem* find(uint32_t a_type, uint32_t a_id, uint16_t a_lang) const
    {
        const PEResourceItem* any = nullptr;
        for (const auto& i : m_items)
        {
            if (!isType(i, a_type) || i.name.isString || i.name.id != a_id)
                continue;
            if (i.language == a_lang)
                return &i;
            if (!any)
                any = &i;
        }
        return any;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Driver
    // ─────────────────────────────────────────────────────────────────────

    RcExportResult run()
    {
        m_out.resourceCount = m_items.size();
        buildRes();
        buildIndexHeader();

        std::ostringstream rc;
        rc << "// Resource script recovered by DumpPDB from the executable's resource directory.\n"
              "// A byte-exact binary copy of every resource is in " << m_base << ".res.\n"
              "// Compile with:  rc /c65001 " << m_base << ".rc\n\n"
              "#pragma code_page(65001)\n"
              "#include <windows.h>\n"
              "#include \"resource.h\"\n\n";

        std::set<uint16_t> languages;
        for (const auto& i : m_items)
            languages.insert(i.language);

        std::ostringstream body;
        for (uint16_t lang : languages)
        {
            body << "/////////////////////////////////////////////////////////////////////////////\n";
            body << "// " << languageName(lang) << "\n";
            body << "LANGUAGE " << (lang & 0x3FF) << ", " << (lang >> 10) << "\n\n";
            emitLanguage(body, lang);
        }

        // Icon / cursor images that no group refers to are kept as raw files.
        for (const auto& i : m_items)
        {
            if ((isType(i, RT_ICON_) || isType(i, RT_CURSOR_)) && !m_consumed.count(&i))
            {
                const std::string f = addFile("binary", stem(i, isType(i, RT_ICON_) ? "orphan_icon_" : "orphan_cursor_"), ".bin", i.data);
                m_out.notes.push_back("ungrouped " + PEResources::typeName(i.type) + " " + i.name.text() + " saved as " + f);
            }
        }

        m_out.rc = rc.str() + body.str();
        m_out.header = buildHeader(m_defines);
        return std::move(m_out);
    }

    static std::string languageName(uint16_t a_lang)
    {
        if (a_lang == 0)
            return "Neutral";
        static const std::map<uint16_t, const char*> names = {{0x0409, "English (United States)"},
            {0x0809, "English (United Kingdom)"}, {0x0407, "German (Germany)"}, {0x040C, "French (France)"},
            {0x0C0A, "Spanish (Spain)"}, {0x0410, "Italian (Italy)"}, {0x0419, "Russian (Russia)"},
            {0x0411, "Japanese (Japan)"}, {0x0804, "Chinese (Simplified, PRC)"}, {0x0405, "Czech (Czechia)"},
            {0x0415, "Polish (Poland)"}, {0x0816, "Portuguese (Portugal)"}, {0x0416, "Portuguese (Brazil)"},
            {0x040A, "Spanish (Traditional Sort)"}, {0x0412, "Korean (Korea)"}, {0x0413, "Dutch (Netherlands)"}};
        const auto it = names.find(a_lang);
        return it != names.end() ? it->second : "Language 0x" + hex(a_lang, 4).substr(2);
    }

    void emitLanguage(std::ostringstream& o, uint16_t a_lang)
    {
        // Fixed order keeps the script stable and readable.
        static const uint32_t order[] = {RT_GROUP_ICON_, RT_GROUP_CURSOR_, RT_BITMAP_, RT_MENU_,
            RT_DIALOG_, RT_STRING_, RT_ACCELERATOR_, RT_VERSION_, RT_MANIFEST_, RT_RCDATA_,
            RT_HTML_, RT_MESSAGETABLE_, RT_FONT_, RT_ANICURSOR_, RT_ANIICON_, RT_DLGINCLUDE_, RT_FONTDIR_,
            RT_PLUGPLAY_, RT_VXD_};

        std::set<uint32_t> done;
        for (uint32_t t : order)
        {
            done.insert(t);
            if (t == RT_STRING_)
            {
                emitStringTable(o, a_lang);
                continue;
            }
            for (const auto& i : m_items)
                if (i.language == a_lang && isType(i, t))
                    emitOne(o, i);
        }
        // everything else: numeric types we do not know, and string-named user types
        for (const auto& i : m_items)
        {
            if (i.language != a_lang)
                continue;
            if (!i.type.isString && (done.count(i.type.id) || i.type.id == RT_ICON_ || i.type.id == RT_CURSOR_))
                continue;
            emitOne(o, i);
        }
        o << "\n";
    }

    void emitOne(std::ostringstream& o, const PEResourceItem& a_i)
    {
        m_consumed.insert(&a_i);
        if (!a_i.type.isString)
        {
            switch (a_i.type.id)
            {
            case RT_GROUP_ICON_:
                return emitIconGroup(o, a_i, false);
            case RT_GROUP_CURSOR_:
                return emitIconGroup(o, a_i, true);
            case RT_BITMAP_:
                return emitBitmap(o, a_i);
            case RT_MENU_:
                return emitMenu(o, a_i);
            case RT_DIALOG_:
                return emitDialog(o, a_i);
            case RT_ACCELERATOR_:
                return emitAccelerators(o, a_i);
            case RT_VERSION_:
                return emitVersion(o, a_i);
            case RT_MANIFEST_:
                return emitFileResource(o, a_i, "manifest", ".manifest", "24", "MANIFEST");
            case RT_RCDATA_:
                return emitFileResource(o, a_i, "binary", ".bin", "RCDATA", "RCDATA");
            case RT_HTML_:
                return emitFileResource(o, a_i, "binary", ".html", "HTML", "HTML");
            case RT_MESSAGETABLE_:
                return emitFileResource(o, a_i, "binary", ".bin", "MESSAGETABLE", "MESSAGETABLE");
            case RT_FONT_:
                return emitFileResource(o, a_i, "binary", ".fnt", "FONT", "FONT");
            case RT_ANICURSOR_:
                return emitFileResource(o, a_i, "binary", ".ani", "ANICURSOR", "ANICURSOR");
            case RT_ANIICON_:
                return emitFileResource(o, a_i, "binary", ".ani", "ANIICON", "ANIICON");
            case RT_DLGINCLUDE_:
            {
                std::string s(a_i.data.begin(), a_i.data.end());
                s.erase(std::find(s.begin(), s.end(), '\0'), s.end());
                o << "// DLGINCLUDE: " << s << "\n";
                return;
            }
            case RT_FONTDIR_:
                o << "// RT_FONTDIR is generated by the resource compiler from the FONT resources.\n";
                return;
            default:
                break;
            }
        }
        // Unknown numeric type, PLUGPLAY, VXD or a string-named user type.
        const std::string typeTok = a_i.type.isString ? q(a_i.type.name) : std::to_string(a_i.type.id);
        emitFileResource(o, a_i, "binary", ".bin", typeTok, "", true);
    }

    // -- file-backed resources --------------------------------------------------------------

    void emitFileResource(std::ostringstream& o, const PEResourceItem& a_i, const char* a_dir,
        const char* a_ext, const std::string& a_typeToken, const char*, bool a_userType = false)
    {
        std::string prefix = a_userType && a_i.type.isString ? sanitize(a_i.type.name) + "_"
                                                             : (a_userType ? "type" + std::to_string(a_i.type.id) + "_" : "");
        const std::string file = addFile(a_dir, stem(a_i, prefix), a_ext, a_i.data);
        o << idText(a_i.name, a_i.type.id == RT_MANIFEST_ ? "IDR_MANIFEST_" : "IDR_RES_") << " " << a_typeToken << " " << q(file) << "\n";
    }

    // -- icons / cursors ---------------------------------------------------------------------

    void emitIconGroup(std::ostringstream& o, const PEResourceItem& a_g, bool a_cursor)
    {
        Reader r(a_g.data);
        r.u16();
        const uint16_t type = r.u16();
        const uint16_t count = r.u16();
        if (!r.ok || count == 0 || count > 256)
        {
            fallback(o, a_g, "malformed icon/cursor group");
            return;
        }

        struct Entry
        {
            uint16_t w = 0, h = 0, planes = 0, bits = 0;
            uint8_t colors = 0;
            uint32_t bytes = 0;
            uint16_t id = 0;
            const PEResourceItem* img = nullptr;
        };
        std::vector<Entry> es;
        for (uint16_t k = 0; k < count && r.ok; ++k)
        {
            Entry e;
            if (a_cursor)
            {
                e.w = r.u16();
                e.h = static_cast<uint16_t>(r.u16() / 2); // cursor group stores doubled height
                e.planes = r.u16();
                e.bits = r.u16();
                e.bytes = r.u32();
                e.id = r.u16();
            }
            else
            {
                e.w = r.u8();
                e.h = r.u8();
                e.colors = r.u8();
                r.u8();
                e.planes = r.u16();
                e.bits = r.u16();
                e.bytes = r.u32();
                e.id = r.u16();
            }
            e.img = find(a_cursor ? RT_CURSOR_ : RT_ICON_, e.id, a_g.language);
            if (e.img)
                m_consumed.insert(e.img);
            es.push_back(e);
        }
        (void)type;

        // ICONDIR (type 1 icon / 2 cursor) + 16-byte entries + image data.
        std::vector<uint8_t> file;
        put16(file, 0);
        put16(file, a_cursor ? 2 : 1);
        std::vector<const Entry*> good;
        for (const auto& e : es)
            if (e.img)
                good.push_back(&e);
        put16(file, static_cast<uint16_t>(good.size()));
        uint32_t offset = 6 + 16 * static_cast<uint32_t>(good.size());
        for (const Entry* e : good)
        {
            file.push_back(e->w >= 256 ? 0 : static_cast<uint8_t>(e->w));
            file.push_back(e->h >= 256 ? 0 : static_cast<uint8_t>(e->h));
            file.push_back(e->colors);
            file.push_back(0);
            if (a_cursor)
            {
                // The hotspot is stored in the first 4 bytes of the cursor resource.
                uint16_t hx = 0, hy = 0;
                if (e->img->data.size() >= 4)
                {
                    hx = static_cast<uint16_t>(e->img->data[0] | (e->img->data[1] << 8));
                    hy = static_cast<uint16_t>(e->img->data[2] | (e->img->data[3] << 8));
                }
                put16(file, hx);
                put16(file, hy);
                const uint32_t sz = e->img->data.size() >= 4 ? static_cast<uint32_t>(e->img->data.size() - 4) : 0;
                put32(file, sz);
                put32(file, offset);
                offset += sz;
            }
            else
            {
                put16(file, e->planes);
                put16(file, e->bits);
                put32(file, static_cast<uint32_t>(e->img->data.size()));
                put32(file, offset);
                offset += static_cast<uint32_t>(e->img->data.size());
            }
        }
        for (const Entry* e : good)
        {
            if (a_cursor)
            {
                if (e->img->data.size() > 4)
                    file.insert(file.end(), e->img->data.begin() + 4, e->img->data.end());
            }
            else
                file.insert(file.end(), e->img->data.begin(), e->img->data.end());
        }

        if (good.size() != es.size())
            m_out.notes.push_back(PEResources::typeName(a_g.type) + " " + a_g.name.text() + ": "
                                  + std::to_string(es.size() - good.size()) + " image(s) missing");

        const std::string path = addFile(a_cursor ? "cursors" : "icons", stem(a_g, a_cursor ? "cursor_" : "icon_"),
            a_cursor ? ".cur" : ".ico", std::move(file));
        o << idText(a_g.name, a_cursor ? "IDC_" : "IDI_") << (a_cursor ? " CURSOR " : " ICON ") << q(path) << "\n";
    }

    void emitBitmap(std::ostringstream& o, const PEResourceItem& a_i)
    {
        const auto& d = a_i.data;
        if (d.size() < 40)
        {
            fallback(o, a_i, "bitmap too small");
            return;
        }
        Reader r(d);
        const uint32_t hdr = r.u32();
        r.skip(4);
        r.skip(4);
        r.u16();
        const uint16_t bpp = r.u16();
        const uint32_t compression = r.u32();
        r.skip(12);
        const uint32_t used = r.u32();
        uint32_t palette = 0;
        if (hdr >= 40)
        {
            if (bpp <= 8)
                palette = (used ? used : (1u << bpp)) * 4;
            else
                palette = used * 4;
            if (compression == 3 && hdr == 40)
                palette += 12; // BI_BITFIELDS masks follow the header
        }
        std::vector<uint8_t> file{'B', 'M'};
        put32(file, static_cast<uint32_t>(14 + d.size()));
        put32(file, 0);
        put32(file, 14 + hdr + palette);
        file.insert(file.end(), d.begin(), d.end());
        const std::string path = addFile("bitmaps", stem(a_i, "bitmap_"), ".bmp", std::move(file));
        o << idText(a_i.name, "IDB_") << " BITMAP " << q(path) << "\n";
    }

    // -- STRINGTABLE ---------------------------------------------------------------------------

    void emitStringTable(std::ostringstream& o, uint16_t a_lang)
    {
        std::map<uint32_t, std::string> strings;
        bool any = false;
        for (const auto& i : m_items)
        {
            if (i.language != a_lang || !isType(i, RT_STRING_) || i.name.isString)
                continue;
            any = true;
            m_consumed.insert(&i);
            Reader r(i.data);
            for (uint32_t k = 0; k < 16 && r.ok; ++k)
            {
                const uint16_t len = r.u16();
                if (!r.ok)
                    break;
                std::vector<uint16_t> w;
                for (uint16_t c = 0; c < len; ++c)
                    w.push_back(r.u16());
                if (len && r.ok)
                    strings[(i.name.id - 1) * 16 + k] = Reader::utf8(w);
            }
        }
        if (!any)
            return;
        o << "STRINGTABLE\nBEGIN\n";
        for (const auto& [id, text] : strings)
        {
            const std::string name = "IDS_STRING_" + std::to_string(id);
            m_defines.emplace(name, id);
            o << "    " << name << ", " << q(text) << "\n";
        }
        o << "END\n\n";
    }

    // -- VERSIONINFO -----------------------------------------------------------------------------

    struct VerBlock
    {
        std::string key;
        uint16_t type = 0;
        std::vector<uint8_t> value;
        uint16_t valueLen = 0;
        std::vector<VerBlock> kids;
    };

    static bool readVerBlock(Reader& r, VerBlock& b, size_t a_limit, int a_depth)
    {
        if (a_depth > 8)
            return false;
        const size_t start = r.pos;
        const uint16_t len = r.u16();
        b.valueLen = r.u16();
        b.type = r.u16();
        if (!r.ok || len < 6 || start + len > a_limit)
            return false;
        b.key = r.wstr();
        r.align(4);
        const size_t end = start + len;
        const size_t valBytes = b.type == 1 ? static_cast<size_t>(b.valueLen) * 2 : b.valueLen;
        if (r.pos + valBytes > end)
            return false;
        b.value.assign(r.p + r.pos, r.p + r.pos + valBytes);
        r.pos += valBytes;
        r.align(4);
        while (r.ok && r.pos + 6 <= end)
        {
            VerBlock kid;
            if (!readVerBlock(r, kid, end, a_depth + 1))
                return false;
            b.kids.push_back(std::move(kid));
            r.align(4);
        }
        r.pos = std::min(end, r.n);
        return true;
    }

    static std::string verText(const VerBlock& b)
    {
        std::vector<uint16_t> w;
        for (size_t i = 0; i + 1 < b.value.size(); i += 2)
        {
            const uint16_t c = static_cast<uint16_t>(b.value[i] | (b.value[i + 1] << 8));
            if (c == 0)
                break;
            w.push_back(c);
        }
        return Reader::utf8(w);
    }

    void emitVersion(std::ostringstream& o, const PEResourceItem& a_i)
    {
        Reader r(a_i.data);
        VerBlock root;
        if (!readVerBlock(r, root, a_i.data.size(), 0) || root.value.size() < 52)
        {
            fallback(o, a_i, "malformed VERSIONINFO");
            return;
        }
        Reader f(root.value);
        const uint32_t sig = f.u32();
        f.u32();
        const uint32_t fvMs = f.u32(), fvLs = f.u32(), pvMs = f.u32(), pvLs = f.u32();
        const uint32_t mask = f.u32(), flags = f.u32(), os = f.u32(), type = f.u32(), sub = f.u32();
        if (sig != 0xFEEF04BDu)
        {
            fallback(o, a_i, "bad VS_FIXEDFILEINFO signature");
            return;
        }
        auto ver = [](uint32_t ms, uint32_t ls)
        {
            return std::to_string(ms >> 16) + "," + std::to_string(ms & 0xFFFF) + "," + std::to_string(ls >> 16)
                   + "," + std::to_string(ls & 0xFFFF);
        };

        o << idText(a_i.name, "VS_VERSION_INFO_") << " VERSIONINFO\n"
          << " FILEVERSION " << ver(fvMs, fvLs) << "\n"
          << " PRODUCTVERSION " << ver(pvMs, pvLs) << "\n"
          << " FILEFLAGSMASK " << hex(mask) << "L\n"
          << " FILEFLAGS " << hex(flags) << "L\n"
          << " FILEOS " << hex(os) << "L\n"
          << " FILETYPE " << hex(type) << "L\n"
          << " FILESUBTYPE " << hex(sub) << "L\n"
          << "BEGIN\n";
        for (const auto& info : root.kids)
        {
            o << "    BLOCK " << q(info.key) << "\n    BEGIN\n";
            for (const auto& tbl : info.kids)
            {
                if (info.key == "StringFileInfo")
                {
                    o << "        BLOCK " << q(tbl.key) << "\n        BEGIN\n";
                    for (const auto& s : tbl.kids)
                        o << "            VALUE " << q(s.key) << ", " << q(verText(s)) << "\n";
                    o << "        END\n";
                }
                else
                {
                    // VarFileInfo: "Translation" = pairs of (language, code page)
                    o << "        VALUE " << q(tbl.key);
                    for (size_t k = 0; k + 3 < tbl.value.size(); k += 4)
                    {
                        const uint16_t lang = static_cast<uint16_t>(tbl.value[k] | (tbl.value[k + 1] << 8));
                        const uint16_t cp = static_cast<uint16_t>(tbl.value[k + 2] | (tbl.value[k + 3] << 8));
                        o << ", " << hex(lang) << ", " << cp;
                    }
                    o << "\n";
                }
            }
            o << "    END\n";
        }
        o << "END\n\n";
    }

    // -- ACCELERATORS -------------------------------------------------------------------------------

    static std::string vkName(uint16_t k)
    {
        if ((k >= '0' && k <= '9') || (k >= 'A' && k <= 'Z'))
            return q(std::string(1, static_cast<char>(k)));
        if (k >= 0x70 && k <= 0x87)
            return "VK_F" + std::to_string(k - 0x6F);
        static const std::map<uint16_t, const char*> names = {{0x08, "VK_BACK"}, {0x09, "VK_TAB"},
            {0x0D, "VK_RETURN"}, {0x1B, "VK_ESCAPE"}, {0x20, "VK_SPACE"}, {0x21, "VK_PRIOR"},
            {0x22, "VK_NEXT"}, {0x23, "VK_END"}, {0x24, "VK_HOME"}, {0x25, "VK_LEFT"}, {0x26, "VK_UP"},
            {0x27, "VK_RIGHT"}, {0x28, "VK_DOWN"}, {0x2D, "VK_INSERT"}, {0x2E, "VK_DELETE"},
            {0xBB, "VK_OEM_PLUS"}, {0xBD, "VK_OEM_MINUS"}, {0xBC, "VK_OEM_COMMA"}, {0xBE, "VK_OEM_PERIOD"}};
        const auto it = names.find(k);
        return it != names.end() ? it->second : hex(k);
    }

    void emitAccelerators(std::ostringstream& o, const PEResourceItem& a_i)
    {
        Reader r(a_i.data);
        std::ostringstream t;
        bool last = false;
        int guard = 0;
        while (!last && r.has(8) && ++guard < 4096)
        {
            const uint16_t fl = r.u16();
            const uint16_t key = r.u16();
            const uint16_t id = r.u16();
            r.u16();
            last = (fl & 0x80) != 0;
            const bool virt = (fl & 1) != 0;
            const std::string idName = "ID_ACCEL_" + std::to_string(id);
            m_defines.emplace(idName, id);
            t << "    " << (virt ? vkName(key) : (key >= 0x20 && key < 0x7F ? q(std::string(1, static_cast<char>(key))) : hex(key)))
              << ", " << idName << ", " << (virt ? "VIRTKEY" : "ASCII");
            if (fl & 2)
                t << ", NOINVERT";
            if (fl & 4)
                t << ", SHIFT";
            if (fl & 8)
                t << ", CONTROL";
            if (fl & 0x10)
                t << ", ALT";
            t << "\n";
        }
        if (!r.ok && guard == 0)
        {
            fallback(o, a_i, "malformed accelerator table");
            return;
        }
        o << idText(a_i.name, "IDR_ACCEL_") << " ACCELERATORS\nBEGIN\n" << t.str() << "END\n\n";
    }

    // -- MENU / MENUEX ----------------------------------------------------------------------------------

    void emitMenu(std::ostringstream& o, const PEResourceItem& a_i)
    {
        Reader r(a_i.data);
        const uint16_t version = r.u16();
        const uint16_t hdrSize = r.u16();
        std::ostringstream t;
        bool ok = false;
        if (version == 0)
        {
            r.skip(hdrSize);
            ok = menuItems(r, t, 1) && r.ok;
            if (ok)
                o << idText(a_i.name, "IDR_MENU_") << " MENU\nBEGIN\n" << t.str() << "END\n\n";
        }
        else if (version == 1)
        {
            r.u32(); // help id
            r.pos = 4 + hdrSize;
            ok = menuExItems(r, t, 1) && r.ok;
            if (ok)
                o << idText(a_i.name, "IDR_MENU_") << " MENUEX\nBEGIN\n" << t.str() << "END\n\n";
        }
        if (!ok)
            fallback(o, a_i, "malformed menu");
    }

    bool menuItems(Reader& r, std::ostringstream& t, int a_depth)
    {
        const std::string pad(static_cast<size_t>(a_depth) * 4, ' ');
        for (int guard = 0; guard < 4096 && r.ok; ++guard)
        {
            const uint16_t flags = r.u16();
            if (!r.ok)
                return false;
            const bool popup = (flags & 0x10) != 0;
            const bool end = (flags & 0x80) != 0;
            uint16_t id = 0;
            if (!popup)
                id = r.u16();
            const std::string text = r.wstr();
            std::string opts;
            auto add = [&](uint16_t bit, const char* n)
            {
                if (flags & bit)
                    opts += std::string(", ") + n;
            };
            add(0x01, "GRAYED");
            add(0x02, "INACTIVE");
            add(0x08, "CHECKED");
            add(0x20, "MENUBARBREAK");
            add(0x40, "MENUBREAK");
            add(0x4000, "HELP");
            if (popup)
            {
                t << pad << "POPUP " << q(text) << opts << "\n" << pad << "BEGIN\n";
                if (!menuItems(r, t, a_depth + 1))
                    return false;
                t << pad << "END\n";
            }
            else if (id == 0 && text.empty() && !(flags & ~0x80))
                t << pad << "MENUITEM SEPARATOR\n";
            else
            {
                const std::string name = "ID_MENU_" + std::to_string(id);
                m_defines.emplace(name, id);
                t << pad << "MENUITEM " << q(text) << ", " << name << opts << "\n";
            }
            if (end)
                return true;
        }
        return false;
    }

    bool menuExItems(Reader& r, std::ostringstream& t, int a_depth)
    {
        const std::string pad(static_cast<size_t>(a_depth) * 4, ' ');
        for (int guard = 0; guard < 4096 && r.ok; ++guard)
        {
            r.align(4);
            const uint32_t type = r.u32();
            const uint32_t state = r.u32();
            const uint32_t id = r.u32();
            const uint16_t resInfo = r.u16();
            const std::string text = r.wstr();
            r.align(4);
            if (!r.ok)
                return false;
            const bool popup = (resInfo & 1) != 0;
            const bool last = (resInfo & 0x80) != 0;
            const std::string name = "ID_MENU_" + std::to_string(id);
            if (popup)
            {
                const uint32_t help = r.u32();
                m_defines.emplace(name, id);
                t << pad << "POPUP " << q(text) << ", " << name << ", " << hex(type) << ", " << hex(state)
                  << ", " << help << "\n" << pad << "BEGIN\n";
                if (!menuExItems(r, t, a_depth + 1))
                    return false;
                t << pad << "END\n";
            }
            else if (type & 0x800)
                t << pad << "MENUITEM \"\", " << static_cast<int32_t>(id) << ", MFT_SEPARATOR\n";
            else
            {
                m_defines.emplace(name, id);
                t << pad << "MENUITEM " << q(text) << ", " << name << ", " << hex(type) << ", " << hex(state) << "\n";
            }
            if (last)
                return true;
        }
        return false;
    }

    // -- DIALOG / DIALOGEX -------------------------------------------------------------------------------

    static std::string classText(const SzOrOrd& c)
    {
        if (!c.isOrdinal)
            return q(c.text);
        switch (c.ordinal)
        {
        case 0x80:
            return "\"Button\"";
        case 0x81:
            return "\"Edit\"";
        case 0x82:
            return "\"Static\"";
        case 0x83:
            return "\"ListBox\"";
        case 0x84:
            return "\"ScrollBar\"";
        case 0x85:
            return "\"ComboBox\"";
        default:
            return hex(c.ordinal);
        }
    }

    static std::string controlStyle(const SzOrOrd& cls, uint32_t style)
    {
        const std::vector<Flag>* classTable = nullptr;
        if (cls.isOrdinal)
        {
            switch (cls.ordinal)
            {
            case 0x80:
                classTable = &buttonFlags();
                break;
            case 0x81:
                classTable = &editFlags();
                break;
            case 0x82:
                classTable = &staticFlags();
                break;
            case 0x83:
                classTable = &listFlags();
                break;
            case 0x84:
                classTable = &scrollFlags();
                break;
            case 0x85:
                classTable = &comboFlags();
                break;
            }
        }
        else
        {
            std::string l;
            for (char c : cls.text)
                l.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (l == "button")
                classTable = &buttonFlags();
            else if (l == "edit")
                classTable = &editFlags();
            else if (l == "static")
                classTable = &staticFlags();
            else if (l == "listbox")
                classTable = &listFlags();
            else if (l == "combobox")
                classTable = &comboFlags();
            else if (l == "scrollbar")
                classTable = &scrollFlags();
        }
        std::vector<const std::vector<Flag>*> tables = {&wsCommon(), &wsControlBoxes()};
        if (classTable)
        {
            // Class-specific bits live in the low word; decode them on that part only.
            const uint32_t lowMask = 0xFFFF;
            const std::string high = decode(style & ~lowMask, tables);
            std::vector<const std::vector<Flag>*> ct = {classTable};
            const std::string low = (style & lowMask) ? decode(style & lowMask, ct) : std::string();
            std::string out = (style & ~lowMask) ? high : std::string();
            if (!low.empty())
                out += (out.empty() ? "" : " | ") + low;
            return out.empty() ? "0" : out;
        }
        return decode(style, tables);
    }

    void emitDialog(std::ostringstream& o, const PEResourceItem& a_i)
    {
        Reader r(a_i.data);
        const size_t save = r.pos;
        const uint16_t ver = r.u16();
        const uint16_t sig = r.u16();
        const bool ex = ver == 1 && sig == 0xFFFF;
        uint32_t help = 0, style = 0, exStyle = 0;
        if (ex)
        {
            help = r.u32();
            exStyle = r.u32();
            style = r.u32();
        }
        else
        {
            r.pos = save;
            style = r.u32();
            exStyle = r.u32();
        }
        const uint16_t count = r.u16();
        const int16_t x = r.i16(), y = r.i16(), cx = r.i16(), cy = r.i16();
        const SzOrOrd menu = readSzOrOrd(r);
        const SzOrOrd cls = readSzOrOrd(r);
        const std::string title = r.wstr();
        bool hasFont = (style & 0x40) != 0;
        uint16_t pt = 0, weight = 0;
        uint8_t italic = 0, charset = 1;
        std::string face;
        if (hasFont)
        {
            pt = r.u16();
            if (ex)
            {
                weight = r.u16();
                italic = r.u8();
                charset = r.u8();
            }
            face = r.wstr();
        }
        if (!r.ok || count > 1024)
        {
            fallback(o, a_i, "malformed dialog header");
            return;
        }

        std::ostringstream ctl;
        for (uint16_t k = 0; k < count; ++k)
        {
            r.align(4);
            uint32_t chelp = 0, cstyle = 0, cex = 0, cid = 0;
            if (ex)
            {
                chelp = r.u32();
                cex = r.u32();
                cstyle = r.u32();
            }
            else
            {
                cstyle = r.u32();
                cex = r.u32();
            }
            const int16_t cxp = r.i16(), cyp = r.i16(), cw = r.i16(), ch = r.i16();
            cid = ex ? r.u32() : r.u16();
            const SzOrOrd ccls = readSzOrOrd(r);
            const SzOrOrd ctitle = readSzOrOrd(r);
            const uint16_t extra = r.u16();
            r.skip(extra);
            if (!r.ok)
            {
                fallback(o, a_i, "malformed dialog control");
                return;
            }
            const std::string idTok = cid == 0xFFFF || cid == 0xFFFFFFFFu ? "-1" : ("IDC_CTRL_" + std::to_string(cid));
            if (cid != 0xFFFF && cid != 0xFFFFFFFFu)
                m_defines.emplace(idTok, static_cast<uint32_t>(cid));
            std::string text = ctitle.isOrdinal ? hex(ctitle.ordinal) : q(ctitle.text);
            ctl << "    CONTROL         " << text << ", " << idTok << ", " << classText(ccls) << ", "
                << controlStyle(ccls, cstyle) << ", " << cxp << ", " << cyp << ", " << cw << ", " << ch;
            if (cex || chelp)
                ctl << ", " << hex(cex);
            if (ex && chelp)
                ctl << ", " << chelp;
            ctl << "\n";
        }

        o << idText(a_i.name, "IDD_DIALOG_") << (ex ? " DIALOGEX " : " DIALOG ") << x << ", " << y << ", " << cx
          << ", " << cy;
        if (ex && help)
            o << ", " << help;
        o << "\n";
        o << "STYLE " << decode(style, {&wsCommon(), &wsDialogBoxes(), &dsFlags()}) << "\n";
        if (exStyle)
            o << "EXSTYLE " << hex(exStyle) << "\n";
        if (!title.empty())
            o << "CAPTION " << q(title) << "\n";
        if (!menu.empty())
            o << "MENU " << (menu.isOrdinal ? std::to_string(menu.ordinal) : q(menu.text)) << "\n";
        if (!cls.empty())
            o << "CLASS " << (cls.isOrdinal ? std::to_string(cls.ordinal) : q(cls.text)) << "\n";
        if (hasFont)
        {
            o << "FONT " << pt << ", " << q(face);
            if (ex)
                o << ", " << weight << ", " << static_cast<int>(italic) << ", " << hex(charset);
            o << "\n";
        }
        o << "BEGIN\n" << ctl.str() << "END\n\n";
    }

    // -- fallbacks ---------------------------------------------------------------------------------------------

    void fallback(std::ostringstream& o, const PEResourceItem& a_i, const char* a_why)
    {
        const std::string file = addFile("binary", stem(a_i, sanitize(PEResources::typeName(a_i.type)) + "_"), ".bin", a_i.data);
        m_out.notes.push_back(PEResources::typeName(a_i.type) + " " + a_i.name.text() + ": " + a_why + " - raw copy in "
                              + file + " and in the .res");
        o << "// " << PEResources::typeName(a_i.type) << " " << a_i.name.text() << ": " << a_why << "; raw data: " << file
          << " (present in the .res)\n\n";
    }

    // ─────────────────────────────────────────────────────────────────────
    // Byte-exact .res, index and resource.h
    // ─────────────────────────────────────────────────────────────────────

    static void resId(std::vector<uint8_t>& v, const PEResId& a_id)
    {
        if (!a_id.isString)
        {
            put16(v, 0xFFFF);
            put16(v, static_cast<uint16_t>(a_id.id));
            return;
        }
        // UTF-8 -> UTF-16 (BMP + surrogates)
        const std::string& s = a_id.name;
        for (size_t i = 0; i < s.size();)
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            uint32_t cp = c;
            size_t len = 1;
            if (c >= 0xF0 && i + 3 < s.size())
            {
                cp = ((c & 7u) << 18) | ((s[i + 1] & 0x3Fu) << 12) | ((s[i + 2] & 0x3Fu) << 6) | (s[i + 3] & 0x3Fu);
                len = 4;
            }
            else if (c >= 0xE0 && i + 2 < s.size())
            {
                cp = ((c & 0xFu) << 12) | ((s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu);
                len = 3;
            }
            else if (c >= 0xC0 && i + 1 < s.size())
            {
                cp = ((c & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu);
                len = 2;
            }
            if (cp >= 0x10000)
            {
                cp -= 0x10000;
                put16(v, static_cast<uint16_t>(0xD800 + (cp >> 10)));
                put16(v, static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
            }
            else
                put16(v, static_cast<uint16_t>(cp));
            i += len;
        }
        put16(v, 0);
    }

    void buildRes()
    {
        auto& v = m_out.res;
        // The mandatory empty first resource (32-bit .res marker).
        const uint8_t marker[32] = {0, 0, 0, 0, 0x20, 0, 0, 0, 0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        v.insert(v.end(), marker, marker + 32);

        for (const auto& i : m_items)
        {
            std::vector<uint8_t> hdr;
            resId(hdr, i.type);
            resId(hdr, i.name);
            while (hdr.size() % 4)
                hdr.push_back(0);
            put32(hdr, 0);                          // DataVersion
            put16(hdr, 0x0030);                     // MemoryFlags: MOVEABLE | PURE
            put16(hdr, i.language);
            put32(hdr, 0);                          // Version
            put32(hdr, 0);                          // Characteristics
            put32(v, static_cast<uint32_t>(i.data.size()));
            put32(v, static_cast<uint32_t>(hdr.size() + 8));
            v.insert(v.end(), hdr.begin(), hdr.end());
            v.insert(v.end(), i.data.begin(), i.data.end());
            while (v.size() % 4)
                v.push_back(0);
        }
    }

    void buildIndexHeader()
    {
        std::ostringstream csv;
        csv << "type,name,language,codepage,size,rva\n";
        for (const auto& i : m_items)
            csv << PEResources::typeName(i.type) << "," << i.name.text() << "," << hex(i.language, 4) << ","
                << i.codePage << "," << i.data.size() << "," << hex(i.rva, 8) << "\n";
        m_out.index = csv.str();
    }

public:

    /// resource.h text. Call after build(); uses the ids collected while writing the script.
    [[nodiscard]] static std::string buildHeader(const std::map<std::string, uint32_t>& a_defines)
    {
        std::ostringstream h;
        h << "// Generated by DumpPDB. Ids found in the executable's resources.\n#pragma once\n\n";
        for (const auto& [n, v] : a_defines)
            h << "#define " << n << " " << v << "\n";
        return h.str();
    }
};

} // namespace DumpPDB
