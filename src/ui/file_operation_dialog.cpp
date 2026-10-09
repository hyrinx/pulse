#include "../common/windows_compat.h"
#include "file_operation_dialog.h"
#include "../ops/operation_presentation.h"
#include "../common/localization.h"
#include "../common/text_format.h"
#include "typography.h"
#include "window_helpers.h"

#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <string>

namespace pulse::ui {
namespace {

constexpr wchar_t kTransferClass[] = L"PulseFileOperationWindow";
constexpr wchar_t kConflictClass[] = L"PulseFileConflictWindow";
constexpr UINT_PTR kRenderTimer = 1;
constexpr float kDlgW = 460.0f;
constexpr float kTitleH = 36.0f;
constexpr float kPadX = 20.0f;
constexpr float kFooterH = 52.0f;
constexpr float kCollapsedH = 268.0f;
constexpr float kGraphBlockH = 156.0f;
constexpr float kDetailedH = kCollapsedH + kGraphBlockH;

using pulse::ui::ScaleDip;

struct TransferChrome {
    D2D1_RECT_F minimize{};
    D2D1_RECT_F close{};
    D2D1_RECT_F details{};
    D2D1_RECT_F pause{};
    D2D1_RECT_F cancel{};
    D2D1_RECT_F retry{};
    D2D1_RECT_F skip{};
};

struct TransferLabels {
    std::wstring details;
    std::wstring pause;
    std::wstring cancel;
    std::wstring retry;
    std::wstring skip;
    bool show_details = true;
    bool show_retry = false;
    bool show_skip = false;
    bool actions_enabled = true;   // false while a retry is in flight
    std::wstring retry_reserve;    // widest retry label, keeps the layout fixed
    bool show_pause = true;
};

TransferLabels MakeTransferLabels(const ops::OpStatus& status, bool detailed) {
    TransferLabels labels;
    labels.details = detailed ? l10n::Get(l10n::StringId::OpSummary)
                              : l10n::Get(l10n::StringId::OpDetails);
    labels.pause = status.phase == ops::OpPhase::Paused
        ? l10n::Get(l10n::StringId::OpResume) : l10n::Get(l10n::StringId::OpPause);
    const bool emptying = status.type == ops::OpType::EmptyRecycle;
    const bool authorization = status.authorization != ops::AuthorizationState::None;
    labels.show_details = !authorization;
    labels.show_retry = status.active && status.authorization == ops::AuthorizationState::ActionRequired
        && status.phase != ops::OpPhase::Cancelling;
    labels.show_skip = labels.show_retry && status.can_skip_authorization;
    labels.actions_enabled = !status.authorization_retrying;
    labels.retry = l10n::Get(l10n::StringId::OpRetryAuthorization);
    const std::wstring retrying = l10n::Pick(L"正在重试…", L"Retrying…");
    labels.retry_reserve = retrying.size() > labels.retry.size() ? retrying : labels.retry;
    if (!labels.actions_enabled) labels.retry = retrying;
    labels.skip = l10n::Get(l10n::StringId::OpSkipAuthorization);
    labels.show_pause = !authorization && !emptying && status.active && status.can_pause;
    labels.cancel = status.active && !emptying
        ? l10n::Get(l10n::StringId::Cancel) : l10n::Get(l10n::StringId::Close);
    return labels;
}

TransferChrome MakeTransferChrome(float scale, float width, float height,
                                  const fluent::Painter& painter, const TransferLabels& labels) {
    TransferChrome chrome;
    const float title = ScaleDip(scale, kTitleH);
    const float caption = ScaleDip(scale, 40.0f);
    chrome.close = D2D1::RectF(width - caption, 0, width, title);
    chrome.minimize = D2D1::RectF(width - caption * 2.0f, 0, width - caption, title);
    const float footer_h = ScaleDip(scale, kFooterH);
    const float footer_top = height - footer_h;
    const float pad = ScaleDip(scale, kPadX);
    const float gap = ScaleDip(scale, 8.0f);
    const float btn_h = painter.MeasureButtonHeight();
    const float y0 = footer_top + (footer_h - btn_h) * 0.5f;
    const float y1 = y0 + btn_h;

    const float cancel_w = painter.MeasureButtonWidth(labels.cancel);
    chrome.cancel = D2D1::RectF(width - pad - cancel_w, y0, width - pad, y1);
    if (labels.show_pause) {
        const float pause_w = painter.MeasureButtonWidth(labels.pause);
        chrome.pause = D2D1::RectF(chrome.cancel.left - gap - pause_w, y0,
                                   chrome.cancel.left - gap, y1);
    }
    float next_right = chrome.cancel.left - gap;
    if (labels.show_skip) {
        const float skip_w = painter.MeasureButtonWidth(labels.skip);
        chrome.skip = D2D1::RectF(next_right - skip_w, y0, next_right, y1);
        next_right = chrome.skip.left - gap;
    }
    if (labels.show_retry) {
        const float retry_w = (std::max)(painter.MeasureButtonWidth(labels.retry, L"\xEA18"),
                                         painter.MeasureButtonWidth(labels.retry_reserve, L"\xEA18"));
        chrome.retry = D2D1::RectF(next_right - retry_w, y0, next_right, y1);
    }
    if (labels.show_details) chrome.details = painter.FitButtonBounds(D2D1::RectF(pad, y0, pad, y1),
                                             labels.details, L"\xE70D");
    return chrome;
}

void DrawEllipsizedText(Compositor& compositor, IDWriteTextFormat* format,
                        const D2D1_RECT_F& bounds, const std::wstring& text,
                        const D2D1_COLOR_F& color, bool wrap = false) {
    auto* factory = compositor.DwriteFactory();
    auto* dc = compositor.Dc();
    if (!factory || !dc || !format || text.empty()) return;
    const float width = bounds.right - bounds.left;
    const float height = bounds.bottom - bounds.top;
    if (width <= 0.0f || height <= 0.0f) return;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()),
                                         format, width, height, &layout))) return;
    layout->SetWordWrapping(wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    ComPtr<IDWriteInlineObject> ellipsis;
    if (SUCCEEDED(factory->CreateEllipsisTrimmingSign(format, &ellipsis))) {
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        layout->SetTrimming(&trimming, ellipsis.get());
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(dc->CreateSolidColorBrush(color, &brush))) return;
    dc->DrawTextLayout(D2D1::Point2F(bounds.left, bounds.top), layout.get(), brush.get(),
                       D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void DrawSpeedGraph(ID2D1DeviceContext* dc, const D2D1_RECT_F& plot,
                    const std::deque<double>& history, D2D1_COLOR_F accent, float scale) {
    if (!dc) return;
    ComPtr<ID2D1SolidColorBrush> grid;
    D2D1_COLOR_F grid_color = accent;
    grid_color.a = 0.08f;
    dc->CreateSolidColorBrush(grid_color, &grid);
    const float step = ScaleDip(scale, 20.0f);
    for (float y = plot.top + ScaleDip(scale, 12.0f); y < plot.bottom; y += step) {
        dc->DrawLine(D2D1::Point2F(plot.left, y), D2D1::Point2F(plot.right, y),
                     grid.get(), 1.0f);
    }
    if (history.size() < 2) return;

    const double max_speed = (std::max)(1.0, *std::max_element(history.begin(), history.end()));
    const float left = plot.left;
    const float right = plot.right;
    const float top = plot.top + ScaleDip(scale, 6.0f);
    const float bottom = plot.bottom;
    const size_t count = history.size();
    auto y_at = [&](size_t index) {
        return bottom - (bottom - top) *
            static_cast<float>(history[index] / max_speed);
    };
    auto x_at = [&](size_t index) {
        return left + (right - left) * static_cast<float>(index)
            / static_cast<float>(count - 1);
    };

    ID2D1Factory* factory = nullptr;
    dc->GetFactory(&factory);
    if (factory) {
        ComPtr<ID2D1PathGeometry> geometry;
        if (SUCCEEDED(factory->CreatePathGeometry(&geometry))) {
            ComPtr<ID2D1GeometrySink> sink;
            if (SUCCEEDED(geometry->Open(&sink))) {
                sink->BeginFigure(D2D1::Point2F(x_at(0), y_at(0)), D2D1_FIGURE_BEGIN_FILLED);
                for (size_t i = 1; i < count; ++i)
                    sink->AddLine(D2D1::Point2F(x_at(i), y_at(i)));
                sink->AddLine(D2D1::Point2F(x_at(count - 1), bottom));
                sink->AddLine(D2D1::Point2F(x_at(0), bottom));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                sink->Close();
                D2D1_COLOR_F fill_color = accent;
                fill_color.a = 0.28f;
                ComPtr<ID2D1SolidColorBrush> fill;
                dc->CreateSolidColorBrush(fill_color, &fill);
                dc->FillGeometry(geometry.get(), fill.get());
            }
        }
        factory->Release();
    }

    ComPtr<ID2D1SolidColorBrush> line;
    dc->CreateSolidColorBrush(accent, &line);
    for (size_t i = 1; i < count; ++i) {
        dc->DrawLine(D2D1::Point2F(x_at(i - 1), y_at(i - 1)),
                     D2D1::Point2F(x_at(i), y_at(i)), line.get(), 2.0f * scale);
    }
    const float last_x = x_at(count - 1);
    const float last_y = y_at(count - 1);
    dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(last_x, last_y),
                                  3.5f * scale, 3.5f * scale), line.get());
}

D2D1_RECT_F Rect(float scale, float x, float y, float width, float height) {
    return pulse::ui::DipRect(scale, x, y, width, height);
}

std::wstring LeafName(const std::wstring& path) {
    std::wstring_view view = path;
    while (view.size() > 1 && (view.back() == L'\\' || view.back() == L'/'))
        view.remove_suffix(1);
    const size_t slash = view.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? std::wstring(view)
                                             : std::wstring(view.substr(slash + 1));
}

std::wstring ParentName(const std::wstring& path) {
    std::wstring copy = path;
    while (copy.size() > 1 && (copy.back() == L'\\' || copy.back() == L'/')) copy.pop_back();
    const size_t slash = copy.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return copy;
    return LeafName(copy.substr(0, slash));
}

// Speed comes in one of two units: bytes for copy/move/empty-recycle, items for
// delete/restore. Formatting in one place keeps the current readout and the peak
// label in step with whichever unit the task actually tracks.
std::wstring FormatSpeedText(ops::OpSpeedBasis basis, double value) {
    if (basis != ops::OpSpeedBasis::Items)
        return pulse::format::ByteSize(static_cast<uint64_t>(value)) + L"/s";
    // Item rates are small: deleting a folder tree of many files reports one item per
    // several seconds, so rounding to whole items would print a flat "0 items/s" while
    // the graph visibly moves. Keep decimals below 10 and trim the trailing zeros.
    if (value >= 10.0)
        return pulse::format::GroupedInt(static_cast<uint64_t>(value + 0.5)) + L" " +
            l10n::Get(l10n::StringId::OpItemsPerSecond);
    wchar_t text[32]{};
    if (value >= 1.0) swprintf_s(text, L"%.1f", value);
    else if (value >= 0.01) swprintf_s(text, L"%.2f", value);
    else wcscpy_s(text, L"0");
    std::wstring number = text;
    if (number.find(L'.') != std::wstring::npos) {
        while (!number.empty() && number.back() == L'0') number.pop_back();
        if (!number.empty() && number.back() == L'.') number.pop_back();
    }
    return number + L" " + l10n::Get(l10n::StringId::OpItemsPerSecond);
}

std::wstring FormatDuration(uint64_t seconds) {
    if (seconds == 0) return l10n::Get(l10n::StringId::OpEstimating).c_str();
    if (seconds < 60) return std::to_wstring(seconds) + l10n::Get(l10n::StringId::OpSeconds).c_str();
    const uint64_t minutes = seconds / 60;
    if (minutes < 60) return std::to_wstring(minutes) + l10n::Get(l10n::StringId::OpMinutes).c_str()
        + std::to_wstring(seconds % 60) + l10n::Get(l10n::StringId::OpSeconds).c_str();
    return std::to_wstring(minutes / 60) + l10n::Get(l10n::StringId::OpHours).c_str()
        + std::to_wstring(minutes % 60) + l10n::Get(l10n::StringId::OpMinutesEnd).c_str();
}

class ConflictWindow {
public:
    ConflictDialogResult Show(HWND owner, const ops::ConflictItemInfo& conflict,
                              bool dark, D2D1_COLOR_F accent) {
        owner_ = owner;
        conflict_ = conflict;
        conflict_.source = path::FriendlyPathText(conflict_.source);
        conflict_.destination = path::FriendlyPathText(conflict_.destination);
        dark_ = dark;
        accent_ = accent;
        scale_ = static_cast<float>(pulse::compat::WindowDpi(owner)) / 96.0f;

        WNDCLASSEXW wc{ sizeof(wc) };
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpfnWndProc = WndProc;
        wc.lpszClassName = kConflictClass;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        if (!GetClassInfoExW(wc.hInstance, kConflictClass, &wc)) RegisterClassExW(&wc);

        const int width = static_cast<int>(480 * scale_);
        const int height = static_cast<int>(428 * scale_);
        hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kConflictClass, l10n::Get(l10n::StringId::OpConflictTitle).c_str(),
            WS_POPUP | WS_THICKFRAME | WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, width, height, owner, nullptr,
            wc.hInstance, this);
        if (!hwnd_) return result_;
        pulse::ui::CenterOwnedWindow(hwnd_, owner_, width, height);
        EnableWindow(owner_, FALSE);
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);

        MSG message{};
        while (!done_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (IsWindow(hwnd_)) { HideComposedDialog(hwnd_, owner_); DestroyWindow(hwnd_); }
        hwnd_ = nullptr;
        EnableWindow(owner_, TRUE);
        return result_;
    }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<ConflictWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = static_cast<ConflictWindow*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        }
        return self ? self->Handle(message, wparam, lparam)
                    : DefWindowProcW(hwnd, message, wparam, lparam);
    }

    D2D1_RECT_F CloseRect() const { return Rect(scale_, 434, 0, 46, 44); }
    D2D1_RECT_F CardRect(int index) const { return Rect(scale_, 20, 121 + index * 66.0f, 440, 58); }
    D2D1_RECT_F CheckRect() const { return Rect(scale_, 20, details_ ? 438.0f : 326.0f, 350, 30); }
    D2D1_RECT_F DetailRect() const { return Rect(scale_, 20, details_ ? 486.0f : 374.0f, 124, 32); }
    D2D1_RECT_F CancelRect() const { return Rect(scale_, 362, details_ ? 482.0f : 370.0f, 98, 34); }

    int Hit(float x, float y) const {
        if (pulse::ui::ContainsRect(CloseRect(), x, y)) return 7;
        for (int i = 0; i < 3; ++i) if (pulse::ui::ContainsRect(CardRect(i), x, y)) return i + 1;
        if (conflict_.remaining > 1 && pulse::ui::ContainsRect(CheckRect(), x, y)) return 4;
        if (pulse::ui::ContainsRect(DetailRect(), x, y)) return 5;
        if (pulse::ui::ContainsRect(CancelRect(), x, y)) return 6;
        return 0;
    }

    void Complete(ops::ConflictChoice choice) {
        result_.choice = choice;
        result_.apply_to_all = apply_all_;
        done_ = true;
        if (hwnd_) { HideComposedDialog(hwnd_, owner_); DestroyWindow(hwnd_); }
    }

    void ToggleDetails() {
        details_ = !details_;
        RECT window{};
        GetWindowRect(hwnd_, &window);
        const int target = static_cast<int>((details_ ? 538 : 428) * scale_);
        SetWindowPos(hwnd_, nullptr, 0, 0, window.right - window.left, target,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void DrawCard(const Theme& theme, int index, const wchar_t* title,
                  const wchar_t* subtitle, const wchar_t* glyph,
                  D2D1_COLOR_F icon_color, const wchar_t* shortcut) {
        const auto bounds = CardRect(index);
        fluent::ControlState state{};
        state.hovered = hover_ == index + 1;
        state.pressed = pressed_ == index + 1;
        state.focused = focus_ == index;
        D2D1_COLOR_F fill = dark_ ? HexColor(state.hovered ? 0x474747 : 0x3A3A3A, 0.92f)
                                  : HexColor(state.hovered ? 0xE4E4E4 : 0xEEEEEE, 0.92f);
        painter_.FillRoundedRect(bounds, 10 * scale_, fill);
        painter_.StrokeRoundedRect(bounds, 10 * scale_,
            state.focused ? WithAlpha(theme.accent, 0.35f) : theme.stroke_card,
            state.focused ? 1.5f : 1.0f);
        const auto icon = Rect(scale_, 32, 133 + index * 66.0f, 34, 34);
        painter_.FillRoundedRect(icon, 17 * scale_, icon_color);
        painter_.DrawGlyph(glyph, icon, HexColor(0xFFFFFF));
        painter_.DrawText(title, Rect(scale_, 78, 126 + index * 66.0f, 272, 25),
                          compositor_.TextFormat(), theme.text);
        painter_.DrawText(subtitle, Rect(scale_, 78, 150 + index * 66.0f, 300, 20),
                          compositor_.SmallFormat(), theme.text_secondary);
        fluent::BadgeSpec badge{ Rect(scale_, 382, 139 + index * 66.0f, 58, 22), shortcut,
                                 fluent::BadgeKind::Keycap };
        painter_.DrawBadge(badge);
    }

    void Render() {
        if (compositor_.NeedsRecovery()) {
            if (!compositor_.Recover()) return;
            compositor_.RecreateTextFormats(scale_);
            painter_.SetCompositor(&compositor_);
            painter_.SetScale(scale_);
        }
        if (!compositor_.Dc()) return;
        const bool high_contrast = IsHighContrast();
        const Theme theme = high_contrast ? MakeHighContrastTheme() : MakeTheme(dark_, accent_);
        pulse::ui::BeginSurface(compositor_, painter_, theme, dark_, high_contrast, backdrop_enabled_,
                     scale_);

        painter_.DrawGlyph(L"\xE8C8", Rect(scale_, 14, 11, 22, 22), theme.accent);
        painter_.DrawText(l10n::Get(l10n::StringId::OpConflictTitle).c_str(), Rect(scale_, 42, 0, 260, 44),
                          compositor_.SmallFormat(), theme.text);
        fluent::ControlState close_state{};
        close_state.hovered = hover_ == 7;
        close_state.pressed = pressed_ == 7;
        painter_.DrawTitleBarButton(CloseRect(), fluent::TitleBarButtonRole::Close,
                                    {}, close_state);
        painter_.FillRoundedRect(Rect(scale_, 0, 43, 480, 1), 0, theme.stroke_divider);

        const std::wstring route = ParentName(conflict_.source) + L"  →  "
                                 + ParentName(conflict_.destination);
        painter_.DrawBadge({ Rect(scale_, 20, 58, 250, 24), route,
                             fluent::BadgeKind::Neutral });
        const std::wstring headline = l10n::Get(l10n::StringId::OpConflictLead).c_str() + LeafName(conflict_.destination) + L"”";
        painter_.DrawText(headline, Rect(scale_, 20, 84, 440, 30),
                          compositor_.HeaderFormat(), theme.text);

        DrawCard(theme, 0, l10n::Get(l10n::StringId::OpReplaceFile).c_str(), l10n::Get(l10n::StringId::OpReplaceDesc).c_str(),
                 L"\xE73E", HexColor(0x107C10), L"Alt+R");
        DrawCard(theme, 1, l10n::Get(l10n::StringId::OpSkipFile).c_str(), l10n::Get(l10n::StringId::OpSkipDesc).c_str(),
                 L"\xE72A", HexColor(0x0078D4), L"Alt+S");
        DrawCard(theme, 2, l10n::Get(l10n::StringId::OpKeepBoth).c_str(), l10n::Get(l10n::StringId::OpKeepBothDesc).c_str(),
                 L"\xE8C8", HexColor(0x6B69D6), L"Alt+K");

        if (conflict_.remaining > 1) {
            fluent::ControlState check{};
            check.checked = apply_all_;
            check.hovered = hover_ == 4;
            check.focused = focus_ == 3;
            const std::wstring label = l10n::Get(l10n::StringId::OpApplyLead).c_str() + std::to_wstring(conflict_.remaining)
                                     + l10n::Get(l10n::StringId::OpApplyEnd).c_str();
            painter_.DrawCheckBox(CheckRect(), label, check);
        }
        painter_.FillRoundedRect(Rect(scale_, 0, details_ ? 474.0f : 360.0f, 480, 1), 0,
                                 theme.stroke_divider);
        fluent::ControlState detail_state{};
        detail_state.hovered = hover_ == 5;
        detail_state.focused = focus_ == (conflict_.remaining > 1 ? 4 : 3);
        painter_.DrawButton({ DetailRect(), details_ ? l10n::Get(l10n::StringId::OpBrief).c_str() : l10n::Get(l10n::StringId::OpMore).c_str(),
                              details_ ? L"\xE70E" : L"\xE70D",
                              fluent::ButtonKind::Transparent, detail_state });
        fluent::ControlState cancel_state{};
        cancel_state.hovered = hover_ == 6;
        cancel_state.pressed = pressed_ == 6;
        cancel_state.focused = focus_ == (conflict_.remaining > 1 ? 5 : 4);
        painter_.DrawButton({ CancelRect(), l10n::Get(l10n::StringId::Cancel).c_str(), {}, fluent::ButtonKind::Standard,
                              cancel_state });

        if (details_) {
            const float top = 326.0f;
            painter_.FillRoundedRect(Rect(scale_, 20, top, 440, 104), 7 * scale_,
                                     dark_ ? HexColor(0x161616, 0.62f) : HexColor(0xFFFFFF, 0.62f));
            painter_.StrokeRoundedRect(Rect(scale_, 20, top, 440, 104), 7 * scale_, theme.stroke_card);
            painter_.DrawText(l10n::Get(l10n::StringId::OpSource).c_str(), Rect(scale_, 32, top + 8, 190, 20),
                              compositor_.SmallFormat(), theme.accent);
            painter_.DrawText(l10n::Get(l10n::StringId::OpExisting).c_str(), Rect(scale_, 252, top + 8, 190, 20),
                              compositor_.SmallFormat(), theme.text_secondary);
            const std::wstring source_meta = pulse::format::ByteSize(conflict_.source_size) + L"  ·  "
                                           + pulse::format::LocalFileTime(conflict_.source_modified, l10n::Get(l10n::StringId::OpUnknown).c_str());
            const std::wstring target_meta = pulse::format::ByteSize(conflict_.destination_size) + L"  ·  "
                                           + pulse::format::LocalFileTime(conflict_.destination_modified, l10n::Get(l10n::StringId::OpUnknown).c_str());
            painter_.DrawText(source_meta, Rect(scale_, 32, top + 30, 198, 20),
                              compositor_.SmallFormat(), theme.text);
            painter_.DrawText(target_meta, Rect(scale_, 252, top + 30, 196, 20),
                              compositor_.SmallFormat(), theme.text);
            painter_.DrawText(conflict_.source, Rect(scale_, 32, top + 55, 198, 38),
                              compositor_.SmallFormat(), theme.text_secondary);
            painter_.DrawText(conflict_.destination, Rect(scale_, 252, top + 55, 196, 38),
                              compositor_.SmallFormat(), theme.text_secondary);
        }
        // The paths are cut to their boxes; hovering one shows it whole (#55).
        SetPathTip(1, details_ ? Rect(scale_, 32, 326.0f + 55, 198, 38) : D2D1_RECT_F{});
        SetPathTip(2, details_ ? Rect(scale_, 252, 326.0f + 55, 196, 38) : D2D1_RECT_F{});
        pulse::ui::EndSurface(compositor_);
    }

    LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            scale_ = static_cast<float>(pulse::compat::WindowDpi(hwnd_)) / 96.0f;
            backdrop_enabled_ = pulse::ui::ApplyBackdrop(hwnd_, dark_);
            if (!compositor_.Init(hwnd_)) return -1;
            compositor_.RecreateTextFormats(scale_);
            painter_.SetCompositor(&compositor_);
            painter_.SetScale(scale_);
            CreatePathTips();
            return 0;
        case WM_NCCALCSIZE:
            return 0;
        case WM_NCHITTEST:
            return pulse::ui::BorderlessHitTest(hwnd_, lparam, 44 * scale_, CloseRect());
        case WM_SIZE:
            if (compositor_.Dc()) compositor_.Resize(LOWORD(lparam), HIWORD(lparam));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_MOVE:
            compositor_.UpdateTextRenderingParams(
                MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST));
            return 0;
        case WM_DPICHANGED: {
            scale_ = HIWORD(wparam) / 96.0f;
            compositor_.RecreateTextFormats(scale_);
            painter_.SetScale(scale_);
            const auto* suggested = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOOWNERZORDER);
            return 0;
        }
        case WM_MOUSEMOVE: {
            const int next = Hit(static_cast<float>(GET_X_LPARAM(lparam)),
                                 static_cast<float>(GET_Y_LPARAM(lparam)));
            if (next != hover_) { hover_ = next; InvalidateRect(hwnd_, nullptr, FALSE); }
            TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd_, 0 };
            TrackMouseEvent(&track);
            return 0;
        }
        case WM_MOUSELEAVE:
            hover_ = 0;
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONDOWN:
            pressed_ = Hit(static_cast<float>(GET_X_LPARAM(lparam)),
                           static_cast<float>(GET_Y_LPARAM(lparam)));
            SetCapture(hwnd_);
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP: {
            const int hit = Hit(static_cast<float>(GET_X_LPARAM(lparam)),
                                static_cast<float>(GET_Y_LPARAM(lparam)));
            const int pressed = pressed_;
            pressed_ = 0;
            ReleaseCapture();
            if (hit == pressed) {
                if (hit >= 1 && hit <= 3) {
                    focus_ = hit - 1;
                    Complete(hit == 1 ? ops::ConflictChoice::Replace
                             : hit == 2 ? ops::ConflictChoice::Skip
                                        : ops::ConflictChoice::KeepBoth);
                } else if (hit == 4) apply_all_ = !apply_all_;
                else if (hit == 5) ToggleDetails();
                else if (hit == 6 || hit == 7) Complete(ops::ConflictChoice::Cancel);
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_KEYDOWN:
            if (GetKeyState(VK_MENU) < 0) {
                if (wparam == 'R') { Complete(ops::ConflictChoice::Replace); return 0; }
                if (wparam == 'S') { Complete(ops::ConflictChoice::Skip); return 0; }
                if (wparam == 'K') { Complete(ops::ConflictChoice::KeepBoth); return 0; }
            }
            if (wparam == VK_ESCAPE) { Complete(ops::ConflictChoice::Cancel); return 0; }
            if (wparam == VK_TAB) {
                const int count = conflict_.remaining > 1 ? 6 : 5;
                focus_ = (focus_ + (GetKeyState(VK_SHIFT) < 0 ? count - 1 : 1)) % count;
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            if (wparam == VK_RETURN || wparam == VK_SPACE) {
                if (focus_ <= 2) Complete(focus_ == 0 ? ops::ConflictChoice::Replace
                                         : focus_ == 1 ? ops::ConflictChoice::Skip
                                                       : ops::ConflictChoice::KeepBoth);
                else if (focus_ == 3 && conflict_.remaining > 1) apply_all_ = !apply_all_;
                else if (focus_ == (conflict_.remaining > 1 ? 4 : 3)) ToggleDetails();
                else Complete(ops::ConflictChoice::Cancel);
                InvalidateRect(hwnd_, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_SYSKEYDOWN:
            if (wparam == 'R') { Complete(ops::ConflictChoice::Replace); return 0; }
            if (wparam == 'S') { Complete(ops::ConflictChoice::Skip); return 0; }
            if (wparam == 'K') { Complete(ops::ConflictChoice::KeepBoth); return 0; }
            break;
        case WM_CLOSE:
            Complete(ops::ConflictChoice::Cancel);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(hwnd_, &paint);
            Render();
            EndPaint(hwnd_, &paint);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
            compositor_.Shutdown();
            done_ = true;
            return 0;
        }
        return DefWindowProcW(hwnd_, message, wparam, lparam);
    }

    void CreatePathTips() {
        INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_BAR_CLASSES };
        InitCommonControlsEx(&controls);
        tooltip_ = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd_, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!tooltip_) return;
        SendMessageW(tooltip_, TTM_SETMAXTIPWIDTH, 0, static_cast<LPARAM>(560 * scale_));
        for (UINT id : { 1u, 2u }) {
            TOOLINFOW info{ sizeof(info) };
            info.uFlags = TTF_SUBCLASS;
            info.hwnd = hwnd_;
            info.uId = id;
            info.lpszText = const_cast<wchar_t*>(
                (id == 1 ? conflict_.source : conflict_.destination).c_str());
            SendMessageW(tooltip_, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
        }
    }

    void SetPathTip(UINT id, const D2D1_RECT_F& box) {
        if (!tooltip_) return;
        TOOLINFOW info{ sizeof(info) };
        info.hwnd = hwnd_;
        info.uId = id;
        info.rect = { static_cast<LONG>(std::floor(box.left)), static_cast<LONG>(std::floor(box.top)),
                      static_cast<LONG>(std::ceil(box.right)), static_cast<LONG>(std::ceil(box.bottom)) };
        SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&info));
    }

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    HWND tooltip_ = nullptr;
    Compositor compositor_;
    fluent::Painter painter_;
    ops::ConflictItemInfo conflict_;
    ConflictDialogResult result_;
    D2D1_COLOR_F accent_ = HexColor(0x0078D4);
    float scale_ = 1.0f;
    bool dark_ = false;
    bool backdrop_enabled_ = false;
    bool details_ = false;
    bool apply_all_ = false;
    bool done_ = false;
    int hover_ = 0;
    int pressed_ = 0;
    int focus_ = 0;
};

} // namespace

FileOperationWindow::FileOperationWindow() : painter_(&compositor_) {}
FileOperationWindow::~FileOperationWindow() { Destroy(); }

bool FileOperationWindow::Create(HWND owner, FileOperationCallbacks callbacks) {
    if (hwnd_) return true;
    owner_ = owner;
    callbacks_ = std::move(callbacks);
    dark_ = ShouldUseDarkMode(ThemeMode::Auto);
    accent_ = GetAccentColor();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = kTransferClass;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    if (!GetClassInfoExW(wc.hInstance, kTransferClass, &wc)) RegisterClassExW(&wc);
    const UINT owner_dpi = pulse::compat::WindowDpi(owner_);
    const int width = MulDiv(static_cast<int>(kDlgW), owner_dpi, 96);
    const int height = MulDiv(static_cast<int>(kCollapsedH), owner_dpi, 96);
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP, kTransferClass, l10n::Get(l10n::StringId::OpWindow).c_str(),
        WS_POPUP | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height, owner_, nullptr,
        wc.hInstance, this);
    return hwnd_ != nullptr;
}

void FileOperationWindow::Destroy() {
    if (hwnd_) { HideComposedDialog(hwnd_); DestroyWindow(hwnd_); }
    hwnd_ = nullptr;
}

void FileOperationWindow::SetTheme(bool dark, D2D1_COLOR_F accent) {
    dark_ = dark;
    accent_ = accent;
    if (hwnd_) {
        ApplyWindowTheme();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void FileOperationWindow::Update(const ops::OpStatus& status) {
    const bool new_task = status.task_id != status_.task_id;
    const ULONGLONG now = GetTickCount64();
    if (!new_task && status.active && status_.active) {
        const bool leaving_authorization = status.authorization == ops::AuthorizationState::None &&
            status_.authorization != ops::AuthorizationState::None;
        const bool retry_ended = status_.authorization_retrying && !status.authorization_retrying;
        if (hold_until_ == 0) {
            if (leaving_authorization) hold_until_ = now + kAuthorizationExitDelayMs;
            else if (retry_ended) hold_until_ = retry_shown_tick_ + kRetryMinVisibleMs;
        }
        if ((leaving_authorization || retry_ended) && now < hold_until_) {
            held_status_ = status;  // released by the render timer if it sticks
            has_held_status_ = true;
            return;
        }
    }
    hold_until_ = 0;
    has_held_status_ = false;
    if (status.authorization_retrying && !status_.authorization_retrying) retry_shown_tick_ = now;
    const bool was_active = status_.active;
    if (new_task || status.authorization != status_.authorization) pressed_ = 0;
    status_ = ops::PresentOperationStatus(status);
    if (new_task) {
        speed_history_.clear();
        speed_sample_tick_ = 0;
    }
    if (!hwnd_) return;
    if (status_.authorization != ops::AuthorizationState::None) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        const int min_width = static_cast<int>(kDlgW * scale_);
        const int min_height = static_cast<int>(kCollapsedH * scale_);
        if (rect.right - rect.left < min_width || rect.bottom - rect.top < min_height)
            SetWindowPos(hwnd_, nullptr, 0, 0, std::max(min_width, static_cast<int>(rect.right - rect.left)),
                         std::max(min_height, static_cast<int>(rect.bottom - rect.top)),
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    // The render timer stops once an operation ends; restart it for the next.
    if (status_.active && !was_active && IsWindowVisible(hwnd_))
        SetTimer(hwnd_, kRenderTimer, 33, nullptr);
}

void FileOperationWindow::Show(bool activate) {
    if (!hwnd_) return;
    const bool was_visible = IsWindowVisible(hwnd_) != FALSE;
    if (!positioned_) {
        RECT rect{};
        GetWindowRect(hwnd_, &rect);
        pulse::ui::CenterOwnedWindow(hwnd_, owner_, rect.right - rect.left, rect.bottom - rect.top);
        positioned_ = true;
    }
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER |
                 (activate ? 0 : SWP_NOACTIVATE) | SWP_SHOWWINDOW);
    // SWP_SHOWWINDOW sends no WM_SHOWWINDOW; start the animation timer here.
    if (!was_visible) SetTimer(hwnd_, kRenderTimer, 33, nullptr);
    if (activate) SetForegroundWindow(hwnd_);
}

void FileOperationWindow::Hide() {
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
}

bool FileOperationWindow::IsVisible() const {
    return hwnd_ && IsWindowVisible(hwnd_);
}

void FileOperationWindow::ApplyWindowTheme() {
    backdrop_enabled_ = pulse::ui::ApplyBackdrop(hwnd_, dark_);
}

void FileOperationWindow::ResizeForDetails(bool preserve_center) {
    if (!hwnd_) return;
    RECT rect{};
    GetWindowRect(hwnd_, &rect);
    const int width = rect.right - rect.left;
    const int old_height = rect.bottom - rect.top;
    const int height = static_cast<int>((detailed_ ? kDetailedH : kCollapsedH) * scale_);
    const int y = preserve_center ? rect.top - (height - old_height) / 2 : rect.top;
    SetWindowPos(hwnd_, nullptr, rect.left, y, width, height,
                 SWP_NOZORDER | SWP_NOOWNERZORDER);
}

int FileOperationWindow::HitTestControl(float x, float y) const {
    const auto labels = MakeTransferLabels(status_, detailed_);
    const auto chrome = MakeTransferChrome(scale_,
        static_cast<float>(compositor_.Width()), static_cast<float>(compositor_.Height()),
        painter_, labels);
    if (pulse::ui::ContainsRect(chrome.close, x, y)) return 1;
    if (pulse::ui::ContainsRect(chrome.minimize, x, y)) return 2;
    if (labels.show_retry && labels.actions_enabled && pulse::ui::ContainsRect(chrome.retry, x, y)) return 6;
    if (labels.show_skip && labels.actions_enabled && pulse::ui::ContainsRect(chrome.skip, x, y)) return 7;
    if (labels.show_pause && pulse::ui::ContainsRect(chrome.pause, x, y)) return 3;
    if (pulse::ui::ContainsRect(chrome.cancel, x, y)) return 4;
    if (labels.show_details && pulse::ui::ContainsRect(chrome.details, x, y)) return 5;
    return 0;
}

void FileOperationWindow::RequestClose() {
    const bool emptying = status_.type == ops::OpType::EmptyRecycle;
    if (status_.active && status_.phase != ops::OpPhase::Completed &&
        status_.phase != ops::OpPhase::Failed && !emptying) {
        if (callbacks_.cancel) callbacks_.cancel();
        return;
    }
    if (callbacks_.dismiss) callbacks_.dismiss();
    Hide();
}

void FileOperationWindow::Render() {
    if (compositor_.NeedsRecovery()) {
        if (!compositor_.Recover()) return;
        compositor_.RecreateTextFormats(scale_);
        painter_.SetCompositor(&compositor_);
        painter_.SetScale(scale_);
    }
    if (!compositor_.Dc()) return;
    const bool high_contrast = IsHighContrast();
    const Theme theme = high_contrast ? MakeHighContrastTheme() : MakeTheme(dark_, accent_);
    pulse::ui::BeginSurface(compositor_, painter_, theme, dark_, high_contrast, backdrop_enabled_,
                 scale_);
    const float width = static_cast<float>(compositor_.Width());
    const float height = static_cast<float>(compositor_.Height());
    const float dip_w = width / std::max(scale_, 0.001f);
    const auto labels = MakeTransferLabels(status_, detailed_);
    const auto chrome = MakeTransferChrome(scale_, width, height, painter_, labels);
    const bool authorization = status_.authorization != ops::AuthorizationState::None;
    const bool requesting = status_.authorization == ops::AuthorizationState::Requesting;
    const bool failed = status_.phase == ops::OpPhase::Failed;
    const bool paused = status_.phase == ops::OpPhase::Paused;
    const bool waiting = status_.phase == ops::OpPhase::WaitingForConflict;
    const bool scanning = status_.phase == ops::OpPhase::Scanning;
    const bool completed = status_.phase == ops::OpPhase::Completed;
    const bool moving = status_.type == ops::OpType::Move;
    const bool deleting = status_.type == ops::OpType::RecycleDelete
                       || status_.type == ops::OpType::RealDelete;
    const bool restoring = status_.type == ops::OpType::RestoreRecycle;
    const bool emptying = status_.type == ops::OpType::EmptyRecycle;
    const D2D1_COLOR_F sky = dark_ ? HexColor(0x38BDF8) : HexColor(0x0284C7);

    const wchar_t* operation_glyph = authorization ? L"\xEA18" : (deleting || emptying) ? L"\xE74D"
        : restoring ? L"\xE777" : (moving ? L"\xE7C2" : L"\xE8C8");
    painter_.DrawGlyph(operation_glyph, Rect(scale_, 14, 10, 16, 16), sky);
    std::wstring title;
    if (emptying) {
        if (failed) title = l10n::Get(l10n::StringId::OpCannotEmpty);
        else if (completed) title = l10n::Get(l10n::StringId::OpEmptied);
        else title = l10n::Get(l10n::StringId::OpEmptying);
    } else if (failed) {
        title = deleting ? l10n::Get(l10n::StringId::OpDeleteFailed).c_str() : restoring ? l10n::Get(l10n::StringId::OpRestoreFailed).c_str()
            : moving ? l10n::Get(l10n::StringId::OpMoveFailed).c_str() : l10n::Get(l10n::StringId::OpCopyFailed).c_str();
    } else if (completed) {
        title = (deleting ? l10n::Get(l10n::StringId::OpDeletedPrefix).c_str() : restoring ? l10n::Get(l10n::StringId::OpRestoredPrefix).c_str() : moving ? l10n::Get(l10n::StringId::OpMovedPrefix).c_str() : l10n::Get(l10n::StringId::OpCopiedPrefix).c_str())
              + std::to_wstring(status_.completed_items) + l10n::Get(l10n::StringId::OpItemsSuffix).c_str();
    } else if (paused) {
        title = deleting ? l10n::Get(l10n::StringId::OpDeletePaused).c_str() : restoring ? l10n::Get(l10n::StringId::OpRestorePaused).c_str()
            : moving ? l10n::Get(l10n::StringId::OpMovePaused).c_str() : l10n::Get(l10n::StringId::OpCopyPaused).c_str();
    } else if (scanning) title = l10n::Get(l10n::StringId::OpPreparingList).c_str();
    else {
        const uint64_t count = status_.total_items > 0 ? status_.total_items
            : (std::max)(status_.completed_items, static_cast<uint64_t>(1));
        title = (deleting ? l10n::Get(l10n::StringId::OpDeletingPrefix).c_str() : restoring ? l10n::Get(l10n::StringId::OpRestoringPrefix).c_str() : moving ? l10n::Get(l10n::StringId::OpMovingPrefix).c_str() : l10n::Get(l10n::StringId::OpCopyingPrefix).c_str())
              + std::to_wstring(count) + l10n::Get(l10n::StringId::OpItemsSuffix).c_str();
    }
    painter_.DrawText(title, D2D1::RectF(ScaleDip(scale_, 36.0f), 0,
                                         chrome.minimize.left - ScaleDip(scale_, 8.0f),
                                         ScaleDip(scale_, kTitleH)),
                      compositor_.SmallFormat(), theme.text);

    fluent::ControlState minimize{};
    minimize.hovered = hover_ == 2;
    minimize.pressed = pressed_ == 2;
    painter_.DrawTitleBarButton(chrome.minimize, fluent::TitleBarButtonRole::Minimize, {}, minimize);
    fluent::ControlState close{};
    close.hovered = hover_ == 1;
    close.pressed = pressed_ == 1;
    painter_.DrawTitleBarButton(chrome.close, fluent::TitleBarButtonRole::Close, {}, close);
    painter_.FillRoundedRect(D2D1::RectF(0, ScaleDip(scale_, kTitleH), width,
                                         ScaleDip(scale_, kTitleH) + 1.0f), 0, theme.stroke_divider);

    const std::wstring src = status_.source_label.empty() ? l10n::Get(l10n::StringId::OpSourceShort).c_str() : status_.source_label;
    const std::wstring dst = status_.destination_label.empty() ? l10n::Get(l10n::StringId::OpDestination).c_str() : status_.destination_label;
    std::wstring subtitle;
    if (completed || failed) {
        subtitle = emptying ? title : src;
        if (!emptying && !status_.destination_label.empty()) subtitle += L" → " + dst;
    }
    else if (emptying) subtitle = l10n::Get(l10n::StringId::OpEmptyingSub);
    else if (deleting) subtitle = l10n::Get(l10n::StringId::OpDeletingPrefix).c_str() + src;
    else if (restoring) subtitle = l10n::Get(l10n::StringId::OpRestoringPrefix).c_str() + src;
    else if (moving) subtitle = l10n::Get(l10n::StringId::OpFromPrefix).c_str() + src + l10n::Get(l10n::StringId::OpMoveTo).c_str() + dst;
    else subtitle = l10n::Get(l10n::StringId::OpFromPrefix).c_str() + src + l10n::Get(l10n::StringId::OpCopyTo).c_str() + dst;
    DrawEllipsizedText(compositor_, compositor_.SmallFormat(),
        Rect(scale_, kPadX, 48, dip_w - kPadX * 2.0f - 72.0f, 18), subtitle, theme.text);
    wchar_t percent[32];
    if (authorization || status_.percent < 0.0f) percent[0] = 0;
    else swprintf_s(percent, L"%.0f%%", std::clamp(status_.percent, 0.0f, 100.0f));
    painter_.DrawText(percent, Rect(scale_, dip_w - kPadX - 56.0f, 46, 56, 20),
                      compositor_.TextFormat(), failed ? theme.danger : sky,
                      fluent::HorizontalAlignment::Right);

    std::wstring file_line;
    std::wstring badge_text;
    fluent::BadgeKind badge_kind = fluent::BadgeKind::Success;
    if (authorization) {
        file_line = l10n::Get(l10n::StringId::OpItemPrefix) +
            (status_.current_item.empty() ? src : status_.current_item);
        badge_text = l10n::Get(requesting ? l10n::StringId::OpAuthorizationRequesting
                                        : l10n::StringId::OpAuthorizationRequired);
        badge_kind = fluent::BadgeKind::Warning;
    } else if (failed) {
        // pulse_shell errors are Simplified; known messages are localized and
        // file names are kept as they are on disk.
        file_line = status_.last_error.empty() ? l10n::Get(l10n::StringId::OpFailed)
                                               : l10n::ServiceErrorText(status_.last_error);
        badge_text = l10n::Get(l10n::StringId::OpFailedBadge).c_str();
        badge_kind = fluent::BadgeKind::Danger;
    } else if (paused) {
        file_line = l10n::Get(l10n::StringId::OpItemPrefix) + (status_.current_item.empty() ? src : status_.current_item);
        badge_text = l10n::Get(l10n::StringId::OpPaused).c_str();
        badge_kind = fluent::BadgeKind::Warning;
    } else if (waiting) {
        file_line = l10n::Get(l10n::StringId::OpItemPrefix) + (status_.current_item.empty() ? src : status_.current_item);
        badge_text = l10n::Get(l10n::StringId::OpWaitingConflict).c_str();
        badge_kind = fluent::BadgeKind::Warning;
    } else if (completed) {
        file_line = title;
        badge_text = l10n::Get(l10n::StringId::OpCompleted).c_str();
        badge_kind = fluent::BadgeKind::Success;
    } else if (scanning) {
        file_line = l10n::Get(l10n::StringId::OpScanning);
        badge_text = l10n::Get(l10n::StringId::OpPreparing).c_str();
        badge_kind = fluent::BadgeKind::Neutral;
    } else if (emptying) {
        file_line = l10n::Get(l10n::StringId::OpEmptyingSub);
        badge_text = l10n::Get(l10n::StringId::OpEmptying);
        badge_kind = fluent::BadgeKind::Warning;
    } else {
        const std::wstring item = status_.current_item.empty() ? src : status_.current_item;
        file_line = l10n::Get(l10n::StringId::OpItemPrefix).c_str() + item;
        badge_text = deleting ? l10n::Get(l10n::StringId::OpDeleting).c_str() : moving ? l10n::Get(l10n::StringId::OpMoving).c_str() : l10n::Get(l10n::StringId::OpCopying).c_str();
        badge_kind = fluent::BadgeKind::Success;
    }
    const float badge_w = std::max(48.0f, painter_.MeasureBadgeWidth(badge_text) / std::max(scale_, 0.001f));
    DrawEllipsizedText(compositor_, compositor_.SmallFormat(),
        Rect(scale_, kPadX, 70, dip_w - kPadX * 2.0f - badge_w - 12.0f, 20),
        file_line, theme.text);
    painter_.DrawBadge({ Rect(scale_, dip_w - kPadX - badge_w, 70, badge_w, 18),
                         badge_text, badge_kind });

    const auto track = Rect(scale_, kPadX, 98, dip_w - kPadX * 2.0f, 10);
    painter_.FillRoundedRect(track, ScaleDip(scale_, 5.0f),
        dark_ ? HexColor(0xFFFFFF, 0.10f) : HexColor(0x000000, 0.10f));
    const bool indeterminate = requesting || (emptying && status_.active && !failed && !completed
        && status_.percent < 0.0f);
    if (indeterminate) {
        fluent::ProgressSpec bar;
        bar.bounds = track;
        bar.indeterminate = true;
        bar.animation_progress = static_cast<float>(
            std::fmod(static_cast<double>(GetTickCount64()), 1952.0) / 1952.0);
        painter_.DrawProgressBar(bar);
    } else {
        const float inner = ScaleDip(scale_, 2.0f);
        D2D1_RECT_F fill = D2D1::RectF(track.left + inner, track.top + inner,
                                       track.right - inner, track.bottom - inner);
        const float value = status_.percent < 0.0f ? 0.0f
            : std::clamp(status_.percent / 100.0f, 0.0f, 1.0f);
        fill.right = fill.left + (fill.right - fill.left) * value;
        D2D1_COLOR_F bar = failed ? theme.danger
            : (paused || waiting) ? HexColor(0xF59E0B)
            : HexColor(0x0EA5E9);
        if (fill.right > fill.left) {
            painter_.FillRoundedRect(fill, ScaleDip(scale_, 3.0f), bar);
        }
    }

    const auto stats = Rect(scale_, kPadX, 118, dip_w - kPadX * 2.0f, authorization ? 86.0f : 52.0f);
    painter_.FillRoundedRect(stats, ScaleDip(scale_, 12.0f),
        dark_ ? HexColor(0xFFFFFF, 0.05f) : HexColor(0x000000, 0.05f));
    if (authorization) {
        const std::wstring explanation = requesting
            ? l10n::Get(l10n::StringId::OpAuthorizationPrompt)
            : status_.last_error.empty() ? l10n::Get(l10n::StringId::OpAuthorizationNotGranted)
                                        : l10n::ServiceErrorText(status_.last_error);
        painter_.DrawGlyph(L"\xEA18", Rect(scale_, 30, 150, 22, 22), theme.accent);
        DrawEllipsizedText(compositor_, compositor_.SmallFormat(),
            Rect(scale_, 62, 124, dip_w - 94, 72), explanation, theme.text, true);
    } else {
        const bool byte_basis = status_.speed_basis == ops::OpSpeedBasis::Bytes;
        const std::wstring time_text = failed ? l10n::Get(l10n::StringId::OpIncomplete).c_str()
            : completed ? l10n::Get(l10n::StringId::OpCompleted).c_str()
            : paused ? l10n::Get(l10n::StringId::OpPaused).c_str()
            : waiting ? l10n::Get(l10n::StringId::OpWaiting).c_str()
            : emptying ? l10n::Get(l10n::StringId::OpEmptying)
            : status_.speed_basis == ops::OpSpeedBasis::None ? l10n::Get(l10n::StringId::OpNoEstimate).c_str()
            : status_.eta_seconds == 0 ? l10n::Get(l10n::StringId::OpEstimating).c_str()
            : l10n::Get(l10n::StringId::OpApprox).c_str() + FormatDuration(status_.eta_seconds);
        const uint64_t remain_items = status_.total_items > status_.completed_items
            ? status_.total_items - status_.completed_items : 0;
        const uint64_t remain_bytes = status_.total_bytes > status_.transferred_bytes
            ? status_.total_bytes - status_.transferred_bytes : 0;
        std::wstring items_text;
        if (completed) items_text = l10n::Get(l10n::StringId::OpZeroItems).c_str();
        else if (failed && status_.total_items == 0) items_text = L"—";
        else if (status_.total_items == 0) items_text = l10n::Get(l10n::StringId::OpCalculating).c_str();
        else if (byte_basis && status_.total_bytes > 0)
            items_text = std::to_wstring(remain_items) + l10n::Get(l10n::StringId::OpCountBytes).c_str()
                + pulse::format::ByteSize(remain_bytes) + L")";
        else items_text = std::to_wstring(remain_items) + l10n::Get(l10n::StringId::OpCountSuffix).c_str();
        const double live_speed = status_.speed_basis == ops::OpSpeedBasis::Items
            ? status_.items_per_second : status_.bytes_per_second;
        const std::wstring speed_text =
            status_.speed_basis == ops::OpSpeedBasis::None
                ? l10n::Get(l10n::StringId::OpNoEstimate).c_str()
            : completed || failed || paused || waiting
                ? FormatSpeedText(status_.speed_basis, 0.0)
            : live_speed <= 0.0 ? l10n::Get(l10n::StringId::OpEstimating).c_str()
            : FormatSpeedText(status_.speed_basis, live_speed);
        const D2D1_COLOR_F time_color = failed ? theme.danger : theme.text;
        painter_.DrawText(l10n::Get(l10n::StringId::OpRemainingTime).c_str(), Rect(scale_, 32, 124, 120, 14),
                          compositor_.SmallFormat(), theme.text_secondary);
        painter_.DrawText(l10n::Get(l10n::StringId::OpRemainingItems).c_str(), Rect(scale_, 176, 124, 140, 14),
                          compositor_.SmallFormat(), theme.text_secondary);
        painter_.DrawText(l10n::Get(l10n::StringId::OpCurrentSpeed).c_str(), Rect(scale_, 328, 124, 110, 14),
                          compositor_.SmallFormat(), theme.text_secondary);
        painter_.DrawText(time_text, Rect(scale_, 32, 140, 136, 22),
                          compositor_.SmallFormat(), time_color);
        painter_.DrawText(items_text, Rect(scale_, 176, 140, 148, 22),
                          compositor_.SmallFormat(), theme.text);
        painter_.DrawText(speed_text, Rect(scale_, 328, 140, 112, 22),
                          compositor_.SmallFormat(), sky);
    }

    if (detailed_ && !authorization) {
        const auto card = Rect(scale_, kPadX, 180, dip_w - kPadX * 2.0f, 140);
        painter_.FillRoundedRect(card, ScaleDip(scale_, 12.0f),
            dark_ ? HexColor(0x000000, 0.30f) : HexColor(0x000000, 0.05f));
        painter_.StrokeRoundedRect(card, ScaleDip(scale_, 12.0f), theme.stroke_card);
        painter_.DrawGlyph(L"\xE8F1", Rect(scale_, 30, 188, 14, 14), sky);
        painter_.DrawText(l10n::Get(l10n::StringId::OpSpeedHistory).c_str(), Rect(scale_, 48, 186, 220, 18),
                          compositor_.SmallFormat(), theme.text_secondary);
        const double peak_speed = status_.speed_basis == ops::OpSpeedBasis::Items
            ? status_.peak_items_per_second : status_.peak_bytes_per_second;
        const std::wstring peak = l10n::Get(l10n::StringId::OpPeak).c_str() +
            FormatSpeedText(status_.speed_basis == ops::OpSpeedBasis::None
                ? ops::OpSpeedBasis::Bytes : status_.speed_basis, peak_speed);
        painter_.DrawText(peak, Rect(scale_, 280, 186, 156, 18),
                          compositor_.SmallFormat(), theme.text_secondary,
                          fluent::HorizontalAlignment::Right);
        const auto plot = Rect(scale_, 30, 208, dip_w - kPadX * 2.0f - 20.0f, 100);
        painter_.FillRoundedRect(plot, ScaleDip(scale_, 8.0f),
            dark_ ? HexColor(0x0F172A, 0.60f) : HexColor(0xFFFFFF, 0.50f));
        DrawSpeedGraph(compositor_.Dc(), plot, speed_history_, sky, scale_);
    }

    painter_.FillRoundedRect(D2D1::RectF(0, height - ScaleDip(scale_, kFooterH), width,
                                         height - ScaleDip(scale_, kFooterH) + 1.0f),
                             0, theme.stroke_divider);
    const auto footer_bg = D2D1::RectF(0, height - ScaleDip(scale_, kFooterH) + 1.0f,
                                       width, height);
    painter_.FillRoundedRect(footer_bg, 0,
        dark_ ? HexColor(0x202022, 0.92f) : HexColor(0x000000, 0.04f));

    fluent::ControlState detail_state{};
    detail_state.hovered = hover_ == 5;
    detail_state.pressed = pressed_ == 5;
    if (labels.show_details) painter_.DrawButton({ chrome.details, labels.details,
        detailed_ ? L"\xE70E" : L"\xE70D", fluent::ButtonKind::Transparent, detail_state });
    if (labels.show_pause) {
        fluent::ControlState pause_state{};
        pause_state.enabled = status_.active && !failed && status_.phase != ops::OpPhase::Cancelling
            && status_.phase != ops::OpPhase::WaitingForConflict;
        pause_state.hovered = hover_ == 3;
        pause_state.pressed = pressed_ == 3;
        painter_.DrawButton({ chrome.pause, labels.pause, {},
                              fluent::ButtonKind::Standard, pause_state });
    }
    if (labels.show_retry) {
        fluent::ControlState retry_state{};
        retry_state.hovered = hover_ == 6;
        retry_state.pressed = pressed_ == 6;
        retry_state.enabled = labels.actions_enabled;
        painter_.DrawButton({ chrome.retry, labels.retry, L"\xEA18",
                              fluent::ButtonKind::Primary, retry_state });
    }
    if (labels.show_skip) {
        fluent::ControlState skip_state{};
        skip_state.hovered = hover_ == 7;
        skip_state.pressed = pressed_ == 7;
        skip_state.enabled = labels.actions_enabled;
        painter_.DrawButton({ chrome.skip, labels.skip, {},
                              fluent::ButtonKind::Standard, skip_state });
    }
    fluent::ControlState cancel_state{};
    cancel_state.enabled = status_.phase != ops::OpPhase::Cancelling;
    cancel_state.hovered = hover_ == 4;
    cancel_state.pressed = pressed_ == 4;
    painter_.DrawButton({ chrome.cancel, labels.cancel, {},
                          fluent::ButtonKind::Standard, cancel_state });
    pulse::ui::EndSurface(compositor_);
}

LRESULT CALLBACK FileOperationWindow::WndProc(HWND hwnd, UINT message,
                                               WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<FileOperationWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<FileOperationWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    return self ? self->HandleMessage(message, wparam, lparam)
                : DefWindowProcW(hwnd, message, wparam, lparam);
}

LRESULT FileOperationWindow::HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        scale_ = static_cast<float>(pulse::compat::WindowDpi(hwnd_)) / 96.0f;
        ApplyWindowTheme();
        if (!compositor_.Init(hwnd_)) return -1;
        compositor_.RecreateTextFormats(scale_);
        painter_.SetCompositor(&compositor_);
        painter_.SetScale(scale_);
        // The window is created hidden and kept for reuse; its 33 ms
        // animation timer only runs while it is shown.
        return 0;
    case WM_SHOWWINDOW:
        if (wparam) SetTimer(hwnd_, kRenderTimer, 33, nullptr);
        else KillTimer(hwnd_, kRenderTimer);
        break;
    case WM_NCCALCSIZE:
        return 0;
    case WM_NCHITTEST: {
        const auto labels = MakeTransferLabels(status_, detailed_);
        const auto chrome = MakeTransferChrome(scale_,
            static_cast<float>(compositor_.Width()), static_cast<float>(compositor_.Height()),
            painter_, labels);
        return pulse::ui::BorderlessHitTest(hwnd_, lparam, ScaleDip(scale_, kTitleH),
            D2D1::RectF(chrome.minimize.left, 0, chrome.close.right, chrome.close.bottom));
    }
    case WM_GETMINMAXINFO: {
        if (status_.authorization == ops::AuthorizationState::None) break;
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = { static_cast<LONG>(kDlgW * scale_),
                                 static_cast<LONG>(kCollapsedH * scale_) };
        return 0;
    }
    case WM_SIZE:
        if (compositor_.Dc() && wparam != SIZE_MINIMIZED)
            compositor_.Resize(LOWORD(lparam), HIWORD(lparam));
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_MOVE:
        compositor_.UpdateTextRenderingParams(
            MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST));
        return 0;
    case WM_DPICHANGED: {
        scale_ = HIWORD(wparam) / 96.0f;
        compositor_.RecreateTextFormats(scale_);
        painter_.SetScale(scale_);
        const auto* suggested = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOOWNERZORDER);
        return 0;
    }
    case WM_TIMER:
        if (wparam == kRenderTimer && !IsWindowVisible(hwnd_)) {
            KillTimer(hwnd_, kRenderTimer);
            return 0;
        }
        if (wparam == kRenderTimer && has_held_status_ && GetTickCount64() >= hold_until_) {
            const ops::OpStatus held = held_status_;
            Update(held);
        }
        if (wparam == kRenderTimer) {
            // Only a running operation animates (indeterminate bar, speed
            // graph); a finished one is redrawn by Update and input alone.
            if (!status_.active) {
                KillTimer(hwnd_, kRenderTimer);
                return 0;
            }
            const ULONGLONG now = GetTickCount64();
            if (speed_sample_tick_ == 0 || now - speed_sample_tick_ >= 250) {
                speed_sample_tick_ = now;
                const bool running = status_.phase == ops::OpPhase::Running;
                // Sample whichever unit this task tracks: byte transfers and
                // empty-recycle feed the byte rate, delete and restore the item rate.
                double sample = 0.0;
                if (running) {
                    if (status_.speed_basis == ops::OpSpeedBasis::Bytes)
                        sample = status_.bytes_per_second;
                    else if (status_.speed_basis == ops::OpSpeedBasis::Items)
                        sample = status_.items_per_second;
                }
                speed_history_.push_back((std::max)(0.0, sample));
                while (speed_history_.size() > 120) speed_history_.pop_front();
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        const int next = HitTestControl(static_cast<float>(GET_X_LPARAM(lparam)),
                                        static_cast<float>(GET_Y_LPARAM(lparam)));
        if (next != hover_) { hover_ = next; InvalidateRect(hwnd_, nullptr, FALSE); }
        TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, hwnd_, 0 };
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        hover_ = 0;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        pressed_ = HitTestControl(static_cast<float>(GET_X_LPARAM(lparam)),
                                  static_cast<float>(GET_Y_LPARAM(lparam)));
        SetCapture(hwnd_);
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        const int hit = HitTestControl(static_cast<float>(GET_X_LPARAM(lparam)),
                                       static_cast<float>(GET_Y_LPARAM(lparam)));
        const int pressed = pressed_;
        pressed_ = 0;
        ReleaseCapture();
        if (hit == pressed) {
            if (hit == 1 || hit == 4) {
                RequestClose();
            } else if (hit == 2) ShowWindow(hwnd_, SW_MINIMIZE);
            else if (hit == 3 && status_.active &&
                     status_.phase != ops::OpPhase::WaitingForConflict &&
                     status_.phase != ops::OpPhase::Cancelling) {
                if (status_.phase == ops::OpPhase::Paused) {
                    if (callbacks_.resume) callbacks_.resume();
                } else if (callbacks_.pause) callbacks_.pause();
            } else if ((hit == 6 || hit == 7) && status_.active &&
                       status_.authorization == ops::AuthorizationState::ActionRequired &&
                       status_.phase != ops::OpPhase::Cancelling) {
                if (hit == 6 && callbacks_.retry_authorization)
                    callbacks_.retry_authorization(status_.task_id);
                else if (hit == 7 && status_.can_skip_authorization && callbacks_.skip_authorization)
                    callbacks_.skip_authorization(status_.task_id);
            } else if (hit == 5) {
                detailed_ = !detailed_;
                ResizeForDetails(true);
            }
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            RequestClose();
            return 0;
        }
        break;
    case WM_CLOSE:
        RequestClose();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(hwnd_, &paint);
        Render();
        EndPaint(hwnd_, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        KillTimer(hwnd_, kRenderTimer);
        compositor_.Shutdown();
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

ConflictDialogResult ShowFileConflictDialog(HWND owner,
                                            const ops::ConflictItemInfo& conflict,
                                            bool dark,
                                            D2D1_COLOR_F accent) {
    ConflictWindow window;
    return window.Show(owner, conflict, dark, accent);
}


} // namespace pulse::ui
