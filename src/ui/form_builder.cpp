#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "form_builder.h"
#include "settings_controls.h"
#include "config_store.h"
#include "ui_theme.h"
#include "ui_utils.h"
#include "hotkey.h"
#include "utils.h"

#include <windowsx.h>
#include <atomic>
#include <cstdio>
#include <format>

#include "settings_controls.h"

namespace ui_form {

LayoutCursor::LayoutCursor(int startY, int rowHeight)
    : m_currentY(startY), m_rowHeight(rowHeight) {}

int LayoutCursor::NextRowY() {
    int y = m_currentY;
    m_currentY += m_rowHeight;
    return y;
}

HWND FormBinder::AddEditRow(HWND parent, LayoutCursor& cursor, int idc,
                            const wchar_t* label, std::wstring Config::* field,
                            int editW) {
    int y = cursor.NextRowY();
    HWND lbl = CreateLabel(parent, S(UiStyle::ContentLeft), S(y + UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), label);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
        S(UiStyle::InputLeft), S(y), S(editW), S(UiStyle::EditH),
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idc)),
        GetParentInstance(parent), nullptr);
    ApplyUiFont(edit);
    m_controls.push_back(lbl);
    m_controls.push_back(edit);
    m_bindings.push_back({idc, field, ControlType::Edit});
    return edit;
}

HWND FormBinder::AddPasswordRow(HWND parent, LayoutCursor& cursor, int idcEdit, int idcToggle,
                                const wchar_t* label, std::wstring Config::* field,
                                int editW) {
    int y = cursor.NextRowY();
    HWND lbl = CreateLabel(parent, S(UiStyle::ContentLeft), S(y + UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), label);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
        S(UiStyle::InputLeft), S(y), S(editW), S(UiStyle::EditH),
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(idcEdit)),
        GetParentInstance(parent), nullptr);
    ApplyUiFont(edit);
    HWND btn = CreateButton(parent, idcToggle, S(UiStyle::SmallBtnX), S(y) - S(1), S(UiStyle::SmallBtnW), S(UiStyle::BtnH), L"Show");
    m_controls.push_back(lbl);
    m_controls.push_back(edit);
    m_controls.push_back(btn);
    Binding b;
    b.idc = idcEdit;
    b.member = field;
    b.type = ControlType::Password;
    b.idcToggle = idcToggle;
    b.passwordVisible = false;
    m_bindings.push_back(std::move(b));
    return edit;
}

HWND FormBinder::AddComboRow(HWND parent, LayoutCursor& cursor, int idc,
                             const wchar_t* label, std::wstring Config::* field,
                             std::span<const std::pair<std::wstring, std::wstring>> options,
                             int comboW) {
    int y = cursor.NextRowY();
    HWND lbl = CreateLabel(parent, S(UiStyle::ContentLeft), S(y + UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), label);
    HWND combo = CreateCombo(parent, idc, S(UiStyle::InputLeft), S(y), S(comboW), S(UiStyle::ComboH));
    Binding b;
    b.idc = idc;
    b.member = field;
    b.type = ControlType::ComboBox;
    for (const auto& opt : options) {
        ComboBox_AddString(combo, opt.second.c_str());
        b.options.push_back(opt);
    }
    m_controls.push_back(lbl);
    m_controls.push_back(combo);
    m_bindings.push_back(std::move(b));
    return combo;
}

HWND FormBinder::AddComboRow(HWND parent, LayoutCursor& cursor, int idc,
                             const wchar_t* label, int Config::* field,
                             std::span<const std::pair<int, std::wstring>> options,
                             int comboW) {
    int y = cursor.NextRowY();
    HWND lbl = CreateLabel(parent, S(UiStyle::ContentLeft), S(y + UiStyle::LabelYOffset), S(UiStyle::LabelWidth), S(UiStyle::LabelH), label);
    HWND combo = CreateCombo(parent, idc, S(UiStyle::InputLeft), S(y), S(comboW), S(UiStyle::ComboH));
    Binding b;
    b.idc = idc;
    b.member = field;
    b.type = ControlType::IntComboBox;
    for (const auto& opt : options) {
        ComboBox_AddString(combo, opt.second.c_str());
        b.intOptions.push_back(opt);
    }
    m_controls.push_back(lbl);
    m_controls.push_back(combo);
    m_bindings.push_back(std::move(b));
    return combo;
}

void FormBinder::ShowAll(bool visible) {
    const int cmd = visible ? SW_SHOW : SW_HIDE;
    for (HWND c : m_controls) {
        ShowWindow(c, cmd);
    }
    if (!visible) {
        for (auto& b : m_bindings) {
            if (b.type == ControlType::Password) {
                b.passwordVisible = false;
                for (HWND c : m_controls) {
                    if (GetDlgCtrlID(c) == b.idc) {
                        SendMessageW(c, EM_SETPASSWORDCHAR, L'\u25CF', 0);
                        InvalidateRect(c, nullptr, TRUE);
                    } else if (b.idcToggle != 0 && GetDlgCtrlID(c) == b.idcToggle) {
                        SetWindowTextW(c, L"Show");
                    }
                }
            }
        }
    }
}

void FormBinder::LoadFromConfig(HWND parent, const Config& cfg) {
    for (auto& b : m_bindings) {
        HWND h = GetDlgItem(parent, b.idc);
        if (!h) continue;
        switch (b.type) {
        case ControlType::Edit: {
            const std::wstring& val = cfg.*(std::get<std::wstring Config::*>(b.member));
            SetWindowTextW(h, val.c_str());
            break;
        }
        case ControlType::Password: {
            b.passwordVisible = false;
            SendMessageW(h, EM_SETPASSWORDCHAR, L'\u25CF', 0);
            if (b.idcToggle != 0) {
                HWND btn = GetDlgItem(parent, b.idcToggle);
                if (btn) SetWindowTextW(btn, L"Show");
            }
            const std::wstring& val = cfg.*(std::get<std::wstring Config::*>(b.member));
            SetWindowTextW(h, val.c_str());
            break;
        }
        case ControlType::ComboBox: {
            const std::wstring& val = cfg.*(std::get<std::wstring Config::*>(b.member));
            int matched = -1;
            if (!b.options.empty()) {
                for (size_t i = 0; i < b.options.size(); ++i) {
                    if (b.options[i].first == val) {
                        matched = static_cast<int>(i);
                        break;
                    }
                }
            } else {
                int count = ComboBox_GetCount(h);
                for (int i = 0; i < count; ++i) {
                    wchar_t buf[256] = {};
                    ComboBox_GetLBText(h, i, buf);
                    if (val == buf) {
                        matched = i;
                        break;
                    }
                }
            }
            if (matched >= 0) {
                ComboBox_SetCurSel(h, matched);
            }
            break;
        }
        case ControlType::IntComboBox: {
            const int val = cfg.*(std::get<int Config::*>(b.member));
            int matched = -1;
            for (size_t i = 0; i < b.intOptions.size(); ++i) {
                if (b.intOptions[i].first == val) {
                    matched = static_cast<int>(i);
                    break;
                }
            }
            if (matched >= 0) {
                ComboBox_SetCurSel(h, matched);
            }
            break;
        }
        }
    }
}

void FormBinder::SaveToConfig(HWND parent, Config& cfg) {
    for (const auto& b : m_bindings) {
        HWND h = GetDlgItem(parent, b.idc);
        if (!h) continue;
        switch (b.type) {
        case ControlType::Edit:
        case ControlType::Password: {
            int len = GetWindowTextLengthW(h);
            std::wstring text(len + 1, L'\0');
            GetWindowTextW(h, text.data(), len + 1);
            text.resize(len);
            cfg.*(std::get<std::wstring Config::*>(b.member)) = Trim(text);
            break;
        }
        case ControlType::ComboBox: {
            int sel = ComboBox_GetCurSel(h);
            if (sel >= 0) {
                if (!b.options.empty() && static_cast<size_t>(sel) < b.options.size()) {
                    cfg.*(std::get<std::wstring Config::*>(b.member)) = b.options[sel].first;
                } else {
                    wchar_t buf[256] = {};
                    ComboBox_GetLBText(h, sel, buf);
                    cfg.*(std::get<std::wstring Config::*>(b.member)) = buf;
                }
            }
            break;
        }
        case ControlType::IntComboBox: {
            int sel = ComboBox_GetCurSel(h);
            if (sel >= 0 && static_cast<size_t>(sel) < b.intOptions.size()) {
                cfg.*(std::get<int Config::*>(b.member)) = b.intOptions[sel].first;
            }
            break;
        }
        }
    }
}

bool FormBinder::HandlePasswordToggle(HWND parent, WORD controlId) {
    for (auto& b : m_bindings) {
        if (b.type == ControlType::Password && b.idcToggle == controlId) {
            b.passwordVisible = !b.passwordVisible;
            HWND edit = GetDlgItem(parent, b.idc);
            if (edit) {
                SendMessageW(edit, EM_SETPASSWORDCHAR, b.passwordVisible ? 0 : L'\u25CF', 0);
                InvalidateRect(edit, nullptr, TRUE);
            }
            HWND btn = GetDlgItem(parent, b.idcToggle);
            if (btn) {
                SetWindowTextW(btn, b.passwordVisible ? L"Hide" : L"Show");
            }
            return true;
        }
    }
    return false;
}

void FormBinder::DestroyControls() {
    for (HWND c : m_controls) {
        if (c && IsWindow(c)) DestroyWindow(c);
    }
    m_controls.clear();
    m_bindings.clear();
}

} // namespace ui_form
