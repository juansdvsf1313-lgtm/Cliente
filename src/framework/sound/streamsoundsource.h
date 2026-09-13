/*
 * Copyright (c) 2010-2026 OTClient <https://github.com/edubart/otclient>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#pragma once

#include "soundsource.h"

class StreamSoundSource final : public SoundSource
{
    enum
    {
        // 400 KB eran 2,3 segundos de audio (estereo 16 bits a 44,1 kHz) que
        // queueBuffers() descodifica DE GOLPE en el hilo principal cada vez que
        // arranca un sonido. Eso son los micro-parones al sonar efectos nuevos,
        // y se notaba mas cuando coincidian varios (la llamarada de Duke Krule).
        //
        // 96 KB son ~0,55 s, de sobra para que update() vaya rellenando fotograma
        // a fotograma, y el descodificado inicial cuesta la cuarta parte. Si
        // apareciera "audio buffer underrun" en el log, subir este numero.
        STREAM_BUFFER_SIZE = 1024 * 96,
        STREAM_FRAGMENTS = 4,
        STREAM_FRAGMENT_SIZE = STREAM_BUFFER_SIZE / STREAM_FRAGMENTS
    };

public:
    enum DownMix { NoDownMix, DownMixLeft, DownMixRight };

    StreamSoundSource();
    ~StreamSoundSource() override;

    void play() override;
    void stop() override;

    bool isPlaying() override { return m_playing; }

    void setSoundFile(const SoundFilePtr& soundFile);

    void setFile(std::string filename);

    void downMix(DownMix downMix);

    void update() override;

private:
    void queueBuffers();
    void unqueueBuffers() const;
    bool fillBufferAndQueue(uint32_t buffer);

    SoundFilePtr m_soundFile;
    std::array<SoundBufferPtr, STREAM_FRAGMENTS> m_buffers;
    DownMix m_downMix;
    bool m_looping{ false };
    bool m_playing{ false };
    bool m_eof{ false };
    bool m_waitingFile{ false };
};
