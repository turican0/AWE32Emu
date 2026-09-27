// Portable realtime output through RtAudio (https://github.com/thestk/rtaudio).
// Built only with the CMake option AWE32EMU_WITH_RTAUDIO.
#include "AudioOutput.h"
#include "I18n.h"

#include <RtAudio.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
    // RtAudio pulls the sound from its own thread (the callback); Write()
    // pushes into a ring buffer between the two and waits while it is full.
    class AudioOutputRt final : public AudioOutput
    {
    public:
        ~AudioOutputRt() override { Close(); }

        bool Open(uint32_t sampleRate, uint32_t framesPerBuffer, std::string& err) override
        {
            if (m_dac.getDeviceCount() == 0)
            {
                err = _("RtAudio found no audio device");
                return false;
            }

            RtAudio::StreamParameters out;
            out.deviceId = m_dac.getDefaultOutputDevice();
            out.nChannels = 2;
            out.firstChannel = 0;

            RtAudio::StreamOptions opt;
            opt.streamName = "AWE32Emu";

            // Room for four blocks, as the waveOut backend has.
            m_ring.assign(static_cast<size_t>(framesPerBuffer) * 2 * 4, 0);
            m_read = m_write = m_fill = 0;

            unsigned int bufferFrames = framesPerBuffer;
            if (m_dac.openStream(&out, nullptr, RTAUDIO_SINT16, sampleRate, &bufferFrames,
                                 &AudioOutputRt::Callback, this, &opt) != RTAUDIO_NO_ERROR
                || m_dac.startStream() != RTAUDIO_NO_ERROR)
            {
                err = StrFormat(_("RtAudio: %s"), m_dac.getErrorText().c_str());
                if (m_dac.isStreamOpen())
                    m_dac.closeStream();
                return false;
            }
            return true;
        }

        void Write(const int16_t* interleavedStereo, uint32_t numFrames) override
        {
            size_t left = static_cast<size_t>(numFrames) * 2;
            std::unique_lock<std::mutex> lock(m_mutex);
            while (left > 0 && m_dac.isStreamRunning())
            {
                m_cv.wait_for(lock, std::chrono::milliseconds(50),
                              [&] { return m_fill < m_ring.size(); });
                const size_t n = std::min(left, m_ring.size() - m_fill);
                for (size_t i = 0; i < n; ++i)
                {
                    m_ring[m_write] = *interleavedStereo++;
                    m_write = (m_write + 1) % m_ring.size();
                }
                m_fill += n;
                left -= n;
            }
        }

        void Close() override
        {
            if (!m_dac.isStreamOpen())
                return;
            {
                // Let the queue play out; the timeout guards against a device
                // that stopped pulling.
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait_for(lock, std::chrono::seconds(2), [&] { return m_fill == 0; });
            }
            if (m_dac.isStreamRunning())
                m_dac.stopStream();          // plays out the device buffers
            m_dac.closeStream();
        }

    private:
        static int Callback(void* output, void*, unsigned int nFrames, double,
                            RtAudioStreamStatus, void* user)
        {
            auto* self = static_cast<AudioOutputRt*>(user);
            auto* out = static_cast<int16_t*>(output);
            const size_t want = static_cast<size_t>(nFrames) * 2;
            size_t got = 0;
            {
                std::lock_guard<std::mutex> lock(self->m_mutex);
                got = std::min(want, self->m_fill);
                for (size_t i = 0; i < got; ++i)
                {
                    out[i] = self->m_ring[self->m_read];
                    self->m_read = (self->m_read + 1) % self->m_ring.size();
                }
                self->m_fill -= got;
            }
            // Underrun: silence rather than a repeated block.
            if (got < want)
                std::memset(out + got, 0, (want - got) * sizeof(int16_t));
            self->m_cv.notify_all();
            return 0;
        }

        RtAudio m_dac;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::vector<int16_t> m_ring;
        size_t m_read = 0, m_write = 0, m_fill = 0;
    };
}

std::unique_ptr<AudioOutput> CreateAudioOutputRt()
{
    return std::make_unique<AudioOutputRt>();
}
