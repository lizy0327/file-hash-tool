#include "app/result_text.h"
#include "app/file_metadata.h"
#include "app/language.h"
#include "app/theme.h"
#include "core/hash_engine.h"

#include <gtk/gtk.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

enum Column { kIndex, kChecked, kPath, kStatus, kResult, kColumnCount };
const filehash::HashAlgorithm kAlgorithms[] = {
    filehash::HashAlgorithm::Crc32, filehash::HashAlgorithm::Md5, filehash::HashAlgorithm::Sha1,
    filehash::HashAlgorithm::Sha256, filehash::HashAlgorithm::Sha384, filehash::HashAlgorithm::Sha512,
};

struct Palette {
    const char* window_bg;
    const char* surface;
    const char* surface_alt;
    const char* text_primary;
    const char* text_secondary;
    const char* border;
    const char* primary;
    const char* primary_text;
    const char* selection;
    const char* selection_text;
    const char* progress_track;
    const char* success;
    const char* danger;
    const char* disabled_bg;
    const char* disabled_text;
    const char* drop_zone_bg;
};

constexpr std::array<Palette, filehash::ui::kThemes.size()> kPalettes{{
    {"#f8fafc", "#ffffff", "#f1f5f9", "#0f172a", "#64748b", "#e2e8f0", "#2563eb", "#ffffff", "#dbeafe", "#0f172a", "#e2e8f0", "#16a34a", "#dc2626", "#f1f5f9", "#94a3b8", "#f8fafc"},
    {"#111827", "#1f2937", "#182231", "#e5e7eb", "#9ca3af", "#374151", "#22d3ee", "#0f172a", "#0f6cbd", "#ffffff", "#374151", "#4ade80", "#f87171", "#1f2937", "#6b7280", "#151f2e"},
    {"#fffbf5", "#ffffff", "#fff7ed", "#263238", "#63737c", "#e8dbcc", "#e8792e", "#ffffff", "#fde6d1", "#263238", "#f1e1cf", "#2e7d32", "#d32f2f", "#f7f3ee", "#9e9e9e", "#fffaf4"},
    {"#f5faf8", "#ffffff", "#edf7f3", "#17352f", "#667a74", "#cfe2dc", "#0f9d83", "#ffffff", "#d9f3ec", "#17352f", "#cfe2dc", "#0f9d83", "#d32f2f", "#edf4f2", "#8b9d98", "#f7fcfa"},
    {"#faf9ff", "#ffffff", "#f6f3ff", "#29233f", "#716a84", "#ddd7ee", "#7157d9", "#ffffff", "#e9e3ff", "#29233f", "#e1dcf2", "#2e7d32", "#d32f2f", "#f5f3fa", "#9c96ac", "#fcfaff"},
    {"#171717", "#232323", "#1d1d1d", "#f4f1ea", "#aaa69d", "#45423d", "#f2a93b", "#171717", "#5b421c", "#fff8e5", "#45423d", "#4ade80", "#f87171", "#282828", "#75736e", "#1e1e1e"},
}};

struct State;
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
struct ProgressMessage { State* state; std::uint64_t id; std::uint64_t bytes_read; std::uint64_t total_bytes; };
struct ResultMessage { State* state; std::uint64_t id; filehash::HashFileResult result; };
struct FinishedMessage { State* state; std::uint64_t id; };

struct State {
    GtkWidget* window = nullptr;
    GtkWidget* copy_button = nullptr;
    GtkWidget* delete_button = nullptr;
    GtkWidget* cancel_button = nullptr;
    GtkWidget* clean_button = nullptr;
    GtkWidget* compare_button = nullptr;
    GtkWidget* confirm_button = nullptr;
    GtkWidget* language_button = nullptr;
    GtkWidget* title = nullptr;
    GtkWidget* subtitle = nullptr;
    GtkWidget* add_button = nullptr;
    GtkWidget* algorithm_frame = nullptr;
    GtkWidget* drop_hint = nullptr;
    GtkWidget* theme_combo = nullptr;
    GtkWidget* status = nullptr;
    GtkWidget* file_progress = nullptr;
    GtkWidget* progress = nullptr;
    GtkWidget* file_progress_label = nullptr;
    GtkWidget* progress_label = nullptr;
    GtkWidget* empty_hint = nullptr;
    GtkTreeSelection* selection = nullptr;
    GtkListStore* store = nullptr;
    GtkWidget* view = nullptr;
    GtkTreeViewColumn* compare_column = nullptr;
    GtkTreeViewColumn* result_columns[3]{};
    GtkWidget* checks[6]{};
    GtkCssProvider* css_provider = nullptr;
    filehash::ui::ThemeId theme = filehash::ui::ThemeId::ArcticBlue;
    filehash::ui::Language language = filehash::ui::Language::Chinese;
    bool compare_mode = false;
    std::vector<Row> rows;
    std::uint64_t next_id = 1;
};

const char* tr(const State& state, const char* chinese, const char* english) {
    return state.language == filehash::ui::Language::Chinese ? chinese : english;
}

void set_compare_mode(State& state, bool enabled);

std::vector<filehash::HashAlgorithm> selected_algorithms(const State& state) {
    std::vector<filehash::HashAlgorithm> result;
    for (int index = 0; index < 6; ++index) {
        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(state.checks[index]))) result.push_back(kAlgorithms[index]);
    }
    return result;
}

Row* find_row(State& state, std::uint64_t id) {
    for (auto& row : state.rows) if (row.id == id) return &row;
    return nullptr;
}

int find_row_index(const State& state, std::uint64_t id) {
    for (std::size_t index = 0; index < state.rows.size(); ++index) {
        if (state.rows[index].id == id) return static_cast<int>(index);
    }
    return -1;
}

bool any_running(const State& state) {
    for (const auto& row : state.rows) if (row.job) return true;
    return false;
}

void set_status(State& state, const char* text) { gtk_label_set_text(GTK_LABEL(state.status), text); }

void update_buttons(State& state) {
    gtk_widget_set_sensitive(state.copy_button, !state.rows.empty());
    gtk_widget_set_sensitive(state.delete_button, !state.rows.empty());
    gtk_widget_set_sensitive(state.clean_button, !state.rows.empty());
    gtk_widget_set_sensitive(state.cancel_button, any_running(state));
    gtk_widget_set_sensitive(state.compare_button, state.rows.size() >= 2);
    if (state.empty_hint != nullptr) gtk_widget_set_visible(state.empty_hint, state.rows.empty());
}

void update_status(State& state) {
    if (any_running(state)) {
        std::size_t count = 0;
        for (const auto& row : state.rows) if (row.job) ++count;
        const std::string text = std::string(tr(state, "正在并行计算 ", "Calculating ")) + std::to_string(count) +
            tr(state, " 个文件...", " file(s) in parallel...");
        set_status(state, text.c_str());
    } else if (state.rows.empty()) {
        set_status(state, tr(state, "准备就绪——添加文件或将文件拖到这里", "Ready — add files or drag them here"));
    } else {
        set_status(state, tr(state, "已完成——复制结果或继续添加文件", "Finished — copy results or add more files"));
    }
}

void update_progress(State& state) {
    std::uint64_t completed = 0;
    std::uint64_t total = 0;
    for (const auto& row : state.rows) {
        completed += row.progress_bytes;
        total += row.total_bytes;
    }
    const double fraction = total == 0
        ? (any_running(state) ? 0.0 : (state.rows.empty() ? 0.0 : 1.0))
        : std::min(1.0, static_cast<double>(completed) / static_cast<double>(total));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state.progress), fraction);
    const int percent = static_cast<int>(fraction * 100.0);
    const std::string text = std::string(tr(state, "全部文件  ", "All files  ")) + std::to_string(percent) + "%";
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(state.progress), text.c_str());
}

void update_file_progress(State& state, std::uint64_t id, std::uint64_t bytes_read, std::uint64_t total_bytes) {
    const double fraction = total_bytes == 0
        ? 1.0
        : std::min(1.0, static_cast<double>(bytes_read) / static_cast<double>(total_bytes));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state.file_progress), fraction);
    const std::string text = std::string(tr(state, "文件进度  ", "File  ")) + std::to_string(static_cast<int>(fraction * 100.0)) + "%";
    gtk_label_set_text(GTK_LABEL(state.file_progress_label), text.c_str());
    (void)id;
}

void update_store_row(State& state, int index, const char* status, const std::string& result) {
    if (index < 0) return;
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(state.store), &iter);
    while (valid) {
        unsigned int current = 0;
        gtk_tree_model_get(GTK_TREE_MODEL(state.store), &iter, kIndex, &current, -1);
        if (static_cast<int>(current) == index) {
            gtk_list_store_set(state.store, &iter, kStatus, status, kResult, result.c_str(), -1);
            return;
        }
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(state.store), &iter);
    }
}

void update_row_progress(State& state, const ProgressMessage& message) {
    Row* row = find_row(state, message.id);
    if (row == nullptr) return;
    row->progress_bytes = std::min(message.bytes_read, message.total_bytes);
    const int percent = message.total_bytes == 0
        ? 100
        : static_cast<int>(std::min<std::uint64_t>(100, message.bytes_read * 100 / message.total_bytes));
    const std::string status = std::string(tr(state, "计算中 ", "Running ")) + std::to_string(percent) + "%";
    update_store_row(state, find_row_index(state, message.id), status.c_str(), tr(state, "计算中...", "Calculating..."));
    update_file_progress(state, message.id, message.bytes_read, message.total_bytes);
    update_progress(state);
}

gboolean on_progress(gpointer data) {
    std::unique_ptr<ProgressMessage> message(static_cast<ProgressMessage*>(data));
    update_row_progress(*message->state, *message);
    return G_SOURCE_REMOVE;
}

gboolean on_result(gpointer data) {
    std::unique_ptr<ResultMessage> message(static_cast<ResultMessage*>(data));
    State& state = *message->state;
    Row* row = find_row(state, message->id);
    if (row == nullptr) return G_SOURCE_REMOVE;
    row->result = message->result;
    row->has_result = true;
    if (message->result.error.empty() && !message->result.cancelled) row->progress_bytes = row->total_bytes;
    const char* status = message->result.error.empty() && !message->result.cancelled
        ? tr(state, "已完成", "Done") : (message->result.cancelled ? tr(state, "已取消", "Cancelled") : tr(state, "错误", "Error"));
    update_store_row(state, find_row_index(state, message->id), status,
                     filehash::ui::format_result_values(message->result));
    if (message->result.error.empty() && !message->result.cancelled) {
        update_file_progress(state, message->id, row->total_bytes, row->total_bytes);
    }
    update_progress(state);
    return G_SOURCE_REMOVE;
}

gboolean on_finished(gpointer data) {
    std::unique_ptr<FinishedMessage> message(static_cast<FinishedMessage*>(data));
    State& state = *message->state;
    if (Row* row = find_row(state, message->id)) {
        if (row->job && row->job->worker.joinable()) row->job->worker.join();
        row->job.reset();
    }
    update_buttons(state);
    update_status(state);
    update_progress(state);
    return G_SOURCE_REMOVE;
}

void start_file(State& state, std::uint64_t id) {
    Row* row = find_row(state, id);
    if (row == nullptr || row->job) return;
    const auto algorithms = selected_algorithms(state);
    if (algorithms.empty()) {
        set_status(state, tr(state, "请至少选择一种算法", "Select at least one algorithm"));
        return;
    }
    const auto path = row->path;
    row->job = std::make_unique<Job>();
    row->job->cancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = row->job->cancel;
    row->progress_bytes = 0;
    row->has_result = false;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state.file_progress), 0.0);
    gtk_label_set_text(GTK_LABEL(state.file_progress_label), tr(state, "文件进度  0%", "File  0%"));
    row->job->worker = std::thread([&state, id, path, algorithms, cancel] {
        std::atomic<int> last_percent{-1};
        const auto result = filehash::hash_file(path, algorithms, [cancel] { return cancel->load(); },
            [&state, id, &last_percent](std::uint64_t bytes, std::uint64_t total) {
                const int percent = total == 0 ? 100 : static_cast<int>(std::min<std::uint64_t>(100, bytes * 100 / total));
                if (percent == last_percent.load() && percent != 100) return;
                last_percent.store(percent);
                g_idle_add(on_progress, new ProgressMessage{&state, id, bytes, total});
            });
        g_idle_add(on_result, new ResultMessage{&state, id, result});
        g_idle_add(on_finished, new FinishedMessage{&state, id});
    });
}

void append_path(State& state, const char* filename) {
    if (filename == nullptr || *filename == '\0') return;
    std::filesystem::path path(filename);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) return;
    Row row;
    row.id = state.next_id++;
    row.path = std::filesystem::absolute(path, error);
    if (error) row.path = path;
    row.total_bytes = std::filesystem::file_size(row.path, error);
    state.rows.push_back(std::move(row));
    const int index = static_cast<int>(state.rows.size() - 1);
    GtkTreeIter iter;
    gtk_list_store_append(state.store, &iter);
    gtk_list_store_set(state.store, &iter, kIndex, static_cast<unsigned int>(index),
                       kPath, state.rows.back().path.string().c_str(), kStatus, tr(state, "排队中", "Queued"),
                       kResult, tr(state, "自动开始计算...", "Starting automatically..."), -1);
    start_file(state, state.rows.back().id);
    update_buttons(state);
    update_status(state);
    update_progress(state);
}

void choose_files(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new(tr(state, "选择文件", "Select files"), GTK_WINDOW(state.window), GTK_FILE_CHOOSER_ACTION_OPEN,
        tr(state, "取消", "Cancel"), GTK_RESPONSE_CANCEL, tr(state, "添加", "Add"), GTK_RESPONSE_ACCEPT, nullptr);
    gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(dialog), TRUE);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GSList* files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(dialog));
        for (GSList* item = files; item != nullptr; item = item->next) {
            append_path(state, static_cast<const char*>(item->data));
            g_free(item->data);
        }
        g_slist_free(files);
    }
    gtk_widget_destroy(dialog);
}

void cancel_all(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    for (auto& row : state.rows) if (row.job) row.job->cancel->store(true);
    set_status(state, tr(state, "正在取消计算中的文件...", "Cancelling active files..."));
}

void delete_selected(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    GList* selected = gtk_tree_selection_get_selected_rows(state.selection, nullptr);
    std::vector<int> indexes;
    for (GList* item = selected; item != nullptr; item = item->next) {
        GtkTreePath* tree_path = static_cast<GtkTreePath*>(item->data);
        gint* indices = gtk_tree_path_get_indices(tree_path);
        if (indices) indexes.push_back(indices[0]);
        gtk_tree_path_free(tree_path);
    }
    g_list_free(selected);
    std::sort(indexes.rbegin(), indexes.rend());
    indexes.erase(std::unique(indexes.begin(), indexes.end()), indexes.end());
    for (int index : indexes) {
        if (index < 0 || index >= static_cast<int>(state.rows.size())) continue;
        if (state.rows[index].job) {
            state.rows[index].job->cancel->store(true);
            if (state.rows[index].job->worker.joinable()) state.rows[index].job->worker.join();
        }
        state.rows.erase(state.rows.begin() + index);
        GtkTreeIter iter;
        gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(state.store), &iter);
        while (valid) {
            unsigned int current = 0;
            gtk_tree_model_get(GTK_TREE_MODEL(state.store), &iter, kIndex, &current, -1);
            if (static_cast<int>(current) == index) {
                gtk_list_store_remove(state.store, &iter);
                break;
            }
            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(state.store), &iter);
        }
    }
    GtkTreeIter iter;
    unsigned int index = 0;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(state.store), &iter);
    while (valid) {
        gtk_list_store_set(state.store, &iter, kIndex, index++, -1);
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(state.store), &iter);
    }
    if (state.compare_mode && state.rows.size() < 2) set_compare_mode(state, false);
    update_buttons(state);
    update_status(state);
    update_progress(state);
}

std::vector<int> selected_indexes(const State& state) {
    std::vector<int> indexes;
    GList* selected = gtk_tree_selection_get_selected_rows(state.selection, nullptr);
    for (GList* item = selected; item != nullptr; item = item->next) {
        GtkTreePath* tree_path = static_cast<GtkTreePath*>(item->data);
        gint* indices = gtk_tree_path_get_indices(tree_path);
        if (indices) indexes.push_back(indices[0]);
        gtk_tree_path_free(tree_path);
    }
    g_list_free(selected);
    std::sort(indexes.begin(), indexes.end());
    return indexes;
}

std::vector<int> all_indexes(const State& state) {
    std::vector<int> indexes;
    indexes.reserve(state.rows.size());
    for (std::size_t index = 0; index < state.rows.size(); ++index) indexes.push_back(static_cast<int>(index));
    return indexes;
}

std::string copy_result_text(const State& state, const std::vector<int>& indexes) {
    std::string text;
    for (const int index : indexes) {
        if (index < 0 || index >= static_cast<int>(state.rows.size())) continue;
        const auto& row = state.rows[static_cast<std::size_t>(index)];
        text += std::string(tr(state, "文件：", "File: ")) + row.path.string() + "\n";
        text += std::string(tr(state, "大小：", "Size: ")) + std::to_string(row.total_bytes) + tr(state, " 字节\n", " bytes\n");
        text += std::string(tr(state, "修改时间：", "Modified: ")) + filehash::ui::format_file_time(row.path) + "\n";
        if (row.has_result) {
            text += std::string(tr(state, "状态：", "Status: ")) +
                std::string(row.result.error.empty() && !row.result.cancelled ? tr(state, "已完成", "Done") : (row.result.cancelled ? tr(state, "已取消", "Cancelled") : tr(state, "错误", "Error"))) + "\n";
            text += filehash::ui::format_result_lines(row.result);
        } else if (row.job) {
            text += tr(state, "状态：计算中...", "Status: Calculating...");
        } else {
            text += tr(state, "状态：排队中", "Status: Queued");
        }
        text += "\n\n";
    }
    return text;
}

void copy_results(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    const std::string text = copy_result_text(state, all_indexes(state));
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
    set_status(state, tr(state, "全部结果已复制。", "All results copied."));
}

void copy_selected_results(GtkMenuItem*, gpointer data) {
    State& state = *static_cast<State*>(data);
    const auto indexes = selected_indexes(state);
    if (indexes.empty()) {
        set_status(state, tr(state, "请先选择一条结果记录。", "Select a result row first."));
        return;
    }
    const std::string text = copy_result_text(state, indexes);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
    set_status(state, tr(state, "结果已复制。", "Result copied."));
}

void copy_row(State& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.rows.size())) return;
    const std::string text = copy_result_text(state, {index});
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
    set_status(state, tr(state, "结果已复制。", "Result copied."));
}

void select_all(GtkMenuItem*, gpointer data) {
    State& state = *static_cast<State*>(data);
    gtk_tree_selection_select_all(state.selection);
    set_status(state, tr(state, "已全选所有记录。", "All rows selected."));
}

void clean_all(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    if (state.compare_mode) set_compare_mode(state, false);
    for (auto& row : state.rows) if (row.job) {
        row.job->cancel->store(true);
        if (row.job->worker.joinable()) row.job->worker.join();
    }
    state.rows.clear();
    gtk_list_store_clear(state.store);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state.file_progress), 0.0);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state.progress), 0.0);
    gtk_label_set_text(GTK_LABEL(state.file_progress_label), tr(state, "文件进度  0%", "File  0%"));
    gtk_label_set_text(GTK_LABEL(state.progress_label), tr(state, "全部文件  0%", "All files  0%"));
    update_buttons(state);
    update_status(state);
}

void set_compare_mode(State& state, const bool enabled) {
    state.compare_mode = enabled;
    if (enabled) {
        gtk_tree_view_column_set_visible(state.compare_column, TRUE);
        gtk_button_set_label(GTK_BUTTON(state.compare_button), tr(state, "取消比较", "Cancel"));
        gtk_widget_set_visible(state.confirm_button, TRUE);
        set_status(state, tr(state, "请选择两条记录，再点击“确认”。", "Select two records, then click Confirm."));
    } else {
        GtkTreeIter iter;
        gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(state.store), &iter);
        while (valid) {
            gtk_list_store_set(state.store, &iter, kChecked, FALSE, -1);
            valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(state.store), &iter);
        }
        gtk_tree_view_column_set_visible(state.compare_column, FALSE);
        gtk_button_set_label(GTK_BUTTON(state.compare_button), tr(state, "比较", "Compare"));
        gtk_widget_set_visible(state.confirm_button, FALSE);
    }
    update_buttons(state);
}

void compare_files(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    std::vector<int> indexes;
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(state.store), &iter);
    while (valid) {
        gboolean checked = FALSE;
        unsigned int index = 0;
        gtk_tree_model_get(GTK_TREE_MODEL(state.store), &iter, kIndex, &index, kChecked, &checked, -1);
        if (checked) indexes.push_back(static_cast<int>(index));
        valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(state.store), &iter);
    }
    if (indexes.size() != 2) {
        set_status(state, tr(state, "请选择恰好两条记录，再点击“确认”。", "Select exactly two files, then click Confirm."));
        return;
    }
    const Row& left = state.rows[static_cast<std::size_t>(indexes[0])];
    const Row& right = state.rows[static_cast<std::size_t>(indexes[1])];
    if (left.job || right.job || !left.has_result || !right.has_result) {
        set_status(state, tr(state, "请等待两条记录都完成校验。", "Please wait until both files finish hashing."));
        return;
    }
    if (!left.result.error.empty() || !right.result.error.empty() || left.result.cancelled || right.result.cancelled) {
        set_status(state, tr(state, "校验失败或已取消的记录不能比较。", "Cannot compare files with a failed or cancelled result."));
        return;
    }
    const bool equal = filehash::hash_results_equal(left.result, right.result);
    GtkWidget* dialog = gtk_message_dialog_new(GTK_WINDOW(state.window), GTK_DIALOG_MODAL,
        equal ? GTK_MESSAGE_INFO : GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
        "%s", equal ? tr(state, "这两个文件一致。", "The two files are identical.")
                     : tr(state, "这两个文件不一致。", "The two files are different."));
    gtk_window_set_title(GTK_WINDOW(dialog), tr(state, "比较结果", "Comparison result"));
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    set_status(state, equal ? tr(state, "比较结果：两个文件一致。", "Compare result: the two files are identical.")
                           : tr(state, "比较结果：两个文件不一致。", "Compare result: the two files are different."));
    set_compare_mode(state, false);
}

void compare_button_clicked(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    if (state.compare_mode) {
        set_compare_mode(state, false);
        set_status(state, tr(state, "已取消比较。", "Comparison cancelled."));
    } else {
        set_compare_mode(state, true);
    }
}

void compare_toggled(GtkCellRendererToggle*, gchar* path_text, gpointer data) {
    State& state = *static_cast<State*>(data);
    if (!state.compare_mode) return;
    GtkTreePath* path = gtk_tree_path_new_from_string(path_text);
    GtkTreeIter iter;
    if (path != nullptr && gtk_tree_model_get_iter(GTK_TREE_MODEL(state.store), &iter, path)) {
        gboolean checked = FALSE;
        gtk_tree_model_get(GTK_TREE_MODEL(state.store), &iter, kChecked, &checked, -1);
        gtk_list_store_set(state.store, &iter, kChecked, checked ? FALSE : TRUE, -1);
    }
    if (path != nullptr) gtk_tree_path_free(path);
}

gboolean on_key_press(GtkWidget*, GdkEventKey* event, gpointer data) {
    State& state = *static_cast<State*>(data);
    if (event->keyval == GDK_KEY_Escape && state.compare_mode) {
        set_compare_mode(state, false);
        set_status(state, tr(state, "已取消比较。", "Comparison cancelled."));
        return TRUE;
    }
    if (event->keyval == GDK_KEY_Delete) {
        delete_selected(nullptr, data);
        return TRUE;
    }
    if ((event->state & GDK_CONTROL_MASK) != 0 && gdk_keyval_to_lower(event->keyval) == GDK_KEY_c) {
        copy_selected_results(nullptr, data);
        return TRUE;
    }
    return FALSE;
}

gboolean on_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    if (event->type != GDK_BUTTON_PRESS) return FALSE;
    State& state = *static_cast<State*>(data);
    GtkTreePath* path = nullptr;
    if (!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), static_cast<int>(event->x), static_cast<int>(event->y),
                                       &path, nullptr, nullptr, nullptr)) return FALSE;
    gint* indices = gtk_tree_path_get_indices(path);
    const int index = indices ? indices[0] : -1;
    if (event->button == 1) {
        if (state.compare_mode) {
            GtkTreeIter iter;
            if (gtk_tree_model_get_iter(GTK_TREE_MODEL(state.store), &iter, path)) {
                gboolean checked = FALSE;
                gtk_tree_model_get(GTK_TREE_MODEL(state.store), &iter, kChecked, &checked, -1);
                gtk_list_store_set(state.store, &iter, kChecked, checked ? FALSE : TRUE, -1);
                gtk_tree_selection_unselect_all(state.selection);
                gtk_tree_selection_select_path(state.selection, path);
            }
        } else {
            copy_row(state, index);
        }
        gtk_tree_path_free(path);
        return state.compare_mode ? TRUE : FALSE;
    }
    if (event->button != 3) {
        gtk_tree_path_free(path);
        return FALSE;
    }
    if (!gtk_tree_selection_path_is_selected(state.selection, path)) {
        gtk_tree_selection_unselect_all(state.selection);
        gtk_tree_selection_select_path(state.selection, path);
    }
    gtk_tree_path_free(path);

    GtkWidget* menu = gtk_menu_new();
    GtkWidget* copy = gtk_menu_item_new_with_label("Copy result");
    GtkWidget* remove = gtk_menu_item_new_with_label("Delete");
    GtkWidget* select = gtk_menu_item_new_with_label("Select all");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), copy);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), remove);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), select);
    g_signal_connect(copy, "activate", G_CALLBACK(copy_selected_results), &state);
    g_signal_connect(remove, "activate", G_CALLBACK(delete_selected), &state);
    g_signal_connect(select, "activate", G_CALLBACK(select_all), &state);
    g_signal_connect(menu, "deactivate", G_CALLBACK(+[](GtkWidget* widget, gpointer) { gtk_widget_destroy(widget); }), nullptr);
    gtk_widget_show_all(menu);
#if GTK_CHECK_VERSION(3, 22, 0)
    gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
#else
    gtk_menu_popup(GTK_MENU(menu), nullptr, nullptr, nullptr, nullptr, event->button, event->time);
#endif
    return TRUE;
}

void drag_received(GtkWidget*, GdkDragContext*, gint, gint, GtkSelectionData* selection, guint, guint, gpointer data) {
    State& state = *static_cast<State*>(data);
    gchar** uris = gtk_selection_data_get_uris(selection);
    if (!uris) return;
    for (gchar** uri = uris; *uri; ++uri) {
        GError* error = nullptr;
        gchar* filename = g_filename_from_uri(*uri, nullptr, &error);
        if (filename) {
            append_path(state, filename);
            g_free(filename);
        }
        if (error) g_error_free(error);
    }
    g_strfreev(uris);
}

gboolean on_close(GtkWidget*, GdkEvent*, gpointer data) {
    State& state = *static_cast<State*>(data);
    for (auto& row : state.rows) if (row.job) {
        row.job->cancel->store(true);
        if (row.job->worker.joinable()) row.job->worker.join();
    }
    gtk_main_quit();
    return FALSE;
}

std::string settings_path() {
    gchar* directory = g_build_filename(g_get_user_config_dir(), "lizy-file-hash-tool", nullptr);
    gchar* path = g_build_filename(directory, "settings.conf", nullptr);
    std::string result(path);
    g_free(path);
    g_free(directory);
    return result;
}

std::string language_settings_path() {
    gchar* directory = g_build_filename(g_get_user_config_dir(), "lizy-file-hash-tool", nullptr);
    gchar* path = g_build_filename(directory, "language.conf", nullptr);
    std::string result(path);
    g_free(path);
    g_free(directory);
    return result;
}

filehash::ui::Language load_language() {
    gchar* content = nullptr;
    gsize length = 0;
    const std::string path = language_settings_path();
    if (!g_file_get_contents(path.c_str(), &content, &length, nullptr) || content == nullptr) {
        return filehash::ui::Language::Chinese;
    }
    char* end = nullptr;
    const unsigned long index = std::strtoul(content, &end, 10);
    const bool valid = end != content && index < 2;
    g_free(content);
    return valid ? filehash::ui::language_from_index(static_cast<std::size_t>(index)) : filehash::ui::Language::Chinese;
}

void save_language(const filehash::ui::Language language) {
    gchar* directory = g_build_filename(g_get_user_config_dir(), "lizy-file-hash-tool", nullptr);
    if (g_mkdir_with_parents(directory, 0700) == 0) {
        const std::string value = std::to_string(filehash::ui::language_index(language));
        g_file_set_contents(language_settings_path().c_str(), value.c_str(), static_cast<gssize>(value.size()), nullptr);
    }
    g_free(directory);
}

filehash::ui::ThemeId load_theme() {
    gchar* content = nullptr;
    gsize length = 0;
    const std::string path = settings_path();
    if (!g_file_get_contents(path.c_str(), &content, &length, nullptr) || content == nullptr) {
        return filehash::ui::ThemeId::ArcticBlue;
    }
    char* end = nullptr;
    const unsigned long index = std::strtoul(content, &end, 10);
    const bool valid = end != content && index < filehash::ui::kThemes.size();
    g_free(content);
    if (!valid) return filehash::ui::ThemeId::ArcticBlue;
    return filehash::ui::theme_from_index(static_cast<std::size_t>(index));
}

void save_theme(const filehash::ui::ThemeId theme) {
    gchar* directory = g_build_filename(g_get_user_config_dir(), "lizy-file-hash-tool", nullptr);
    if (g_mkdir_with_parents(directory, 0700) == 0) {
        gchar* path = g_build_filename(directory, "settings.conf", nullptr);
        const std::string value = std::to_string(filehash::ui::theme_index(theme));
        g_file_set_contents(path, value.c_str(), static_cast<gssize>(value.size()), nullptr);
        g_free(path);
    }
    g_free(directory);
}

void apply_theme(State& state, const filehash::ui::ThemeId theme, const bool persist) {
    state.theme = theme;
    const auto& colors = kPalettes[filehash::ui::theme_index(theme)];
    std::ostringstream css;
    css
        << "window { background: " << colors.window_bg << "; color: " << colors.text_primary << "; }"
        << ".title { font-size: 24px; font-weight: 700; color: " << colors.text_primary << "; }"
        << ".subtitle, .hint, .status, label { color: " << colors.text_secondary << "; }"
        << ".empty-hint { color: " << colors.primary << "; font-size: 18px; font-weight: 600; }"
        << ".algorithm-card, .results-card { background: " << colors.surface << "; border: 1px solid " << colors.border << "; border-radius: 8px; padding: 10px; }"
        << ".drop-zone { background: " << colors.drop_zone_bg << "; border: 1px dashed " << colors.primary << "; border-radius: 8px; padding: 18px; }"
        << "button { background: " << colors.surface << "; color: " << colors.text_primary << "; border: 1px solid " << colors.border << "; border-radius: 6px; padding: 7px 14px; }"
        << "button:hover { background: " << colors.surface_alt << "; border-color: " << colors.primary << "; }"
        << "button:disabled { background: " << colors.disabled_bg << "; color: " << colors.disabled_text << "; }"
        << ".primary { background: " << colors.primary << "; color: " << colors.primary_text << "; border-color: " << colors.primary << "; }"
        << ".danger { color: " << colors.danger << "; border-color: " << colors.danger << "; }"
        << "combobox button { min-width: 135px; }"
        << "menu, menuitem { background: " << colors.surface << "; color: " << colors.text_primary << "; }"
        << "checkbutton { color: " << colors.text_primary << "; }"
        << "treeview { background: " << colors.surface << "; color: " << colors.text_primary << "; -GtkTreeView-horizontal-separator: 8; }"
        << "treeview.view { -GtkTreeView-grid-line-color: " << colors.border << "; -GtkTreeView-grid-line-width: 1; }"
        << "treeview.view:selected { background: " << colors.selection << "; color: " << colors.selection_text << "; }"
        << "header button { background: " << colors.surface_alt << "; color: " << colors.text_primary << "; }"
        << "progressbar trough { min-height: 10px; border-radius: 5px; background: " << colors.progress_track << "; }"
        << "progressbar progress { min-height: 10px; border-radius: 5px; background: " << colors.primary << "; }";

    GdkScreen* screen = gdk_screen_get_default();
    if (state.css_provider != nullptr) {
        gtk_style_context_remove_provider_for_screen(screen, GTK_STYLE_PROVIDER(state.css_provider));
        g_object_unref(state.css_provider);
    }
    state.css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(state.css_provider, css.str().c_str(), -1, nullptr);
    gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(state.css_provider),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    if (persist) save_theme(theme);
}

void theme_changed(GtkComboBox* combo, gpointer data) {
    State& state = *static_cast<State*>(data);
    const int index = gtk_combo_box_get_active(combo);
    if (index >= 0) apply_theme(state, filehash::ui::theme_from_index(static_cast<std::size_t>(index)), true);
}

const char* theme_name(const State& state, const filehash::ui::ThemeId theme) {
    if (state.language == filehash::ui::Language::English) return filehash::ui::theme_info(theme).name;
    static constexpr const char* names[] = {"极地蓝", "午夜青", "暖橙", "翡翠雾", "紫罗兰云", "石墨琥珀"};
    return names[filehash::ui::theme_index(theme)];
}

void apply_language(State& state, const filehash::ui::Language language, const bool persist) {
    state.language = language;
    gtk_label_set_text(GTK_LABEL(state.title), "Lizy File Hash Tool v1.1");
    gtk_label_set_text(GTK_LABEL(state.subtitle), tr(state, "快速、本地、隐私优先的文件校验", "Fast, local and privacy-first file verification"));
    gtk_button_set_label(GTK_BUTTON(state.add_button), tr(state, "添加文件", "Add files"));
    gtk_button_set_label(GTK_BUTTON(state.copy_button), tr(state, "复制结果", "Copy results"));
    gtk_button_set_label(GTK_BUTTON(state.delete_button), tr(state, "删除", "Delete"));
    gtk_button_set_label(GTK_BUTTON(state.clean_button), tr(state, "清空全部", "Clean all"));
    gtk_button_set_label(GTK_BUTTON(state.cancel_button), tr(state, "取消全部", "Cancel all"));
    gtk_button_set_label(GTK_BUTTON(state.compare_button), state.compare_mode ? tr(state, "取消比较", "Cancel") : tr(state, "比较", "Compare"));
    gtk_button_set_label(GTK_BUTTON(state.confirm_button), tr(state, "确认", "Confirm"));
    gtk_button_set_label(GTK_BUTTON(state.language_button), state.language == filehash::ui::Language::Chinese ? "English" : "中文");
    gtk_frame_set_label(GTK_FRAME(state.algorithm_frame), tr(state, "算法", "Algorithms"));
    gtk_label_set_text(GTK_LABEL(state.drop_hint), tr(state, "将文件拖到窗口任意位置——每个文件独立开始计算",
                                                       "Drop files anywhere in this window — each file starts independently"));
    gtk_label_set_text(GTK_LABEL(state.file_progress_label), tr(state, "文件进度  0%", "File  0%"));
    gtk_label_set_text(GTK_LABEL(state.progress_label), tr(state, "全部文件  0%", "All files  0%"));
    gtk_label_set_text(GTK_LABEL(state.empty_hint), tr(state, "文件拖拽到此处", "Drop files here"));
    const char* titles[] = {tr(state, "文件", "File"), tr(state, "状态", "Status"), tr(state, "校验结果", "Hash values")};
    for (int index = 0; index < 3; ++index) gtk_tree_view_column_set_title(state.result_columns[index], titles[index]);
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(state.theme_combo));
    for (const auto& theme : filehash::ui::kThemes) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.theme_combo), theme_name(state, theme.id));
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.theme_combo), static_cast<int>(filehash::ui::theme_index(state.theme)));
    if (persist) save_language(language);
    update_status(state);
    apply_theme(state, state.theme, false);
}

void language_button_clicked(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    apply_language(state, state.language == filehash::ui::Language::Chinese ? filehash::ui::Language::English : filehash::ui::Language::Chinese, true);
}

}  // 命名空间 / Namespace

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    State state;
    state.theme = load_theme();
    state.language = load_language();
    state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state.window), "Lizy File Hash Tool v1.1");
    gtk_window_set_default_size(GTK_WINDOW(state.window), 1200, 700);
    g_signal_connect(state.window, "delete-event", G_CALLBACK(on_close), &state);

    GtkTargetEntry targets[] = {{const_cast<gchar*>("text/uri-list"), 0, 0}};
    gtk_drag_dest_set(state.window, GTK_DEST_DEFAULT_ALL, targets, 1, GDK_ACTION_COPY);
    g_signal_connect(state.window, "drag-data-received", G_CALLBACK(drag_received), &state);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(root), 18);
    gtk_container_add(GTK_CONTAINER(state.window), root);
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_box_pack_start(GTK_BOX(root), header, FALSE, FALSE, 0);
    GtkWidget* heading = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(header), heading, TRUE, TRUE, 0);
    state.title = gtk_label_new("Lizy File Hash Tool v1.1");
    state.subtitle = gtk_label_new("Fast, local and privacy-first file verification");
    gtk_style_context_add_class(gtk_widget_get_style_context(state.title), "title");
    gtk_style_context_add_class(gtk_widget_get_style_context(state.subtitle), "subtitle");
    gtk_label_set_xalign(GTK_LABEL(state.title), 0.0F);
    gtk_label_set_xalign(GTK_LABEL(state.subtitle), 0.0F);
    gtk_box_pack_start(GTK_BOX(heading), state.title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(heading), state.subtitle, FALSE, FALSE, 0);

    state.add_button = gtk_button_new_with_label("Add files");
    state.copy_button = gtk_button_new_with_label("Copy results");
    state.delete_button = gtk_button_new_with_label("Delete");
    state.cancel_button = gtk_button_new_with_label("Cancel all");
    state.clean_button = gtk_button_new_with_label("Clean all");
    state.compare_button = gtk_button_new_with_label("Compare");
    state.confirm_button = gtk_button_new_with_label("Confirm");
    gtk_widget_set_visible(state.confirm_button, FALSE);
    gtk_widget_set_sensitive(state.copy_button, FALSE);
    gtk_widget_set_sensitive(state.delete_button, FALSE);
    gtk_widget_set_sensitive(state.cancel_button, FALSE);
    gtk_widget_set_sensitive(state.clean_button, FALSE);
    gtk_widget_set_sensitive(state.compare_button, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(state.add_button), "primary");
    gtk_style_context_add_class(gtk_widget_get_style_context(state.delete_button), "danger");
    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_end(GTK_BOX(header), actions, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), state.add_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), state.copy_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), state.delete_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), state.clean_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), state.cancel_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), state.compare_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(actions), state.confirm_button, FALSE, FALSE, 0);
    state.language_button = gtk_button_new_with_label("English");
    gtk_box_pack_start(GTK_BOX(actions), state.language_button, FALSE, FALSE, 0);
    state.theme_combo = gtk_combo_box_text_new();
    for (const auto& theme : filehash::ui::kThemes) {
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state.theme_combo), theme.name);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(state.theme_combo), static_cast<int>(filehash::ui::theme_index(state.theme)));
    gtk_box_pack_start(GTK_BOX(actions), state.theme_combo, FALSE, FALSE, 0);
    g_signal_connect(state.add_button, "clicked", G_CALLBACK(choose_files), &state);
    g_signal_connect(state.copy_button, "clicked", G_CALLBACK(copy_results), &state);
    g_signal_connect(state.delete_button, "clicked", G_CALLBACK(delete_selected), &state);
    g_signal_connect(state.cancel_button, "clicked", G_CALLBACK(cancel_all), &state);
    g_signal_connect(state.clean_button, "clicked", G_CALLBACK(clean_all), &state);
    g_signal_connect(state.compare_button, "clicked", G_CALLBACK(compare_button_clicked), &state);
    g_signal_connect(state.confirm_button, "clicked", G_CALLBACK(compare_files), &state);
    g_signal_connect(state.language_button, "clicked", G_CALLBACK(language_button_clicked), &state);
    g_signal_connect(state.theme_combo, "changed", G_CALLBACK(theme_changed), &state);

    state.algorithm_frame = gtk_frame_new("Algorithms");
    gtk_style_context_add_class(gtk_widget_get_style_context(state.algorithm_frame), "algorithm-card");
    GtkWidget* algorithm_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(algorithm_box), 4);
    gtk_container_add(GTK_CONTAINER(state.algorithm_frame), algorithm_box);
    for (int index = 0; index < 6; ++index) {
        state.checks[index] = gtk_check_button_new_with_label(filehash::algorithm_name(kAlgorithms[index]));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.checks[index]), index == 1 || index == 3);
        gtk_box_pack_start(GTK_BOX(algorithm_box), state.checks[index], FALSE, FALSE, 0);
        g_signal_connect(state.checks[index], "toggled", G_CALLBACK(+[](GtkToggleButton*, gpointer data) {
            State& owner = *static_cast<State*>(data);
            if (!any_running(owner)) for (const auto& row : owner.rows) start_file(owner, row.id);
        }), &state);
    }
    gtk_box_pack_start(GTK_BOX(root), state.algorithm_frame, FALSE, FALSE, 0);
    GtkWidget* drop_zone = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(drop_zone), "drop-zone");
    GtkWidget* drop_icon = gtk_image_new_from_icon_name("document-open-symbolic", GTK_ICON_SIZE_LARGE_TOOLBAR);
    state.drop_hint = gtk_label_new("Drop files anywhere in this window — each file starts independently");
    gtk_style_context_add_class(gtk_widget_get_style_context(state.drop_hint), "hint");
    gtk_label_set_xalign(GTK_LABEL(state.drop_hint), 0.0F);
    gtk_box_pack_start(GTK_BOX(drop_zone), drop_icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(drop_zone), state.drop_hint, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), drop_zone, FALSE, FALSE, 0);
    GtkWidget* progress_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    state.file_progress_label = gtk_label_new("File  0%");
    state.progress_label = gtk_label_new("All files  0%");
    state.file_progress = gtk_progress_bar_new();
    state.progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state.file_progress), FALSE);
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state.progress), FALSE);
    gtk_box_pack_start(GTK_BOX(progress_box), state.file_progress_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(progress_box), state.file_progress, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(progress_box), state.progress_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(progress_box), state.progress, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), progress_box, FALSE, FALSE, 0);

    state.store = gtk_list_store_new(kColumnCount, G_TYPE_UINT, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(state.store));
    state.view = view;
    state.selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(state.selection, GTK_SELECTION_MULTIPLE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(view), GTK_TREE_VIEW_GRID_LINES_BOTH);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view), TRUE);
    g_signal_connect(view, "key-press-event", G_CALLBACK(on_key_press), &state);
    g_signal_connect(view, "button-press-event", G_CALLBACK(on_button_press), &state);
    GtkCellRenderer* toggle_renderer = gtk_cell_renderer_toggle_new();
    state.compare_column = gtk_tree_view_column_new_with_attributes("", toggle_renderer, "active", kChecked, nullptr);
    gtk_tree_view_column_set_sizing(state.compare_column, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(state.compare_column, 34);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view), state.compare_column);
    g_signal_connect(toggle_renderer, "toggled", G_CALLBACK(compare_toggled), &state);
    gtk_tree_view_column_set_visible(state.compare_column, FALSE);
    const char* titles[] = {"File", "Status", "Hash values"};
    const int columns[] = {kPath, kStatus, kResult};
    for (int index = 0; index < 3; ++index) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        if (index == 2) g_object_set(renderer, "family", "monospace", nullptr);
        GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(titles[index], renderer, "text", columns[index], nullptr);
        state.result_columns[index] = column;
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_column_set_expand(column, index != 1);
        gtk_tree_view_append_column(GTK_TREE_VIEW(view), column);
    }
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_style_context_add_class(gtk_widget_get_style_context(scroll), "results-card");
    GtkWidget* results_overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(results_overlay), scroll);
    state.empty_hint = gtk_label_new("文件拖拽到此处");
    gtk_style_context_add_class(gtk_widget_get_style_context(state.empty_hint), "empty-hint");
    gtk_widget_set_halign(state.empty_hint, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(state.empty_hint, GTK_ALIGN_CENTER);
    gtk_overlay_add_overlay(GTK_OVERLAY(results_overlay), state.empty_hint);
    gtk_box_pack_start(GTK_BOX(root), results_overlay, TRUE, TRUE, 0);
    state.status = gtk_label_new("准备就绪——添加文件或将文件拖到这里");
    gtk_style_context_add_class(gtk_widget_get_style_context(state.status), "status");
    gtk_label_set_xalign(GTK_LABEL(state.status), 0.0F);
    gtk_box_pack_start(GTK_BOX(root), state.status, FALSE, FALSE, 0);

    apply_theme(state, state.theme, false);
    apply_language(state, state.language, false);
    gtk_widget_show_all(state.window);
    gtk_main();
    if (state.css_provider != nullptr) g_object_unref(state.css_provider);
    g_object_unref(state.store);
    return 0;
}
