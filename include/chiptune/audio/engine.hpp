#pragma once

#include "chiptune/audio/wav.hpp"

#include <atomic>
#include <array>
#include <cstdint>
#include <span>

namespace chiptune::audio {

// Small, UI-independent PCM playback renderer. Call load() only while stopped
// and only when no output thread is calling render(). render() is allocation-free.
class AudioEngine {
public:
    explicit AudioEngine(std::uint32_t output_sample_rate);

    void load(PcmAudio audio);
    [[nodiscard]] bool add_voice(PcmAudio audio);
    void play() noexcept;
    void set_looping(bool looping) noexcept;
    void stop() noexcept;
    [[nodiscard]] bool is_playing() const noexcept;
    [[nodiscard]] std::uint32_t output_sample_rate() const noexcept;

    // Writes interleaved signed 16-bit output. Supports mono or stereo output;
    // source rate conversion uses linear interpolation.
    void render(std::span<std::int16_t> output, std::uint16_t output_channels) noexcept;

private:
    struct Voice {
        PcmAudio source;
        double position{};
        std::atomic<bool> active{false};
    };
    PcmAudio source_;
    std::array<Voice, 8> voices_{};
    std::uint32_t output_sample_rate_;
    double source_position_{};
    std::atomic<bool> playing_{false};
    std::atomic<bool> looping_{false};
    std::atomic<bool> restart_requested_{false};
};

} // namespace chiptune::audio
