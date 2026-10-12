/*
 *     Copyright (C) 2019  Filippo Scognamiglio
 *
 *     This program is free software: you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation, either version 3 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "log.h"

#include "audio.h"
#include <algorithm>
#include <cmath>
#include <memory>

#ifndef NDEBUG
#include <sys/system_properties.h>
#endif

namespace libretrodroid {

namespace {
    // Ignore tempo changes within this band of 1.0 -- keeps us on the bypass
    // path during normal playback and avoids routing samples through SoundTouch
    // when the user has only nudged the speed by a negligible amount.
    constexpr double kStretchBypassEpsilon = 0.02;
}

Audio::Audio(int32_t sampleRate, double refreshRate, bool preferLowLatencyAudio) {
    LOGI("Audio initialization has been called with input sample rate %d", sampleRate);

    contentRefreshRate = refreshRate;
    inputSampleRate = sampleRate;
    audioLatencySettings = findBestLatencySettings(preferLowLatencyAudio);
    initializeStream();
}

bool Audio::initializeStream() {
    LOGI("Using low latency stream: %d", audioLatencySettings->useLowLatencyStream);

    streamingResampler.reset();
    wasStretchActive = false;
#ifndef NDEBUG
    char legacyProperty[PROP_VALUE_MAX] {};
    __system_property_get("debug.argosy.audio.legacy", legacyProperty);
    useStreamingResampler = legacyProperty[0] != '1';
#endif

    int32_t audioBufferSize = computeAudioBufferSize();

    oboe::AudioStreamBuilder builder;
    builder.setChannelCount(2);
    builder.setDirection(oboe::Direction::Output);
    builder.setFormat(oboe::AudioFormat::I16);
    builder.setDataCallback(this);
    builder.setErrorCallback(this);

    if (audioLatencySettings->useLowLatencyStream) {
        builder.setPerformanceMode(oboe::PerformanceMode::LowLatency);
    } else {
        builder.setFramesPerCallback(audioBufferSize / 10);
    }

    oboe::Result result = builder.openManagedStream(stream);
    if (result == oboe::Result::OK) {
        baseConversionFactor = (double) inputSampleRate / stream->getSampleRate();
#ifndef NDEBUG
        LOGI("AudioDiag config resampler=%s input_hz=%d output_hz=%d fps=%.6f api=%s performance=%s burst_frames=%d stream_buffer_frames=%d fifo_sample_capacity=%d",
             useStreamingResampler ? "streaming" : "legacy", inputSampleRate,
             stream->getSampleRate(), contentRefreshRate,
             oboe::convertToText(stream->getAudioApi()),
             oboe::convertToText(stream->getPerformanceMode()),
             stream->getFramesPerBurst(), stream->getBufferSizeInFrames(), audioBufferSize);
#endif
        fifoBuffer = std::make_unique<oboe::FifoBuffer>(2, audioBufferSize);
        temporaryAudioBuffer = std::unique_ptr<int16_t[]>(new int16_t[audioBufferSize]);
        latencyTuner = std::make_unique<oboe::LatencyTuner>(*stream);

        // SoundTouch operates in stereo float and is configured for the emulator's
        // native sample rate. The final rate conversion to stream->getSampleRate()
        // stays in LinearResampler so SoundTouch only does time-stretch.
        timeStretcher = std::make_unique<soundtouch::SoundTouch>();
        timeStretcher->setChannels(2);
        timeStretcher->setSampleRate(inputSampleRate);
        timeStretcher->setTempo(1.0);
        timeStretcher->setRate(1.0);
        timeStretcher->setPitch(1.0);
        lastStretchTempo = 1.0;

        // audioBufferSize is an int16 sample count (stereo interleaved). Reuse the
        // same capacity for the float scratch buffers so even worst-case reads at
        // max playbackSpeed fit.
        stretchBufferFrameCapacity = audioBufferSize / 2;
        stretchInputBuffer = std::unique_ptr<float[]>(new float[audioBufferSize]);
        stretchOutputBuffer = std::unique_ptr<float[]>(new float[audioBufferSize]);
        return true;
    } else {
        LOGE("Failed to create stream. Error: %s", oboe::convertToText(result));
        stream = nullptr;
        latencyTuner = nullptr;
        return false;
    }
}

std::unique_ptr<Audio::AudioLatencySettings> Audio::findBestLatencySettings(bool preferLowLatencyAudio) {
    if (oboe::AudioStreamBuilder::isAAudioRecommended() && preferLowLatencyAudio) {
        return std::make_unique<AudioLatencySettings>(LOW_LATENCY_SETTINGS);
    } else {
        return std::make_unique<AudioLatencySettings>(DEFAULT_LATENCY_SETTINGS);
    }
}

int32_t Audio::computeAudioBufferSize() {
    double maxLatency = computeMaximumLatency();
    LOGI("Average audio latency set to: %f ms", maxLatency * 0.5);
    double sampleRateDivisor = 500.0 / maxLatency;
    return roundToEven(inputSampleRate / sampleRateDivisor);
}

double Audio::computeMaximumLatency() const {
    double maxLatency = (audioLatencySettings->bufferSizeInVideoFrames / contentRefreshRate) * 1000;
    return std::max(maxLatency, 32.0);
}

void Audio::start() {
    startRequested = true;
    if (stream != nullptr)
        stream->requestStart();
}

void Audio::stop() {
    startRequested = false;
    if (stream != nullptr)
        stream->requestStop();
}

Audio::~Audio() {
    if (stream != nullptr) {
        stream->stop();
        stream->close();
    }
}

void Audio::write(const int16_t *data, size_t frames) {
    const int32_t writtenSamples = fifoBuffer->write(data, frames * 2);
#ifndef NDEBUG
    diagnosticWrittenFrames += std::max(writtenSamples, 0) / 2;
    diagnosticDroppedFrames += frames - std::max(writtenSamples, 0) / 2;
    const auto now = std::chrono::steady_clock::now();
    if (now - diagnosticLastLog >= std::chrono::seconds(2)) {
        diagnosticLastLog = now;
        const auto xruns = stream->getXRunCount();
        LOGI("AudioDiag stats resampler=%s callbacks=%llu callback_min=%d callback_max=%d written_frames=%llu dropped_frames=%llu shortage_frames=%llu shortage_callbacks=%llu fifo_frames=%u fifo_capacity_frames=%u read_factor=%.9f speed=%.3f xruns=%d",
             useStreamingResampler ? "streaming" : "legacy",
             (unsigned long long) diagnosticCallbacks.load(std::memory_order_relaxed),
             diagnosticMinCallbackFrames.load(std::memory_order_relaxed),
             diagnosticMaxCallbackFrames.load(std::memory_order_relaxed),
             (unsigned long long) diagnosticWrittenFrames,
             (unsigned long long) diagnosticDroppedFrames,
             (unsigned long long) diagnosticUnderrunFrames.load(std::memory_order_relaxed),
             (unsigned long long) diagnosticUnderrunCallbacks.load(std::memory_order_relaxed),
             fifoBuffer->getFullFramesAvailable() / 2,
             fifoBuffer->getBufferCapacityInFrames() / 2,
             diagnosticReadFactor.load(std::memory_order_relaxed), playbackSpeed,
             xruns ? xruns.value() : -1);
    }
#else
    (void) writtenSamples;
#endif
}

void Audio::setPlaybackSpeed(const double newPlaybackSpeed) {
    playbackSpeed = newPlaybackSpeed;
}

void Audio::setPitchPreservation(bool enabled) {
    // Just flip the flag. The audio-callback thread owns the SoundTouch state and
    // will pick up the new setting on the next onAudioReady. Calling clear() or
    // setTempo() here would race with putSamples/receiveSamples.
    pitchPreservationEnabled = enabled;
}

void Audio::setOutputVolume(float volume) {
    outputVolume = volume;
}

void Audio::resetBufferState() {
    errorIntegral = 0.0;
    framesToSubmit = 0.0;
    if (fifoBuffer) {
        fifoBuffer->setReadCounter(fifoBuffer->getWriteCounter());
    }
    if (timeStretcher) {
        timeStretcher->clear();
        lastStretchTempo = 1.0;
    }
    resamplerResetRequested.store(true, std::memory_order_release);
}

void Audio::updateTiming(int32_t newSampleRate, double newRefreshRate) {
    LOGI("Audio timing update: sampleRate %d -> %d, refreshRate %f -> %f",
         inputSampleRate, newSampleRate, contentRefreshRate, newRefreshRate);
    inputSampleRate = newSampleRate;
    contentRefreshRate = newRefreshRate;
    if (stream != nullptr) {
        baseConversionFactor = (double) inputSampleRate / stream->getSampleRate();
    }
    if (timeStretcher) {
        timeStretcher->setSampleRate(inputSampleRate);
        timeStretcher->clear();
        lastStretchTempo = 1.0;
    }
    errorIntegral = 0.0;
    framesToSubmit = 0.0;
    resamplerResetRequested.store(true, std::memory_order_release);
}

oboe::DataCallbackResult Audio::onAudioReady(oboe::AudioStream *oboeStream, void *audioData, int32_t numFrames) {
    double dynamicBufferFactor = computeDynamicBufferConversionFactor(0.001 * numFrames);
    const bool stretchActive = pitchPreservationEnabled && timeStretcher != nullptr &&
        std::abs(playbackSpeed - 1.0) > kStretchBypassEpsilon;

    if (resamplerResetRequested.exchange(false, std::memory_order_acquire) ||
        stretchActive != wasStretchActive) {
        streamingResampler.reset();
    }
    wasStretchActive = stretchActive;

    // FIFO read rate covers sample-rate conversion, PI-controller buffer balancing, and
    // tempo. Without stretching, resampling the oversized buffer down to numFrames yields
    // a speed+pitch-shifted output in one pass. With stretching, SoundTouch folds the
    // tempo into the time axis (pitch preserved), and the resampler handles only
    // inputSampleRate -> stream->getSampleRate().
    double fifoReadFactor = baseConversionFactor * dynamicBufferFactor * playbackSpeed;

    framesToSubmit += numFrames * fifoReadFactor;
    int32_t currentFramesToSubmit = std::round(framesToSubmit);
    framesToSubmit -= currentFramesToSubmit;

    if (currentFramesToSubmit > stretchBufferFrameCapacity) {
        currentFramesToSubmit = stretchBufferFrameCapacity;
        fifoReadFactor = (double) currentFramesToSubmit / numFrames;
    }

    const int32_t readSamples = fifoBuffer->readNow(temporaryAudioBuffer.get(), currentFramesToSubmit * 2);
#ifndef NDEBUG
    diagnosticCallbacks.fetch_add(1, std::memory_order_relaxed);
    diagnosticMinCallbackFrames.store(std::min(numFrames,
        diagnosticMinCallbackFrames.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    diagnosticMaxCallbackFrames.store(std::max(numFrames,
        diagnosticMaxCallbackFrames.load(std::memory_order_relaxed)), std::memory_order_relaxed);
    diagnosticReadFactor.store(fifoReadFactor, std::memory_order_relaxed);
    if (readSamples < currentFramesToSubmit * 2) {
        diagnosticUnderrunFrames.fetch_add(currentFramesToSubmit - std::max(readSamples, 0) / 2,
                                           std::memory_order_relaxed);
        diagnosticUnderrunCallbacks.fetch_add(1, std::memory_order_relaxed);
    }
#else
    (void) readSamples;
#endif

    auto outputArray = reinterpret_cast<int16_t *>(audioData);

    if (stretchActive) {
        if (std::abs(playbackSpeed - lastStretchTempo) > 1e-6) {
            timeStretcher->setTempo(playbackSpeed);
            lastStretchTempo = playbackSpeed;
        }

        const int32_t sampleCount = currentFramesToSubmit * 2;
        constexpr float kInt16ToFloat = 1.0f / 32768.0f;
        for (int32_t i = 0; i < sampleCount; ++i) {
            stretchInputBuffer[i] = temporaryAudioBuffer[i] * kInt16ToFloat;
        }
        timeStretcher->putSamples(stretchInputBuffer.get(), currentFramesToSubmit);

        const int32_t wantedOutputFrames = std::min(
            (int32_t) std::round(currentFramesToSubmit / playbackSpeed),
            stretchBufferFrameCapacity
        );
        const uint received = timeStretcher->receiveSamples(
            stretchOutputBuffer.get(),
            (uint) wantedOutputFrames
        );
        const int32_t gotFrames = (int32_t) received;

        const int32_t gotSamples = gotFrames * 2;
        for (int32_t i = 0; i < gotSamples; ++i) {
            float s = stretchOutputBuffer[i] * 32768.0f;
            if (s > 32767.0f) s = 32767.0f;
            else if (s < -32768.0f) s = -32768.0f;
            temporaryAudioBuffer[i] = (int16_t) std::lrintf(s);
        }
        for (int32_t i = gotSamples; i < wantedOutputFrames * 2; ++i) {
            temporaryAudioBuffer[i] = 0;
        }
        currentFramesToSubmit = wantedOutputFrames;
    }

    if (useStreamingResampler && !stretchActive) {
        streamingResampler.resample(temporaryAudioBuffer.get(), currentFramesToSubmit,
                                   outputArray, numFrames, fifoReadFactor);
    } else {
        resampler.resample(temporaryAudioBuffer.get(), currentFramesToSubmit, outputArray, numFrames);
    }

    if (outputVolume != 1.0f) {
        const int32_t totalSamples = numFrames * 2;
        for (int32_t i = 0; i < totalSamples; ++i) {
            float s = outputArray[i] * outputVolume;
            if (s > 32767.0f) s = 32767.0f;
            else if (s < -32768.0f) s = -32768.0f;
            outputArray[i] = (int16_t) std::lrintf(s);
        }
    }

    latencyTuner->tune();

    return oboe::DataCallbackResult::Continue;
}

// To prevent audio buffer overruns or underruns we set up a PI controller. The idea is to run the
// audio slower when the buffer is empty and faster when it's full.
double Audio::computeDynamicBufferConversionFactor(double dt) {
    double framesCapacityInBuffer = fifoBuffer->getBufferCapacityInFrames();
    double framesAvailableInBuffer = fifoBuffer->getFullFramesAvailable();

    // Error is represented by normalized distance to half buffer utilization. Range [-1.0, 1.0]
    double errorMeasure = (framesCapacityInBuffer - 2.0f * framesAvailableInBuffer) / framesCapacityInBuffer;

    errorIntegral += errorMeasure * dt;

    // Wikipedia states that human ear resolution is around 3.6 Hz within the octave of 1000–2000 Hz.
    // This changes continuously, so we should try to keep it a very low value.
    double proportionalAdjustment = std::clamp(kp * errorMeasure, -maxp, maxp);

    // Ki is a lot lower, so it's safe if it exceeds the ear threshold. Hopefully convergence will
    // be slow enough to be not perceptible. We need to battle test this value.
    double integralAdjustment = std::clamp(ki * errorIntegral, -maxi, maxi);

    double finalAdjustment = proportionalAdjustment + integralAdjustment;

    LOGD("Audio speed adjustments (p: %f) (i: %f)", proportionalAdjustment, integralAdjustment);

    return 1.0 - (finalAdjustment);
}

int32_t Audio::roundToEven(int32_t x) {
    return (x / 2) * 2;
}

void Audio::onErrorAfterClose(oboe::AudioStream* oldStream, oboe::Result result) {
    AudioStreamErrorCallback::onErrorAfterClose(oldStream, result);
    LOGI("Stream error in oboe::onErrorAfterClose %s", oboe::convertToText(result));

    if (result != oboe::Result::ErrorDisconnected)
        return;

    initializeStream();
    if (startRequested) {
        start();
    }
}

} //namespace libretrodroid
