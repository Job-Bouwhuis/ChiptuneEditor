#include "chiptune/audio/wav.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

namespace chiptune::audio {
namespace {

std::uint16_t read_u16(std::istream& in) {
    std::array<unsigned char, 2> b{};
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!in) throw std::runtime_error("Unexpected end of WAV file");
    return static_cast<std::uint16_t>(b[0] | (static_cast<std::uint16_t>(b[1]) << 8));
}

std::uint32_t read_u32(std::istream& in) {
    std::array<unsigned char, 4> b{};
    in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    if (!in) throw std::runtime_error("Unexpected end of WAV file");
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

void write_u16(std::ostream& out, std::uint16_t value) {
    const std::array<unsigned char, 2> b{
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8) & 0xffU)};
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

void write_u32(std::ostream& out, std::uint32_t value) {
    const std::array<unsigned char, 4> b{
        static_cast<unsigned char>(value & 0xffU),
        static_cast<unsigned char>((value >> 8) & 0xffU),
        static_cast<unsigned char>((value >> 16) & 0xffU),
        static_cast<unsigned char>((value >> 24) & 0xffU)};
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

void expect_tag(std::istream& in, const char* expected) {
    std::array<char, 4> tag{};
    in.read(tag.data(), static_cast<std::streamsize>(tag.size()));
    if (!in || std::string(tag.data(), tag.size()) != std::string(expected, 4))
        throw std::runtime_error(std::string("Invalid WAV: expected ") + expected);
}

void write_tag(std::ostream& out, const char* tag) {
    out.write(tag, 4);
}

} // namespace

std::uint64_t PcmAudio::frame_count() const noexcept {
    return channels == 0 ? 0 : samples.size() / channels;
}

PcmAudio read_wav(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Could not open WAV file: " + path.string());
    expect_tag(in, "RIFF");
    (void)read_u32(in); // RIFF chunk size
    expect_tag(in, "WAVE");

    std::uint16_t format = 0, channels = 0, bits_per_sample = 0;
    std::uint32_t sample_rate = 0;
    std::vector<unsigned char> data;
    bool have_format = false;
    bool have_data = false;

    while (in && !(have_format && have_data)) {
        std::array<char, 4> id{};
        in.read(id.data(), static_cast<std::streamsize>(id.size()));
        if (in.gcount() == 0) break;
        if (in.gcount() != static_cast<std::streamsize>(id.size()))
            throw std::runtime_error("Truncated WAV chunk header");
        const auto size = read_u32(in);
        const std::string chunk_id(id.data(), id.size());
        if (chunk_id == "fmt ") {
            if (size < 16) throw std::runtime_error("Invalid WAV format chunk");
            format = read_u16(in);
            channels = read_u16(in);
            sample_rate = read_u32(in);
            (void)read_u32(in); // byte rate
            (void)read_u16(in); // block alignment
            bits_per_sample = read_u16(in);
            if (size > 16) in.seekg(static_cast<std::streamoff>(size - 16), std::ios::cur);
            have_format = true;
        } else if (chunk_id == "data") {
            if (size > static_cast<std::uint32_t>(std::numeric_limits<std::streamsize>::max()))
                throw std::runtime_error("WAV data chunk is too large");
            data.resize(size);
            in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
            if (!in) throw std::runtime_error("Truncated WAV data chunk");
            have_data = true;
        } else {
            in.seekg(static_cast<std::streamoff>(size), std::ios::cur);
        }
        if ((size & 1U) != 0U) in.seekg(1, std::ios::cur);
    }
    if (!have_format || !have_data) throw std::runtime_error("WAV is missing format or audio data");
    if (format != 1) throw std::runtime_error("Only integer PCM WAV files are supported");
    if (channels == 0 || sample_rate == 0) throw std::runtime_error("Invalid WAV channel count or sample rate");
    if (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 24 && bits_per_sample != 32)
        throw std::runtime_error("Unsupported WAV bit depth");

    const std::size_t bytes_per_sample = bits_per_sample / 8;
    if (data.size() % bytes_per_sample != 0) throw std::runtime_error("Misaligned WAV sample data");
    PcmAudio audio{sample_rate, channels, {}};
    audio.samples.reserve(data.size() / bytes_per_sample);
    for (std::size_t i = 0; i < data.size(); i += bytes_per_sample) {
        std::int32_t value = 0;
        if (bits_per_sample == 8) {
            value = (static_cast<std::int32_t>(data[i]) - 128) << 8;
        } else if (bits_per_sample == 16) {
            const auto raw = static_cast<std::uint16_t>(data[i]) |
                             (static_cast<std::uint16_t>(data[i + 1]) << 8);
            value = static_cast<std::int16_t>(raw);
        } else if (bits_per_sample == 24) {
            std::int32_t raw = static_cast<std::int32_t>(data[i]) |
                               (static_cast<std::int32_t>(data[i + 1]) << 8) |
                               (static_cast<std::int32_t>(data[i + 2]) << 16);
            if ((raw & 0x800000) != 0) raw |= static_cast<std::int32_t>(0xff000000U);
            value = raw >> 8;
        } else {
            const std::uint32_t raw = static_cast<std::uint32_t>(data[i]) |
                                      (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                      (static_cast<std::uint32_t>(data[i + 2]) << 16) |
                                      (static_cast<std::uint32_t>(data[i + 3]) << 24);
            value = static_cast<std::int32_t>(raw) / 65536;
        }
        audio.samples.push_back(static_cast<std::int16_t>(value));
    }
    if (audio.samples.size() % channels != 0) throw std::runtime_error("Incomplete WAV audio frame");
    return audio;
}

void write_wav(const std::filesystem::path& path, const PcmAudio& audio) {
    if (audio.sample_rate == 0) throw std::invalid_argument("Sample rate must be greater than zero");
    if (audio.channels == 0 || audio.channels > 2) throw std::invalid_argument("WAV output must be mono or stereo");
    if (audio.samples.size() % audio.channels != 0) throw std::invalid_argument("PCM samples must contain complete frames");
    const auto data_size64 = static_cast<std::uint64_t>(audio.samples.size()) * sizeof(std::int16_t);
    if (data_size64 > std::numeric_limits<std::uint32_t>::max() - 36U)
        throw std::length_error("Audio is too large for a standard RIFF/WAVE file");
    const auto data_size = static_cast<std::uint32_t>(data_size64);
    const auto byte_rate64 = static_cast<std::uint64_t>(audio.sample_rate) * audio.channels * 2U;
    if (byte_rate64 > std::numeric_limits<std::uint32_t>::max())
        throw std::invalid_argument("Sample rate and channel count exceed WAV limits");

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("Could not create WAV file: " + path.string());
    write_tag(out, "RIFF"); write_u32(out, 36U + data_size); write_tag(out, "WAVE");
    write_tag(out, "fmt "); write_u32(out, 16); write_u16(out, 1); write_u16(out, audio.channels);
    write_u32(out, audio.sample_rate); write_u32(out, static_cast<std::uint32_t>(byte_rate64));
    write_u16(out, static_cast<std::uint16_t>(audio.channels * 2U)); write_u16(out, 16);
    write_tag(out, "data"); write_u32(out, data_size);
    for (const auto sample : audio.samples) write_u16(out, static_cast<std::uint16_t>(sample));
    if (!out) throw std::runtime_error("Failed while writing WAV file: " + path.string());
}

} // namespace chiptune::audio
