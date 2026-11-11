#include "audio.h"

#include <algorithm>

#include <SDL3/SDL.h>
#include <fluidsynth.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace {
constexpr int  kMaxBlock = 8192;
constexpr int  kScopeSize = 8192;
constexpr int  kNoteOffMs = 1500;
constexpr int  kMaxChannels = 16;

void quietLog(int, const char*, void*) {}

bool sameVoice(const VoiceRef& a, const VoiceRef& b) {
    return a.type == b.type && a.file == b.file && a.bank == b.bank && a.program == b.program;
}
} // namespace

AudioEngine::AudioEngine() {
    lbuf_.assign(kMaxBlock, 0.f);
    rbuf_.assign(kMaxBlock, 0.f);
}

AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init(const std::string& defaultSoundfont, bool openDevice) {
    std::lock_guard<std::recursive_mutex> lock(synthMutex_);
    fluid_set_log_function(FLUID_WARN, &quietLog, nullptr);
    fluid_set_log_function(FLUID_DBG, &quietLog, nullptr);

    defaultSf2_ = defaultSoundfont;

    auto* st = new_fluid_settings();
    if (!st) { error_ = "new_fluid_settings failed"; return false; }
    settings_ = st;

    fluid_settings_setnum(st, "synth.sample-rate", double(kSampleRate));
    fluid_settings_setint(st, "synth.polyphony", 96);
    fluid_settings_setnum(st, "synth.gain", 3.0);
    fluid_settings_setstr(st, "audio.driver", "null");

    synth_ = new_fluid_synth(st);
    if (!synth_) { error_ = "new_fluid_synth failed"; return false; }

    fluid_synth_reverb_on(synth_, 0, 1);
    fluid_synth_chorus_on(synth_, 0, 0);

    if (!defaultSf2_.empty()) {
        if (loadSoundfont(defaultSf2_) < 0) {
            error_ = "音源加载失败: " + defaultSf2_;
            return false;
        }
    }

    if (!openDevice) return true;

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        error_ = std::string("SDL 音频初始化失败: ") + SDL_GetError();
        return false;
    }

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    spec.freq = kSampleRate;

    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                        &AudioEngine::audioCallback, this);
    if (!stream_) {
        error_ = std::string("打开音频设备失败: ") + SDL_GetError();
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    return true;
}

void AudioEngine::shutdown() {
    std::lock_guard<std::recursive_mutex> lock(synthMutex_);
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    soundfonts_.clear();
    presetCache_.clear();
    voices_.clear();
    {
        std::lock_guard<std::mutex> pl(pendingMutex_);
        pending_.clear();
    }
    if (synth_) { delete_fluid_synth(synth_); synth_ = nullptr; }
    if (settings_) { delete_fluid_settings(static_cast<fluid_settings_t*>(settings_)); settings_ = nullptr; }
}

int AudioEngine::loadSoundfont(const std::string& path) {
    std::lock_guard<std::recursive_mutex> lock(synthMutex_);
    for (const auto& s : soundfonts_)
        if (s.first == path) return s.second;   // 失败也会存成 -1，不再反复尝试
    if (!synth_) return -1;
    const int id = fluid_synth_sfload(synth_, path.c_str(), 0);
    if (id < 0) {
        soundfonts_.push_back({path, -1});   // 失败也记住，别每次撞击都重新解析
        return -1;
    }
    soundfonts_.push_back({path, id});
    return id;
}

const std::vector<AudioEngine::Preset>& AudioEngine::presets(const std::string& file) {
    std::lock_guard<std::recursive_mutex> lock(synthMutex_);
    static const std::vector<Preset> kNone;
    if (!synth_) return kNone;

    const std::string path = file.empty() ? defaultSf2_ : file;
    auto it = presetCache_.find(path);
    if (it != presetCache_.end()) return it->second;

    std::vector<Preset> out;
    const int sid = loadSoundfont(path);
    if (sid >= 0) {
        // 只枚举这个文件对应的 sfont（get_sfont 是按下标，id 从 1 开始，别混用）
        if (fluid_sfont_t* sfont = fluid_synth_get_sfont_by_id(synth_, sid)) {
            fluid_sfont_iteration_start(sfont);
            while (fluid_preset_t* p = fluid_sfont_iteration_next(sfont)) {
                const char* nm = fluid_preset_get_name(p);
                out.push_back({nm ? nm : "(unnamed)", fluid_preset_get_banknum(p),
                               fluid_preset_get_num(p)});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const Preset& a, const Preset& b) {
        if (a.bank != b.bank) return a.bank < b.bank;
        return a.program < b.program;
    });
    return presetCache_[path] = std::move(out);
}

int AudioEngine::ensureVoice(const VoiceRef& voice) {
    std::lock_guard<std::recursive_mutex> lock(synthMutex_);
    ++useCounter_;

    for (auto& v : voices_) {
        if (sameVoice(v.ref, voice)) {
            v.lastUse = useCounter_;
            return v.channel;
        }
    }
    if (!synth_) return -1;

    const std::string file = voice.file.empty() ? defaultSf2_ : voice.file;
    const int sf = loadSoundfont(file);
    if (sf < 0) return -1;

    // MIDI 只有 16 个通道：用完就踢掉最久没发声的那个，而不是取模复用
    // （取模会让好几条线共用一个通道，改一条的音色就全变了）
    auto configure = [&](int ch) {
        fluid_synth_set_channel_type(synth_, ch,
                                     voice.bank >= 128 ? CHANNEL_TYPE_DRUM : CHANNEL_TYPE_MELODIC);
        return fluid_synth_program_select(synth_, ch, sf, voice.bank, voice.program) == FLUID_OK;
    };

    if (int(voices_.size()) < kMaxChannels) {
        const int channel = int(voices_.size());
        if (!configure(channel)) return -1;
        voices_.push_back({voice, sf, channel, useCounter_});
        return channel;
    }

    size_t victim = 0;
    for (size_t i = 1; i < voices_.size(); ++i)
        if (voices_[i].lastUse < voices_[victim].lastUse) victim = i;

    const int channel = voices_[victim].channel;

    // 先确认新音色能选上，成功了再动旧的映射；失败就保持原样
    if (!configure(channel)) return -1;

    // 通道换主人：把旧音符静音、把它的待抬键清掉，别让旧的 noteoff 掐掉新的音
    fluid_synth_all_notes_off(synth_, channel);
    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                      [channel](const PendingOff& p) { return p.channel == channel; }),
                       pending_.end());
    }

    voices_[victim] = {voice, sf, channel, useCounter_};
    return channel;
}

void AudioEngine::trigger(int channel, int note, int velocity, float gain) {
    std::lock_guard<std::recursive_mutex> lock(synthMutex_);
    if (!synth_ || channel < 0) return;

    for (auto& v : voices_)
        if (v.channel == channel) { v.lastUse = ++useCounter_; break; }

    const int n = std::clamp(note, 0, 127);
    const double v = std::clamp(double(velocity) * double(gain), 1.0, 127.0);
    const int vel = int(std::lround(v));
    const long long hold = (long long)(kNoteOffMs * kSampleRate) / 1000;
    {
        // 同一个音重击：先把上一个同音抬掉，再起新的，否则声部会越堆越多
        std::lock_guard<std::mutex> pl(pendingMutex_);
        for (auto it = pending_.begin(); it != pending_.end(); ++it) {
            if (it->channel == channel && it->note == n) {
                fluid_synth_noteoff(synth_, channel, n);
                pending_.erase(it);
                break;
            }
        }
    }
    fluid_synth_noteon(synth_, channel, n, vel);
    {
        std::lock_guard<std::mutex> pl(pendingMutex_);
        pending_.push_back({channel, n, hold});
    }
}

void AudioEngine::allNotesOff() {
    std::lock_guard<std::recursive_mutex> lock(synthMutex_);
    if (!synth_) return;
    fluid_synth_all_notes_off(synth_, -1);
    {
        std::lock_guard<std::mutex> pl(pendingMutex_);
        pending_.clear();
    }
}

void AudioEngine::render(float* out, int frames) {
    std::lock_guard<std::recursive_mutex> lock(synthMutex_);
    if (frames <= 0) return;
    if (!synth_) {
        std::memset(out, 0, size_t(frames) * 2 * sizeof(float));
        return;
    }
    if (frames > kMaxBlock) frames = kMaxBlock;

    fluid_synth_write_float(synth_, frames, lbuf_.data(), 0, 1, rbuf_.data(), 0, 1);

    {
        std::lock_guard<std::mutex> lock(pendingMutex_);
        for (auto it = pending_.begin(); it != pending_.end();) {
            it->samplesLeft -= frames;
            if (it->samplesLeft <= 0) {
                fluid_synth_noteoff(synth_, it->channel, it->note);
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
    }

    const float gain = masterGain_.load();

    for (int i = 0; i < frames; ++i) {
        float l = lbuf_[size_t(i)] * gain;
        float r = rbuf_[size_t(i)] * gain;
        const float l2 = l * l, r2 = r * r;
        l = l * (27.f + l2) / (27.f + 9.f * l2);   // 软限幅
        r = r * (27.f + r2) / (27.f + 9.f * r2);
        out[2 * i] = l;
        out[2 * i + 1] = r;
    }
}

void AudioEngine::audioCallback(void* userdata, SDL_AudioStream* stream, int additional, int total) {
    (void)total;
    auto* self = static_cast<AudioEngine*>(userdata);
    if (additional <= 0) return;
    int frames = additional / int(sizeof(float) * 2);
    if (frames <= 0) return;
    if (frames > kMaxBlock) frames = kMaxBlock;   // 和 render 用同一个上限
    if (self->cbBuf_.size() < size_t(frames) * 2) self->cbBuf_.resize(size_t(frames) * 2);
    self->render(self->cbBuf_.data(), frames);
    SDL_PutAudioStreamData(stream, self->cbBuf_.data(), frames * int(sizeof(float) * 2));
}
