// Copyright (C) 2026 Soni Romanov
// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <xaudio2.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>
#include <random>

namespace audio {
// RIFF chunks may contain metadata before the actual PCM payload.
inline bool readMusic(const std::filesystem::path& path, WAVEFORMATEX& format,
    std::vector<BYTE>& data) {
    std::ifstream file(path, std::ios::binary);
    char header[12]{};
    if (!file.read(header, 12) || std::memcmp(header, "RIFF", 4) ||
        std::memcmp(header + 8, "WAVE", 4)) return false;
    bool foundFormat = false;
    while (file) {
        char id[4]; std::uint32_t size = 0;
        if (!file.read(id, 4) || !file.read(reinterpret_cast<char*>(&size), 4)) break;
        if (size > 128 * 1024 * 1024) return false;
        if (!std::memcmp(id, "fmt ", 4)) {
            if (size < 16 || !file.read(reinterpret_cast<char*>(&format), 16)) return false;
            format.cbSize = 0;
            file.seekg(size - 16, std::ios::cur);
            foundFormat = true;
        } else if (!std::memcmp(id, "data", 4)) {
            data.resize(size);
            if (!file.read(reinterpret_cast<char*>(data.data()), size)) return false;
        } else file.seekg(size, std::ios::cur);
        if (size & 1) file.seekg(1, std::ios::cur);
    }
    return foundFormat && format.wFormatTag == WAVE_FORMAT_PCM &&
        format.wBitsPerSample == 16 && format.nChannels > 0 && format.nChannels <= 2 &&
        format.nSamplesPerSec >= 8000 && format.nSamplesPerSec <= 192000 &&
        format.nBlockAlign == format.nChannels * 2 &&
        !data.empty() && data.size() % format.nBlockAlign == 0;
}

struct KeyRecording {
    WAVEFORMATEX format{};
    std::vector<BYTE> samples;
    IXAudio2SourceVoice* voice = nullptr;
};
inline std::vector<KeyRecording> loadKeys(const std::filesystem::path& folder) {
    std::vector<KeyRecording> result;
    for (unsigned i = 1; i <= 32; ++i) {
        const auto number = std::to_wstring(i);
        KeyRecording key;
        if (readMusic(folder / (L"keypress-" + std::wstring(3 - number.size(), L'0') +
            number + L".wav"), key.format, key.samples))
            result.push_back(std::move(key));
    }
    return result;
}

class Engine {
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* output = nullptr;
    IXAudio2SourceVoice* music = nullptr;
    std::vector<KeyRecording> keys;
    std::vector<BYTE> track;
    size_t previous = 0;
    std::mt19937 random{static_cast<unsigned>(GetTickCount64())};
    bool comInitialized = false;
public:
    ~Engine() { stop(); }
    void stop() {
        for (auto& key : keys) if (key.voice) key.voice->DestroyVoice();
        keys.clear();
        if (music) music->DestroyVoice();
        if (output) output->DestroyVoice();
        if (engine) engine->Release();
        music = nullptr; output = nullptr; engine = nullptr;
        if (comInitialized) CoUninitialize();
        comInitialized = false;
    }
    void start(const std::filesystem::path& path) {
        stop();
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comInitialized = SUCCEEDED(comResult);
        if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE) return;
        if (FAILED(XAudio2Create(&engine)) || FAILED(engine->CreateMasteringVoice(&output))) {
            stop(); return;
        }
        keys = loadKeys(path.parent_path() / L"keys");
        for (auto& key : keys) engine->CreateSourceVoice(&key.voice, &key.format);
        WAVEFORMATEX format{};
        if (readMusic(path, format, track) &&
            SUCCEEDED(engine->CreateSourceVoice(&music, &format))) {
            XAUDIO2_BUFFER buffer{};
            buffer.AudioBytes = static_cast<UINT32>(track.size());
            buffer.pAudioData = track.data();
            buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
            music->SetVolume(.22f);
            if (SUCCEEDED(music->SubmitSourceBuffer(&buffer))) music->Start();
        }
    }
    void key(bool enter = false) {
        if (!engine || keys.empty()) return;
        // Avoid repeating the same recording twice in a row.
        const size_t offset = keys.size() > 1 ?
            std::uniform_int_distribution<size_t>(1, keys.size() - 1)(random) : 0;
        previous = (previous + offset) % keys.size();
        const auto& recording = keys[previous];
        auto* voice = recording.voice;
        if (!voice) return;
        voice->Stop(); voice->FlushSourceBuffers();
        voice->SetVolume(enter ? .6f : .45f);
        voice->SetFrequencyRatio(enter ? .92f : 1.f);
        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(recording.samples.size());
        buffer.pAudioData = recording.samples.data();
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        if (SUCCEEDED(voice->SubmitSourceBuffer(&buffer))) voice->Start();
    }
};
}
