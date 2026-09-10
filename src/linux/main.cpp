#include "app/result_text.h"
#include "app/file_metadata.h"
#include "core/hash_engine.h"

#include <gtk/gtk.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

enum Column { kIndex, kPath, kStatus, kResult, kColumnCount };
const filehash::HashAlgorithm kAlgorithms[] = {
    filehash::HashAlgorithm::Crc32, filehash::HashAlgorithm::Md5, filehash::HashAlgorithm::Sha1,
    filehash::HashAlgorithm::Sha256, filehash::HashAlgorithm::Sha384, filehash::HashAlgorithm::Sha512,
};

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
    GtkWidget* status = nullptr;
    GtkWidget* file_progress = nullptr;
    GtkWidget* progress = nullptr;
    GtkWidget* file_progress_label = nullptr;
    GtkWidget* progress_label = nullptr;
    GtkWidget* empty_hint = nullptr;
    GtkTreeSelection* selection = nullptr;
    GtkListStore* store = nullptr;
    GtkWidget* checks[6]{};
    std::vector<Row> rows;
    std::uint64_t next_id = 1;
};

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
    if (state.empty_hint != nullptr) gtk_widget_set_visible(state.empty_hint, state.rows.empty());
}

void update_status(State& state) {
    if (any_running(state)) {
        std::size_t count = 0;
        for (const auto& row : state.rows) if (row.job) ++count;
        const std::string text = "Calculating " + std::to_string(count) + " file(s) in parallel...";
        set_status(state, text.c_str());
    } else if (state.rows.empty()) {
        set_status(state, "Ready — add files or drag them here");
    } else {
        set_status(state, "Finished — copy results or add more files");
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
    const std::string text = "All files  " + std::to_string(percent) + "%";
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(state.progress), text.c_str());
}

void update_file_progress(State& state, std::uint64_t id, std::uint64_t bytes_read, std::uint64_t total_bytes) {
    const double fraction = total_bytes == 0
        ? 1.0
        : std::min(1.0, static_cast<double>(bytes_read) / static_cast<double>(total_bytes));
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state.file_progress), fraction);
    const std::string text = "File  " + std::to_string(static_cast<int>(fraction * 100.0)) + "%";
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
    const std::string status = "Running " + std::to_string(percent) + "%";
    update_store_row(state, find_row_index(state, message.id), status.c_str(), "Calculating...");
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
        ? "Done" : (message->result.cancelled ? "Cancelled" : "Error");
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
        set_status(state, "Select at least one algorithm");
        return;
    }
    const auto path = row->path;
    row->job = std::make_unique<Job>();
    row->job->cancel = std::make_shared<std::atomic<bool>>(false);
    const auto cancel = row->job->cancel;
    row->progress_bytes = 0;
    row->has_result = false;
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state.file_progress), 0.0);
    gtk_label_set_text(GTK_LABEL(state.file_progress_label), "File  0%");
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
                       kPath, state.rows.back().path.string().c_str(), kStatus, "Queued",
                       kResult, "Starting automatically...", -1);
    start_file(state, state.rows.back().id);
    update_buttons(state);
    update_status(state);
    update_progress(state);
}

void choose_files(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    GtkWidget* dialog = gtk_file_chooser_dialog_new("Select files", GTK_WINDOW(state.window), GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL, "Add", GTK_RESPONSE_ACCEPT, nullptr);
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
    set_status(state, "Cancelling active files...");
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
        text += "File: " + row.path.string() + "\n";
        text += "Size: " + std::to_string(row.total_bytes) + " bytes\n";
        text += "Modified: " + filehash::ui::format_file_time(row.path) + "\n";
        if (row.has_result) {
            text += "Status: " + std::string(row.result.error.empty() && !row.result.cancelled ? "Done" : (row.result.cancelled ? "Cancelled" : "Error")) + "\n";
            text += filehash::ui::format_result_lines(row.result);
        } else if (row.job) {
            text += "Status: Calculating...";
        } else {
            text += "Status: Queued";
        }
        text += "\n\n";
    }
    return text;
}

void copy_results(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    const std::string text = copy_result_text(state, all_indexes(state));
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
    set_status(state, "All results copied.");
}

void copy_selected_results(GtkMenuItem*, gpointer data) {
    State& state = *static_cast<State*>(data);
    const auto indexes = selected_indexes(state);
    if (indexes.empty()) {
        set_status(state, "Select a result row first.");
        return;
    }
    const std::string text = copy_result_text(state, indexes);
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
    set_status(state, "Result copied.");
}

void copy_row(State& state, int index) {
    if (index < 0 || index >= static_cast<int>(state.rows.size())) return;
    const std::string text = copy_result_text(state, {index});
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text.c_str(), -1);
    set_status(state, "Result copied.");
}

void select_all(GtkMenuItem*, gpointer data) {
    State& state = *static_cast<State*>(data);
    gtk_tree_selection_select_all(state.selection);
    set_status(state, "All rows selected.");
}

void clean_all(GtkButton*, gpointer data) {
    State& state = *static_cast<State*>(data);
    for (auto& row : state.rows) if (row.job) {
        row.job->cancel->store(true);
        if (row.job->worker.joinable()) row.job->worker.join();
    }
    state.rows.clear();
    gtk_list_store_clear(state.store);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state.file_progress), 0.0);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state.progress), 0.0);
    gtk_label_set_text(GTK_LABEL(state.file_progress_label), "File  0%");
    gtk_label_set_text(GTK_LABEL(state.progress_label), "All files  0%");
    update_buttons(state);
    update_status(state);
}

gboolean on_key_press(GtkWidget*, GdkEventKey* event, gpointer data) {
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
        copy_row(state, index);
        gtk_tree_path_free(path);
        return FALSE;
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

void apply_css() {
    const char* css =
        "window { background: #e8f0fa; }"
        ".title { font-size: 24px; font-weight: 700; color: #112d57; }"
        ".subtitle, .hint, .status { color: #2f486a; }"
        ".empty-hint { color: #2563eb; font-size: 18px; font-weight: 600; }"
        ".algorithm-card, .results-card { background: #f5f9ff; border: 1px solid #c8d9ee; border-radius: 10px; padding: 10px; }"
        ".primary { background: #2563eb; color: #ffffff; }"
        "treeview { background: #eff6ff; color: #18304f; -GtkTreeView-horizontal-separator: 8; }"
        "treeview.view { -GtkTreeView-grid-line-color: #000000; -GtkTreeView-grid-line-width: 1; }"
        "treeview.view:selected { background: #b9d5fa; color: #112d57; }"
        "progressbar trough { min-height: 10px; border-radius: 5px; background: #cfdef2; }"
        "progressbar progress { min-height: 10px; border-radius: 5px; background: #2563eb; }";
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

}  // 命名空间 / Namespace

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    apply_css();
    State state;
    state.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(state.window), "Lizy File Hash Tool v1.0");
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
    GtkWidget* title = gtk_label_new("Lizy File Hash Tool v1.0");
    GtkWidget* subtitle = gtk_label_new("Fast, local and privacy-first file verification");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "title");
    gtk_style_context_add_class(gtk_widget_get_style_context(subtitle), "subtitle");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0F);
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0F);
    gtk_box_pack_start(GTK_BOX(heading), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(heading), subtitle, FALSE, FALSE, 0);

    GtkWidget* add = gtk_button_new_with_label("Add files");
    state.copy_button = gtk_button_new_with_label("Copy results");
    state.delete_button = gtk_button_new_with_label("Delete");
    state.cancel_button = gtk_button_new_with_label("Cancel all");
    state.clean_button = gtk_button_new_with_label("Clean all");
    gtk_widget_set_sensitive(state.copy_button, FALSE);
    gtk_widget_set_sensitive(state.delete_button, FALSE);
    gtk_widget_set_sensitive(state.cancel_button, FALSE);
    gtk_widget_set_sensitive(state.clean_button, FALSE);
    gtk_style_context_add_class(gtk_widget_get_style_context(add), "primary");
    gtk_box_pack_end(GTK_BOX(header), state.cancel_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), state.delete_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), state.copy_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), state.clean_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(header), add, FALSE, FALSE, 0);
    g_signal_connect(add, "clicked", G_CALLBACK(choose_files), &state);
    g_signal_connect(state.copy_button, "clicked", G_CALLBACK(copy_results), &state);
    g_signal_connect(state.delete_button, "clicked", G_CALLBACK(delete_selected), &state);
    g_signal_connect(state.cancel_button, "clicked", G_CALLBACK(cancel_all), &state);
    g_signal_connect(state.clean_button, "clicked", G_CALLBACK(clean_all), &state);

    GtkWidget* algorithms = gtk_frame_new("Algorithms");
    gtk_style_context_add_class(gtk_widget_get_style_context(algorithms), "algorithm-card");
    GtkWidget* algorithm_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(algorithm_box), 4);
    gtk_container_add(GTK_CONTAINER(algorithms), algorithm_box);
    for (int index = 0; index < 6; ++index) {
        state.checks[index] = gtk_check_button_new_with_label(filehash::algorithm_name(kAlgorithms[index]));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(state.checks[index]), index == 1 || index == 3);
        gtk_box_pack_start(GTK_BOX(algorithm_box), state.checks[index], FALSE, FALSE, 0);
        g_signal_connect(state.checks[index], "toggled", G_CALLBACK(+[](GtkToggleButton*, gpointer data) {
            State& owner = *static_cast<State*>(data);
            if (!any_running(owner)) for (const auto& row : owner.rows) start_file(owner, row.id);
        }), &state);
    }
    gtk_box_pack_start(GTK_BOX(root), algorithms, FALSE, FALSE, 0);
    GtkWidget* hint = gtk_label_new("Drop files anywhere in this window — each file starts independently");
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "hint");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0F);
    gtk_box_pack_start(GTK_BOX(root), hint, FALSE, FALSE, 0);
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

    state.store = gtk_list_store_new(kColumnCount, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    GtkWidget* view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(state.store));
    state.selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
    gtk_tree_selection_set_mode(state.selection, GTK_SELECTION_MULTIPLE);
    gtk_tree_view_set_grid_lines(GTK_TREE_VIEW(view), GTK_TREE_VIEW_GRID_LINES_BOTH);
    gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(view), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view), TRUE);
    g_signal_connect(view, "key-press-event", G_CALLBACK(on_key_press), &state);
    g_signal_connect(view, "button-press-event", G_CALLBACK(on_button_press), &state);
    const char* titles[] = {"File", "Status", "Hash values"};
    const int columns[] = {kPath, kStatus, kResult};
    for (int index = 0; index < 3; ++index) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(titles[index], renderer, "text", columns[index], nullptr);
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
    state.status = gtk_label_new("Ready — add files or drag them here");
    gtk_style_context_add_class(gtk_widget_get_style_context(state.status), "status");
    gtk_label_set_xalign(GTK_LABEL(state.status), 0.0F);
    gtk_box_pack_start(GTK_BOX(root), state.status, FALSE, FALSE, 0);

    gtk_widget_show_all(state.window);
    gtk_main();
    g_object_unref(state.store);
    return 0;
}
