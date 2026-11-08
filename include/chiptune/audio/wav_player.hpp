#pragma once

#include "chiptune/audio/engine.hpp"

#include <filesystem>
#include <string>
#include <utility>

#ifdef CHIPTUNE_HAS_ALSA
#include "chiptune/audio/alsa_output.hpp"
#endif

namespace chiptune::audio {

// Coordinates WAV decoding, the PCM renderer, and the optional device output.
// UI code can control this service without handling PCM buffers or ALSA state.
class WavPlayer {
public:
    explicit WavPlayer(std::uint32_t output_sample_rate = 44100);
    ~WavPlayer();

    WavPlayer(const WavPlayer&) = delete;
    WavPlayer& operator=(const WavPlayer&) = delete;

    void load(const std::filesystem::path& path);
    void load_audio(PcmAudio audio, std::string display_name);
    [[nodiscard]] bool play_audio_voice(PcmAudio audio, std::string display_name);
    void play();
    void set_looping(bool looping) noexcept;
    void stop() noexcept;

    [[nodiscard]] bool has_file() const noexcept;
    [[nodiscard]] bool is_playing() const noexcept;
    [[nodiscard]] bool output_is_running() const noexcept;
    [[nodiscard]] std::string file_name() const;

private:
    AudioEngine engine_;
#ifdef CHIPTUNE_HAS_ALSA
    AlsaOutput output_;
#endif
    std::filesystem::path path_;
    std::string display_name_;
    bool loaded_{};
};

} // namespace chiptune::audio
