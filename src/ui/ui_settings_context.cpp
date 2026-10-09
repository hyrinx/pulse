#include "ui_renderer.h"
#include "ui_renderer_internal.h"

#include <algorithm>

namespace pulse::ui {
void MainRenderer::DrawSettingsContext(const WindowViewModel& vm,const D2D1_RECT_F& rect,const Theme& theme) {
    using I=l10n::StringId;
    auto* dc=compositor_->Dc();
    const auto l=MakeSettingsLayout(vm,rect,scale_,title_bar_height_,status_height_,&painter_);
    painter_.DrawText(l10n::Get(I::ContextMenuDesc),D2D1::RectF(l.content.left+20*scale_,l.content_origin+64*scale_,l.content.right-20*scale_,l.content_origin+86*scale_),compositor_->SmallFormat(),theme.text_secondary);
    const I titles[]={I::ContextSoftware,I::ContextOpenWith,I::ContextShare,I::ContextSystem,I::ContextPrint,I::ContextBuiltin};
    const I descriptions[]={I::ContextSoftwareDesc,I::ContextOpenWithDesc,I::ContextShareDesc,I::ContextSystemDesc,I::ContextPrintDesc,I::ContextBuiltinDesc};
    const wchar_t* icons[]={L"\xE74C",L"\xE8A7",L"\xE72D",L"\xE713",L"\xE749",L"\xE8FD"};
    static_assert(sizeof(titles)/sizeof(titles[0])==SettingsLayout::kContextCards);
    DrawSettingsPulseMenu(vm,rect,theme);
    painter_.DrawText(l10n::Get(I::ContextOtherSoftware),l.context_other,compositor_->SmallFormat(),theme.text_secondary);
    for(int g=0;g<5;++g) {
        const bool has_switch=true;
        const float text_right=has_switch ? l.context_toggle[g].left-12*scale_ : l.context_cards[g].right-56*scale_;
        const auto r=l.context_cards[g],header=l.context_header[g];
        MakeBrush(dc,theme.fill_input,brFillInput_);MakeBrush(dc,theme.stroke_card,brStrokeCard_);
        dc->FillRoundedRectangle(D2D1::RoundedRect(r,8*scale_,8*scale_),brFillInput_.get());
        dc->DrawRoundedRectangle(D2D1::RoundedRect(r,8*scale_,8*scale_),brStrokeCard_.get(),1);
        if(IsHovered(vm,HitTestResult::SettingsDisclosure,8+g)) {
            MakeBrush(dc,theme.fill_hover,brFillHover_);
            dc->FillRoundedRectangle(D2D1::RoundedRect(header,8*scale_,8*scale_),brFillHover_.get());
        }
        DrawIconText(r.left+16*scale_,r.top+20*scale_,24*scale_,24*scale_,icons[g],L"",theme.text_secondary,0.85f);
        painter_.DrawText(l10n::Get(titles[g]),D2D1::RectF(r.left+54*scale_,r.top+12*scale_,text_right,r.top+36*scale_),compositor_->TextFormat(),theme.text);
        painter_.DrawText(l10n::Get(descriptions[g]),D2D1::RectF(r.left+54*scale_,r.top+39*scale_,text_right,r.top+65*scale_),compositor_->SmallFormat(),theme.text_secondary);
        if(has_switch) {
            fluent::ControlState state{};state.checked=vm.settings_group_on[g];state.hovered=IsHovered(vm,HitTestResult::SettingsToggle,10+g);
            painter_.DrawSwitch(l.context_toggle[g],L"",state);
        }
        DrawIconText(r.right-36*scale_,r.top+24*scale_,18*scale_,18*scale_,(vm.settings_expanded&(1u<<(8+g))) ? L"\xE70D" : L"\xE76C",L"",theme.text_secondary,0.75f);
    }
    for(const auto& empty:l.context_empty) if(empty.bottom>empty.top)
        painter_.DrawText(l10n::Get(I::SettingsContextEmpty),empty,compositor_->SmallFormat(),theme.text_secondary);
    for(size_t i=0;i<l.context_rows.size();++i) {
        const auto r=l.context_rows[i];if(r.bottom<=r.top) continue;
        const auto& row=vm.settings_items[i];
        if(row.group==5) continue; // drawn by DrawSettingsPulseMenu
        if(IsHovered(vm,HitTestResult::SettingsToggle,100+static_cast<int>(i))) {
            MakeBrush(dc,theme.fill_hover,brFillHover_);
            dc->FillRoundedRectangle(D2D1::RoundedRect(r,4*scale_,4*scale_),brFillHover_.get());
        }
        // A row auto-disabled after timeouts (#77) is dimmed and carries the
        // reason right of its text; the switch stays clickable to recover.
        const float text_right=row.slow_disabled ? r.right-268*scale_ : r.right-76*scale_;
        painter_.DrawText(row.text,D2D1::RectF(r.left+16*scale_,r.top,text_right,r.bottom),compositor_->TextFormat(),row.slow_disabled?theme.text_secondary:theme.text);
        if(row.slow_disabled)
            painter_.DrawText(l10n::Get(I::ContextSlowDisabled),D2D1::RectF(r.right-252*scale_,r.top,r.right-76*scale_,r.bottom),compositor_->SmallFormat(),theme.text_secondary);
        fluent::ControlState state{};state.checked=row.on;state.hovered=IsHovered(vm,HitTestResult::SettingsToggle,100+static_cast<int>(i));
        painter_.DrawSwitch(D2D1::RectF(r.right-60*scale_,r.top+4*scale_,r.right-16*scale_,r.bottom-4*scale_),L"",state);
    }
    fluent::ControlState restore{};restore.hovered=IsHovered(vm,HitTestResult::SettingsRestore);
    painter_.DrawButton({l.context_restore,l10n::Get(I::RestoreDefaults),L"",fluent::ButtonKind::Standard,restore});
}

void MainRenderer::DrawSettingsPulseMenu(const WindowViewModel& vm,const D2D1_RECT_F& rect,const Theme& theme) {
    using I=l10n::StringId;
    const auto l=MakeSettingsLayout(vm,rect,scale_,title_bar_height_,status_height_,&painter_);
    if(l.pulse_card.bottom<=l.pulse_card.top) return;
    auto* dc=compositor_->Dc();
    auto fill=[&](const D2D1_RECT_F& r,float radius,const D2D1_COLOR_F& c) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;dc->CreateSolidColorBrush(c,&b);
        if(b) dc->FillRoundedRectangle(D2D1::RoundedRect(r,radius,radius),b.Get());
    };
    auto stroke=[&](const D2D1_RECT_F& r,float radius,const D2D1_COLOR_F& c,float w) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;dc->CreateSolidColorBrush(c,&b);
        if(b) dc->DrawRoundedRectangle(D2D1::RoundedRect(r,radius,radius),b.Get(),w);
    };
    auto hline=[&](float x0,float x1,float y,const D2D1_COLOR_F& c) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;dc->CreateSolidColorBrush(c,&b);
        if(b) dc->DrawLine(D2D1::Point2F(x0,y),D2D1::Point2F(x1,y),b.Get(),1);
    };
    const float s=scale_;
    const auto card=l.pulse_card;
    fill(card,8*s,theme.fill_input);
    stroke(card,8*s,theme.stroke_card,1);
    DrawIconText(card.left+16*s,card.top+20*s,24*s,24*s,L"\xE8FD",L"",theme.text_secondary,0.85f);
    painter_.DrawText(l10n::Get(I::ContextPulseMenu),D2D1::RectF(card.left+54*s,card.top+12*s,l.pulse_restore.left-12*s,card.top+36*s),compositor_->TextFormat(),theme.text);
    painter_.DrawText(l10n::Get(I::ContextPulseMenuDesc),D2D1::RectF(card.left+54*s,card.top+39*s,l.pulse_restore.left-12*s,card.top+65*s),compositor_->SmallFormat(),theme.text_secondary);
    // 恢复默认: an accent link, only live when something is hidden.
    {
        bool any=vm.settings_builtin_hidden!=0;
        for(int i=0;i<static_cast<int>(app::BuiltinMenuSurface::Count);++i) any|=PulseMenuOrderCustom(vm,static_cast<app::BuiltinMenuSurface>(i));
        const bool hot=any && IsHovered(vm,HitTestResult::SettingsToggle,67);
        if(hot) fill(l.pulse_restore,4*s,theme.fill_hover);
        painter_.DrawText(l10n::Get(I::RestoreDefaults),D2D1::RectF(l.pulse_restore.left+4*s,l.pulse_restore.top,l.pulse_restore.right,l.pulse_restore.bottom),
                          compositor_->SmallFormat(),any ? theme.accent : theme.text_disabled);
    }
    // Presets.
    const auto active=app::BuiltinMenuPresetFor(vm.settings_builtin_hidden);
    static constexpr I kPresetName[]={I::ContextPresetSlim,I::ContextPresetStandard,I::ContextPresetFull,I::ContextPresetCustom};
    static constexpr I kPresetDesc[]={I::ContextPresetSlimDesc,I::ContextPresetStandardDesc,I::ContextPresetFullDesc,I::ContextPresetCustomDesc};
    for(int i=0;i<4;++i) {
        const auto r=l.pulse_preset[i];
        const bool on=static_cast<int>(active)==i;
        const bool dim=i==3 && !on;
        if(i<3 && IsHovered(vm,HitTestResult::SettingsToggle,60+i) && !on) fill(r,6*s,theme.fill_hover);
        stroke(r,6*s,on ? theme.accent : theme.stroke_card,on ? 1.5f*s : 1.0f);
        const auto name_color=dim ? theme.text_disabled : on ? theme.accent : theme.text;
        painter_.DrawText(l10n::Get(kPresetName[i]),D2D1::RectF(r.left+10*s,r.top+5*s,r.right-6*s,r.top+25*s),compositor_->TextFormat(),name_color);
        painter_.DrawText(l10n::Get(kPresetDesc[i]),D2D1::RectF(r.left+10*s,r.top+24*s,r.right-4*s,r.bottom-4*s),compositor_->SmallFormat(),dim ? theme.text_disabled : theme.text_secondary);
    }
    // Surface tabs.
    fill(l.pulse_tabs,7*s,theme.fill_hover);
    static constexpr I kTabs[]={I::ContextTabItem,I::ContextTabBackground,I::ContextTabRow};
    for(int i=0;i<3;++i) {
        const auto r=l.pulse_tab[i];
        const bool on=vm.settings_context_tab==i;
        if(on) {fill(r,5*s,theme.fill_input);stroke(r,5*s,theme.stroke_card,1);}
        else if(IsHovered(vm,HitTestResult::SettingsToggle,64+i)) fill(r,5*s,theme.fill_pressed);
        const auto label=l10n::Get(kTabs[i]);
        const float w=CellTextWidth(label);
        const float x=r.left+std::max(0.0f,(r.right-r.left-w)*0.5f);
        painter_.DrawText(label,D2D1::RectF(x,r.top,r.right,r.bottom),compositor_->TextFormat(),on ? theme.accent : theme.text);
    }
    // Rows (list order = menu order of the selected surface).
    const auto rows=app::BuiltinMenuOrderedRows(PulseMenuSurface(vm),PulseMenuOrder(vm));
    for(size_t k=0;k<rows.size();++k) {
        const auto& row=rows[k];
        D2D1_RECT_F r{};
        bool on=true,hot=false;int hit=-1;
        if(row.fixed!=app::BuiltinFixedRow::None) {
            r=l.pulse_fixed[row.fixed==app::BuiltinFixedRow::Open ? 0 : 1];
        } else {
            const size_t i=vm.settings_builtin_first+static_cast<size_t>(row.item);
            if(i>=l.context_rows.size()||i>=vm.settings_items.size()) continue;
            r=l.context_rows[i];on=vm.settings_items[i].on;hit=100+static_cast<int>(i);
            hot=IsHovered(vm,HitTestResult::SettingsToggle,hit);
        }
        if(r.bottom<=r.top) continue;
        if(k>0) hline(r.left,r.right,r.top,theme.stroke_divider);
        if(hot) fill(D2D1::RectF(r.left-8*s,r.top+2*s,r.right+8*s,r.bottom-2*s),4*s,theme.fill_hover);
        const auto label=app::BuiltinMenuRowLabel(row);
        painter_.DrawText(label,D2D1::RectF(r.left,r.top,r.right-120*s,r.bottom),compositor_->TextFormat(),theme.text);
        if(hit<0) {
            const auto note=l10n::Get(I::ContextAlwaysShown);
            const float w=CellTextWidth(note,true);
            painter_.DrawText(note,D2D1::RectF(r.right-w-2*s,r.top,r.right,r.bottom),compositor_->SmallFormat(),theme.text_secondary);
            DrawIconText(r.right-w-22*s,r.top+14*s,16*s,16*s,L"\xE72E",L"",theme.text_secondary,0.7f);
        } else {
            if(vm.settings_context_tab!=2 && app::BuiltinMenuShared(row.item)) {
                const float x=r.left+CellTextWidth(label)+10*s;
                painter_.DrawText(l10n::Get(I::ContextShared),D2D1::RectF(x,r.top,r.right-60*s,r.bottom),compositor_->SmallFormat(),theme.text_secondary);
            }
            fluent::ControlState state{};state.checked=on;state.hovered=hot;
            painter_.DrawSwitch(D2D1::RectF(r.right-44*s,r.top+6*s,r.right,r.bottom-6*s),L"",state);
        }
    }
    // Live preview built from the same rows the real menu keeps. Movable
    // rows show a grip on hover and can be dragged to a new place.
    const auto pv=l.pulse_preview;
    if(pv.bottom<=pv.top) return;
    if(vm.settings_context_tab!=2)
        painter_.DrawText(l10n::Get(vm.settings_context_tab==1 ? I::ContextPreviewBackground : I::ContextPreviewItem),
                          D2D1::RectF(pv.left,pv.top,pv.right,pv.top+18*s),compositor_->SmallFormat(),theme.text_secondary);
    if(l.pulse_order_reset.right>l.pulse_order_reset.left) {
        const auto label=l10n::Get(I::ContextOrderReset);
        const float w=CellTextWidth(label,true);
        if(IsHovered(vm,HitTestResult::SettingsToggle,kSettingsMenuOrderReset))
            fill(D2D1::RectF(l.pulse_order_reset.right-w-8*s,l.pulse_order_reset.top,l.pulse_order_reset.right+4*s,l.pulse_order_reset.bottom),4*s,theme.fill_hover);
        painter_.DrawText(label,D2D1::RectF(l.pulse_order_reset.right-w-2*s,l.pulse_order_reset.top,l.pulse_order_reset.right+2*s,l.pulse_order_reset.bottom),
                          compositor_->SmallFormat(),theme.accent);
    }
    const auto box=PulsePreviewBox(pv,s);
    fill(D2D1::RectF(box.left+1*s,box.top+2*s,box.right+1*s,box.bottom+3*s),8*s,D2D1::ColorF(0,0,0,0.06f));
    fill(box,8*s,theme.surface_flyout);
    stroke(box,8*s,theme.stroke_card,1);
    painter_.DrawText(l10n::Get(vm.settings_context_tab==2 ? I::ContextOrderHintRow : I::ContextOrderHint),
                      D2D1::RectF(box.left+2*s,box.bottom+6*s,box.right,pv.bottom),compositor_->SmallFormat(),theme.text_secondary);
    const auto pl=LayoutPulsePreview(vm,box,s);
    auto hovered=[&](const app::BuiltinMenuRow& row) {
        return !pl.dragging && IsHovered(vm,HitTestResult::SettingsToggle,PulseMenuRowHit(row));
    };
    auto grip=[&](float x,float cy,const D2D1_COLOR_F& color) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;dc->CreateSolidColorBrush(color,&b);
        if(!b) return;
        for(int col=0;col<2;++col) for(int k=-1;k<=1;++k)
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x+col*3.5f*s,cy+k*4.5f*s),1.1f*s,1.1f*s),b.Get());
    };
    auto ghost_card=[&](const D2D1_RECT_F& g) {
        fill(D2D1::RectF(g.left-1*s,g.top+3*s,g.right+1*s,g.bottom+6*s),7*s,D2D1::ColorF(0.16f,0.2f,0.35f,0.08f));
        fill(D2D1::RectF(g.left,g.top+1*s,g.right,g.bottom+3*s),6*s,D2D1::ColorF(0.16f,0.2f,0.35f,0.08f));
        fill(g,6*s,theme.surface_flyout);
        stroke(g,6*s,theme.stroke_card,1);
    };
    auto insertion=[&](float x0,float y0,float x1,float y1) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;dc->CreateSolidColorBrush(theme.accent,&b);
        if(!b) return;
        dc->DrawLine(D2D1::Point2F(x0,y0),D2D1::Point2F(x1,y1),b.Get(),2*s);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> inner;dc->CreateSolidColorBrush(theme.surface_flyout,&inner);
        if(inner) dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x0,y0),3.5f*s,3.5f*s),inner.Get());
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x0,y0),3*s,3*s),b.Get(),1.5f*s);
    };
    if(vm.settings_context_tab==2) {
        // One list row with the hover buttons that stay on.
        const auto row=D2D1::RectF(box.left+8*s,box.top+12*s,box.right-8*s,box.bottom-12*s);
        fill(row,4*s,theme.fill_hover);
        DrawIconText(row.left+8*s,row.top+6*s,20*s,20*s,L"\xE8A5",L"",theme.text_secondary,0.8f);
        painter_.DrawText(L"notes.txt",D2D1::RectF(row.left+36*s,row.top,row.right-90*s,row.bottom),compositor_->TextFormat(),theme.text);
        for(const auto& v:pl.rows) {
            if(hovered(v.row)) fill(v.rect,4*s,theme.fill_pressed);
            DrawIconText(v.rect.left+4*s,v.rect.top+4*s,16*s,16*s,v.row.glyph,L"",theme.text_secondary,0.75f);
        }
        if(pl.dragging) {
            const float x=(pl.gap.left+pl.gap.right)*0.5f;
            insertion(x,pl.gap.top+1*s,x,pl.gap.bottom-1*s);
            ghost_card(pl.ghost);
            DrawIconText(pl.ghost.left+4*s,pl.ghost.top+4*s,16*s,16*s,pl.drag_row.glyph,L"",theme.text,0.8f);
        }
        return;
    }
    auto draw_row=[&](const app::BuiltinMenuRow& row,const D2D1_RECT_F& rect) {
        float y=rect.top;
        if(row.item==app::BuiltinMenuItem::Tags) {
            static constexpr D2D1_COLOR_F kSwatch[]={{0.93f,0.27f,0.27f,1},{0.96f,0.56f,0.13f,1},{0.13f,0.73f,0.38f,1},{0.92f,0.70f,0.03f,1},{0.66f,0.33f,0.97f,1},{0.23f,0.51f,0.96f,1},{0.58f,0.64f,0.72f,1}};
            float sx=rect.left+16*s;
            for(const auto& c:kSwatch) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> b;dc->CreateSolidColorBrush(c,&b);
                if(b) dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(sx+5*s,y+kPulsePreviewSwatches*s*0.5f),5*s,5*s),b.Get());
                sx+=28*s;
            }
            y+=kPulsePreviewSwatches*s;
        }
        const auto r=D2D1::RectF(rect.left,y,rect.right,y+kPulsePreviewRow*s);
        if(row.fixed==app::BuiltinFixedRow::Strip) {
            static constexpr const wchar_t* kStrip[]={L"\xE8C6",L"\xE8C8",L"\xE74D",L"\xE8AC"};
            float sx=r.left+28*s;
            for(const auto* g:kStrip) {DrawIconText(sx,r.top+9*s,16*s,16*s,g,L"",theme.text,0.8f);sx+=44*s;}
            return;
        }
        if(row.glyph) DrawIconText(r.left+10*s,r.top+9*s,16*s,16*s,row.glyph,L"",theme.text,0.8f);
        const auto label=row.item==app::BuiltinMenuItem::Tags ? l10n::Get(I::TagsEllipsis) : app::BuiltinMenuRowLabel(row);
        painter_.DrawText(label,D2D1::RectF(r.left+36*s,r.top,r.right-8*s,r.bottom),compositor_->SmallFormat(),theme.text);
        if(row.submenu) {
            DrawIconText(r.right-22*s,r.top+11*s,12*s,12*s,L"\xE76C",L"",theme.text_secondary,0.7f);
        } else if(row.shortcut) {
            const float w=CellTextWidth(row.shortcut,true);
            painter_.DrawText(row.shortcut,D2D1::RectF(r.right-10*s-w,r.top,r.right-6*s,r.bottom),compositor_->SmallFormat(),theme.text_secondary);
        }
    };
    const float x0=box.left+6*s,x1=box.right-6*s;
    for(const auto& v:pl.rows) {
        const bool fixed=v.row.fixed!=app::BuiltinFixedRow::None;
        if(hovered(v.row)) {
            if(!fixed) {
                fill(v.rect,4*s,theme.fill_hover);
                grip(box.left+7*s,(v.rect.top+v.rect.bottom)*0.5f,theme.text_secondary);
            } else {
                // Open and the action strip stay on top: a lock where the grip would be.
                DrawIconText(box.left+3*s,(v.rect.top+v.rect.bottom)*0.5f-6*s,12*s,12*s,L"\xE72E",L"",theme.text_secondary,0.7f);
            }
        }
        draw_row(v.row,v.rect);
        if(v.separator_after)
            hline(x0+4*s,x1-4*s,v.rect.bottom+kPulsePreviewSeparator*s*0.5f,theme.stroke_divider);
    }
    if(pl.dragging) {
        const float y=(pl.gap.top+pl.gap.bottom)*0.5f;
        insertion(x0+2*s,y,x1-2*s,y);
        ghost_card(pl.ghost);
        draw_row(pl.drag_row,pl.ghost);
        grip(box.left+7*s,(pl.ghost.top+pl.ghost.bottom)*0.5f,theme.text_secondary);
    }
}

float MainRenderer::SettingsMenuRowStart(const WindowViewModel& vm,const D2D1_RECT_F& rect,int item) {
    const auto l=MakeSettingsLayout(vm,rect,scale_,title_bar_height_,status_height_,&painter_);
    if(l.pulse_preview.bottom<=l.pulse_preview.top) return NAN;
    WindowViewModel still=vm;still.settings_menu_drag=-1;
    for(const auto& r:LayoutPulsePreview(still,PulsePreviewBox(l.pulse_preview,scale_),scale_).rows)
        if(r.row.fixed==app::BuiltinFixedRow::None && static_cast<int>(r.row.item)==item)
            return vm.settings_context_tab==2 ? r.rect.left : r.rect.top;
    return NAN;
}

app::BuiltinMenuOrder MainRenderer::SettingsMenuDropOrder(const WindowViewModel& vm,const D2D1_RECT_F& rect,float x,float y) {
    const auto l=MakeSettingsLayout(vm,rect,scale_,title_bar_height_,status_height_,&painter_);
    const auto pv=l.pulse_preview;
    const float slack=40*scale_;
    if(pv.bottom<=pv.top || x<pv.left-slack || x>pv.right+slack || y<pv.top-slack || y>pv.bottom+slack) return {};
    const auto pl=LayoutPulsePreview(vm,PulsePreviewBox(pv,scale_),scale_);
    return pl.dragging ? pl.order : app::BuiltinMenuOrder{};
}
}
