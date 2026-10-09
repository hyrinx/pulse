#include "ui_renderer_internal.h"
#include "pane_header_icons.h"
#include "command_icons.h"

namespace pulse::ui {

void MainRenderer::DrawPaneHeaderIcon(const D2D1_RECT_F& rc, PaneHeaderIcon icon,
                                      const D2D1_COLOR_F& color) {
    auto* dc = compositor_ ? compositor_->Dc() : nullptr;
    if (!dc) return;
    if (!paneHeaderStroke_.get()) command_icons::CreateStrokeStyle(dc, &paneHeaderStroke_);
    using I = command_icons::Icon;
    I command = I::None;
    switch (icon) {
    case PaneHeaderIcon::Back: command = I::Back; break;
    case PaneHeaderIcon::Forward: command = I::Forward; break;
    case PaneHeaderIcon::Up: command = I::Up; break;
    case PaneHeaderIcon::Columns: command = I::Columns; break;
    case PaneHeaderIcon::MediumIcons: command = I::Grid; break;
    case PaneHeaderIcon::More: command = I::More; break;
    case PaneHeaderIcon::Split: command = I::Split; break;
    case PaneHeaderIcon::SplitSingle: command = I::SplitSingle; break;
    case PaneHeaderIcon::SplitStacked: command = I::SplitStacked; break;
    case PaneHeaderIcon::SplitThree: command = I::SplitThree; break;
    case PaneHeaderIcon::SplitFour: command = I::SplitFour; break;
    case PaneHeaderIcon::View: command = I::List; break;
    case PaneHeaderIcon::Filter: command = I::Filter; break;
    case PaneHeaderIcon::SwapHorizontal: command = I::SwapHorizontal; break;
    case PaneHeaderIcon::SwapVertical: command = I::SwapVertical; break;
    }
    MakeBrush(dc, color, brText_);
    command_icons::Draw(dc, brText_.get(), paneHeaderStroke_.get(), command,
                        command_icons::CenteredBounds(rc, 18.0f * scale_));
}

} // namespace pulse::ui
