#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace chiptune::audio {

struct AdsrParameters {
    float attack_seconds{0.01F};
    float decay_seconds{0.15F};
    float sustain_level{0.60F};
    float release_seconds{0.18F};
};

// Applies a one-shot ADSR envelope in place. gate_frames is the note-on
// duration; samples after it contain the release tail. No memory is allocated.
void apply_adsr(std::span<float> samples, std::uint32_t sample_rate,
                std::size_t gate_frames, const AdsrParameters& parameters) noexcept;

} // namespace chiptune::audio
