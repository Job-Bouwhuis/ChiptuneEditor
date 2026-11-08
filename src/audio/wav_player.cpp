#include "chiptune/audio/wav_player.hpp"

#include "chiptune/audio/wav.hpp"

#include <stdexcept>
#include <utility>

namespace chiptune::audio {

WavPlayer::WavPlayer(std::uint32_t output_sample_rate)
    : engine_(output_sample_rate)
#ifdef CHIPTUNE_HAS_ALSA
    , output_(engine_, 2)
#endif
{}

WavPlayer::~WavPlayer() {
    stop();
}

void WavPlayer::load(const std::filesystem::path& path) {
    auto audio = read_wav(path);
    stop();
    loaded_ = false;
    path_.clear();
    engine_.load(std::move(audio));
    path_ = path;
    display_name_.clear();
    loaded_ = true;
}

void WavPlayer::load_audio(PcmAudio audio, std::string display_name) {
    stop();
    loaded_ = false;
    path_.clear();
    display_name_.clear();
    engine_.load(std::move(audio));
    display_name_ = std::move(display_name);
    loaded_ = true;
}

bool WavPlayer::play_audio_voice(PcmAudio audio, std::string display_name) {
#ifdef CHIPTUNE_HAS_ALSA
    output_.start();
#else
    throw std::runtime_error("ALSA output is unavailable; run setup.sh to install the audio development package");
#endif
    if (!engine_.add_voice(std::move(audio))) return false;
    display_name_ = std::move(display_name);
    path_.clear();
    loaded_ = true;
    return true;
}

void WavPlayer::play() {
    if (!loaded_) throw std::logic_error("Load a WAV file before playback");
#ifdef CHIPTUNE_HAS_ALSA
    output_.start();
    engine_.play();
#else
    throw std::runtime_error("ALSA output is unavailable; run setup.sh to install the audio development package");
#endif
}

void WavPlayer::set_looping(bool looping) noexcept {
    engine_.set_looping(looping);
}

void WavPlayer::stop() noexcept {
    engine_.stop();
#ifdef CHIPTUNE_HAS_ALSA
    output_.stop();
#endif
}

bool WavPlayer::has_file() const noexcept {
    return loaded_;
}

bool WavPlayer::is_playing() const noexcept {
    return engine_.is_playing();
}

bool WavPlayer::output_is_running() const noexcept {
#ifdef CHIPTUNE_HAS_ALSA
    return output_.is_running();
#else
    return false;
#endif
}

std::string WavPlayer::file_name() const {
    return display_name_.empty() ? path_.filename().string() : display_name_;
}

} // namespace chiptune::audio
