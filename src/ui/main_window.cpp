#include "chiptune/ui/main_window.hpp"

#include "chiptune/audio/envelope.hpp"
#include "chiptune/audio/oscillator.hpp"
#include "chiptune/audio/wav.hpp"
#include "chiptune/audio/wav_player.hpp"

#include <Raven.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chiptune::ui {
namespace {

struct AppState;

enum class SliderKind { volume_db, pulse_width, vibrato_cents, vibrato_rate, attack_ms, decay_ms, sustain_percent, release_ms, duration_seconds };

struct SliderAction {
    AppState* app{};
    Rid value_label{R_INVALID_ID};
    SliderKind kind{};
};

struct NoteAction {
    AppState* app{};
    int semitone{};
};

struct OctaveAction {
    AppState* app{};
    int direction{};
};

struct AppState {
    Rid window{R_INVALID_ID};
    Rid workspace_switcher{R_INVALID_ID};
    Rid status{R_INVALID_ID};
    Rid loaded_file{R_INVALID_ID};
    Rid sample_name{R_INVALID_ID};
    Rid sample_details{R_INVALID_ID};
    Rid sample_plot{R_INVALID_ID};
    Rid trim_start_slider{R_INVALID_ID};
    Rid trim_end_slider{R_INVALID_ID};
    Rid trim_readout{R_INVALID_ID};
    Rid sample_volume_slider{R_INVALID_ID};
    Rid sample_volume_readout{R_INVALID_ID};
    Rid sample_loop_button{R_INVALID_ID};
    int sample_upper_series{-1};
    int sample_lower_series{-1};
    chiptune::audio::PcmAudio sample_audio;
    std::string sample_display_name;
    bool sample_loop_enabled{};
    Rid waveform_selector{R_INVALID_ID};
    Rid pulse_row{R_INVALID_ID};
    Rid note_controls{R_INVALID_ID};
    Rid note_label{R_INVALID_ID};
    Rid octave_label{R_INVALID_ID};
    Rid envelope_plot{R_INVALID_ID};
    int envelope_series{-1};
    Rid volume_slider{R_INVALID_ID};
    Rid pulse_slider{R_INVALID_ID};
    Rid vibrato_depth_slider{R_INVALID_ID};
    Rid vibrato_rate_slider{R_INVALID_ID};
    Rid attack_slider{R_INVALID_ID};
    Rid decay_slider{R_INVALID_ID};
    Rid sustain_slider{R_INVALID_ID};
    Rid release_slider{R_INVALID_ID};
    Rid duration_slider{R_INVALID_ID};
    std::array<Rid, 12> note_buttons{};
    std::array<SliderAction, 9> slider_actions{};
    std::array<NoteAction, 12> note_actions{};
    std::array<OctaveAction, 2> octave_actions{};
    chiptune::audio::WavPlayer player{44100};
    int first_midi_note{60}; // C4
    int selected_midi_note{60};
    std::uint32_t noise_seed{0x5a17U};
    bool playback_active{};
};

struct Workspace {
    const char* title;
    const char* description;
    const char* detail;
};

struct WorkspaceAction {
    AppState* app;
    int index;
};

constexpr std::array<chiptune::audio::Waveform, 5> waveforms{{
    chiptune::audio::Waveform::square,
    chiptune::audio::Waveform::triangle,
    chiptune::audio::Waveform::sawtooth,
    chiptune::audio::Waveform::sine,
    chiptune::audio::Waveform::noise
}};
constexpr std::array<const char*, 5> waveform_names{{"Square", "Triangle", "Sawtooth", "Sine", "Noise"}};
constexpr std::array<const char*, 12> note_names{{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}};

void preview_tone(Rid, void* user_data);
void export_tone(Rid, void* user_data);
void save_preset(Rid, void* user_data);
void load_preset(Rid, void* user_data);

void show_workspace(Rid, void* user_data) {
    const auto& action = *static_cast<WorkspaceAction*>(user_data);
    Rswitcher_set_index(action.app->workspace_switcher, action.index);
    Rlabel_set_text(action.app->status, "Workspace changed");
}

void quit_application(Rid, void*) { Rquit(0); }

Rid make_label(const char* text, float size) {
    const Rid label = Rlabel_create(text);
    Rlabel_set_text_size(label, size);
    return label;
}

void add_workspace_button(Rid parent, WorkspaceAction& action, const char* text) {
    const Rid button = Rbutton_create(text);
    Rbutton_set_style(button, R_BUTTON_FLAT);
    Rbutton_set_clicked_callback(button, show_workspace, &action);
    Rwidget_set_min_size(button, 160.0F, 42.0F);
    Rwidget_add_child(parent, button);
}

Rid make_info_page(const Workspace& workspace) {
    const Rid page = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(page, 18.0F);
    Rwidget_add_child(page, make_label(workspace.title, 34.0F));
    const Rid description = make_label(workspace.description, 18.0F);
    Rlabel_set_wrap(description, R_TRUE);
    Rwidget_add_child(page, description);
    Rwidget_add_child(page, Rwidget_create(R_WIDGET_SPACER));
    Rwidget_add_child(page, make_label("A small studio for 8-bit ideas", 23.0F));
    const Rid detail = make_label(workspace.detail, 16.0F);
    Rlabel_set_wrap(detail, R_TRUE);
    Rwidget_add_child(page, detail);
    const Rid fill = Rwidget_create(R_WIDGET_SPACER);
    Rslot_set_fill(fill, 1.0F);
    Rwidget_add_child(page, fill);
    return page;
}

std::string format_midi_note(int midi_note) {
    const int pitch_class = ((midi_note % 12) + 12) % 12;
    const int octave = midi_note / 12 - 1;
    return std::string(note_names[static_cast<std::size_t>(pitch_class)]) + std::to_string(octave);
}

float slider_value(const AppState& app, SliderKind kind) {
    switch (kind) {
    case SliderKind::volume_db: return Rslider_get_value(app.volume_slider);
    case SliderKind::pulse_width: return Rslider_get_value(app.pulse_slider);
    case SliderKind::vibrato_cents: return Rslider_get_value(app.vibrato_depth_slider);
    case SliderKind::vibrato_rate: return Rslider_get_value(app.vibrato_rate_slider);
    case SliderKind::attack_ms: return Rslider_get_value(app.attack_slider);
    case SliderKind::decay_ms: return Rslider_get_value(app.decay_slider);
    case SliderKind::sustain_percent: return Rslider_get_value(app.sustain_slider);
    case SliderKind::release_ms: return Rslider_get_value(app.release_slider);
    case SliderKind::duration_seconds: return Rslider_get_value(app.duration_slider);
    }
    return 0.0F;
}

void refresh_envelope_plot(AppState& app) {
    if (app.envelope_plot == R_INVALID_ID || app.envelope_series < 0) return;
    const double attack = slider_value(app, SliderKind::attack_ms) / 1000.0;
    const double decay = slider_value(app, SliderKind::decay_ms) / 1000.0;
    const double hold = 0.45;
    const double release = slider_value(app, SliderKind::release_ms) / 1000.0;
    const double total = std::max(attack + decay + hold + release, 0.001);
    const double x[]{0.0, attack / total, (attack + decay) / total,
                      (attack + decay + hold) / total, 1.0};
    const double sustain = slider_value(app, SliderKind::sustain_percent) / 100.0;
    const double y[]{0.0, 1.0, sustain, sustain, 0.0};
    Rplot_set_series_data(app.envelope_plot, app.envelope_series, x, y, 5);
}

void update_slider_readout(Rid, float value, void* user_data) {
    const auto& action = *static_cast<SliderAction*>(user_data);
    char text[32]{};
    switch (action.kind) {
    case SliderKind::volume_db:
        if (value <= -47.5F) std::snprintf(text, sizeof(text), "Very quiet");
        else std::snprintf(text, sizeof(text), "%.1f dB", value);
        break;
    case SliderKind::pulse_width:
    case SliderKind::sustain_percent:
        std::snprintf(text, sizeof(text), "%.0f%%", value * (action.kind == SliderKind::pulse_width ? 100.0F : 1.0F));
        break;
    case SliderKind::vibrato_cents:
        std::snprintf(text, sizeof(text), "%.0f cents", value);
        break;
    case SliderKind::vibrato_rate:
        std::snprintf(text, sizeof(text), "%.1f Hz", value);
        break;
    case SliderKind::duration_seconds:
        std::snprintf(text, sizeof(text), "%.1f s", value);
        break;
    case SliderKind::attack_ms:
    case SliderKind::decay_ms:
    case SliderKind::release_ms:
        std::snprintf(text, sizeof(text), "%.0f ms", value);
        break;
    }
    Rlabel_set_text(action.value_label, text);
    if (action.kind == SliderKind::attack_ms || action.kind == SliderKind::decay_ms ||
        action.kind == SliderKind::sustain_percent || action.kind == SliderKind::release_ms)
        refresh_envelope_plot(*action.app);
}

void add_slider_row(Rid parent, const char* label_text, Rid slider, Rid readout) {
    const Rid row = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(row, 12.0F);
    const Rid label = make_label(label_text, 14.0F);
    Rwidget_set_min_size(label, 86.0F, 38.0F);
    Rslot_set_valign(label, R_VALIGN_CENTER);
    Rwidget_add_child(row, label);
    Rwidget_set_min_size(slider, 110.0F, 38.0F);
    Rslot_set_fill(slider, 1.0F);
    Rwidget_add_child(row, slider);
    Rwidget_set_min_size(readout, 58.0F, 38.0F);
    Rslot_set_valign(readout, R_VALIGN_CENTER);
    Rwidget_add_child(row, readout);
    Rwidget_add_child(parent, row);
}

void refresh_note_keyboard(AppState& app) {
    for (std::size_t i = 0; i < app.note_buttons.size(); ++i) {
        const int midi = app.first_midi_note + static_cast<int>(i);
        Rbutton_set_text(app.note_buttons[i], note_names[static_cast<std::size_t>(midi % 12)]);
        const bool is_black = note_names[static_cast<std::size_t>(midi % 12)][1] == '#';
        Rbutton_set_style(app.note_buttons[i], midi == app.selected_midi_note ? R_BUTTON_PRIMARY
            : (is_black ? R_BUTTON_FLAT : R_BUTTON_DEFAULT));
    }
    const auto note = format_midi_note(app.selected_midi_note);
    const double frequency = 440.0 * std::pow(2.0, (app.selected_midi_note - 69) / 12.0);
    char text[80]{};
    std::snprintf(text, sizeof(text), "Note: %s  |  %.1f Hz", note.c_str(), frequency);
    Rlabel_set_text(app.note_label, text);
    const auto first = format_midi_note(app.first_midi_note);
    const auto last = format_midi_note(app.first_midi_note + 11);
    const std::string octave_text = first + " to " + last;
    Rlabel_set_text(app.octave_label, octave_text.c_str());
}

void select_note(Rid, void* user_data) {
    const auto& action = *static_cast<NoteAction*>(user_data);
    action.app->selected_midi_note = action.app->first_midi_note + action.semitone;
    refresh_note_keyboard(*action.app);
    preview_tone(R_INVALID_ID, action.app);
}

void shift_octave(Rid, void* user_data) {
    const auto& action = *static_cast<OctaveAction*>(user_data);
    auto& app = *action.app;
    app.first_midi_note = std::clamp(app.first_midi_note + action.direction * 12, 24, 84);
    app.selected_midi_note = app.first_midi_note;
    refresh_note_keyboard(app);
}

void waveform_changed(Rid, int selected, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    const bool pulse_wave = selected >= 0 && waveforms[static_cast<std::size_t>(selected)] == chiptune::audio::Waveform::square;
    Rwidget_set_enabled(app.pulse_row, pulse_wave ? R_TRUE : R_FALSE);
    Rwidget_set_enabled(app.note_controls, selected >= 0 && waveforms[static_cast<std::size_t>(selected)] != chiptune::audio::Waveform::noise);
}

void save_preset(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    const RFileFilter filter{"Chiptune instrument preset", "*.chipinst"};
    RFileDialogDesc dialog{};
    dialog.parent = app.window;
    dialog.title = "Save instrument preset";
    dialog.filters = &filter;
    dialog.filter_count = 1;
    dialog.default_extension = "chipinst";
    const char* selected = Rfile_dialog_save(&dialog);
    if (!selected) return;
    try {
        std::ofstream output(selected);
        if (!output) throw std::runtime_error("Could not open preset file for writing");
        output << "CHIPTUNE_PRESET 1\n";
        output << Rcombo_box_get_selected(app.waveform_selector) << '\n';
        const std::array<Rid, 9> sliders{{app.volume_slider, app.pulse_slider, app.vibrato_depth_slider,
            app.vibrato_rate_slider, app.attack_slider, app.decay_slider, app.sustain_slider,
            app.release_slider, app.duration_slider}};
        for (const Rid slider : sliders) output << Rslider_get_value(slider) << '\n';
        if (!output) throw std::runtime_error("Could not write the complete preset file");
        Rlabel_set_text(app.status, "Instrument preset saved");
    } catch (const std::exception& error) {
        Rlabel_set_text(app.status, error.what());
    }
}

void load_preset(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    const RFileFilter filter{"Chiptune instrument preset", "*.chipinst"};
    RFileDialogDesc dialog{};
    dialog.parent = app.window;
    dialog.title = "Load instrument preset";
    dialog.filters = &filter;
    dialog.filter_count = 1;
    const char* selected = Rfile_dialog_open(&dialog);
    if (!selected) return;
    try {
        std::ifstream input(selected);
        std::string signature;
        int version{};
        int waveform{};
        std::array<float, 9> values{};
        if (!(input >> signature >> version >> waveform))
            throw std::runtime_error("This preset file is incomplete");
        for (auto& value : values)
            if (!(input >> value)) throw std::runtime_error("This preset file is incomplete");
        std::string trailing;
        if (signature != "CHIPTUNE_PRESET" || version != 1 || waveform < 0 ||
            static_cast<std::size_t>(waveform) >= waveforms.size() || (input >> trailing))
            throw std::runtime_error("This preset file is not a supported Chiptune Studio preset");
        const std::array<std::pair<float, float>, 9> ranges{{
            {-48.0F, 0.0F}, {0.05F, 0.95F}, {0.0F, 100.0F}, {0.5F, 12.0F},
            {0.0F, 500.0F}, {0.0F, 800.0F}, {0.0F, 100.0F}, {0.0F, 1000.0F}, {0.1F, 5.0F}
        }};
        for (std::size_t i = 0; i < values.size(); ++i)
            if (!std::isfinite(values[i]) || values[i] < ranges[i].first || values[i] > ranges[i].second)
                throw std::runtime_error("Preset contains a setting outside the supported range");

        Rcombo_box_set_selected(app.waveform_selector, waveform);
        waveform_changed(app.waveform_selector, waveform, &app);
        const std::array<Rid, 9> sliders{{app.volume_slider, app.pulse_slider, app.vibrato_depth_slider,
            app.vibrato_rate_slider, app.attack_slider, app.decay_slider, app.sustain_slider,
            app.release_slider, app.duration_slider}};
        const std::array<std::size_t, 9> action_indices{{0, 1, 6, 7, 2, 3, 4, 5, 8}};
        for (std::size_t i = 0; i < values.size(); ++i) {
            Rslider_set_value(sliders[i], values[i]);
            update_slider_readout(sliders[i], Rslider_get_value(sliders[i]),
                                  &app.slider_actions[action_indices[i]]);
        }
        Rlabel_set_text(app.status, "Instrument preset loaded; settings are ready to edit");
    } catch (const std::exception& error) {
        Rlabel_set_text(app.status, error.what());
    }
}

void refresh_sample_waveform(AppState& app, const chiptune::audio::PcmAudio& audio) {
    if (app.sample_plot == R_INVALID_ID || app.sample_upper_series < 0 || app.sample_lower_series < 0)
        return;
    const auto frames = audio.frame_count();
    if (frames == 0) return;
    constexpr std::size_t maximum_buckets = 1200;
    const auto bucket_count = static_cast<std::size_t>(std::min<std::uint64_t>(frames, maximum_buckets));
    std::vector<double> x(bucket_count);
    std::vector<double> upper(bucket_count);
    std::vector<double> lower(bucket_count);
    for (std::size_t bucket = 0; bucket < bucket_count; ++bucket) {
        const auto begin = static_cast<std::uint64_t>(bucket) * frames / bucket_count;
        const auto end = std::max(begin + 1, static_cast<std::uint64_t>(bucket + 1) * frames / bucket_count);
        double minimum = 1.0;
        double maximum = -1.0;
        for (auto frame = begin; frame < end; ++frame) {
            double mono = 0.0;
            for (std::uint16_t channel = 0; channel < audio.channels; ++channel)
                mono += audio.samples[static_cast<std::size_t>(frame * audio.channels + channel)] / 32768.0;
            mono /= audio.channels;
            minimum = std::min(minimum, mono);
            maximum = std::max(maximum, mono);
        }
        x[bucket] = static_cast<double>(begin) / audio.sample_rate;
        upper[bucket] = maximum;
        lower[bucket] = minimum;
    }
    const double duration = static_cast<double>(frames) / audio.sample_rate;
    Rplot_set_axis_range(app.sample_plot, R_PLOT_X, 0.0, std::max(duration, 0.001));
    Rplot_set_series_data(app.sample_plot, app.sample_upper_series, x.data(), upper.data(),
                          static_cast<int>(bucket_count));
    Rplot_set_series_data(app.sample_plot, app.sample_lower_series, x.data(), lower.data(),
                          static_cast<int>(bucket_count));
}

void trim_selection_changed(Rid, float, void* user_data);

void import_sample_path(AppState& app, const std::string& path) {
    auto audio = chiptune::audio::read_wav(path);
    const auto frames = audio.frame_count();
    const auto rate = audio.sample_rate;
    const auto channels = audio.channels;
    const auto name = std::filesystem::path(path).filename().string();
    refresh_sample_waveform(app, audio);
    app.sample_audio = audio;
    app.sample_display_name = name;
    app.player.load_audio(std::move(audio), name);
    app.sample_loop_enabled = false;
    app.player.set_looping(false);
    if (app.sample_loop_button != R_INVALID_ID) Rbutton_set_text(app.sample_loop_button, "Loop sample: Off");
    app.playback_active = false;
    if (app.sample_name != R_INVALID_ID) Rlabel_set_text(app.sample_name, name.c_str());
    if (app.sample_details != R_INVALID_ID) {
        char details[128]{};
        const double seconds = rate == 0 ? 0.0 : static_cast<double>(frames) / rate;
        std::snprintf(details, sizeof(details), "%.2f seconds  ·  %u Hz  ·  %s  ·  %llu frames",
                      seconds, rate, channels == 1 ? "Mono" : "Stereo",
                      static_cast<unsigned long long>(frames));
        Rlabel_set_text(app.sample_details, details);
    }
    if (app.loaded_file != R_INVALID_ID) Rlabel_set_text(app.loaded_file, name.c_str());
    if (app.trim_start_slider != R_INVALID_ID) {
        Rwidget_set_enabled(app.trim_start_slider, R_TRUE);
        Rwidget_set_enabled(app.trim_end_slider, R_TRUE);
        Rslider_set_value(app.trim_start_slider, 0.0F);
        Rslider_set_value(app.trim_end_slider, 100.0F);
        trim_selection_changed(R_INVALID_ID, 0.0F, &app);
    }
    if (app.status != R_INVALID_ID) Rlabel_set_text(app.status, "WAV sample imported — ready to play");
}

void trim_selection_changed(Rid, float, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    if (app.sample_audio.sample_rate == 0 || app.sample_audio.frame_count() == 0) return;
    const double duration = static_cast<double>(app.sample_audio.frame_count()) / app.sample_audio.sample_rate;
    const double start = duration * Rslider_get_value(app.trim_start_slider) / 100.0;
    const double end = duration * Rslider_get_value(app.trim_end_slider) / 100.0;
    char text[128]{};
    if (end <= start) std::snprintf(text, sizeof(text), "End must be after start");
    else std::snprintf(text, sizeof(text), "Selection: %.2f s to %.2f s  (%.2f s)", start, end, end - start);
    Rlabel_set_text(app.trim_readout, text);
}

void apply_sample_trim(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    try {
        const auto frames = app.sample_audio.frame_count();
        if (frames == 0 || app.sample_audio.channels == 0)
            throw std::runtime_error("Import a WAV sample before trimming");
        const double start_percent = Rslider_get_value(app.trim_start_slider) / 100.0;
        const double end_percent = Rslider_get_value(app.trim_end_slider) / 100.0;
        const auto start_frame = static_cast<std::uint64_t>(start_percent * static_cast<double>(frames));
        const auto end_frame = static_cast<std::uint64_t>(end_percent * static_cast<double>(frames));
        if (end_frame <= start_frame)
            throw std::runtime_error("Move the end point after the start point to trim the sample");
        if (start_frame == 0 && end_frame == frames) {
            Rlabel_set_text(app.status, "Selection already contains the full sample");
            return;
        }
        chiptune::audio::PcmAudio trimmed{app.sample_audio.sample_rate, app.sample_audio.channels, {}};
        const auto first = static_cast<std::size_t>(start_frame * trimmed.channels);
        const auto last = static_cast<std::size_t>(end_frame * trimmed.channels);
        trimmed.samples.assign(app.sample_audio.samples.begin() + static_cast<std::ptrdiff_t>(first),
                               app.sample_audio.samples.begin() + static_cast<std::ptrdiff_t>(last));
        app.sample_audio = std::move(trimmed);
        refresh_sample_waveform(app, app.sample_audio);
        app.player.load_audio(app.sample_audio, app.sample_display_name);
        app.playback_active = false;
        const auto trimmed_frames = app.sample_audio.frame_count();
        const double seconds = static_cast<double>(trimmed_frames) / app.sample_audio.sample_rate;
        char details[128]{};
        std::snprintf(details, sizeof(details), "%.2f seconds  ·  %u Hz  ·  %s  ·  %llu frames",
                      seconds, app.sample_audio.sample_rate,
                      app.sample_audio.channels == 1 ? "Mono" : "Stereo",
                      static_cast<unsigned long long>(trimmed_frames));
        Rlabel_set_text(app.sample_details, details);
        Rslider_set_value(app.trim_start_slider, 0.0F);
        Rslider_set_value(app.trim_end_slider, 100.0F);
        trim_selection_changed(R_INVALID_ID, 0.0F, &app);
        Rlabel_set_text(app.loaded_file, app.sample_display_name.c_str());
        Rlabel_set_text(app.status, "Sample trimmed — the selected section is now active");
    } catch (const std::exception& error) {
        Rlabel_set_text(app.status, error.what());
    }
}

void import_sample(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    const RFileFilter filter{"WAV audio", "*.wav;*.WAV"};
    RFileDialogDesc dialog{};
    dialog.parent = app.window;
    dialog.title = "Import WAV sample";
    dialog.filters = &filter;
    dialog.filter_count = 1;
    const char* selected = Rfile_dialog_open(&dialog);
    if (!selected) return;
    try {
        import_sample_path(app, selected);
    } catch (const std::exception& error) {
        Rlabel_set_text(app.status, error.what());
    }
}

void sample_volume_changed(Rid, float value, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    char text[24]{};
    std::snprintf(text, sizeof(text), "%.0f%%", value * 100.0F);
    Rlabel_set_text(app.sample_volume_readout, text);
}

void preview_sample(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    try {
        if (app.sample_audio.frame_count() == 0)
            throw std::runtime_error("Import a WAV sample before previewing it");
        auto preview = app.sample_audio;
        const float volume = Rslider_get_value(app.sample_volume_slider);
        for (auto& sample : preview.samples) {
            const auto scaled = std::lround(static_cast<double>(sample) * volume);
            sample = static_cast<std::int16_t>(std::clamp<long>(scaled, -32768L, 32767L));
        }
        app.player.load_audio(std::move(preview), app.sample_display_name);
        app.player.set_looping(app.sample_loop_enabled);
        app.player.play();
        app.playback_active = true;
        Rlabel_set_text(app.loaded_file, app.sample_display_name.c_str());
        Rlabel_set_text(app.status, "Previewing sample at the selected volume");
    } catch (const std::exception& error) {
        app.playback_active = false;
        Rlabel_set_text(app.status, error.what());
    }
}

void toggle_sample_loop(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    app.sample_loop_enabled = !app.sample_loop_enabled;
    app.player.set_looping(app.sample_loop_enabled);
    Rbutton_set_text(app.sample_loop_button, app.sample_loop_enabled ? "Loop sample: On" : "Loop sample: Off");
    Rlabel_set_text(app.status, app.sample_loop_enabled ? "Sample will loop during playback" : "Sample looping is off");
}

void export_sample(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    try {
        if (app.sample_audio.frame_count() == 0)
            throw std::runtime_error("Import a WAV sample before exporting it");
        const RFileFilter filter{"WAV audio", "*.wav;*.WAV"};
        RFileDialogDesc dialog{};
        dialog.parent = app.window;
        dialog.title = "Export modified sample";
        dialog.filters = &filter;
        dialog.filter_count = 1;
        dialog.default_extension = "wav";
        const char* selected = Rfile_dialog_save(&dialog);
        if (!selected) return;
        chiptune::audio::write_wav(selected, app.sample_audio);
        Rlabel_set_text(app.status, "Modified sample exported as WAV");
    } catch (const std::exception& error) {
        Rlabel_set_text(app.status, error.what());
    }
}

void save_sample_instrument(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    try {
        if (app.sample_audio.frame_count() == 0)
            throw std::runtime_error("Import a WAV sample before saving an instrument");
        const RFileFilter filter{"Chiptune sample instrument", "*.chipsample"};
        const auto suggested = std::filesystem::path(app.sample_display_name).stem().string() + ".chipsample";
        RFileDialogDesc dialog{};
        dialog.parent = app.window;
        dialog.title = "Save sample instrument";
        dialog.file_name = suggested.c_str();
        dialog.filters = &filter;
        dialog.filter_count = 1;
        dialog.default_extension = "chipsample";
        const char* selected = Rfile_dialog_save(&dialog);
        if (!selected) return;
        const std::filesystem::path definition_path(selected);
        auto asset_path = definition_path;
        asset_path.replace_extension(".wav");
        chiptune::audio::write_wav(asset_path, app.sample_audio);
        std::ofstream definition(definition_path);
        if (!definition) throw std::runtime_error("Could not write sample instrument definition");
        definition << "CHIPTUNE_SAMPLE_INSTRUMENT 1\n"
                   << std::quoted(definition_path.stem().string()) << '\n'
                   << std::quoted(asset_path.filename().string()) << '\n'
                   << (app.sample_loop_enabled ? 1 : 0) << '\n'
                   << Rslider_get_value(app.sample_volume_slider) << '\n';
        if (!definition) throw std::runtime_error("Could not finish writing the sample instrument definition");
        Rlabel_set_text(app.status, "Sample instrument saved with its WAV asset");
    } catch (const std::exception& error) {
        Rlabel_set_text(app.status, error.what());
    }
}

void load_sample_instrument(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    const RFileFilter filter{"Chiptune sample instrument", "*.chipsample"};
    RFileDialogDesc dialog{};
    dialog.parent = app.window;
    dialog.title = "Load sample instrument";
    dialog.filters = &filter;
    dialog.filter_count = 1;
    const char* selected = Rfile_dialog_open(&dialog);
    if (!selected) return;
    try {
        std::ifstream definition(selected);
        std::string signature;
        int version{};
        std::string instrument_name;
        std::string asset_name;
        int loop{};
        float volume{};
        if (!(definition >> signature >> version >> std::quoted(instrument_name) >> std::quoted(asset_name) >> loop >> volume))
            throw std::runtime_error("Sample instrument definition is incomplete");
        std::string trailing;
        const std::filesystem::path asset_reference(asset_name);
        if (signature != "CHIPTUNE_SAMPLE_INSTRUMENT" || version != 1 || instrument_name.empty() ||
            asset_name.empty() || asset_reference.is_absolute() || asset_reference.has_parent_path() ||
            (loop != 0 && loop != 1) || !std::isfinite(volume) || volume < 0.0F || volume > 1.0F ||
            (definition >> trailing))
            throw std::runtime_error("Unsupported or invalid sample instrument definition");
        const auto asset_path = std::filesystem::path(selected).parent_path() / asset_reference;
        import_sample_path(app, asset_path.string());
        app.sample_display_name = instrument_name;
        Rlabel_set_text(app.sample_name, instrument_name.c_str());
        Rlabel_set_text(app.loaded_file, instrument_name.c_str());
        Rslider_set_value(app.sample_volume_slider, volume);
        sample_volume_changed(app.sample_volume_slider, volume, &app);
        app.sample_loop_enabled = loop != 0;
        app.player.set_looping(app.sample_loop_enabled);
        Rbutton_set_text(app.sample_loop_button, app.sample_loop_enabled ? "Loop sample: On" : "Loop sample: Off");
        Rlabel_set_text(app.status, "Sample instrument loaded and ready to use");
    } catch (const std::exception& error) {
        Rlabel_set_text(app.status, error.what());
    }
}

Rid make_sample_page(AppState& app) {
    const Rid page = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(page, 18.0F);
    Rwidget_add_child(page, make_label("Sample Workshop", 31.0F));
    const Rid intro = make_label("Import a WAV sample to prepare it for your chiptune projects.", 16.0F);
    Rlabel_set_wrap(intro, R_TRUE);
    Rwidget_add_child(page, intro);

    const Rid sample_card = Rwidget_create(R_WIDGET_BORDER);
    Rborder_set_padding(sample_card, 24.0F, 22.0F, 24.0F, 22.0F);
    Rborder_set_background(sample_card, 0.08F, 0.10F, 0.14F, 1.0F);
    Rborder_set_outline(sample_card, 1.0F, 0.20F, 0.24F, 0.30F, 1.0F);
    Rborder_set_corner_radius(sample_card, 14.0F, 14.0F, 14.0F, 14.0F);
    Rwidget_set_min_size(sample_card, 480.0F, 370.0F);
    Rwidget_add_child(page, sample_card);
    const Rid sample_info = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(sample_info, 10.0F);
    Rwidget_add_child(sample_card, sample_info);
    app.sample_name = make_label("No sample imported", 20.0F);
    Rwidget_add_child(sample_info, app.sample_name);
    app.sample_details = make_label("WAV files can be imported from your computer.", 14.0F);
    Rlabel_set_wrap(app.sample_details, R_TRUE);
    Rwidget_add_child(sample_info, app.sample_details);
    app.sample_plot = Rwidget_create(R_WIDGET_PLOT);
    Rplot_set_title(app.sample_plot, "Waveform overview");
    app.sample_upper_series = Rplot_add_series(app.sample_plot, "Peak");
    app.sample_lower_series = Rplot_add_series(app.sample_plot, "Trough");
    Rplot_set_series_color(app.sample_plot, app.sample_upper_series, 0.34F, 0.88F, 0.82F, 1.0F);
    Rplot_set_series_color(app.sample_plot, app.sample_lower_series, 0.34F, 0.88F, 0.82F, 1.0F);
    Rplot_set_series_line(app.sample_plot, app.sample_upper_series, R_PLOT_LINE_SOLID, 1.5F);
    Rplot_set_series_line(app.sample_plot, app.sample_lower_series, R_PLOT_LINE_SOLID, 1.5F);
    Rplot_set_series_marker(app.sample_plot, app.sample_upper_series, R_PLOT_MARKER_NONE, 0.0F);
    Rplot_set_series_marker(app.sample_plot, app.sample_lower_series, R_PLOT_MARKER_NONE, 0.0F);
    Rplot_set_legend(app.sample_plot, R_PLOT_LEGEND_HIDDEN);
    Rplot_set_axis_label(app.sample_plot, R_PLOT_X, "Time (seconds)");
    Rplot_set_axis_range(app.sample_plot, R_PLOT_X, 0.0, 1.0);
    Rplot_set_axis_range(app.sample_plot, R_PLOT_Y, -1.0, 1.0);
    Rplot_set_axis_auto_fit(app.sample_plot, R_PLOT_X, R_FALSE);
    Rplot_set_axis_auto_fit(app.sample_plot, R_PLOT_Y, R_FALSE);
    Rplot_set_axis_grid(app.sample_plot, R_PLOT_X, R_FALSE);
    Rplot_set_axis_grid(app.sample_plot, R_PLOT_Y, R_TRUE);
    Rwidget_set_min_size(app.sample_plot, 500.0F, 220.0F);
    Rwidget_add_child(sample_info, app.sample_plot);
    const Rid trim_header = make_label("TRIM SAMPLE", 13.0F);
    Rwidget_add_child(sample_info, trim_header);
    const Rid trim_row = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(trim_row, 10.0F);
    const Rid start_label = make_label("Start", 13.0F);
    Rslot_set_valign(start_label, R_VALIGN_CENTER);
    Rwidget_add_child(trim_row, start_label);
    app.trim_start_slider = Rwidget_create(R_WIDGET_SLIDER);
    Rslider_set_range(app.trim_start_slider, 0.0F, 99.0F);
    Rslider_set_step(app.trim_start_slider, 0.1F);
    Rslider_set_value(app.trim_start_slider, 0.0F);
    Rslot_set_fill(app.trim_start_slider, 1.0F);
    Rwidget_set_min_size(app.trim_start_slider, 120.0F, 38.0F);
    Rslider_set_changed_callback(app.trim_start_slider, trim_selection_changed, &app);
    Rwidget_add_child(trim_row, app.trim_start_slider);
    const Rid end_label = make_label("End", 13.0F);
    Rslot_set_valign(end_label, R_VALIGN_CENTER);
    Rwidget_add_child(trim_row, end_label);
    app.trim_end_slider = Rwidget_create(R_WIDGET_SLIDER);
    Rslider_set_range(app.trim_end_slider, 1.0F, 100.0F);
    Rslider_set_step(app.trim_end_slider, 0.1F);
    Rslider_set_value(app.trim_end_slider, 100.0F);
    Rslot_set_fill(app.trim_end_slider, 1.0F);
    Rwidget_set_min_size(app.trim_end_slider, 120.0F, 38.0F);
    Rslider_set_changed_callback(app.trim_end_slider, trim_selection_changed, &app);
    Rwidget_add_child(trim_row, app.trim_end_slider);
    Rwidget_add_child(sample_info, trim_row);
    app.trim_readout = make_label("Import a sample, then choose its start and end.", 12.0F);
    Rwidget_add_child(sample_info, app.trim_readout);
    Rwidget_set_enabled(app.trim_start_slider, R_FALSE);
    Rwidget_set_enabled(app.trim_end_slider, R_FALSE);
    const Rid apply_trim = Rbutton_create("Apply trim");
    Rbutton_set_clicked_callback(apply_trim, apply_sample_trim, &app);
    Rwidget_add_child(sample_info, apply_trim);
    const Rid volume_row = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(volume_row, 10.0F);
    const Rid volume_label = make_label("Playback volume", 13.0F);
    Rslot_set_valign(volume_label, R_VALIGN_CENTER);
    Rwidget_add_child(volume_row, volume_label);
    app.sample_volume_slider = Rwidget_create(R_WIDGET_SLIDER);
    Rslider_set_range(app.sample_volume_slider, 0.0F, 1.0F);
    Rslider_set_step(app.sample_volume_slider, 0.01F);
    Rslider_set_value(app.sample_volume_slider, 0.8F);
    Rwidget_set_min_size(app.sample_volume_slider, 180.0F, 38.0F);
    Rslot_set_fill(app.sample_volume_slider, 1.0F);
    app.sample_volume_readout = make_label("80%", 12.0F);
    Rwidget_set_min_size(app.sample_volume_readout, 48.0F, 38.0F);
    Rslot_set_valign(app.sample_volume_readout, R_VALIGN_CENTER);
    Rslider_set_changed_callback(app.sample_volume_slider, sample_volume_changed, &app);
    Rwidget_add_child(volume_row, app.sample_volume_slider);
    Rwidget_add_child(volume_row, app.sample_volume_readout);
    Rwidget_add_child(sample_info, volume_row);
    const Rid preview_button = Rbutton_create("Preview sample");
    Rbutton_set_style(preview_button, R_BUTTON_PRIMARY);
    Rbutton_set_clicked_callback(preview_button, preview_sample, &app);
    Rwidget_add_child(sample_info, preview_button);
    const Rid sample_action_row = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(sample_action_row, 8.0F);
    app.sample_loop_button = Rbutton_create("Loop sample: Off");
    Rbutton_set_clicked_callback(app.sample_loop_button, toggle_sample_loop, &app);
    Rwidget_add_child(sample_action_row, app.sample_loop_button);
    const Rid export_button = Rbutton_create("Export modified WAV");
    Rbutton_set_clicked_callback(export_button, export_sample, &app);
    Rwidget_add_child(sample_action_row, export_button);
    Rwidget_add_child(sample_info, sample_action_row);
    const Rid instrument_row = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(instrument_row, 8.0F);
    const Rid save_instrument = Rbutton_create("Save as instrument");
    Rbutton_set_clicked_callback(save_instrument, save_sample_instrument, &app);
    Rwidget_add_child(instrument_row, save_instrument);
    const Rid load_instrument = Rbutton_create("Load instrument");
    Rbutton_set_clicked_callback(load_instrument, load_sample_instrument, &app);
    Rwidget_add_child(instrument_row, load_instrument);
    Rwidget_add_child(sample_info, instrument_row);
    const Rid import_button = Rbutton_create("Import WAV sample");
    Rbutton_set_style(import_button, R_BUTTON_PRIMARY);
    Rwidget_set_min_size(import_button, 180.0F, 44.0F);
    Rbutton_set_clicked_callback(import_button, import_sample, &app);
    Rwidget_add_child(page, import_button);
    const Rid hint = make_label("Set a comfortable level, then preview the current trimmed sample.", 13.0F);
    Rlabel_set_wrap(hint, R_TRUE);
    Rwidget_add_child(page, hint);
    return page;
}

Rid make_sound_page(AppState& app) {
    const Rid page = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(page, 14.0F);
    Rwidget_add_child(page, make_label("Sound Workshop", 31.0F));
    const Rid intro = make_label("Shape a tone, then tap a key to hear it.", 16.0F);
    Rlabel_set_wrap(intro, R_TRUE);
    Rwidget_add_child(page, intro);

    const Rid preset_row = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(preset_row, 10.0F);
    const Rid save_preset_button = Rbutton_create("Save instrument preset");
    Rbutton_set_clicked_callback(save_preset_button, save_preset, &app);
    Rwidget_add_child(preset_row, save_preset_button);
    const Rid load_preset_button = Rbutton_create("Load instrument preset");
    Rbutton_set_clicked_callback(load_preset_button, load_preset, &app);
    Rwidget_add_child(preset_row, load_preset_button);
    Rwidget_add_child(page, preset_row);

    const Rid columns = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(columns, 30.0F);
    Rslot_set_fill(columns, 1.0F);
    Rwidget_add_child(page, columns);

    const Rid oscillator_panel = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(oscillator_panel, 10.0F);
    Rslot_set_fill(oscillator_panel, 1.0F);
    Rwidget_add_child(columns, oscillator_panel);
    Rwidget_add_child(oscillator_panel, make_label("OSCILLATOR", 13.0F));

    app.waveform_selector = Rwidget_create(R_WIDGET_COMBO_BOX);
    for (const auto* name : waveform_names) Rcombo_box_add_item(app.waveform_selector, name);
    Rcombo_box_set_selected(app.waveform_selector, 0);
    const Rid waveform_row = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(waveform_row, 12.0F);
    const Rid waveform_label = make_label("Waveform", 14.0F);
    Rwidget_set_min_size(waveform_label, 86.0F, 38.0F);
    Rslot_set_valign(waveform_label, R_VALIGN_CENTER);
    Rwidget_add_child(waveform_row, waveform_label);
    Rwidget_set_min_size(app.waveform_selector, 110.0F, 38.0F);
    Rslot_set_fill(app.waveform_selector, 1.0F);
    Rwidget_add_child(waveform_row, app.waveform_selector);
    Rwidget_add_child(oscillator_panel, waveform_row);
    Rcombo_box_set_changed_callback(app.waveform_selector, waveform_changed, &app);

    app.note_label = make_label("", 13.0F);
    Rwidget_add_child(oscillator_panel, app.note_label);
    const Rid octave_row = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(octave_row, 8.0F);
    const Rid octave_down = Rbutton_create("- Octave");
    app.octave_actions[0] = OctaveAction{&app, -1};
    Rbutton_set_clicked_callback(octave_down, shift_octave, &app.octave_actions[0]);
    Rwidget_add_child(octave_row, octave_down);
    app.octave_label = make_label("C4 to B4", 13.0F);
    Rwidget_add_child(octave_row, app.octave_label);
    const Rid octave_up = Rbutton_create("+ Octave");
    app.octave_actions[1] = OctaveAction{&app, 1};
    Rbutton_set_clicked_callback(octave_up, shift_octave, &app.octave_actions[1]);
    Rwidget_add_child(octave_row, octave_up);
    Rwidget_add_child(oscillator_panel, octave_row);

    app.note_controls = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(app.note_controls, 3.0F);
    Rwidget_add_child(oscillator_panel, app.note_controls);
    for (std::size_t i = 0; i < app.note_buttons.size(); ++i) {
        if (i % 6 == 0) {
            const Rid key_row = Rwidget_create(R_WIDGET_HBOX);
            Rpanel_set_spacing(key_row, 3.0F);
            Rwidget_add_child(app.note_controls, key_row);
        }
        const Rid key_row = Rwidget_get_child(app.note_controls, static_cast<int>(i / 6));
        const Rid button = Rbutton_create(note_names[i]);
        Rwidget_set_min_size(button, 31.0F, 44.0F);
        app.note_actions[i] = NoteAction{&app, static_cast<int>(i)};
        Rbutton_set_clicked_callback(button, select_note, &app.note_actions[i]);
        Rwidget_add_child(key_row, button);
        app.note_buttons[i] = button;
    }
    refresh_note_keyboard(app);

    app.volume_slider = Rwidget_create(R_WIDGET_SLIDER);
    Rslider_set_range(app.volume_slider, -48.0F, 0.0F);
    Rslider_set_step(app.volume_slider, 0.5F);
    Rslider_set_value(app.volume_slider, -24.0F);
    const Rid volume_readout = make_label("", 12.0F);
    app.slider_actions[0] = SliderAction{&app, volume_readout, SliderKind::volume_db};
    Rslider_set_changed_callback(app.volume_slider, update_slider_readout, &app.slider_actions[0]);
    add_slider_row(oscillator_panel, "Level", app.volume_slider, volume_readout);
    update_slider_readout(app.volume_slider, -24.0F, &app.slider_actions[0]);

    app.pulse_slider = Rwidget_create(R_WIDGET_SLIDER);
    Rslider_set_range(app.pulse_slider, 0.05F, 0.95F);
    Rslider_set_step(app.pulse_slider, 0.01F);
    Rslider_set_value(app.pulse_slider, 0.5F);
    const Rid pulse_readout = make_label("", 12.0F);
    app.slider_actions[1] = SliderAction{&app, pulse_readout, SliderKind::pulse_width};
    Rslider_set_changed_callback(app.pulse_slider, update_slider_readout, &app.slider_actions[1]);
    app.pulse_row = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(app.pulse_row, 12.0F);
    const Rid pulse_label = make_label("Pulse width", 14.0F);
    Rwidget_set_min_size(pulse_label, 108.0F, 38.0F);
    Rwidget_add_child(app.pulse_row, pulse_label);
    Rwidget_set_min_size(app.pulse_slider, 110.0F, 38.0F);
    Rslot_set_fill(app.pulse_slider, 1.0F);
    Rwidget_add_child(app.pulse_row, app.pulse_slider);
    Rwidget_set_min_size(pulse_readout, 58.0F, 38.0F);
    Rwidget_add_child(app.pulse_row, pulse_readout);
    Rwidget_add_child(oscillator_panel, app.pulse_row);
    update_slider_readout(app.pulse_slider, 0.5F, &app.slider_actions[1]);

    Rwidget_add_child(oscillator_panel, make_label("VIBRATO", 13.0F));
    const std::array<const char*, 2> vibrato_names{{"Depth", "Rate"}};
    const std::array<SliderKind, 2> vibrato_kinds{{SliderKind::vibrato_cents, SliderKind::vibrato_rate}};
    const std::array<float, 2> vibrato_min{{0.0F, 0.5F}};
    const std::array<float, 2> vibrato_max{{100.0F, 12.0F}};
    const std::array<float, 2> vibrato_step{{1.0F, 0.1F}};
    const std::array<float, 2> vibrato_initial{{0.0F, 5.0F}};
    std::array<Rid*, 2> vibrato_slots{{&app.vibrato_depth_slider, &app.vibrato_rate_slider}};
    for (std::size_t i = 0; i < vibrato_names.size(); ++i) {
        *vibrato_slots[i] = Rwidget_create(R_WIDGET_SLIDER);
        Rslider_set_range(*vibrato_slots[i], vibrato_min[i], vibrato_max[i]);
        Rslider_set_step(*vibrato_slots[i], vibrato_step[i]);
        Rslider_set_value(*vibrato_slots[i], vibrato_initial[i]);
        const Rid readout = make_label("", 12.0F);
        app.slider_actions[i + 6] = SliderAction{&app, readout, vibrato_kinds[i]};
        Rslider_set_changed_callback(*vibrato_slots[i], update_slider_readout, &app.slider_actions[i + 6]);
        add_slider_row(oscillator_panel, vibrato_names[i], *vibrato_slots[i], readout);
        update_slider_readout(*vibrato_slots[i], vibrato_initial[i], &app.slider_actions[i + 6]);
    }

    const Rid envelope_panel = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(envelope_panel, 10.0F);
    Rslot_set_fill(envelope_panel, 1.0F);
    Rwidget_add_child(columns, envelope_panel);
    Rwidget_add_child(envelope_panel, make_label("AMPLITUDE ENVELOPE", 13.0F));
    app.envelope_plot = Rwidget_create(R_WIDGET_PLOT);
    Rplot_set_title(app.envelope_plot, "How the note fades in and out");
    app.envelope_series = Rplot_add_series(app.envelope_plot, "Level");
    Rplot_set_series_color(app.envelope_plot, app.envelope_series, 0.34F, 0.88F, 0.82F, 1.0F);
    Rplot_set_series_line(app.envelope_plot, app.envelope_series, R_PLOT_LINE_SOLID, 2.5F);
    Rplot_set_series_marker(app.envelope_plot, app.envelope_series, R_PLOT_MARKER_NONE, 0.0F);
    Rplot_set_legend(app.envelope_plot, R_PLOT_LEGEND_HIDDEN);
    Rplot_set_axis_range(app.envelope_plot, R_PLOT_X, 0.0, 1.0);
    Rplot_set_axis_range(app.envelope_plot, R_PLOT_Y, 0.0, 1.0);
    Rplot_set_axis_auto_fit(app.envelope_plot, R_PLOT_X, R_FALSE);
    Rplot_set_axis_auto_fit(app.envelope_plot, R_PLOT_Y, R_FALSE);
    Rplot_set_axis_grid(app.envelope_plot, R_PLOT_X, R_FALSE);
    Rplot_set_axis_grid(app.envelope_plot, R_PLOT_Y, R_FALSE);
    Rwidget_set_min_size(app.envelope_plot, 280.0F, 150.0F);
    Rwidget_add_child(envelope_panel, app.envelope_plot);

    const std::array<const char*, 4> envelope_names{{"Attack", "Decay", "Sustain", "Release"}};
    const std::array<SliderKind, 4> envelope_kinds{{SliderKind::attack_ms, SliderKind::decay_ms,
                                                    SliderKind::sustain_percent, SliderKind::release_ms}};
    const std::array<float, 4> minima{{0.0F, 0.0F, 0.0F, 0.0F}};
    const std::array<float, 4> maxima{{500.0F, 800.0F, 100.0F, 1000.0F}};
    const std::array<float, 4> steps{{1.0F, 1.0F, 1.0F, 1.0F}};
    const std::array<float, 4> initial{{12.0F, 180.0F, 65.0F, 240.0F}};
    std::array<Rid*, 4> slider_slots{{&app.attack_slider, &app.decay_slider,
                                      &app.sustain_slider, &app.release_slider}};
    for (std::size_t i = 0; i < envelope_names.size(); ++i) {
        *slider_slots[i] = Rwidget_create(R_WIDGET_SLIDER);
        Rslider_set_range(*slider_slots[i], minima[i], maxima[i]);
        Rslider_set_step(*slider_slots[i], steps[i]);
        Rslider_set_value(*slider_slots[i], initial[i]);
        const Rid readout = make_label("", 12.0F);
        app.slider_actions[i + 2] = SliderAction{&app, readout, envelope_kinds[i]};
        Rslider_set_changed_callback(*slider_slots[i], update_slider_readout, &app.slider_actions[i + 2]);
        add_slider_row(envelope_panel, envelope_names[i], *slider_slots[i], readout);
        update_slider_readout(*slider_slots[i], initial[i], &app.slider_actions[i + 2]);
    }
    refresh_envelope_plot(app);

    const Rid fill = Rwidget_create(R_WIDGET_SPACER);
    Rslot_set_fill(fill, 1.0F);
    Rwidget_add_child(page, fill);
    app.duration_slider = Rwidget_create(R_WIDGET_SLIDER);
    Rslider_set_range(app.duration_slider, 0.1F, 5.0F);
    Rslider_set_step(app.duration_slider, 0.1F);
    Rslider_set_value(app.duration_slider, 0.5F);
    const Rid duration_readout = make_label("", 12.0F);
    app.slider_actions[8] = SliderAction{&app, duration_readout, SliderKind::duration_seconds};
    Rslider_set_changed_callback(app.duration_slider, update_slider_readout, &app.slider_actions[8]);
    add_slider_row(page, "Note length", app.duration_slider, duration_readout);
    update_slider_readout(app.duration_slider, 0.5F, &app.slider_actions[8]);
    const Rid preview = Rbutton_create("Preview selected note");
    Rbutton_set_style(preview, R_BUTTON_PRIMARY);
    Rwidget_set_min_size(preview, 180.0F, 44.0F);
    Rbutton_set_clicked_callback(preview, preview_tone, &app);
    Rwidget_add_child(page, preview);
    const Rid export_button = Rbutton_create("Export note as WAV");
    Rbutton_set_clicked_callback(export_button, export_tone, &app);
    Rwidget_add_child(page, export_button);
    return page;
}

void open_wav(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    const RFileFilter filter{"WAV audio", "*.wav;*.WAV"};
    RFileDialogDesc dialog{};
    dialog.parent = app.window;
    dialog.title = "Open WAV audio";
    dialog.filters = &filter;
    dialog.filter_count = 1;
    const char* selected = Rfile_dialog_open(&dialog);
    if (!selected) return;
    const std::string path(selected);
    try {
        import_sample_path(app, path);
    } catch (const std::exception& error) {
        Rlabel_set_text(app.status, error.what());
    }
}

chiptune::audio::PcmAudio generate_tone(AppState& app, std::size_t gate_frames, std::size_t selection) {
    constexpr std::uint32_t sample_rate = 44100;
    const float release_seconds = Rslider_get_value(app.release_slider) / 1000.0F;
    const std::size_t release_frames = static_cast<std::size_t>(release_seconds * sample_rate);
    const std::size_t frame_count = gate_frames + release_frames;
    chiptune::audio::Oscillator oscillator(sample_rate);
    oscillator.set_waveform(waveforms[selection]);
    const double frequency = 440.0 * std::pow(2.0, (app.selected_midi_note - 69) / 12.0);
    oscillator.set_frequency(static_cast<float>(frequency));
    oscillator.set_volume(std::pow(10.0F, Rslider_get_value(app.volume_slider) / 20.0F));
    oscillator.set_duty_cycle(Rslider_get_value(app.pulse_slider));
    oscillator.set_vibrato(Rslider_get_value(app.vibrato_depth_slider), Rslider_get_value(app.vibrato_rate_slider));
    oscillator.set_noise_seed(app.noise_seed++);
    std::vector<float> rendered(frame_count);
    oscillator.render(rendered);
    const chiptune::audio::AdsrParameters envelope{
        Rslider_get_value(app.attack_slider) / 1000.0F,
        Rslider_get_value(app.decay_slider) / 1000.0F,
        Rslider_get_value(app.sustain_slider) / 100.0F,
        release_seconds
    };
    chiptune::audio::apply_adsr(rendered, sample_rate, gate_frames, envelope);
    chiptune::audio::PcmAudio audio{sample_rate, 1, {}};
    audio.samples.resize(frame_count);
    for (std::size_t i = 0; i < frame_count; ++i) {
        const float clipped = std::clamp(rendered[i], -1.0F, 1.0F);
        audio.samples[i] = static_cast<std::int16_t>(std::lround(clipped * 32767.0F));
    }
    return audio;
}

std::size_t selected_waveform(AppState& app) {
    const int selection = Rcombo_box_get_selected(app.waveform_selector);
    if (selection < 0 || static_cast<std::size_t>(selection) >= waveforms.size())
        throw std::runtime_error("Choose a waveform first");
    return static_cast<std::size_t>(selection);
}

void preview_tone(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    try {
        const auto selection = selected_waveform(app);
        constexpr std::uint32_t sample_rate = 44100;
        const auto gate_frames = static_cast<std::size_t>(Rslider_get_value(app.duration_slider) * sample_rate);
        auto preview = generate_tone(app, gate_frames, selection);
        const std::string note = format_midi_note(app.selected_midi_note);
        if (!app.player.play_audio_voice(std::move(preview), "Generated " + note))
            throw std::runtime_error("Too many overlapping notes; wait for one to finish");
        app.playback_active = true;
        const std::string file_text = "Generated tone - " + note;
        Rlabel_set_text(app.loaded_file, file_text.c_str());
        const std::string status = "Previewing " + note + " - " + waveform_names[selection];
        Rlabel_set_text(app.status, status.c_str());
    } catch (const std::exception& error) {
        app.playback_active = false;
        Rlabel_set_text(app.status, error.what());
    }
}

void export_tone(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    try {
        const auto selection = selected_waveform(app);
        const RFileFilter filter{"WAV audio", "*.wav;*.WAV"};
        RFileDialogDesc dialog{};
        dialog.parent = app.window;
        dialog.title = "Export generated sound";
        dialog.filters = &filter;
        dialog.filter_count = 1;
        dialog.default_extension = "wav";
        const char* selected = Rfile_dialog_save(&dialog);
        if (!selected) return;
        constexpr std::uint32_t sample_rate = 44100;
        const auto gate_frames = static_cast<std::size_t>(Rslider_get_value(app.duration_slider) * sample_rate);
        chiptune::audio::write_wav(selected, generate_tone(app, gate_frames, selection));
        Rlabel_set_text(app.status, "Generated sound exported as WAV");
    } catch (const std::exception& error) {
        Rlabel_set_text(app.status, error.what());
    }
}

void play_wav(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    if (!app.player.has_file()) {
        Rlabel_set_text(app.status, "Open a WAV file or preview a note first");
        return;
    }
    try {
        app.player.play();
        app.playback_active = true;
        Rlabel_set_text(app.status, "Playing");
    } catch (const std::exception& error) {
        app.playback_active = false;
        Rlabel_set_text(app.status, error.what());
    }
}

void stop_wav(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    app.player.stop();
    app.playback_active = false;
    Rlabel_set_text(app.status, app.player.has_file() ? "Playback stopped" : "Studio ready");
}

void update_playback_status(Rid, void* user_data) {
    auto& app = *static_cast<AppState*>(user_data);
    if (!app.playback_active) return;
    if (!app.player.is_playing()) {
        app.playback_active = false;
        app.player.stop();
        Rlabel_set_text(app.status, "Playback finished");
        return;
    }
    if (!app.player.output_is_running()) {
        app.playback_active = false;
        app.player.stop();
        Rlabel_set_text(app.status, "Audio output stopped unexpectedly");
    }
}

} // namespace

int run_application() {
    if (!Rinit(R_BACKEND_VULKAN)) {
        const char* detail = nullptr;
        RSeverity severity{};
        (void)Rerror_get_last(&detail, &severity);
        std::cerr << "Could not initialize Raven UI";
        if (detail && *detail) std::cerr << ": " << detail;
        std::cerr << '\n';
        return 1;
    }

    const Rid window = Rwindow_create("Chiptune Studio", 1180, 760, R_WINDOW_DEFAULT);
    Rwindow_set_min_size(window, 760, 560);
    AppState app;
    app.window = window;

    const Rid root = Rwidget_create(R_WIDGET_VBOX);
    Rpanel_set_spacing(root, 0.0F);
    Rwindow_set_content(window, root);
    const Rid header = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(header, 16.0F);
    Rslot_set_padding(header, 26.0F, 20.0F, 26.0F, 16.0F);
    Rwidget_add_child(root, header);
    Rwidget_add_child(header, make_label("CHIPTUNE STUDIO", 19.0F));
    const Rid header_spacer = Rwidget_create(R_WIDGET_SPACER);
    Rslot_set_fill(header_spacer, 1.0F);
    Rwidget_add_child(header, header_spacer);
    Rwidget_add_child(header, make_label("SOUND  /  SONG  /  SAMPLES", 12.0F));
    const Rid open_button = Rbutton_create("Open WAV");
    Rbutton_set_style(open_button, R_BUTTON_PRIMARY);
    Rbutton_set_clicked_callback(open_button, open_wav, &app);
    Rwidget_add_child(header, open_button);

    const Rid navigation = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(navigation, 8.0F);
    Rslot_set_padding(navigation, 26.0F, 0.0F, 26.0F, 18.0F);
    Rwidget_add_child(root, navigation);
    const std::array<Workspace, 3> workspaces{{
        {"Song Workshop", "Compose with a piano roll and reusable instruments.",
         "Piano roll editing is the next composition milestone."},
        {"Sound Workshop", "Shape simple chiptune tones and bass sounds.",
         "Choose a waveform, set its pitch and volume, then audition notes."},
        {"Sample Workshop", "Bring in audio and prepare it for your songs.",
         "Import a WAV file, inspect its format, and audition it from the playback controls."}
    }};
    std::array<WorkspaceAction, 3> actions{};
    const std::array<const char*, 3> button_names{{"Song Workshop", "Sound Workshop", "Sample Workshop"}};
    for (std::size_t i = 0; i < workspaces.size(); ++i) {
        actions[i] = WorkspaceAction{&app, static_cast<int>(i)};
        add_workspace_button(navigation, actions[i], button_names[i]);
    }

    const Rid card = Rwidget_create(R_WIDGET_BORDER);
    Rborder_set_padding(card, 28.0F, 24.0F, 28.0F, 24.0F);
    Rborder_set_background(card, 0.105F, 0.12F, 0.16F, 1.0F);
    Rborder_set_outline(card, 1.0F, 0.20F, 0.24F, 0.30F, 1.0F);
    Rborder_set_corner_radius(card, 18.0F, 18.0F, 18.0F, 18.0F);
    Rslot_set_padding(card, 26.0F, 0.0F, 26.0F, 20.0F);
    Rslot_set_fill(card, 1.0F);
    Rwidget_add_child(root, card);
    const Rid card_content = Rwidget_create(R_WIDGET_VBOX);
    Rslot_set_fill(card_content, 1.0F);
    Rwidget_add_child(card, card_content);
    app.workspace_switcher = Rwidget_create(R_WIDGET_SWITCHER);
    Rslot_set_fill(app.workspace_switcher, 1.0F);
    Rwidget_add_child(card_content, app.workspace_switcher);
    Rwidget_add_child(app.workspace_switcher, make_info_page(workspaces[0]));
    Rwidget_add_child(app.workspace_switcher, make_sound_page(app));
    Rwidget_add_child(app.workspace_switcher, make_sample_page(app));
    Rswitcher_set_index(app.workspace_switcher, 1);

    const Rid footer = Rwidget_create(R_WIDGET_HBOX);
    Rpanel_set_spacing(footer, 12.0F);
    Rslot_set_padding(footer, 26.0F, 0.0F, 26.0F, 18.0F);
    Rwidget_add_child(root, footer);
    app.status = make_label("Studio ready", 13.0F);
    Rwidget_add_child(footer, app.status);
    const Rid footer_spacer = Rwidget_create(R_WIDGET_SPACER);
    Rslot_set_fill(footer_spacer, 1.0F);
    Rwidget_add_child(footer, footer_spacer);
    app.loaded_file = make_label("No audio loaded", 13.0F);
    Rwidget_add_child(footer, app.loaded_file);
    const Rid play = Rbutton_create("Play");
    Rbutton_set_style(play, R_BUTTON_PRIMARY);
    Rbutton_set_clicked_callback(play, play_wav, &app);
    Rwidget_add_child(footer, play);
    const Rid stop = Rbutton_create("Stop");
    Rbutton_set_clicked_callback(stop, stop_wav, &app);
    Rwidget_add_child(footer, stop);
    const Rid quit = Rbutton_create("Quit");
    Rbutton_set_style(quit, R_BUTTON_FLAT);
    Rbutton_set_clicked_callback(quit, quit_application, nullptr);
    Rwidget_add_child(footer, quit);

    (void)Rtimer_create(0.1, R_TRUE, update_playback_status, &app);
    while (Rwait_frame(-1.0)) {}
    app.player.stop();
    Rshutdown();
    return 0;
}

} // namespace chiptune::ui
