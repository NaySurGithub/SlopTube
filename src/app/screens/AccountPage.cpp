// Signed-in identity: the "You" header and the account switcher.

#include "app/App.hpp"
#include "app/ui/Strings.hpp"
#include "app/ui/Theme.hpp"

namespace sloptube::app {
namespace {

constexpr float kHeaderAvatar = 64;   // dp
constexpr float kAccountRow = 64;     // dp
constexpr float kSwitcherAvatar = 40; // dp
constexpr float kActionRow = 56;      // dp
constexpr float kSheetTop = 24;       // dp

} // namespace

void App::refreshProfile() {
    if (!m_session.signedIn()) {
        return;
    }
    const auto account = m_session.activeAccount();
    m_tasks.run([this, account] {
        auto accounts = m_api.accounts();
        if (!accounts || accounts->empty()) {
            return;
        }
        m_tasks.post([this, account, profile = std::move(accounts->front())]() mutable {
            if (m_session.activeAccount() == account) {
                m_session.setProfile(std::move(profile));
            }
        });
    });
}

void App::switchAccount(std::size_t index) {
    if (index == m_session.activeAccount()) {
        return;
    }
    m_session.switchAccount(index);
    reloadAccountFeeds();
    refreshProfile();
}

float App::drawAccountHeader(Frame& frame, ImVec2 origin, float width) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float avatar = c.dp(kHeaderAvatar);
    const auto accounts = m_session.accounts();
    if (accounts.empty()) {
        return 0;
    }
    const auto& me = accounts[std::min(m_session.activeAccount(), accounts.size() - 1)];

    float y = origin.y + gutter;
    drawAvatar(frame, me.photo, me.name, {origin.x + gutter + avatar * 0.5f, y + avatar * 0.5f},
               kHeaderAvatar);
    const float textX = origin.x + gutter * 2 + avatar;
    const float textWidth = origin.x + width - gutter - textX;
    const float block = c.lineHeight(theme::kHeadlineSize) + c.lineHeight(theme::kCaptionSize);
    const float textTop = y + (avatar - block) * 0.5f;
    c.with(theme::kHeadingWeight)
        .line({textX, textTop}, textWidth, theme::kHeadlineSize, theme::kTextPrimary,
              me.name.empty() ? me.byline : me.name);
    c.line({textX, textTop + c.lineHeight(theme::kHeadlineSize)}, textWidth, theme::kCaptionSize,
           theme::kTextSecondary, me.handle.empty() ? me.byline : me.handle);
    y += avatar + gutter;

    // "Switch account" chip, as under the profile in the official app.
    const float chip = c.dp(theme::kChipHeight);
    const std::string_view label = tr(Text::SwitchAccount);
    const float chipWidth =
        c.dp(theme::kIconSize) + gutter * 2.5f + medium.measure(theme::kCaptionSize, label).x;
    const ImVec2 chipMin{origin.x + gutter, y};
    const ImVec2 chipMax{chipMin.x + chipWidth, y + chip};
    c.rect(chipMin, chipMax, theme::kSurface, theme::kPillRadius);
    drawPressed(frame, chipMin, chipMax, theme::kPillRadius);
    c.icon(Icon::SwitchAccount, {chipMin.x + gutter + c.dp(8), y + chip * 0.5f}, 16,
           theme::kTextPrimary);
    medium.text({chipMin.x + gutter * 1.5f + c.dp(16),
                 y + (chip - c.lineHeight(theme::kCaptionSize)) * 0.5f},
                theme::kCaptionSize, theme::kTextPrimary, label);
    if (frame.touch.tapped(chipMin, chipMax)) {
        later([this] { openSheet(Sheet::Accounts); });
    }
    y += chip + gutter;
    return y - origin.y;
}

float App::accountsSheetHeight(const Canvas& c) const {
    constexpr int kActions = 2; // Add account, sign out.
    return c.dp(kSheetTop) + c.lineHeight(theme::kSheetTitleSize) + c.dp(theme::kGutter) +
           c.dp(kAccountRow) * static_cast<float>(m_session.accounts().size()) + c.dp(4) +
           c.dp(kActionRow) * kActions + c.dp(theme::kGutter);
}

void App::drawAccountsSheet(Frame& frame, ImVec2 min, ImVec2 max) {
    const Canvas& c = frame.canvas;
    const Canvas medium = c.with(Weight::Medium);
    const float gutter = c.dp(theme::kGutter);
    const float row = c.dp(kAccountRow);
    const float avatar = c.dp(kSwitcherAvatar);
    const auto accounts = m_session.accounts();

    float y = min.y + c.dp(kSheetTop);
    c.with(theme::kHeadingWeight)
        .text({min.x + gutter * 1.5f, y}, theme::kSheetTitleSize, theme::kTextPrimary,
              tr(Text::Accounts));
    y += c.lineHeight(theme::kSheetTitleSize) + gutter;

    for (std::size_t i = 0; i < accounts.size(); ++i) {
        const auto& account = accounts[i];
        const ImVec2 rowMin{min.x, y};
        const ImVec2 rowMax{max.x, y + row};
        drawPressed(frame, rowMin, rowMax);
        const float centerY = y + row * 0.5f;
        drawAvatar(frame, account.photo, account.name,
                   {min.x + gutter * 1.5f + avatar * 0.5f, centerY}, kSwitcherAvatar);
        const float textX = min.x + gutter * 3 + avatar;
        const float textWidth = max.x - textX - gutter * 4;
        const float block = c.lineHeight(theme::kBodySize) + c.lineHeight(theme::kCaptionSize) * 2;
        medium.line({textX, centerY - block * 0.5f}, textWidth, theme::kBodySize,
                    theme::kTextPrimary, account.name.empty() ? account.byline : account.name);
        c.line({textX, centerY - block * 0.5f + c.lineHeight(theme::kBodySize)}, textWidth,
               theme::kCaptionSize, theme::kTextSecondary, account.handle);
        c.line({textX, centerY - block * 0.5f + c.lineHeight(theme::kBodySize) +
                           c.lineHeight(theme::kCaptionSize)},
               textWidth, theme::kCaptionSize, theme::kTextSecondary, account.byline);
        if (i == m_session.activeAccount()) {
            c.icon(Icon::Check, {max.x - gutter * 2.5f, centerY}, theme::kIconSize, theme::kLink);
        }
        if (frame.touch.tapped(rowMin, rowMax)) {
            later([this, i] {
                closeSheet();
                switchAccount(i);
            });
        }
        y += row;
    }

    const auto action = [&](Icon icon, Text label) {
        const ImVec2 rowMin{min.x, y};
        const ImVec2 rowMax{max.x, y + c.dp(kActionRow)};
        drawPressed(frame, rowMin, rowMax);
        const float centerY = y + c.dp(kActionRow) * 0.5f;
        c.icon(icon, {min.x + gutter * 1.5f + avatar * 0.5f, centerY}, theme::kIconSize,
               theme::kTextPrimary);
        c.text({min.x + gutter * 3 + avatar, centerY - c.lineHeight(theme::kBodySize) * 0.5f},
               theme::kBodySize, theme::kTextPrimary, tr(label));
        y += c.dp(kActionRow);
        return frame.touch.tapped(rowMin, rowMax);
    };
    c.rect({min.x, y}, {max.x, y + c.dp(1)}, theme::kDivider);
    y += c.dp(4);
    if (action(Icon::Add, Text::AddAccount)) {
        later([this] {
            closeSheet();
            beginSignIn();
        });
    }
    if (action(Icon::Back, Text::SignOut)) {
        later([this] {
            closeSheet();
            m_session.signOut();
            reloadAccountFeeds();
            refreshProfile();
        });
    }
}

} // namespace sloptube::app
