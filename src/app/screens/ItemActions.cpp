// The ⋮ / ••• menu of a video tile or card: save to a playlist, download, share.

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

namespace sloptube::app {
namespace {

constexpr float kSheetTop = 24; // dp above the first row.
constexpr float kRow = 52;      // dp, as the official app's menu rows.

struct Action {
    Icon icon;
    Text label;
};

constexpr std::array kVideoActions{
    Action{Icon::Save, Text::SaveToPlaylist},
    Action{Icon::Download, Text::DownloadVideo},
    Action{Icon::Share, Text::Share},
};
constexpr std::array kOtherActions{
    Action{Icon::Share, Text::Share},
};

bool isVideo(const innertube::Item& item) {
    return item.kind == innertube::ItemKind::Video || item.kind == innertube::ItemKind::Short;
}

} // namespace

void App::openItemActions(innertube::Item item) {
    m_actionItem = std::move(item);
    openSheet(Sheet::ItemActions);
}

float App::itemActionsSheetHeight(const Canvas& c) const {
    const std::size_t rows = isVideo(m_actionItem) ? kVideoActions.size() : kOtherActions.size();
    return c.dp(kSheetTop) + c.dp(kRow) * static_cast<float>(rows) + c.dp(theme::kGutter);
}

void App::drawItemActionsSheet(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const float gutter = c.dp(theme::kGutter);
    const float row = c.dp(kRow);
    const std::span<const Action> actions = isVideo(m_actionItem)
                                                ? std::span<const Action>(kVideoActions)
                                                : std::span<const Action>(kOtherActions);
    float y = min.y + c.dp(kSheetTop);
    for (const Action& action : actions) {
        const ImVec2 rowMin{min.x, y};
        const ImVec2 rowMax{max.x, y + row};
        drawPressed(frame, rowMin, rowMax);
        const float centerY = y + row * 0.5f;
        c.icon(action.icon, {min.x + gutter * 2 + c.dp(theme::kIconSize) * 0.5f, centerY},
               theme::kIconSize, theme::kTextPrimary);
        c.line({min.x + gutter * 4 + c.dp(theme::kIconSize),
                centerY - c.lineHeight(theme::kBodySize) * 0.5f},
               max.x - min.x - gutter * 6 - c.dp(theme::kIconSize), theme::kBodySize,
               theme::kTextPrimary, tr(action.label));
        if (frame.touch.tapped(rowMin, rowMax)) {
            later([this, label = action.label, item = m_actionItem] {
                closeSheet();
                switch (label) {
                case Text::SaveToPlaylist:
                    openSave(item.id);
                    break;
                case Text::DownloadVideo:
                    openDownload(item);
                    break;
                default:
                    shareVideo(item, item.kind == innertube::ItemKind::Short);
                    break;
                }
            });
        }
        y += row;
    }
}

} // namespace sloptube::app
