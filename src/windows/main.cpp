#include "app/result_text.h"
#include "app/file_metadata.h"
#include "resource.h"
#include "core/hash_engine.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
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
constexpr int kContextCopy = 2001;
constexpr int kContextDelete = 2002;
constexpr int kContextSelectAll = 2003;
constexpr int kList = 1100;
constexpr int kStatus = 1101;
constexpr int kProgress = 1102;
constexpr int kTitle = 1103;
constexpr int kSubtitle = 1104;
constexpr int kAlgorithmBase = 1200;
constexpr UINT kResultMessage = WM_APP + 1;
constexpr UINT kProgressMessage = WM_APP + 2;
constexpr UINT kFinishedMessage = WM_APP + 3;

const filehash::HashAlgorithm kAlgorithms[] = {
    filehash::HashAlgorithm::Crc32, filehash::HashAlgorithm::Md5, filehash::HashAlgorithm::Sha1,
    filehash::HashAlgorithm::Sha256, filehash::HashAlgorithm::Sha384, filehash::HashAlgorithm::Sha512,
};

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

std::vector<filehash::HashAlgorithm> selected_algorithms(const State& state) {
    std::vector<filehash::HashAlgorithm> result;
    for (int index = 0; index < 6; ++index) {
        if (SendMessageW(state.checks[index], BM_GETCHECK, 0, 0) == BST_CHECKED) result.push_back(kAlgorithms[index]);
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
    MoveWindow(state.add_files, width - 530, 18, 94, 30, TRUE);
    MoveWindow(state.copy_results, width - 428, 18, 108, 30, TRUE);
    MoveWindow(state.delete_files, width - 310, 18, 92, 30, TRUE);
    MoveWindow(state.clean_all, width - 210, 18, 92, 30, TRUE);
    MoveWindow(state.cancel_all, width - 108, 18, 92, 30, TRUE);
    MoveWindow(state.algorithm_group, 16, 76, content_width, 48, TRUE);
    for (int index = 0; index < 6; ++index) MoveWindow(state.checks[index], 32 + index * 95, 94, 88, 20, TRUE);
    MoveWindow(state.drop_hint, 16, 132, content_width, 22, TRUE);
    MoveWindow(state.file_progress_label, 16, 157, 110, 18, TRUE);
    MoveWindow(state.file_progress, 132, 159, std::max(50, content_width - 116), 12, TRUE);
    MoveWindow(state.progress_label, 16, 180, 110, 18, TRUE);
    MoveWindow(state.progress, 132, 182, std::max(50, content_width - 116), 12, TRUE);
    const int list_height = std::max(80, height - 260);
    MoveWindow(state.list, 16, 206, content_width, list_height, TRUE);
    MoveWindow(state.empty_hint, 16, 206 + std::max(0, (list_height - 34) / 2), content_width, 34, TRUE);
    MoveWindow(state.status, 16, height - 42, content_width, 22, TRUE);
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
            state->font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->title_font = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                            DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            state->background = CreateSolidBrush(RGB(232, 240, 250));
            state->title = CreateWindowW(L"STATIC", L"Lizy File Hash Tool v1.0", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kTitle), nullptr, nullptr);
            state->subtitle = CreateWindowW(L"STATIC", L"Fast, local and privacy-first file verification", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kSubtitle), nullptr, nullptr);
            state->add_files = CreateWindowW(L"BUTTON", L"Add files", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kAddFiles), nullptr, nullptr);
            state->copy_results = CreateWindowW(L"BUTTON", L"Copy results", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kCopyResults), nullptr, nullptr);
            state->delete_files = CreateWindowW(L"BUTTON", L"Delete", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kDeleteFiles), nullptr, nullptr);
            state->cancel_all = CreateWindowW(L"BUTTON", L"Cancel all", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kCancelAll), nullptr, nullptr);
            state->clean_all = CreateWindowW(L"BUTTON", L"Clean all", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kCleanAll), nullptr, nullptr);
            state->algorithm_group = CreateWindowW(L"BUTTON", L"Algorithms", WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            for (int index = 0; index < 6; ++index) {
                state->checks[index] = CreateWindowW(L"BUTTON", widen(filehash::algorithm_name(kAlgorithms[index])).c_str(), WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kAlgorithmBase + index), nullptr, nullptr);
            }
            SendMessageW(state->checks[1], BM_SETCHECK, BST_CHECKED, 0);
            SendMessageW(state->checks[3], BM_SETCHECK, BST_CHECKED, 0);
            state->drop_hint = CreateWindowW(L"STATIC", L"Drop files anywhere in this window — each file starts independently", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            state->file_progress_label = CreateWindowW(L"STATIC", L"File progress  0%", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            state->file_progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            SendMessageW(state->file_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageW(state->file_progress, PBM_SETBARCOLOR, 0, RGB(37, 99, 235));
            SendMessageW(state->file_progress, PBM_SETBKCOLOR, 0, RGB(207, 222, 242));
            state->progress_label = CreateWindowW(L"STATIC", L"All files  0%", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
            state->progress = CreateWindowExW(0, PROGRESS_CLASSW, L"", WS_CHILD | WS_VISIBLE | PBS_SMOOTH, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kProgress), nullptr, nullptr);
            SendMessageW(state->progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
            SendMessageW(state->progress, PBM_SETBARCOLOR, 0, RGB(37, 99, 235));
            SendMessageW(state->progress, PBM_SETBKCOLOR, 0, RGB(207, 222, 242));
            state->list = CreateWindowW(WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(kList), nullptr, nullptr);
            ListView_SetExtendedListViewStyle(state->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
            ListView_SetBkColor(state->list, RGB(239, 246, 255));
            ListView_SetTextBkColor(state->list, RGB(239, 246, 255));
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
            for (HWND control : {state->title, state->subtitle, state->add_files, state->copy_results, state->delete_files, state->cancel_all, state->clean_all, state->algorithm_group, state->drop_hint, state->file_progress_label, state->file_progress, state->progress_label, state->progress, state->list, state->status}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            SendMessageW(state->empty_hint, WM_SETFONT, reinterpret_cast<WPARAM>(state->empty_hint_font), TRUE);
            SendMessageW(state->title, WM_SETFONT, reinterpret_cast<WPARAM>(state->title_font), TRUE);
            for (HWND check : state->checks) SendMessageW(check, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            DragAcceptFiles(window, TRUE);
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
                default:
                    if (LOWORD(wparam) >= kAlgorithmBase && LOWORD(wparam) < kAlgorithmBase + 6 && HIWORD(wparam) == BN_CLICKED) {
                        for (const auto& row : state->rows) if (!row.job) start_file(*state, row.id);
                        update_action_buttons(*state);
                        update_summary(*state);
                        return 0;
                    }
                    break;
            }
            break;
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
                    draw->clrText = RGB(24, 47, 79);
                    draw->clrTextBk = draw->nmcd.dwItemSpec % 2 == 0 ? RGB(245, 249, 255) : RGB(229, 239, 252);
                    return CDRF_NOTIFYPOSTPAINT;
                }
                if (draw->nmcd.dwDrawStage == CDDS_ITEMPOSTPAINT) {
                    RECT item_rect{};
                    ListView_GetItemRect(state->list, static_cast<int>(draw->nmcd.dwItemSpec), &item_rect, LVIR_BOUNDS);
                    RECT line_rect{};
                    GetClientRect(state->list, &line_rect);
                    line_rect.top = item_rect.bottom - 1;
                    line_rect.bottom = item_rect.bottom;
                    HBRUSH line_brush = CreateSolidBrush(RGB(0, 0, 0));
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
        case WM_CTLCOLORSTATIC: {
            const HDC dc = reinterpret_cast<HDC>(wparam);
            const HWND control = reinterpret_cast<HWND>(lparam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, control == state->title ? RGB(17, 45, 87) : (control == state->empty_hint ? RGB(37, 99, 235) : RGB(47, 72, 106)));
            return reinterpret_cast<LRESULT>(state->background);
        }
        case WM_ERASEBKGND: { RECT rect; GetClientRect(window, &rect); FillRect(reinterpret_cast<HDC>(wparam), &rect, state->background); return 1; }
        case WM_DESTROY:
            for (auto& row : state->rows) if (row.job) { row.job->cancel->store(true); if (row.job->worker.joinable()) row.job->worker.join(); }
            if (state->font) DeleteObject(state->font);
            if (state->title_font) DeleteObject(state->title_font);
            if (state->empty_hint_font) DeleteObject(state->empty_hint_font);
            if (state->background) DeleteObject(state->background);
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
