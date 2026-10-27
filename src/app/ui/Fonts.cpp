#include "app/ui/Fonts.hpp"

#include "app/ui/text/Shaping.hpp"

#include <imgui_freetype.h>
#include <imgui_internal.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sloptube::app {
namespace {

constexpr std::string_view kFontDirectory = "/system/fonts/";
// Roboto ships as one variable font since Android 12; each weight is one of its named instances.
// Older phones have a static file per weight instead. A weight found in neither is the regular
// face emboldened.
constexpr std::string_view kVariableFont = "Roboto-Regular.ttf";

struct WeightFace {
    FT_Fixed weight; // The OpenType wght axis value, 16.16.
    std::string_view staticFont;
};

constexpr std::array<WeightFace, kWeightCount> kWeightFaces{{
    {400 << 16, "Roboto-Regular.ttf"},
    {500 << 16, "Roboto-Medium.ttf"},
    {600 << 16, "Roboto-SemiBold.ttf"},
    {700 << 16, "Roboto-Bold.ttf"},
}};

constexpr FT_ULong kWeightAxis = FT_MAKE_TAG('w', 'g', 'h', 't');

// Android draws text unhinted, sized by its em square. FreeType requests the nominal (em) size only
// for fonts that may hold bitmaps; none of the system's outline fonts do, so enabling them changes
// nothing else.
constexpr ImGuiFreeTypeLoaderFlags kLoaderFlags =
    ImGuiFreeTypeLoaderFlags_NoHinting | ImGuiFreeTypeLoaderFlags_Bitmap;

// Fallbacks tried before the generic scan, in priority order; the first file of each group that
// loads is used. Arabic needs a face carrying the presentation forms our shaper emits (all listed
// do). The static Samsung and Noto Serif CJK collections back up Noto Sans CJK on phones without
// it; Samsung's lacks Hangul, hence the separate Korean group.
using Candidates = std::array<std::string_view, 6>;
constexpr std::array<Candidates, 7> kPreferredFallbacks{{
    {"NotoSansSymbols-Regular-Subsetted.ttf"},
    {"NotoSansSymbols-Regular-Subsetted2.ttf"},
    {"NotoNaskhArabicUI-Regular.ttf", "NotoNaskhArabic-Regular.ttf", "NotoSansArabicUI-Regular.ttf",
     "NotoSansArabic-Regular.ttf", "SECNaskhArabicUI-Regular.ttf", "DroidNaskh-Regular.ttf"},
    {"NotoSansHebrew-Regular.ttf"},
    {"NotoSansCJK-Regular.ttc", "SECCJK-Regular.ttc", "NotoSerifCJK-Regular.ttc",
     "DroidSansFallbackFull.ttf", "DroidSansFallback.ttf"},
    {"OneUISansKR-VF.ttf", "NotoSerifCJK-Regular.ttc"},
    {"SECCJK-Regular-Extra.ttf"},
}};

// Every other script comes from the per-script Noto families, one file per script: sans before
// serif, the compact UI variant before the regular one. Colour emoji (CBDT, PNG bitmaps) is never
// matched: the platform draws emoji instead (see addEmoji).
constexpr std::array<std::string_view, 2> kScanFamilies{"NotoSans", "NotoSerif"};
constexpr std::array<std::string_view, 3> kScanSuffixes{"-Regular.ttf", "-Regular.otf", "-VF.ttf"};
constexpr std::string_view kCompactVariant = "UI";

// --- Colour emoji ------------------------------------------------------------------------------

const EmojiSource* g_emoji = nullptr;

// Joiners, presentation selectors, skin tone modifiers and tags shape emoji sequences; drawn one
// code point at a time they would show as boxes, so they take no space instead.
bool isEmojiModifier(char32_t c) {
    return c == 0x200D || c == 0xFE0E || c == 0xFE0F || (c >= 0x1F3FB && c <= 0x1F3FF) ||
           (c >= 0xE0020 && c <= 0xE007F);
}

// Blocks holding emoji: miscellaneous technical, symbols and dingbats, arrows and stars, and the
// supplementary planes' pictographs.
bool isEmojiBlock(char32_t c) {
    return (c >= 0x2300 && c <= 0x23FF) || (c >= 0x2600 && c <= 0x27BF) ||
           (c >= 0x2B00 && c <= 0x2BFF) || (c >= 0x1F000 && c <= 0x1FAFF);
}

bool emojiContainsGlyph(ImFontAtlas*, ImFontConfig*, ImWchar c) {
    return isEmojiModifier(c) || (isEmojiBlock(c) && g_emoji->has(c));
}

bool emojiLoadGlyph(ImFontAtlas* atlas, ImFontConfig* src, ImFontBaked* baked, void*, ImWchar c,
                    ImFontGlyph* out, float* outAdvance) {
    if (isEmojiModifier(c)) {
        if (outAdvance != nullptr) {
            *outAdvance = 0;
        } else {
            out->Codepoint = c;
        }
        return true;
    }
    if (!isEmojiBlock(c) || !g_emoji->has(c)) {
        return false;
    }
    const float density = src->RasterizerDensity * baked->RasterizerDensity;
    const auto glyph = g_emoji->draw(c, baked->Size * density);
    if (!glyph) {
        return false;
    }
    const float advance = glyph->advance / density;
    if (outAdvance != nullptr) {
        *outAdvance = advance;
        return true;
    }
    const ImFontAtlasRectId pack = ImFontAtlasPackAddRect(atlas, glyph->width, glyph->height);
    if (pack == ImFontAtlasRectId_Invalid) {
        return false;
    }
    ImTextureRect* rect = ImFontAtlasPackGetRect(atlas, pack);
    out->Codepoint = c;
    out->AdvanceX = advance;
    out->X0 = 0;
    out->Y0 = IM_ROUND(baked->Ascent) - glyph->baseline / density;
    out->X1 = static_cast<float>(glyph->width) / density;
    out->Y1 = out->Y0 + static_cast<float>(glyph->height) / density;
    out->Visible = true;
    out->Colored = true;
    out->PackId = pack;
    ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, src, out, rect,
                                       reinterpret_cast<const unsigned char*>(glyph->pixels.data()),
                                       ImTextureFormat_RGBA32, glyph->width * 4);
    return true;
}

const ImFontLoader& emojiLoader() {
    static const ImFontLoader loader = [] {
        ImFontLoader l;
        l.Name = "platform emoji";
        l.FontSrcContainsGlyph = emojiContainsGlyph;
        l.FontBakedLoadGlyph = emojiLoadGlyph;
        return l;
    }();
    return loader;
}

// Face order inside the Noto/Samsung CJK collections: ja, ko, zh-Hans, zh-Hant, zh-Hant-HK.
int cjkFaceIndex(std::string_view language, std::string_view region) {
    if (language == "ko") {
        return 1;
    }
    if (language == "zh") {
        return region == "HK" ? 4 : (region == "TW" || region == "MO") ? 3 : 2;
    }
    return 0;
}

struct MappedFile {
    const unsigned char* data = nullptr;
    std::size_t size = 0;
};

std::uint32_t readBe32(const unsigned char* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) |
           std::uint32_t{p[3]};
}

// Read-only, demand-paged mapping, kept for the process lifetime and shared by later atlases
// (the activity can recreate the app). Null when missing or not a TrueType/OpenType file.
MappedFile mapFont(const std::string& path) {
    static std::unordered_map<std::string, MappedFile> mapped;
    if (const auto found = mapped.find(path); found != mapped.end()) {
        return found->second;
    }
    MappedFile file;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return file;
    }

    struct stat info{};

    if (::fstat(fd, &info) == 0 && info.st_size >= 12) {
        const auto size = static_cast<std::size_t>(info.st_size);
        void* data = ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data != MAP_FAILED) {
            file = {static_cast<const unsigned char*>(data), size};
        }
    }
    ::close(fd);
    if (file.data == nullptr) {
        return file;
    }
    const std::uint32_t tag = readBe32(file.data);
    if (tag != 0x00010000 && tag != 0x4F54544F /* OTTO */ && tag != 0x74727565 /* true */ &&
        tag != 0x74746366 /* ttcf */) {
        ::munmap(const_cast<unsigned char*>(file.data), file.size);
        return {};
    }
    mapped.emplace(path, file);
    return file;
}

// What the variable primary face offers: the named instance (1-based, 0 when absent) holding each
// weight at the default width and upright, and the face's line height. lineSpacing is 0 when the
// file cannot be read.
struct PrimaryFace {
    std::array<FT_Long, kWeightCount> instances{};
    float lineSpacing = 0;
};

PrimaryFace inspectPrimary(const MappedFile& file) {
    PrimaryFace primary;
    FT_Library library = nullptr;
    if (file.data == nullptr || FT_Init_FreeType(&library) != 0) {
        return primary;
    }
    FT_Face face = nullptr;
    if (FT_New_Memory_Face(library, file.data, static_cast<FT_Long>(file.size), 0, &face) == 0) {
        // Android's default line height: the hhea ascender to descender.
        primary.lineSpacing = static_cast<float>(face->ascender - face->descender) /
                              static_cast<float>(face->units_per_EM);
        FT_MM_Var* variations = nullptr;
        if (FT_HAS_MULTIPLE_MASTERS(face) && FT_Get_MM_Var(face, &variations) == 0) {
            for (FT_UInt style = 0; style < variations->num_namedstyles; ++style) {
                const FT_Fixed* coords = variations->namedstyle[style].coords;
                std::optional<FT_Fixed> weight;
                bool otherwiseDefault = true;
                for (FT_UInt axis = 0; axis < variations->num_axis; ++axis) {
                    if (variations->axis[axis].tag == kWeightAxis) {
                        weight = coords[axis];
                    } else if (coords[axis] != variations->axis[axis].def) {
                        otherwiseDefault = false;
                    }
                }
                for (std::size_t w = 0; w < kWeightCount; ++w) {
                    if (otherwiseDefault && weight == kWeightFaces[w].weight &&
                        primary.instances[w] == 0) {
                        primary.instances[w] = static_cast<FT_Long>(style) + 1;
                    }
                }
            }
            FT_Done_MM_Var(library, variations);
        }
        FT_Done_Face(face);
    }
    FT_Done_FreeType(library);
    return primary;
}

class Loader {
public:
    Loader(ImFontAtlas& atlas, float sizePx, int cjkFace)
        : m_atlas(atlas)
        , m_sizePx(sizePx)
        , m_cjkFace(cjkFace) {}

    [[nodiscard]] ImFont* font() const { return m_font; }

    // Adds the first available of `names` (empty entries are skipped).
    bool addFirst(std::span<const std::string_view> names) {
        return std::ranges::any_of(
            names, [&](std::string_view name) { return !name.empty() && add(name); });
    }

    // Adds /system/fonts/`name`, merged into the first font added. False when unavailable.
    // `fontNo` is FreeType's face index: a named instance of a variable font in its high 16 bits.
    bool add(std::string_view name, FT_Long fontNo = 0, ImGuiFreeTypeLoaderFlags flags = 0) {
        if (std::ranges::find(m_added, name) != m_added.end()) {
            return true;
        }
        const MappedFile file = mapFont(std::string(kFontDirectory) + std::string(name));
        if (file.data == nullptr) {
            return false;
        }
        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        config.MergeMode = m_font != nullptr;
        config.FontNo = static_cast<int>(fontNo);
        config.FontLoaderFlags = flags;
        // Android rounds each glyph's advance to whole pixels; text is as wide as there.
        config.PixelSnapH = true;
        if (readBe32(file.data) == 0x74746366) {
            // Collections hold several faces; stay on the first if ours is absent.
            const std::uint32_t faces = file.size >= 12 ? readBe32(file.data + 8) : 0;
            config.FontNo = std::cmp_less(m_cjkFace, faces) ? m_cjkFace : 0;
        }
        // ImGui only reads the data when FontDataOwnedByAtlas is false.
        ImFont* added = m_atlas.AddFontFromMemoryTTF(
            const_cast<unsigned char*>(file.data), static_cast<int>(file.size), m_sizePx, &config);
        if (added == nullptr) {
            return false;
        }
        if (m_font == nullptr) {
            m_font = added;
        }
        m_added.emplace_back(name);
        return true;
    }

    // Merges the platform's colour emoji. Placed before the symbol fonts so that pictographs they
    // also carry (hearts, stars) come out in colour, as elsewhere on the phone.
    void addEmoji() {
        ImFontConfig config;
        config.MergeMode = true;
        config.SizePixels = m_sizePx;
        config.FontLoader = &emojiLoader();
        ImFormatString(config.Name, IM_COUNTOF(config.Name), "%s", emojiLoader().Name);
        m_atlas.AddFont(&config);
    }

private:
    ImFontAtlas& m_atlas;
    float m_sizePx;
    int m_cjkFace;
    ImFont* m_font = nullptr;
    std::vector<std::string> m_added;
};

// Script fonts found in the font directory, best variant per script, in a stable order.
std::vector<std::string> scanScriptFonts() {
    struct Choice {
        std::string name;
        int rank = 0; // Lower is better.
    };

    std::map<std::string, Choice> scripts;
    std::error_code error;
    for (const auto& entry :
         std::filesystem::directory_iterator(std::string(kFontDirectory), error)) {
        const std::string name = entry.path().filename().string();
        for (std::size_t family = 0; family < kScanFamilies.size(); ++family) {
            if (!name.starts_with(kScanFamilies[family])) {
                continue;
            }
            const auto suffix = std::ranges::find_if(
                kScanSuffixes, [&](std::string_view s) { return name.ends_with(s); });
            if (suffix == kScanSuffixes.end()) {
                continue;
            }
            std::string script =
                name.substr(kScanFamilies[family].size(),
                            name.size() - kScanFamilies[family].size() - suffix->size());
            const bool compact = script.ends_with(kCompactVariant);
            if (compact) {
                script.resize(script.size() - kCompactVariant.size());
            }
            if (script.empty()) {
                continue; // Latin families, covered by the primary font.
            }
            const int rank = static_cast<int>(family) * 2 + (compact ? 0 : 1);
            const auto [it, inserted] = scripts.try_emplace(script, Choice{name, rank});
            if (!inserted && rank < it->second.rank) {
                it->second = {name, rank};
            }
        }
    }
    std::vector<std::string> names;
    names.reserve(scripts.size());
    for (auto& [script, choice] : scripts) {
        names.push_back(std::move(choice.name));
    }
    return names;
}

} // namespace

Fonts loadFonts(ImFontAtlas& atlas, float sizePx, std::string_view language,
                std::string_view region, const EmojiSource& emoji) {
    g_emoji = &emoji;
    atlas.FontLoaderFlags |= kLoaderFlags;
    const int cjkFace = cjkFaceIndex(language, region);
    const std::vector<std::string> scripts = scanScriptFonts();
    const PrimaryFace primary =
        inspectPrimary(mapFont(std::string(kFontDirectory) + std::string(kVariableFont)));
    // One ImFont per weight, each carrying the same fallback chain.
    const auto load = [&](std::size_t weight) -> ImFont* {
        Loader loader(atlas, sizePx, cjkFace);
        const FT_Long instance = primary.instances[weight];
        const bool added = instance != 0
                               ? loader.add(kVariableFont, instance << 16)
                               : loader.add(kWeightFaces[weight].staticFont) ||
                                     loader.add(kVariableFont, 0, ImGuiFreeTypeLoaderFlags_Bold);
        if (!added) {
            return nullptr;
        }
        loader.addEmoji();
        for (const Candidates& group : kPreferredFallbacks) {
            loader.addFirst(group);
        }
        for (const std::string& name : scripts) {
            loader.add(name);
        }
        return loader.font();
    };

    Fonts fonts;
    for (std::size_t weight = 0; weight < kWeightCount; ++weight) {
        fonts.faces[weight] = load(weight);
    }
    ImFont*& regular = fonts.faces[static_cast<std::size_t>(Weight::Regular)];
    if (regular == nullptr) {
        ImFontConfig config;
        config.SizePixels = sizePx;
        regular = atlas.AddFontDefault(&config);
    }
    for (ImFont*& face : fonts.faces) {
        face = face != nullptr ? face : regular;
    }
    if (primary.lineSpacing > 0) {
        fonts.lineSpacing = primary.lineSpacing;
    }
    text::setArabicShapingEnabled(regular->IsGlyphInFont(text::kPresentationFormProbe));
    return fonts;
}

} // namespace sloptube::app
