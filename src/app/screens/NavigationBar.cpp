// Bottom navigation bar, measured on the official app (Galaxy A14, 2.8125 px/dp): five equal
// slots, the centre one a round "create" button; outlined icons, filled when selected; white
// labels for every tab; the signed-in account's photo for "You".

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

#include <optional>

namespace sloptube::app {
namespace {

struct NavSlot {
    Glyph outline;
    Glyph filled;
    Text label;
    std::optional<Tab> tab; // None: the create button.
};

constexpr std::array kSlots{
    NavSlot{Glyph::HomeOutline, Glyph::HomeFilled, Text::Home, Tab::Home},
    NavSlot{Glyph::ShortsOutline, Glyph::ShortsFilled, Text::Shorts, Tab::Shorts},
    NavSlot{Glyph::Create, Glyph::Create, Text::Count, std::nullopt},
    NavSlot{Glyph::SubscriptionsOutline, Glyph::SubscriptionsFilled, Text::Subscriptions,
            Tab::Subscriptions},
    NavSlot{Glyph::YouOutline, Glyph::YouFilled, Text::You, Tab::You},
};

constexpr float kIconCenter = 17.5f; // dp from the top of the bar.
constexpr float kLabelCenter = 37;   // dp
constexpr float kCreateSize = 36;    // dp, diameter of the create button.
constexpr float kAvatarRing = 1.5f;  // dp, ring around the account photo when "You" is selected.
constexpr float kRingGap = 1.5f;     // dp between the photo and its ring.
constexpr ImU32 kCreateFill = IM_COL32(0x26, 0x26, 0x26, 0xff);

} // namespace

void App::drawNavigationBar(Frame& frame, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const float height = c.dp(theme::kBottomBarHeight);
    const ImVec2 max{origin.x + width, origin.y + height + m_insets.bottom};
    c.rect(origin, max, theme::kBackground);
    c.rect(origin, {max.x, origin.y + c.dp(1)}, theme::kDivider);

    const auto accounts = m_session.accounts();
    const float slot = width / static_cast<float>(kSlots.size());
    for (std::size_t index = 0; index < kSlots.size(); ++index) {
        const NavSlot& item = kSlots[index];
        const float x = origin.x + slot * static_cast<float>(index);
        const ImVec2 slotMin{x, origin.y};
        const ImVec2 slotMax{x + slot, origin.y + height};
        const float centerX = x + slot * 0.5f;

        if (!item.tab) {
            const ImVec2 center{centerX, origin.y + height * 0.5f};
            const float radius = c.dp(kCreateSize) * 0.5f;
            const bool pressed = frame.touch.pressed(slotMin, slotMax);
            c.list()->AddCircleFilled(center, radius,
                                      pressed ? theme::kSurfacePressed : kCreateFill);
            m_icons->draw(c.list(), Glyph::Create, center, theme::kTextPrimary);
            if (frame.touch.tapped(slotMin, slotMax)) {
                later([this] { showToast(tr(Text::CreateUnavailable)); });
            }
            continue;
        }

        const bool selected = m_tab == *item.tab;
        const ImVec2 iconCenter{centerX, origin.y + c.dp(kIconCenter)};
        if (frame.touch.pressed(slotMin, slotMax)) {
            c.list()->AddCircleFilled(iconCenter, c.dp(theme::kIconSize), theme::kSurfacePressed);
            frame.animating = true;
        }
        if (*item.tab == Tab::You && !accounts.empty()) {
            const auto& me = accounts[std::min(m_session.activeAccount(), accounts.size() - 1)];
            drawAvatar(frame, me.photo, me.name, iconCenter, theme::kIconSize);
            if (selected) {
                c.list()->AddCircle(iconCenter,
                                    c.dp(theme::kIconSize * 0.5f + kRingGap + kAvatarRing * 0.5f),
                                    theme::kTextPrimary, 0, c.dp(kAvatarRing));
            }
        } else {
            m_icons->draw(c.list(), selected ? item.filled : item.outline, iconCenter,
                          theme::kTextPrimary);
        }
        const std::string_view label = tr(item.label);
        const ImVec2 extent = c.measure(theme::kNavLabelSize, label);
        c.text({centerX - extent.x * 0.5f, origin.y + c.dp(kLabelCenter) - extent.y * 0.5f},
               theme::kNavLabelSize, theme::kTextPrimary, label);
        if (frame.touch.tapped(slotMin, slotMax)) {
            later([this, tab = *item.tab] { selectTab(tab); });
        }
    }
}

} // namespace sloptube::app
