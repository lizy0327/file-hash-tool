#include "app/result_text.h"
#include "app/file_metadata.h"
#include "app/theme.h"
#include "resource.h"
#include "core/hash_engine.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kAddFiles = 1001;
constexpr int kCopyResults = 1002;
constexpr int kDeleteFiles = 1003;
constexpr int kCancelAll = 1004;
constexpr int kCleanAll = 1005;
constexpr int kThemePicker = 1006;
constexpr int kContextCopy = 2001;
constexpr int kContextDelete = 2002;
constexpr int kContextSelectAll = 2003;
constexpr int kList = 1100;
constexpr int kStatus = 1101;
constexpr int kProgress = 1102;
constexpr int kTitle = 1103;
constexpr int kSubtitle = 1104;
constexpr int kAlgorithmBase = 1200;
constexpr int kThemeBase = 1300;
constexpr UINT kResultMessage = WM_APP + 1;
constexpr UINT kProgressMessage = WM_APP + 2;
constexpr UINT kFinishedMessage = WM_APP + 3;

const filehash::HashAlgorithm kAlgorithms[] = {
    filehash::HashAlgorithm::Crc32, filehash::HashAlgorithm::Md5, filehash::HashAlgorithm::Sha1,
    filehash::HashAlgorithm::Sha256, filehash::HashAlgorithm::Sha384, filehash::HashAlgorithm::Sha512,
};

struct Palette {
    COLORREF window_bg;
    COLORREF surface;
    COLORREF surface_alt;
    COLORREF text_primary;
    COLORREF text_secondary;
    COLORREF border;
    COLORREF primary;
    COLORREF primary_text;
    COLORREF selection;
    COLORREF selection_text;
    COLORREF progress_track;
    COLORREF success;
    COLORREF danger;
    COLORREF disabled_bg;
    COLORREF disabled_text;
    COLORREF drop_zone_bg;
};

constexpr std::array<Palette, filehash::ui::kThemes.size()> kPalettes{{
    // 中文：Arctic Blue 使用可见的冰蓝色层级，避免窗口退化为默认白色 / English: Arctic Blue uses visible icy-blue layers instead of default white
    {RGB(226, 238, 252), RGB(244, 249, 255), RGB(220, 233, 248), RGB(16, 43, 74), RGB(57, 88, 122), RGB(99, 139, 181), RGB(26, 98, 210), RGB(255, 255, 255), RGB(176, 209, 255), RGB(16, 43, 74), RGB(197, 220, 246), RGB(19, 122, 69), RGB(180, 35, 24), RGB(214, 227, 242), RGB(110, 134, 160), RGB(215, 234, 255)},
    {RGB(17, 24, 39), RGB(31, 41, 55), RGB(24, 34, 49), RGB(229, 231, 235), RGB(156, 163, 175), RGB(55, 65, 81), RGB(34, 211, 238), RGB(15, 23, 42), RGB(15, 108, 189), RGB(255, 255, 255), RGB(55, 65, 81), RGB(74, 222, 128), RGB(248, 113, 113), RGB(31, 41, 55), RGB(107, 114, 128), RGB(21, 31, 46)},
    {RGB(255, 251, 245), RGB(255, 255, 255), RGB(255, 247, 237), RGB(38, 50, 56), RGB(99, 115, 124), RGB(232, 219, 204), RGB(232, 121, 46), RGB(255, 255, 255), RGB(253, 230, 209), RGB(38, 50, 56), RGB(241, 225, 207), RGB(46, 125, 50), RGB(211, 47, 47), RGB(247, 243, 238), RGB(158, 158, 158), RGB(255, 250, 244)},
    {RGB(245, 250, 248), RGB(255, 255, 255), RGB(237, 247, 243), RGB(23, 53, 47), RGB(102, 122, 116), RGB(207, 226, 220), RGB(15, 157, 131), RGB(255, 255, 255), RGB(217, 243, 236), RGB(23, 53, 47), RGB(207, 226, 220), RGB(15, 157, 131), RGB(211, 47, 47), RGB(237, 244, 242), RGB(139, 157, 152), RGB(247, 252, 250)},
    {RGB(250, 249, 255), RGB(255, 255, 255), RGB(246, 243, 255), RGB(41, 35, 63), RGB(113, 106, 132), RGB(221, 215, 238), RGB(113, 87, 217), RGB(255, 255, 255), RGB(233, 227, 255), RGB(41, 35, 63), RGB(225, 220, 242), RGB(46, 125, 50), RGB(211, 47, 47), RGB(245, 243, 250), RGB(156, 150, 172), RGB(252, 250, 255)},
    {RGB(23, 23, 23), RGB(35, 35, 35), RGB(29, 29, 29), RGB(244, 241, 234), RGB(170, 166, 157), RGB(69, 66, 61), RGB(242, 169, 59), RGB(23, 23, 23), RGB(91, 66, 28), RGB(255, 248, 229), RGB(69, 66, 61), RGB(74, 222, 128), RGB(248, 113, 113), RGB(40, 40, 40), RGB(117, 115, 110), RGB(30, 30, 30)},
}};

struct Job { std::shared_ptr<std::atomic<bool>> cancel; std::thread worker; };
struct Row {
    std::uint64_t id = 0;
    std::filesystem::path path;
    filehash::HashFileResult result;
    std::unique_ptr<Job> job;
    std::uint64_t progress_bytes = 0;
    std::uint64_t total_bytes = 0;
    bool has_result = false;
};
struct ResultMessage { std::uint64_t id; filehash::HashFileResult result; };
struct ProgressMessage { std::uint64_t id; std::uint64_t bytes_read; std::uint64_t total_bytes; };

struct State {
    HWND window = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HWND file_progress = nullptr;
    HWND progress = nullptr;
    HWND file_progress_label = nullptr;
    HWND progress_label = nullptr;
    HWND add_files = nullptr;
    HWND copy_results = nullptr;
    HWND delete_files = nullptr;
    HWND cancel_all = nullptr;
    HWND clean_all = nullptr;
    HWND theme_picker = nullptr;
    HWND title = nullptr;
    HWND subtitle = nullptr;
    HWND algorithm_group = nullptr;
    HWND drop_hint = nullptr;
    HWND empty_hint = nullptr;
    HWND checks[6]{};
    HFONT font = nullptr;
    HFONT title_font = nullptr;
    HFONT empty_hint_font = nullptr;
    HBRUSH background = nullptr;
    HBRUSH surface_brush = nullptr;
    HBRUSH drop_zone_brush = nullptr;
    filehash::ui::ThemeId theme = filehash::ui::ThemeId::ArcticBlue;
    // 中文：显式保存复选框状态，确保自绘控件在 Windows 7/10/11 上一致显示 / English: Keep checkbox state explicitly so owner-drawn controls render consistently on Windows 7/10/11
    std::array<bool, 6> algorithm_checked{{false, true, false, true, false, false}};
    std::vector<Row> rows;
    std::uint64_t next_id = 1;
};

std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}
std::wstring result_text(const filehash::HashFileResult& result) { return widen(filehash::ui::format_result_values(result)); }
std::wstring widen_lines(const std::string& value) {
    std::wstring result;
    const std::wstring wide = widen(value);
    for (const wchar_t character : wide) {
        if (character == L'\n') result += L"\r\n";
        else result += character;
    }
    return result;
}
void set_text(HWND control, const std::wstring& text) { SetWindowTextW(control, text.c_str()); }

const Palette& palette(const State& state) {
    return kPalettes[filehash::ui::theme_index(state.theme)];
}

void replace_brush(HBRUSH& brush, const COLORREF color) {
    if (brush != nullptr) DeleteObject(brush);
    brush = CreateSolidBrush(color);
}

filehash::ui::ThemeId load_theme() {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Lizy\\FileHashTool", L"Theme", RRF_RT_REG_DWORD,
                     nullptr, &value, &size) != ERROR_SUCCESS) {
        return filehash::ui::ThemeId::ArcticBlue;
    }
    return filehash::ui::theme_from_index(value);
}

void save_theme(const filehash::ui::ThemeId theme) {
    const DWORD value = static_cast<DWORD>(filehash::ui::theme_index(theme));
    RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Lizy\\FileHashTool", L"Theme", REG_DWORD,
                    &value, sizeof(value));
}

void update_title_bar(HWND window, const bool dark) {
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE module = LoadLibraryW(L"dwmapi.dll");
    if (module == nullptr) return;
    const auto set_attribute = reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(module, "DwmSetWindowAttribute"));
    if (set_attribute != nullptr) {
        const BOOL enabled = dark ? TRUE : FALSE;
        // Attribute 20 is DWMWA_USE_IMMERSIVE_DARK_MODE on current Windows 10/11.
        // Older systems safely ignore it; attribute 19 covers early Windows 10 builds.
        if (FAILED(set_attribute(window, 20, &enabled, sizeof(enabled)))) {
            set_attribute(window, 19, &enabled, sizeof(enabled));
        }
    }
    FreeLibrary(module);
}

void apply_theme(State& state, const filehash::ui::ThemeId theme, const bool persist) {
    state.theme = theme;
    const auto& colors = palette(state);
    replace_brush(state.background, colors.window_bg);
    replace_brush(state.surface_brush, colors.surface);
    replace_brush(state.drop_zone_brush, colors.drop_zone_bg);

    if (state.list != nullptr) {
        ListView_SetBkColor(state.list, colors.surface);
        ListView_SetTextBkColor(state.list, colors.surface);
        ListView_SetTextColor(state.list, colors.text_primary);
    }
    for (HWND progress : {state.file_progress, state.progress}) {
        if (progress == nullptr) continue;
        SendMessageW(progress, PBM_SETBARCOLOR, 0, colors.primary);
        SendMessageW(progress, PBM_SETBKCOLOR, 0, colors.progress_track);
    }
    if (state.theme_picker != nullptr) {
        const std::wstring name = widen(filehash::ui::theme_info(theme).name);
        set_text(state.theme_picker, L"Theme: " + name + L"  ▾");
    }
    update_title_bar(state.window, filehash::ui::theme_info(theme).dark);
    if (persist) save_theme(theme);
    RedrawWindow(state.window, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}

void show_theme_menu(State& state) {
    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    const auto selected = filehash::ui::theme_index(state.theme);
    for (std::size_t index = 0; index < filehash::ui::kThemes.size(); ++index) {
        const auto label = widen(filehash::ui::kThemes[index].name);
        AppendMenuW(menu, MF_STRING | (index == selected ? MF_CHECKED : 0),
                    kThemeBase + static_cast<UINT>(index), label.c_str());
    }
    RECT button{};
    GetWindowRect(state.theme_picker, &button);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       button.left, button.bottom + 2, 0, state.window, nullptr);
    DestroyMenu(menu);
    if (command >= kThemeBase && command < kThemeBase + static_cast<int>(filehash::ui::kThemes.size())) {
        apply_theme(state, filehash::ui::theme_from_index(static_cast<std::size_t>(command - kThemeBase)), true);
    }
}

void draw_button(const State& state, const DRAWITEMSTRUCT& item) {
    const auto& colors = palette(state);
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool primary = item.hwndItem == state.add_files;
    const bool danger = item.hwndItem == state.delete_files;
    COLORREF fill = disabled ? colors.disabled_bg : (primary ? colors.primary : colors.surface);
    COLORREF border = disabled ? colors.border : (danger ? colors.danger : (primary ? colors.primary : colors.border));
    COLORREF text_color = disabled ? colors.disabled_text : (primary ? colors.primary_text : (danger ? colors.danger : colors.text_primary));
    if (pressed && !disabled) fill = primary ? colors.selection : colors.surface_alt;

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    const HGDIOBJ previous_brush = SelectObject(item.hDC, brush);
    const HGDIOBJ previous_pen = SelectObject(item.hDC, pen);
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, 7, 7);
    SelectObject(item.hDC, previous_pen);
    SelectObject(item.hDC, previous_brush);
    DeleteObject(pen);
    DeleteObject(brush);

    wchar_t text[128]{};
    GetWindowTextW(item.hwndItem, text, ARRAYSIZE(text));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text_color);
    RECT text_rect = item.rcItem;
    if (pressed) OffsetRect(&text_rect, 0, 1);
    DrawTextW(item.hDC, text, -1, &text_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if ((item.itemState & ODS_FOCUS) != 0 && !disabled) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -4, -4);
        DrawFocusRect(item.hDC, &focus);
    }
}

void draw_drop_zone(const State& state, const DRAWITEMSTRUCT& item) {
    const auto& colors = palette(state);
    FillRect(item.hDC, &item.rcItem, state.drop_zone_brush);
    HPEN pen = CreatePen(PS_DOT, 1, colors.primary);
    const HGDIOBJ old_pen = SelectObject(item.hDC, pen);
    const HGDIOBJ old_brush = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
    Rectangle(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right - 1, item.rcItem.bottom - 1);
    SelectObject(item.hDC, old_brush);
    SelectObject(item.hDC, old_pen);
    DeleteObject(pen);

    RECT icon{item.rcItem.left + 22, item.rcItem.top + 20, item.rcItem.left + 48, item.rcItem.bottom - 20};
    HPEN icon_pen = CreatePen(PS_SOLID, 2, colors.primary);
    const HGDIOBJ previous = SelectObject(item.hDC, icon_pen);
    MoveToEx(item.hDC, icon.left, icon.top, nullptr);
    LineTo(item.hDC, icon.right - 7, icon.top);
    LineTo(item.hDC, icon.right, icon.top + 7);
    LineTo(item.hDC, icon.right, icon.bottom);
    LineTo(item.hDC, icon.left, icon.bottom);
    LineTo(item.hDC, icon.left, icon.top);
    MoveToEx(item.hDC, icon.right - 7, icon.top, nullptr);
    LineTo(item.hDC, icon.right - 7, icon.top + 7);
    LineTo(item.hDC, icon.right, icon.top + 7);
    SelectObject(item.hDC, previous);
    DeleteObject(icon_pen);

    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, ARRAYSIZE(text));
    RECT text_rect = item.rcItem;
    text_rect.left += 64;
    text_rect.right -= 18;
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, colors.text_secondary);
    DrawTextW(item.hDC, text, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void draw_algorithm_group(const State& state, const DRAWITEMSTRUCT& item) {
    const auto& colors = palette(state);
    FillRect(item.hDC, &item.rcItem, state.surface_brush);
    HPEN pen = CreatePen(PS_SOLID, 1, colors.border);
    const HGDIOBJ old_pen = SelectObject(item.hDC, pen);
    const HGDIOBJ old_brush = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
    Rectangle(item.hDC, item.rcItem.left, item.rcItem.top + 5, item.rcItem.right - 1, item.rcItem.bottom - 1);
    SelectObject(item.hDC, old_brush);
    SelectObject(item.hDC, old_pen);
    DeleteObject(pen);

    RECT label{item.rcItem.left + 8, item.rcItem.top, item.rcItem.left + 92, item.rcItem.top + 14};
    FillRect(item.hDC, &label, state.surface_brush);
    wchar_t text[64]{};
    GetWindowTextW(item.hwndItem, text, ARRAYSIZE(text));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, colors.text_primary);
    DrawTextW(item.hDC, text, -1, &label, DT_LEFT | DT_SINGLELINE);
}

void draw_algorithm_checkbox(const State& state, const DRAWITEMSTRUCT& item, const int index) {
    const auto& colors = palette(state);
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool checked = state.algorithm_checked[index];
    const COLORREF box_color = disabled ? colors.disabled_bg : (checked ? colors.primary : colors.surface);
    const COLORREF border_color = disabled ? colors.border : (checked ? colors.primary : colors.border);
    const COLORREF text_color = disabled ? colors.disabled_text : colors.text_primary;

    FillRect(item.hDC, &item.rcItem, state.surface_brush);
    RECT box{item.rcItem.left + 1, item.rcItem.top + 3, item.rcItem.left + 14, item.rcItem.top + 16};
    HBRUSH box_brush = CreateSolidBrush(box_color);
    FillRect(item.hDC, &box, box_brush);
    DeleteObject(box_brush);
    HPEN box_pen = CreatePen(PS_SOLID, 1, border_color);
    const HGDIOBJ old_pen = SelectObject(item.hDC, box_pen);
    const HGDIOBJ old_brush = SelectObject(item.hDC, GetStockObject(NULL_BRUSH));
    Rectangle(item.hDC, box.left, box.top, box.right, box.bottom);
    if (checked) {
        HPEN check_pen = CreatePen(PS_SOLID, 1, colors.primary_text);
        const HGDIOBJ previous_pen = SelectObject(item.hDC, check_pen);
        MoveToEx(item.hDC, box.left + 3, box.top + 6, nullptr);
        LineTo(item.hDC, box.left + 6, box.bottom - 3);
        LineTo(item.hDC, box.right - 3, box.top + 3);
        SelectObject(item.hDC, previous_pen);
        DeleteObject(check_pen);
    }
    SelectObject(item.hDC, old_brush);
    SelectObject(item.hDC, old_pen);
    DeleteObject(box_pen);

    wchar_t text[64]{};
    GetWindowTextW(item.hwndItem, text, ARRAYSIZE(text));
    RECT text_rect = item.rcItem;
    text_rect.left += 20;
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, text_color);
    DrawTextW(item.hDC, text, -1, &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

std::vector<filehash::HashAlgorithm> selected_algorithms(const State& state) {
    std::vector<filehash::HashAlgorithm> result;
    for (int index = 0; index < 6; ++index) {
        if (state.algorithm_checked[index]) result.push_back(kAlgorithms[index]);
    }
    return result;
}

Row* find_row(State& state, const std::uint64_t id) {
    for (auto& row : state.rows) if (row.id == id) return &row;
    return nullptr;
}
int find_row_index(const State& state, const std::uint64_t id) {
    for (std::size_t index = 0; index < state.rows.size(); ++index) if (state.rows[index].id == id) return static_cast<int>(index);
    return -1;
}
bool any_running(const State& state) {
    for (const auto& row : state.rows) if (row.job) return true;
    return false;
}

void update_action_buttons(State& state) {
    EnableWindow(state.copy_results, state.rows.empty() ? FALSE : TRUE);
    EnableWindow(state.delete_files, state.rows.empty() ? FALSE : TRUE);
    EnableWindow(state.clean_all, state.rows.empty() ? FALSE : TRUE);
    EnableWindow(state.cancel_all, any_running(state) ? TRUE : FALSE);
    if (state.empty_hint != nullptr) ShowWindow(state.empty_hint, state.rows.empty() ? SW_SHOW : SW_HIDE);
}

void update_summary(State& state) {
    if (any_running(state)) {
        std::size_t running = 0;
        for (const auto& row : state.rows) if (row.job) ++running;
        set_text(state.status, L"Calculating " + std::to_wstring(running) + L" file(s) in parallel...");
    } else if (state.rows.empty()) {
        set_text(state.status, L"Ready — add files or drag them here");
    } else {
        set_text(state.status, L"Finished — copy results or add more files");
    }
}

void update_overall_progress(State& state) {
    std::uint64_t completed = 0;
    std::uint64_t total = 0;
    for (const auto& row : state.rows) { completed += row.progress_bytes; total += row.total_bytes; }
    const int percent = total == 0 ? (any_running(state) ? 0 : (state.rows.empty() ? 0 : 100)) :
        static_cast<int>(std::min<std::uint64_t>(100, completed * 100 / total));
    SendMessageW(state.progress, PBM_SETPOS, percent, 0);
    set_text(state.progress_label, L"All files  " + std::to_wstring(percent) + L"%");
}

void update_file_progress(State& state, const std::uint64_t id, const std::uint64_t bytes_read,
                          const std::uint64_t total_bytes) {
    const int percent = total_bytes == 0
        ? 100
        : static_cast<int>(std::min<std::uint64_t>(100, bytes_read * 100 / total_bytes));
    SendMessageW(state.file_progress, PBM_SETPOS, percent, 0);
    const int index = find_row_index(state, id);
    if (index >= 0) {
        const std::wstring label = L"File progress  " + std::to_wstring(percent) + L"%";
        set_text(state.file_progress_label, label);
    }
}

void add_path(State& state, const std::filesystem::path& path);
void start_file(State& state, const std::uint64_t id);

void add_path(State& state, const std::filesystem::path& path) {
    if (path.empty()) return;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return;
    Row row;
    row.id = state.next_id++;
    row.path = std::filesystem::absolute(path, error);
    if (error) row.path = path;
    row.total_bytes = std::filesystem::file_size(row.path, error);
    state.rows.push_back(std::move(row));
    const int index = static_cast<int>(state.rows.size() - 1);
    const std::wstring text = state.rows.back().path.wstring();
    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.pszText = const_cast<wchar_t*>(text.c_str());
    ListView_InsertItem(state.list, &item);
    ListView_SetItemText(state.list, index, 1, const_cast<wchar_t*>(L"Queued"));
    ListView_SetItemText(state.list, index, 2, const_cast<wchar_t*>(L"Starting automatically..."));
    start_file(state, state.rows.back().id);
    update_action_buttons(state);
    update_summary(state);
}

void choose_files(State& state) {
    std::vector<wchar_t> buffer(64 * 1024, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = state.window;
    dialog.lpstrFilter = L"All files\0*.*\0\0";
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&dialog)) return;
    const wchar_t* first = buffer.data();
    const wchar_t* next = first + wcslen(first) + 1;
    if (*next == L'\0') {
        add_path(state, first);
    } else {
        const std::filesystem::path directory(first);
        while (*next != L'\0') { add_path(state, directory / next); next += wcslen(next) + 1; }
    }
}

void update_row_progress(State& state, const ProgressMessage& message) {
    Row* row = find_row(state, message.id);
    if (row == nullptr) return;
    row->progress_bytes = std::min(message.bytes_read, message.total_bytes);
    const int index = find_row_index(state, message.id);
    if (index >= 0) {
        const int percent = message.total_bytes == 0 ? 100 : static_cast<int>(std::min<std::uint64_t>(100, message.bytes_read * 100 / message.total_bytes));
        const std::wstring status = L"Running " + std::to_wstring(percent) + L"%";
        ListView_SetItemText(state.list, index, 1, const_cast<wchar_t*>(status.c_str()));
    }
    update_file_progress(state, message.id, message.bytes_read, message.total_bytes);
    update_overall_progress(state);
}

void update_row(State& state, const ResultMessage& message) {
    Row* row = find_row(state, message.id);
    if (row == nullptr) return;
    row->result = message.result;
    row->has_result = true;
    const int index = find_row_index(state, message.id);
    if (index >= 0) {
        const std::wstring status = message.result.error.empty() && !message.result.cancelled ? L"Done" :
            (message.result.cancelled ? L"Cancelled" : L"Error");
        const std::wstring text = result_text(message.result);
        ListView_SetItemText(state.list, index, 1, const_cast<wchar_t*>(status.c_str()));
        ListView_SetItemText(state.list, index, 2, const_cast<wchar_t*>(text.c_str()));
    }
    if (message.result.error.empty() && !message.result.cancelled) row->progress_bytes = row->total_bytes;
    if (message.result.error.empty() && !message.result.cancelled) update_file_progress(state, message.id, row->total_bytes, row->total_bytes);
    update_overall_progress(state);
}

void start_file(State& state, const std::uint64_t id) {
    Row* row = find_row(state, id);
    if (row == nullptr || row->job) return;
    const auto algorithms = selected_algorithms(state);
    if (algorithms.empty()) {
        set_text(state.status, L"Select at least one algorithm");
        return;
    }
    const auto path = row->path;
    row->job = std::make_unique<Job>();
    row->job->cancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = row->job->cancel;
    row->has_result = false;
    row->progress_bytes = 0;
    SendMessageW(state.file_progress, PBM_SETPOS, 0, 0);
    row->job->worker = std::thread([window = state.window, id, path, algorithms, cancel] {
        std::atomic<int> last{-1};
        const auto result = filehash::hash_file(path, algorithms,
            [cancel] { return cancel->load(); },
            [&last, window, id](const std::uint64_t bytes_read, const std::uint64_t total_bytes) {
                const int percent = total_bytes == 0 ? 100 : static_cast<int>(std::min<std::uint64_t>(100, bytes_read * 100 / total_bytes));
                if (percent == last.load() && percent != 100) return;
                last = percent;
                auto* message = new ProgressMessage{id, bytes_read, total_bytes};
                if (!PostMessageW(window, kProgressMessage, 0, reinterpret_cast<LPARAM>(message))) delete message;
            });
        auto* message = new ResultMessage{id, result};
        if (!PostMessageW(window, kResultMessage, 0, reinterpret_cast<LPARAM>(message))) delete message;
        PostMessageW(window, kFinishedMessage, static_cast<WPARAM>(id), 0);
    });
}

void finish_file(State& state, const std::uint64_t id) {
    Row* row = find_row(state, id);
    if (row == nullptr || !row->job) return;
    if (row->job->worker.joinable()) row->job->worker.join();
    row->job.reset();
    update_action_buttons(state);
    update_summary(state);
}

void cancel_all(State& state) {
    for (auto& row : state.rows) if (row.job) row.job->cancel->store(true);
    set_text(state.status, L"Cancelling active files...");
}

void delete_selected(State& state) {
    std::vector<int> indexes;
    for (int index = -1; (index = ListView_GetNextItem(state.list, index, LVNI_SELECTED)) >= 0;) indexes.push_back(index);
    std::sort(indexes.rbegin(), indexes.rend());
    for (const int index : indexes) {
        if (index < 0 || index >= static_cast<int>(state.rows.size())) continue;
        if (state.rows[index].job) {
            state.rows[index].job->cancel->store(true);
            if (state.rows[index].job->worker.joinable()) state.rows[index].job->worker.join();
        }
        state.rows.erase(state.rows.begin() + index);
        ListView_DeleteItem(state.list, index);
    }
    update_overall_progress(state);
    update_action_buttons(state);
    update_summary(state);
}

std::vector<int> selected_indexes(const State& state) {
    std::vector<int> indexes;
    for (int index = -1; (index = ListView_GetNextItem(state.list, index, LVNI_SELECTED)) >= 0;) indexes.push_back(index);
    return indexes;
}

std::vector<int> all_indexes(const State& state) {
    std::vector<int> indexes;
    indexes.reserve(state.rows.size());
    for (std::size_t index = 0; index < state.rows.size(); ++index) indexes.push_back(static_cast<int>(index));
    return indexes;
}

std::wstring copy_result_text(const State& state, const std::vector<int>& indexes) {
    std::wstring output;
    for (const int index : indexes) {
        if (index < 0 || index >= static_cast<int>(state.rows.size())) continue;
        const auto& row = state.rows[static_cast<std::size_t>(index)];
        output += L"File: " + row.path.wstring() + L"\r\n";
        output += L"Size: " + std::to_wstring(row.total_bytes) + L" bytes\r\n";
        output += L"Modified: " + widen(filehash::ui::format_file_time(row.path)) + L"\r\n";
        if (row.has_result) {
            const wchar_t* status = row.result.error.empty() && !row.result.cancelled
                ? L"Done" : (row.result.cancelled ? L"Cancelled" : L"Error");
            output += L"Status: ";
            output += status;
            output += L"\r\n";
            output += widen_lines(filehash::ui::format_result_lines(row.result));
        } else if (row.job) {
            output += L"Status: Calculating...";
        } else {
            output += L"Status: Queued";
        }
        output += L"\r\n\r\n";
    }
    return output;
}

void copy_text_to_clipboard(State& state, const std::wstring& text, const wchar_t* status) {
    if (!OpenClipboard(state.window)) return;
    EmptyClipboard();
    const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr) {
        auto* destination = static_cast<wchar_t*>(GlobalLock(memory));
        if (destination != nullptr) {
            CopyMemory(destination, text.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
            memory = nullptr;
        }
    }
    if (memory != nullptr) GlobalFree(memory);
    CloseClipboard();
    set_text(state.status, status);
}

void copy_results(State& state) {
    copy_text_to_clipboard(state, copy_result_text(state, all_indexes(state)), L"All results copied.");
}

void copy_selected_results(State& state) {
    const auto indexes = selected_indexes(state);
    if (indexes.empty()) {
        set_text(state.status, L"Select a result row first.");
        return;
    }
    copy_text_to_clipboard(state, copy_result_text(state, indexes), L"Result copied.");
}

void copy_row(State& state, const int index) {
    if (index < 0 || index >= static_cast<int>(state.rows.size())) return;
    copy_text_to_clipboard(state, copy_result_text(state, {index}), L"Result copied.");
}

void select_all(State& state) {
    ListView_SetItemState(state.list, -1, LVIS_SELECTED, LVIS_SELECTED);
    set_text(state.status, L"All rows selected.");
}

void show_context_menu(State& state, POINT screen_point) {
    POINT client_point = screen_point;
    ScreenToClient(state.list, &client_point);
    LVHITTESTINFO hit{};
    hit.pt = client_point;
    const int hit_index = ListView_HitTest(state.list, &hit);
    if (hit_index < 0) return;

    if ((ListView_GetItemState(state.list, hit_index, LVIS_SELECTED) & LVIS_SELECTED) == 0) {
        ListView_SetItemState(state.list, -1, 0, LVIS_SELECTED);
        ListView_SetItemState(state.list, hit_index, LVIS_SELECTED, LVIS_SELECTED);
    }

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    AppendMenuW(menu, MF_STRING, kContextCopy, L"Copy result");
    AppendMenuW(menu, MF_STRING, kContextDelete, L"Delete");
    AppendMenuW(menu, MF_STRING, kContextSelectAll, L"Select all");
    SetMenuDefaultItem(menu, kContextCopy, FALSE);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                       screen_point.x, screen_point.y, 0, state.window, nullptr);
    DestroyMenu(menu);
    if (command == kContextCopy) copy_selected_results(state);
    else if (command == kContextDelete) delete_selected(state);
    else if (command == kContextSelectAll) select_all(state);
}

void clean_all(State& state) {
    for (auto& row : state.rows) if (row.job) {
        row.job->cancel->store(true);
        if (row.job->worker.joinable()) row.job->worker.join();
    }
    state.rows.clear();
    ListView_DeleteAllItems(state.list);
    SendMessageW(state.file_progress, PBM_SETPOS, 0, 0);
    SendMessageW(state.progress, PBM_SETPOS, 0, 0);
    set_text(state.file_progress_label, L"File progress  0%");
    set_text(state.progress_label, L"All files  0%");
    update_action_buttons(state);
    update_summary(state);
}

void layout(State& state, const int width, const int height) {
    const int content_width = std::max(100, width - 32);
    MoveWindow(state.title, 16, 14, 500, 32, TRUE);
    MoveWindow(state.subtitle, 16, 47, 620, 22, TRUE);
    MoveWindow(state.add_files, width - 678, 18, 94, 32, TRUE);
    MoveWindow(state.copy_results, width - 574, 18, 108, 32, TRUE);
    MoveWindow(state.delete_files, width - 456, 18, 92, 32, TRUE);
    MoveWindow(state.clean_all, width - 354, 18, 92, 32, TRUE);
    MoveWindow(state.cancel_all, width - 252, 18, 92, 32, TRUE);
    MoveWindow(state.theme_picker, width - 150, 18, 134, 32, TRUE);
    MoveWindow(state.algorithm_group, 16, 78, content_width, 56, TRUE);
    for (int index = 0; index < 6; ++index) MoveWindow(state.checks[index], 32 + index * 98, 100, 91, 20, TRUE);
    MoveWindow(state.drop_hint, 16, 144, content_width, 78, TRUE);
    MoveWindow(state.file_progress_label, 16, 234, 110, 18, TRUE);
    MoveWindow(state.file_progress, 132, 237, std::max(50, content_width - 116), 10, TRUE);
    MoveWindow(state.progress_label, 16, 258, 110, 18, TRUE);
    MoveWindow(state.progress, 132, 261, std::max(50, content_width - 116), 10, TRUE);
    const int list_top = 286;
    const int list_height = std::max(80, height - 332);
    MoveWindow(state.list, 16, list_top, content_width, list_height, TRUE);
    MoveWindow(state.empty_hint, 16, list_top + std::max(0, (list_height - 34) / 2), content_width, 34, TRUE);
    MoveWindow(state.status, 16, height - 34, content_width, 20, TRUE);
    ListView_SetColumnWidth(state.list, 0, std::max(220, content_width * 32 / 100));
    ListView_SetColumnWidth(state.list, 1, 105);
    ListView_SetColumnWidth(state.list, 2, std::max(240, content_width * 68 / 100 - 105));
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        state = static_cast<State*>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (state == nullptr) return DefWindowProcW(window, message, wparam, lparam);
    switch (message) {
        case WM_CREATE: {
            state->theme = load_theme();
            state->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->title_font = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->background = CreateSolidBrush(palette(*state).window_bg);
            state->surface_brush = CreateSolidBrush(palette(*state).surface);
            state->drop_zone_brush = CreateSolidBrush(palette(*state).drop_zone_bg);
            state->title = CreateWindowW(L"STATIC", L"Lizy File Hash Tool v1.0", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTitle), nullptr, nullptr);
            state->subtitle = CreateWindowW(L"STATIC", L"Fast, local and privacy-first file verification", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kSubtitle), nullptr, nullptr);
            state->add_files = CreateWindowW(L"BUTTON", L"Add files", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kAddFiles), nullptr, nullptr);
            state->copy_results = CreateWindowW(L"BUTTON", L"Copy results", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kCopyResults), nullptr, nullptr);
            state->delete_files = CreateWindowW(L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kDeleteFiles), nullptr, nullptr);
            state->cancel_all = CreateWindowW(L"BUTTON", L"Cancel all", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kCancelAll), nullptr, nullptr);
            state->clean_all = CreateWindowW(L"BUTTON", L"Clean all", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_DISABLED, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kCleanAll), nullptr, nullptr);
            state->theme_picker = CreateWindowW(L"BUTTON", L"Theme", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kThemePicker), nullptr, nullptr);
            state->algorithm_group = CreateWindowW(L"STATIC", L"Algorithms", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            for (int index = 0; index < 6; ++index) {
                state->checks[index] = CreateWindowW(L"BUTTON", widen(filehash::algorithm_name(kAlgorithms[index])).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kAlgorithmBase + index), nullptr, nullptr);
            }
            state->drop_hint = CreateWindowW(L"STATIC", L"Drop files anywhere in this window — each file starts independently", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            state->file_progress_label = CreateWindowW(L"STATIC", L"File progress  0%", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            state->file_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            SendMessageW(state->file_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            state->progress_label = CreateWindowW(L"STATIC", L"All files  0%", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            state->progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kProgress), nullptr, nullptr);
            SendMessageW(state->progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            state->list = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kList), nullptr, nullptr);
            ListView_SetExtendedListViewStyle(state->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            const wchar_t* columns[] = {L"File", L"Status", L"Hash values"};
            const int widths[] = {420, 105, 700};
            for (int index = 0; index < 3; ++index) {
                LVCOLUMNW column{};
                column.mask = LVCF_TEXT | LVCF_WIDTH;
                column.pszText = const_cast<wchar_t*>(columns[index]);
                column.cx = widths[index];
                ListView_InsertColumn(state->list, index, &column);
            }
            state->empty_hint = CreateWindowW(L"STATIC", L"\x6587\x4ef6\x62d6\x62fd\x5230\x6b64\x5904", WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            state->empty_hint_font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->status = CreateWindowW(L"STATIC", L"Ready — add files or drag them here", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kStatus), nullptr, nullptr);
            for (HWND control : {state->title, state->subtitle, state->add_files, state->copy_results, state->delete_files, state->cancel_all, state->clean_all, state->theme_picker, state->algorithm_group, state->drop_hint, state->file_progress_label, state->file_progress, state->progress_label, state->progress, state->list, state->status}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            SendMessageW(state->empty_hint, WM_SETFONT, reinterpret_cast<WPARAM>(state->empty_hint_font), TRUE);
            SendMessageW(state->title, WM_SETFONT, reinterpret_cast<WPARAM>(state->title_font), TRUE);
            for (HWND check : state->checks) SendMessageW(check, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            DragAcceptFiles(window, TRUE);
            apply_theme(*state, state->theme, false);
            return 0;
        }
        case WM_SIZE: layout(*state, LOWORD(lparam), HIWORD(lparam)); return 0;
        case WM_COMMAND:
            switch (LOWORD(wparam)) {
                case kAddFiles: choose_files(*state); return 0;
                case kCopyResults: copy_results(*state); return 0;
                case kDeleteFiles: delete_selected(*state); return 0;
                case kCancelAll: cancel_all(*state); return 0;
                case kCleanAll: clean_all(*state); return 0;
                case kThemePicker: show_theme_menu(*state); return 0;
                default:
                    if (LOWORD(wparam) >= kAlgorithmBase && LOWORD(wparam) < kAlgorithmBase + 6 && HIWORD(wparam) == BN_CLICKED) {
                        const int index = LOWORD(wparam) - kAlgorithmBase;
                        state->algorithm_checked[index] = !state->algorithm_checked[index];
                        InvalidateRect(state->checks[index], nullptr, FALSE);
                        for (const auto& row : state->rows) if (!row.job) start_file(*state, row.id);
                        update_action_buttons(*state);
                        update_summary(*state);
                        return 0;
                    }
                    break;
            }
            break;
        case WM_DRAWITEM: {
            const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            if (item == nullptr) break;
            if (item->hwndItem == state->drop_hint) {
                draw_drop_zone(*state, *item);
                return TRUE;
            }
            if (item->hwndItem == state->algorithm_group) {
                draw_algorithm_group(*state, *item);
                return TRUE;
            }
            for (int index = 0; index < 6; ++index) {
                if (item->hwndItem == state->checks[index]) {
                    draw_algorithm_checkbox(*state, *item, index);
                    return TRUE;
                }
            }
            if (item->hwndItem == state->add_files || item->hwndItem == state->copy_results ||
                item->hwndItem == state->delete_files || item->hwndItem == state->cancel_all ||
                item->hwndItem == state->clean_all || item->hwndItem == state->theme_picker) {
                draw_button(*state, *item);
                return TRUE;
            }
            break;
        }
        case WM_DROPFILES: {
            const HDROP drop = reinterpret_cast<HDROP>(wparam);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            for (UINT index = 0; index < count; ++index) { wchar_t path[MAX_PATH * 4]; if (DragQueryFileW(drop, index, path, ARRAYSIZE(path))) add_path(*state, path); }
            DragFinish(drop);
            return 0;
        }
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lparam)->hwndFrom == state->list && reinterpret_cast<NMHDR*>(lparam)->code == LVN_KEYDOWN) {
                auto* key = reinterpret_cast<LPNMLVKEYDOWN>(lparam);
                if (key->wVKey == VK_DELETE) { delete_selected(*state); return 0; }
                if (key->wVKey == 'C' && (GetKeyState(VK_CONTROL) & 0x8000) != 0) { copy_selected_results(*state); return 0; }
            }
            if (reinterpret_cast<NMHDR*>(lparam)->hwndFrom == state->list && reinterpret_cast<NMHDR*>(lparam)->code == NM_RCLICK) {
                POINT point{};
                GetCursorPos(&point);
                show_context_menu(*state, point);
                return 0;
            }
            if (reinterpret_cast<NMHDR*>(lparam)->hwndFrom == state->list && reinterpret_cast<NMHDR*>(lparam)->code == NM_CLICK) {
                const auto* click = reinterpret_cast<NMLISTVIEW*>(lparam);
                copy_row(*state, click->iItem);
                return 0;
            }
            if (reinterpret_cast<NMHDR*>(lparam)->hwndFrom == state->list && reinterpret_cast<NMHDR*>(lparam)->code == NM_CUSTOMDRAW) {
                auto* draw = reinterpret_cast<NMLVCUSTOMDRAW*>(lparam);
                if (draw->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                    const auto& colors = palette(*state);
                    const bool selected = (draw->nmcd.uItemState & CDIS_SELECTED) != 0;
                    draw->clrText = selected ? colors.selection_text : colors.text_primary;
                    draw->clrTextBk = selected ? colors.selection :
                        (draw->nmcd.dwItemSpec % 2 == 0 ? colors.surface : colors.surface_alt);
                    return CDRF_NOTIFYSUBITEMDRAW | CDRF_NOTIFYPOSTPAINT;
                }
                if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
                    const auto& colors = palette(*state);
                    const bool selected = (draw->nmcd.uItemState & CDIS_SELECTED) != 0;
                    draw->clrText = selected ? colors.selection_text : colors.text_primary;
                    draw->clrTextBk = selected ? colors.selection :
                        (draw->nmcd.dwItemSpec % 2 == 0 ? colors.surface : colors.surface_alt);
                    if (!selected && draw->iSubItem == 1) {
                        wchar_t status[32]{};
                        ListView_GetItemText(state->list, static_cast<int>(draw->nmcd.dwItemSpec), 1,
                                             status, ARRAYSIZE(status));
                        if (wcscmp(status, L"Done") == 0) draw->clrText = colors.success;
                        else if (wcscmp(status, L"Error") == 0) draw->clrText = colors.danger;
                    }
                    return CDRF_DODEFAULT;
                }
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT) {
                    RECT item_rect{};
                    ListView_GetItemRect(state->list, static_cast<int>(draw->nmcd.dwItemSpec), &item_rect, LVIR_BOUNDS);
                    RECT line_rect{};
                    GetClientRect(state->list, &line_rect);
                    line_rect.top = item_rect.bottom - 1;
                    line_rect.bottom = item_rect.bottom;
                    HBRUSH line_brush = CreateSolidBrush(palette(*state).border);
                    FillRect(draw->nmcd.hdc, &line_rect, line_brush);
                    const HWND header = ListView_GetHeader(state->list);
                    if (header != nullptr) {
                        RECT column_rect{};
                        for (int column = 0; column < Header_GetItemCount(header) - 1; ++column) {
                            if (!Header_GetItemRect(header, column, &column_rect)) continue;
                            POINT boundary{column_rect.right, item_rect.top};
                            MapWindowPoints(header, state->list, &boundary, 1);
                            RECT vertical_line{boundary.x, item_rect.top, boundary.x + 1, item_rect.bottom};
                            FillRect(draw->nmcd.hdc, &vertical_line, line_brush);
                        }
                    }
                    DeleteObject(line_brush);
                    return CDRF_DODEFAULT;
                }
            }
            break;
        case kProgressMessage: { std::unique_ptr<ProgressMessage> progress(reinterpret_cast<ProgressMessage*>(lparam)); update_row_progress(*state, *progress); return 0; }
        case kResultMessage: { std::unique_ptr<ResultMessage> result(reinterpret_cast<ResultMessage*>(lparam)); update_row(*state, *result); return 0; }
        case kFinishedMessage:
            finish_file(*state, static_cast<std::uint64_t>(wparam));
            update_overall_progress(*state);
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            const HDC dc = reinterpret_cast<HDC>(wparam);
            const HWND control = reinterpret_cast<HWND>(lparam);
            SetBkMode(dc, TRANSPARENT);
            const auto& colors = palette(*state);
            SetTextColor(dc, control == state->title ? colors.text_primary :
                              (control == state->empty_hint ? colors.primary : colors.text_secondary));
            if (control == state->algorithm_group) {
                SetTextColor(dc, colors.text_primary);
                return reinterpret_cast<LRESULT>(state->surface_brush);
            }
            for (HWND check : state->checks) {
                if (control == check) {
                    SetTextColor(dc, colors.text_primary);
                    return reinterpret_cast<LRESULT>(state->surface_brush);
                }
            }
            return reinterpret_cast<LRESULT>(state->background);
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = 960;
            info->ptMinTrackSize.y = 560;
            return 0;
        }
        case WM_ERASEBKGND: { RECT rect; GetClientRect(window, &rect); FillRect(reinterpret_cast<HDC>(wparam), &rect, state->background); return 1; }
        case WM_DESTROY:
            for (auto& row : state->rows) if (row.job) { row.job->cancel->store(true); if (row.job->worker.joinable()) row.job->worker.join(); }
            if (state->font) DeleteObject(state->font);
            if (state->title_font) DeleteObject(state->title_font);
            if (state->empty_hint_font) DeleteObject(state->empty_hint_font);
            if (state->background) DeleteObject(state->background);
            if (state->surface_brush) DeleteObject(state->surface_brush);
            if (state->drop_zone_brush) DeleteObject(state->drop_zone_brush);
            PostQuitMessage(0);
            return 0;
        default: break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

}  // 命名空间 / Namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
    INITCOMMONCONTROLSEX common_controls{sizeof(common_controls), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&common_controls);
    const wchar_t class_name[] = L"FileHashToolWindow";
    WNDCLASSW window_class{};
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = class_name;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    RegisterClassW(&window_class);
    State state;
    const HWND window = CreateWindowExW(0, class_name, L"Lizy File Hash Tool v1.0", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 700, nullptr, nullptr, instance, &state);
    if (window == nullptr) return 1;
    ShowWindow(window, show_command);
    UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return static_cast<int>(message.wParam);
}
