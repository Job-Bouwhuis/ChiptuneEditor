#pragma once

#include "chiptune/audio/engine.hpp"

#include <cstdint>
#include <memory>

#ifdef CHIPTUNE_HAS_ALSA
namespace chiptune::audio {

// Native Linux PCM output using ALSA. Available when built with alsa-lib-devel.
class AlsaOutput {
public:
    explicit AlsaOutput(AudioEngine& engine, std::uint16_t channels = 2);
    ~AlsaOutput();

    AlsaOutput(const AlsaOutput&) = delete;
    AlsaOutput& operator=(const AlsaOutput&) = delete;

    void start();
    void stop() noexcept;
    [[nodiscard]] bool is_running() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace chiptune::audio
#endif
