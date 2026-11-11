// 音频：FluidSynth 合成 + SDL3 输出。
// 每个不同的音色（sf2 + bank + program）占一个 MIDI 通道，互不干扰。
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "model.h"

struct _fluid_synth_t;
struct SDL_AudioStream;

class AudioEngine {
public:
    struct Preset { std::string name; int bank = 0; int program = 0; };

public:
    static constexpr int kSampleRate = 48000;

    AudioEngine();
    ~AudioEngine();

    bool init(const std::string& defaultSoundfont, bool openDevice);
    void shutdown();

    // 取得（必要时创建）某个音色对应的 MIDI 通道，失败返回 -1
    int  ensureVoice(const VoiceRef& voice);

    void trigger(int channel, int note, int velocity, float gain);
    void allNotesOff();

    // 当前 SF2 的全部音色（首次调用枚举后缓存）
    const std::vector<Preset>& presets(const std::string& file);

    void setMasterGain(double g) { masterGain_.store(float(g)); }

    void  render(float* interleaved, int frames);

    const std::string& error() const { return error_; }

private:
    static void audioCallback(void* userdata, SDL_AudioStream* stream, int additional, int total);

    struct VoiceSlot { VoiceRef ref; int sfont = -1; int channel = 0; unsigned long long lastUse = 0; };

    int  loadSoundfont(const std::string& path);

    void* settings_ = nullptr;
    _fluid_synth_t* synth_ = nullptr;
    SDL_AudioStream* stream_ = nullptr;

    std::string defaultSf2_;
    std::string error_;

    std::vector<VoiceSlot> voices_;
    std::unordered_map<std::string, std::vector<Preset>> presetCache_;
    unsigned long long     useCounter_ = 0;
    std::vector<std::pair<std::string, int>> soundfonts_;

    std::vector<float> lbuf_, rbuf_, cbBuf_;
    std::atomic<float> masterGain_{0.8f};

    struct PendingOff { int channel; int note; long long samplesLeft; };
    mutable std::recursive_mutex synthMutex_;   // FluidSynth 不是线程安全的：GUI 和音频回调都要拿
    mutable std::mutex pendingMutex_;
    std::vector<PendingOff> pending_;
};
