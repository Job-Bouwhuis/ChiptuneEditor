#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace chiptune::audio {

// Interleaved signed 16-bit PCM audio. Samples are normalized to [-1, 1).
struct PcmAudio {
    std::uint32_t sample_rate{};
    std::uint16_t channels{};
    std::vector<std::int16_t> samples;

    [[nodiscard]] std::uint64_t frame_count() const noexcept;
};

// Reads and writes little-endian RIFF/WAVE files containing integer PCM.
// The reader supports 8-, 16-, 24-, and 32-bit PCM and converts to signed 16-bit.
[[nodiscard]] PcmAudio read_wav(const std::filesystem::path& path);
void write_wav(const std::filesystem::path& path, const PcmAudio& audio);

} // namespace chiptune::audio
