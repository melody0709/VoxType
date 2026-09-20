#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <variant>
#include <vector>
#include <span>
#include <utility>

#include "ui_types.h"

struct Config;

namespace ui_form {

class LayoutCursor {
public:
    explicit LayoutCursor(int startY = UiStyle::FirstRowY, int rowHeight = UiStyle::RowHeight);

    int NextRowY();
    int CurrentY() const { return m_currentY; }
    void Advance(int designPx) { m_currentY += designPx; }

private:
    int m_currentY;
    int m_rowHeight;
};

class FormBinder {
public:
    using BoundMember = std::variant<
        std::wstring Config::*,
        int Config::*
    >;

    enum class ControlType {
        Edit,
        Password,
        ComboBox,
        IntComboBox
    };

    struct Binding {
        int idc = 0;
        BoundMember member;
        ControlType type = ControlType::Edit;
        std::vector<std::pair<std::wstring, std::wstring>> options{};
        std::vector<std::pair<int, std::wstring>> intOptions{};
        int idcToggle = 0;
        bool passwordVisible = false;
    };

    HWND AddEditRow(HWND parent, LayoutCursor& cursor, int idc,
                    const wchar_t* label, std::wstring Config::* field,
                    int editW = UiStyle::InputW);

    HWND AddPasswordRow(HWND parent, LayoutCursor& cursor, int idcEdit, int idcToggle,
                        const wchar_t* label, std::wstring Config::* field,
                        int editW = 330);

    HWND AddComboRow(HWND parent, LayoutCursor& cursor, int idc,
                     const wchar_t* label, std::wstring Config::* field,
                     std::span<const std::pair<std::wstring, std::wstring>> options,
                     int comboW = UiStyle::ComboW);

    HWND AddComboRow(HWND parent, LayoutCursor& cursor, int idc,
                     const wchar_t* label, int Config::* field,
                     std::span<const std::pair<int, std::wstring>> options,
                     int comboW = UiStyle::ComboW);

    void LoadFromConfig(HWND parent, const Config& cfg);
    void SaveToConfig(HWND parent, Config& cfg);

    const std::vector<HWND>& GetControls() const { return m_controls; }
    void ShowAll(bool visible);
    bool HandlePasswordToggle(HWND parent, WORD controlId);
    void DestroyControls();

private:
    std::vector<Binding> m_bindings;
    std::vector<HWND> m_controls;
};

} // namespace ui_form
