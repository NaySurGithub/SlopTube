// Search: the query field, suggestions and past searches while typing, filters over the results.

#include "app/App.hpp"
#include "app/DataFiles.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"
#include "app/util/Files.hpp"

#include <imgui.h>
#include <imgui_internal.h>

namespace sloptube::app {
namespace {

// The search bar, as measured on the official app (dp).
constexpr float kBackX = 28;         // Centre of the back arrow.
constexpr float kFieldX = 56;        // Left edge of the field.
constexpr float kFieldRight = 96.5f; // Right edge of the field, from the screen's.
constexpr float kFieldHeight = 32;
constexpr float kFieldTextX = 13;      // From the field's edge to the text.
constexpr float kClearRight = 17.5f;   // Centre of the clear button, from the field's right edge.
constexpr float kClearIcon = 17;       // The clear button's ×.
constexpr float kFieldTextRise = 0.7f; // The text sits this much above the field's centre.
constexpr float kMicRight = 64.5f;     // Centre of the microphone, from the screen's right edge.
constexpr float kMoreRight = 22;       // Centre of "more".

constexpr std::size_t kHistorySize = 30;
constexpr std::size_t kHistoryShown = 10;     // Past searches listed under an empty field.
constexpr float kSuggestionHeight = 48;       // dp
constexpr float kFilterGroupTitleHeight = 28; // dp

// The filter menu's groups: a title and chips, each chip setting one choice.
struct FilterChip {
    Text label;
    bool (*selected)(const innertube::SearchFilters&);
    void (*toggle)(innertube::SearchFilters&);
};

struct FilterGroup {
    Text title;
    std::span<const FilterChip> chips;
};

using innertube::Duration;
using innertube::ResultType;
using innertube::SearchFilters;
using innertube::UploadDate;

template <auto Member, auto Value>
constexpr FilterChip choice(Text label) {
    return {label, [](const SearchFilters& f) { return f.*Member == Value; },
            [](SearchFilters& f) { f.*Member = f.*Member == Value ? decltype(Value){} : Value; }};
}

template <auto Member>
constexpr FilterChip flag(Text label) {
    return {label, [](const SearchFilters& f) { return f.*Member; },
            [](SearchFilters& f) { f.*Member = !(f.*Member); }};
}

constexpr std::array kSortChips{
    flag<&SearchFilters::popular>(Text::FilterPopularity),
};
constexpr std::array kDateChips{
    choice<&SearchFilters::date, UploadDate::LastHour>(Text::FilterLastHour),
    choice<&SearchFilters::date, UploadDate::Today>(Text::FilterToday),
    choice<&SearchFilters::date, UploadDate::ThisWeek>(Text::FilterThisWeek),
    choice<&SearchFilters::date, UploadDate::ThisMonth>(Text::FilterThisMonth),
    choice<&SearchFilters::date, UploadDate::ThisYear>(Text::FilterThisYear),
};
constexpr std::array kTypeChips{
    choice<&SearchFilters::type, ResultType::Video>(Text::FilterVideos),
    choice<&SearchFilters::type, ResultType::Short>(Text::Shorts),
    choice<&SearchFilters::type, ResultType::Channel>(Text::FilterChannels),
    choice<&SearchFilters::type, ResultType::Playlist>(Text::Playlists),
    choice<&SearchFilters::type, ResultType::Movie>(Text::FilterMovies),
};
constexpr std::array kDurationChips{
    choice<&SearchFilters::duration, Duration::UnderThreeMinutes>(Text::FilterUnderThree),
    choice<&SearchFilters::duration, Duration::ThreeToTwentyMinutes>(Text::FilterThreeToTwenty),
    choice<&SearchFilters::duration, Duration::OverTwentyMinutes>(Text::FilterOverTwenty),
};
constexpr std::array kFeatureChips{
    flag<&SearchFilters::live>(Text::LiveTab),
    flag<&SearchFilters::fourK>(Text::Filter4K),
    flag<&SearchFilters::hd>(Text::FilterHd),
    flag<&SearchFilters::subtitles>(Text::Subtitles),
};
constexpr std::array<FilterGroup, 5> kFilterGroups{{
    {Text::FilterSort, kSortChips},
    {Text::FilterUploadDate, kDateChips},
    {Text::FilterType, kTypeChips},
    {Text::FilterDuration, kDurationChips},
    {Text::FilterFeatures, kFeatureChips},
}};

} // namespace

void App::rememberSearch(const std::string& query) {
    std::erase(m_searchHistory, query);
    m_searchHistory.insert(m_searchHistory.begin(), query);
    if (m_searchHistory.size() > kHistorySize) {
        m_searchHistory.resize(kHistorySize);
    }
    std::string text;
    for (const auto& entry : m_searchHistory) {
        text += entry;
        text += '\n';
    }
    writeText(m_platform.dataDirectory() / kSearchHistoryFile, text);
}

void App::searchFor(const std::string& query) {
    // Run later, the search screen may have been left meanwhile.
    if (!m_stack.empty() && m_stack.back().route == Route::Search) {
        submitSearch(m_stack.back(), query);
    }
}

void App::submitSearch(Screen& screen, std::string query) {
    ImGui::ClearActiveID(); // Dismisses the keyboard.
    if (query.empty()) {
        return;
    }
    std::ranges::fill(m_query, '\0');
    std::ranges::copy(query.substr(0, m_query.size() - 1), m_query.begin());
    rememberSearch(query);
    screen.argument = query;
    screen.feed = std::make_shared<Feed>();
    load(screen.feed, [this, query, filters = m_filters] { return m_api.search(query, filters); });
}

void App::requestSuggestions(const std::string& query) {
    if (query == m_suggestedFor || !m_suggestPending.empty()) {
        return; // Up to date, or one request at a time: the next follows when it lands.
    }
    if (query.empty()) {
        m_suggestions.clear();
        m_suggestedFor.clear();
        return;
    }
    m_suggestPending = query;
    m_tasks.run([this, query] {
        auto suggestions = m_api.searchSuggestions(query);
        m_tasks.post([this, query, suggestions = std::move(suggestions)]() mutable {
            m_suggestPending.clear();
            m_suggestedFor = query;
            m_suggestions = suggestions ? std::move(*suggestions) : std::vector<std::string>{};
        });
    });
}

void App::drawSearch(Frame& frame, Screen& screen, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float bar = c.dp(theme::kTopBarHeight);
    const float centerY = min.y + bar * 0.5f;
    const ImVec2 contentMin{min.x, min.y + bar};

    // The field keeps focus while typing: suggestions replace the results until it is left.
    const bool typing = ImGui::GetActiveID() != 0 || screen.argument.empty();
    if (typing) {
        drawSearchSuggestions(frame, contentMin, max);
    } else if (screen.feed->loaded || screen.feed->loading) {
        ListHeader filters;
        if (m_filtersOpen) {
            filters = ListHeader{
                .height = m_searchFiltersHeight,
                .draw =
                    [this, &screen](Frame& f, ImVec2 origin, float w) {
                        m_searchFiltersHeight = drawSearchFilters(f, screen, origin, w);
                    },
            };
        }
        drawFeed(frame, screen.feed, ItemStyle::Card, contentMin, max, filters);
    }

    // The bar, as measured on the official app: back, the rounded field with its clear button,
    // the microphone in a circle, then more.
    c.rect({min.x, 0}, {max.x, contentMin.y}, theme::kBackground);
    if (iconButton(frame, Icon::Back, {min.x + c.dp(kBackX), centerY}, theme::kTextPrimary)) {
        later([this] { goBack(); });
    }
    // Filters apply to the results, so "more" opens them only then; it is lit while any filter
    // is on.
    const bool filtered = m_filters != innertube::SearchFilters{};
    if (iconButton(frame, Icon::MoreVertical, {max.x - c.dp(kMoreRight), centerY},
                   filtered ? theme::kAccent : theme::kTextPrimary) &&
        !typing) {
        m_filtersOpen = !m_filtersOpen;
        screen.feed->scroller.reset();
    }
    const ImVec2 mic{max.x - c.dp(kMicRight), centerY};
    c.list()->AddCircleFilled(mic, c.dp(kFieldHeight) * 0.5f, theme::kSurface);
    c.icon(Icon::Mic, mic, theme::kIconSize, theme::kTextPrimary);
    if (frame.touch.tapped({mic.x - c.dp(kFieldHeight) * 0.5f, min.y},
                           {mic.x + c.dp(kFieldHeight) * 0.5f, contentMin.y})) {
        m_platform.startVoiceSearch();
    }

    const float fieldX = min.x + c.dp(kFieldX);
    const float fieldRight = max.x - c.dp(kFieldRight);
    const float fieldHeight = c.dp(kFieldHeight);
    c.rect({fieldX, centerY - fieldHeight * 0.5f}, {fieldRight, centerY + fieldHeight * 0.5f},
           theme::kSurface, kFieldHeight * 0.5f);
    const bool hasQuery = m_query[0] != '\0';
    if (hasQuery) {
        const ImVec2 clear{fieldRight - c.dp(kClearRight), centerY};
        c.icon(Icon::Close, clear, kClearIcon, theme::kTextPrimary);
        if (frame.touch.tapped({clear.x - c.dp(kClearRight), clear.y - fieldHeight * 0.5f},
                               {fieldRight, clear.y + fieldHeight * 0.5f})) {
            std::ranges::fill(m_query, '\0');
            m_focusSearch = true;
        }
    }

    ImGui::PushFont(nullptr, c.dp(theme::kTitleSize));
    const ImVec2 padding = ImGui::GetStyle().FramePadding;
    const float textX = fieldX + c.dp(kFieldTextX);
    ImGui::SetCursorScreenPos(
        {textX - padding.x, centerY - ImGui::GetFrameHeight() * 0.5f - c.dp(kFieldTextRise)});
    ImGui::SetNextItemWidth(fieldRight - c.dp(kClearRight) * 2 - textX + padding.x);
    // The field draws only its text: the rounded background is ours.
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    if (m_focusSearch) {
        ImGui::SetKeyboardFocusHere();
        m_focusSearch = false;
    }
    const bool submitted = ImGui::InputTextWithHint(
        "##query", tr(Text::SearchHint).data(), m_query.data(), m_query.size(),
        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleColor(3);
    ImGui::PopFont();

    const std::string query(m_query.data());
    if (submitted) {
        later([this, query] { searchFor(query); });
    } else if (typing) {
        requestSuggestions(query);
    }
}

void App::drawSearchSuggestions(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float row = c.dp(kSuggestionHeight);
    const float gutter = c.dp(theme::kGutter);
    const std::string query(m_query.data());

    // Past searches matching what is typed come first, marked with a clock, then suggestions.
    struct Entry {
        std::string_view text;
        bool past;
    };

    std::vector<Entry> entries;
    for (const auto& past : m_searchHistory) {
        if (entries.size() < kHistoryShown && past.starts_with(query)) {
            entries.push_back({past, true});
        }
    }
    if (!query.empty() && m_suggestedFor == query) {
        for (const auto& suggestion : m_suggestions) {
            if (std::ranges::find(m_searchHistory, suggestion) == m_searchHistory.end()) {
                entries.push_back({suggestion, false});
            }
        }
    }

    c.list()->PushClipRect(min, max, true);
    float y = min.y;
    for (const Entry& entry : entries) {
        if (y > max.y) {
            break;
        }
        const ImVec2 rowMax{max.x, y + row};
        const float centerY = y + row * 0.5f;
        const float iconX = min.x + gutter + c.dp(theme::kIconSize) * 0.5f;
        drawPressed(frame, {min.x, y}, rowMax);
        c.icon(entry.past ? Icon::History : Icon::Search, {iconX, centerY}, theme::kIconSize,
               theme::kTextSecondary);
        const float textX = iconX + c.dp(theme::kIconSize) * 0.5f + gutter;
        const float insert = c.dp(theme::kIconSize) + gutter * 2;
        c.line({textX, centerY - c.lineHeight(theme::kBodySize) * 0.5f}, max.x - insert - textX,
               theme::kBodySize, theme::kTextPrimary, entry.text);
        // The arrow puts the suggestion in the field to go on typing; the row searches it.
        const ImVec2 arrow{max.x - gutter - c.dp(theme::kIconSize) * 0.5f, centerY};
        if (iconButton(frame, Icon::InsertUp, arrow, theme::kTextSecondary)) {
            ImGui::ClearActiveID(); // The field reloads the text when it takes focus again.
            std::ranges::fill(m_query, '\0');
            std::ranges::copy(entry.text.substr(0, m_query.size() - 2), m_query.begin());
            m_query[std::min(entry.text.size(), m_query.size() - 2)] = ' ';
            m_focusSearch = true;
        } else if (frame.touch.tapped({min.x, y}, {max.x - insert, rowMax.y})) {
            later([this, text = std::string(entry.text)] { searchFor(text); });
        }
        y += row;
    }
    c.list()->PopClipRect();
}

float App::drawSearchFilters(Frame& frame, Screen& screen, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float chip = c.dp(theme::kChipHeight);
    float y = origin.y + gutter * 0.5f;
    std::optional<innertube::SearchFilters> changed;
    for (const FilterGroup& group : kFilterGroups) {
        medium.text({origin.x + gutter,
                     y + (c.dp(kFilterGroupTitleHeight) - c.lineHeight(theme::kCaptionSize))},
                    theme::kCaptionSize, theme::kTextSecondary, tr(group.title));
        y += c.dp(kFilterGroupTitleHeight) + gutter * 0.5f;
        float x = origin.x + gutter;
        for (const FilterChip& filter : group.chips) {
            // Chips wrap to a new line at the edge.
            const float chipWidth =
                medium.measure(theme::kBodySize, tr(filter.label)).x + gutter * 2;
            if (x > origin.x + gutter && x + chipWidth > origin.x + width - gutter) {
                x = origin.x + gutter;
                y += chip + gutter * 0.5f;
            }
            if (chipButton(frame, tr(filter.label), {x, y}, filter.selected(m_filters), x)) {
                changed = m_filters;
                filter.toggle(*changed);
            }
        }
        y += chip + gutter * 0.5f;
    }
    if (m_filters != innertube::SearchFilters{}) {
        float x = origin.x + gutter;
        if (chipButton(frame, tr(Text::ClearFilters), {x, y}, false, x)) {
            changed = innertube::SearchFilters{};
        }
        y += chip + gutter * 0.5f;
    }
    y += gutter;
    if (changed) {
        later([this, filters = *changed, query = screen.argument] {
            m_filters = filters;
            searchFor(query);
        });
    }
    return y - origin.y;
}

} // namespace sloptube::app
