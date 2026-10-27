// Sheets sliding over the current screen, and transient toasts.

#include "app/App.hpp"
#include "app/ui/Motion.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

#include <imgui_internal.h>

namespace sloptube::app {
namespace {

constexpr float kToastDuration = 2.5f; // Seconds on screen, fades included.
constexpr float kToastBottom = 72;     // dp above the bottom edge.
constexpr float kDismissDrag = 96;     // dp of downward drag that closes a sheet.
constexpr float kHandleWidth = 36;
constexpr float kHandleHeight = 4;
constexpr float kSheetHeaderHeight = 56;
constexpr ImU32 kHandle = IM_COL32(0xff, 0xff, 0xff, 0x40);

} // namespace

double App::now() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - m_start).count();
}

void App::openSheet(Sheet sheet) {
    m_sheet = SheetState{.kind = sheet, .openedAt = now()};
}

void App::closeSheet() {
    if (m_sheet.kind != Sheet::None && m_sheet.closingAt < 0) {
        m_sheet.closingAt = now();
        ImGui::ClearActiveID(); // Drops the keyboard with the sheet.
    }
}

void App::showToast(std::string_view text) {
    m_toast = Toast{.text = std::string(text), .shownAt = now()};
}

void App::drawSheet(Frame& frame, ImVec2 min, ImVec2 max) {
    if (m_sheet.kind == Sheet::None) {
        return;
    }
    const Canvas& c = frame.canvas;
    float shown = motion::easeOutCubic(
        motion::progress(frame.time, m_sheet.openedAt, motion::kSheetDuration));
    if (m_sheet.closingAt >= 0) {
        const float closing =
            motion::progress(frame.time, m_sheet.closingAt, motion::kSheetDuration);
        shown = std::min(shown, 1 - motion::easeInOutCubic(closing));
        if (closing >= 1) {
            m_sheet = SheetState{};
            m_watch.replies.reset();
            m_watch.replyParent.reset();
            return;
        }
    }
    if (shown < 1) {
        frame.animating = true;
    }

    // Modal sheets cover the whole screen with a scrim and size to their content; video sheets
    // fill the area under the player.
    const bool modal = m_sheet.modal();
    if (modal) {
        c.rect({0, 0}, {max.x, max.y + m_insets.bottom},
               motion::withAlpha(theme::kScrim, shown * 1.5f));
        if (m_sheet.closingAt < 0 && frame.touch.tapped({0, 0}, {max.x, min.y})) {
            closeSheet();
        }
    }

    // Swipe down on the header to dismiss.
    const ImVec2 header{max.x, min.y + c.dp(kSheetHeaderHeight)};
    float drag = 0;
    if (frame.touch.dragging() && frame.touch.startedIn(min, header)) {
        drag = std::max(0.0f, frame.touch.position().y - frame.touch.start().y);
    } else if (frame.touch.flingVelocity() && frame.touch.startedIn(min, header) &&
               frame.touch.position().y - frame.touch.start().y > c.dp(kDismissDrag)) {
        closeSheet();
    }

    motion::Layer layer(c.list());
    const float height = max.y + (modal ? m_insets.bottom : 0) - min.y;
    c.list()->AddRectFilled(min, {max.x, max.y + (modal ? m_insets.bottom : 0)},
                            modal ? theme::kSurfaceRaised : theme::kBackground,
                            c.dp(theme::kSheetRadius), ImDrawFlags_RoundCornersTop);
    c.rect({(min.x + max.x - c.dp(kHandleWidth)) * 0.5f, min.y + c.dp(8)},
           {(min.x + max.x + c.dp(kHandleWidth)) * 0.5f, min.y + c.dp(8 + kHandleHeight)}, kHandle,
           kHandleHeight);
    switch (m_sheet.kind) {
    case Sheet::Comments:
        drawCommentsSheet(frame, min, max);
        break;
    case Sheet::Description:
        drawDescriptionSheet(frame, min, max);
        break;
    case Sheet::Accounts:
        drawAccountsSheet(frame, min, max);
        break;
    case Sheet::Save:
        drawSaveSheet(frame, min, max);
        break;
    case Sheet::Download:
        drawDownloadSheet(frame, min, max);
        break;
    case Sheet::None:
        break;
    }
    layer.end({0, (1 - shown) * height + drag});
}

float App::modalSheetHeight(const Canvas& c, float available) const {
    float height = 0;
    switch (m_sheet.kind) {
    case Sheet::Save:
        height = saveSheetHeight(c, available);
        break;
    case Sheet::Download:
        height = downloadSheetHeight(c);
        break;
    default:
        height = accountsSheetHeight(c);
        break;
    }
    return std::min(height, available);
}

void App::drawToast(Frame& frame, ImVec2 min, ImVec2 max) {
    if (m_toast.shownAt < 0) {
        return;
    }
    const float age = static_cast<float>(frame.time - m_toast.shownAt);
    if (age > kToastDuration) {
        m_toast = Toast{};
        return;
    }
    frame.animating = true;
    const float alpha = std::min(
        {1.0f, age / motion::kFadeDuration, (kToastDuration - age) / motion::kFadeDuration});
    const Canvas& c = frame.canvas;
    const float padding = c.dp(theme::kGutter);
    const float width = std::min(c.measure(theme::kBodySize, m_toast.text).x + padding * 2,
                                 max.x - min.x - padding * 2);
    const float height = c.lineHeight(theme::kBodySize) + padding * 1.5f;
    const float lift = (1 - motion::easeOutCubic(alpha)) * c.dp(16);
    const ImVec2 boxMin{(min.x + max.x - width) * 0.5f, max.y - c.dp(kToastBottom) - height + lift};
    motion::Layer layer(c.list());
    c.rect(boxMin, {boxMin.x + width, boxMin.y + height}, theme::kToast, theme::kCornerRadius);
    c.line({boxMin.x + padding, boxMin.y + padding * 0.75f}, width - padding * 2, theme::kBodySize,
           theme::kTextOnLight, m_toast.text, TextAlign::Center);
    layer.end({}, alpha);
}

} // namespace sloptube::app
