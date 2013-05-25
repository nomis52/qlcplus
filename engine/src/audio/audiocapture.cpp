/*
  Q Light Controller Plus
  audiocapture.cpp

  Copyright (c) Massimo Callegari

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  Version 2 as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details. The license is
  in the file "COPYING".

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include <QDebug>
#include <qmath.h>

#include "audiocapture.h"

#include "fftw3.h"

#define USE_HANNING
#define CLEAR_NOISE

#define M_2PI       6.28318530718           /* 2*pi */

AudioCapture::AudioCapture (QObject* parent)
    : QThread (parent)
    , m_userStop(true)
    , m_pause(false)
{
}

AudioCapture::~AudioCapture()
{
    if (m_audioBuffer)
        delete[] m_audioBuffer;
    if (m_fftInputBuffer)
        delete[] m_fftInputBuffer;
    if (m_fftOutputBuffer)
        delete[] (fftw_complex*)m_fftOutputBuffer;
}

bool AudioCapture::initialize(quint32 sampleRate, quint8 channels, quint16 bufferSize)
{
    Q_UNUSED(sampleRate)

    m_captureSize = bufferSize * channels;
    m_sampleRate = sampleRate;
    m_channels = channels;

    m_audioBuffer = new qint16[m_captureSize];
    m_fftInputBuffer = new double[m_captureSize];
    m_fftOutputBuffer = fftw_malloc(sizeof(fftw_complex) * m_captureSize);

    return true;
}

void AudioCapture::stop()
{
    m_userStop = true;
    while (this->isRunning())
        usleep(10000);
}

void AudioCapture::processData()
{
    int i;
    quint64 pwrSum = 0;

    // 1 ********* Initialize FFTW
    fftw_plan plan_forward;
    plan_forward = fftw_plan_dft_r2c_1d(m_captureSize, m_fftInputBuffer, (fftw_complex*)m_fftOutputBuffer , 0);

    // 2 ********* Apply a window to audio data
    // *********** and convert it to doubles
    for (i = 0; i < m_captureSize; i++)
    {
        if(m_audioBuffer[i] < 0)
            pwrSum += -1 * m_audioBuffer[i];
        else
            pwrSum += m_audioBuffer[i];

#ifdef USE_BLACKMAN
        double a0 = (1-0.16)/2;
        double a1 = 0.5;
        double a2 = 0.16/2;
        m_fftInputBuffer[i] = m_audioBuffer[i]  * (a0 - a1 * qCos((M_2PI * i) / (m_captureSize - 1)) +
                              a2 * qCos((2 * M_2PI * i) / (m_captureSize - 1)));
#endif
#ifdef USE_HANNING
        m_fftInputBuffer[i] = m_audioBuffer[i] * (0.5 * (1.00 - qCos((M_2PI * i) / (m_captureSize - 1))));
#endif
#ifdef USE_NO_WINDOW
        m_fftInputBuffer[i] = (double)m_audioBuffer[i];
#endif
    }

    // 3 ********* Perform FFT
    fftw_execute(plan_forward);
    fftw_destroy_plan(plan_forward);

    // 4 ********* Clear FFT noise
#ifdef CLEAR_NOISE
    //We delete some values since these will ruin our output
    ((fftw_complex*)m_fftOutputBuffer)[0][0] = 0;
    ((fftw_complex*)m_fftOutputBuffer)[0][1] = 0;
    ((fftw_complex*)m_fftOutputBuffer)[1][0] = 0;
    ((fftw_complex*)m_fftOutputBuffer)[1][1] = 0;
#endif

    // 5 ********* Calculate the average signal power
    m_signalPower = pwrSum / m_captureSize;

    // 6 ********* Calculate vector magnitude

    // m_fftOutputBuffer contains the real and imaginary data of a spectrum
    // representing all the frequencies from 0 to m_sampleRate Hz.
    // I will just consider 0 to 5000Hz and will calculate average magnitude
    // for the number of desired bands.
    i = 0;
    int subBandWidth = ((m_captureSize * 5000) / m_sampleRate) / FREQ_SUBBANDS_NUMBER;

    for (int b = 0; b < FREQ_SUBBANDS_NUMBER; b++)
    {
        quint64 magnitudeSum = 0;
        for (int s = 0; s < subBandWidth; s++, i++)
        {
            if (i == m_captureSize)
                break;
            magnitudeSum += qSqrt((((fftw_complex*)m_fftOutputBuffer)[i][0] * ((fftw_complex*)m_fftOutputBuffer)[i][0]) +
                                  (((fftw_complex*)m_fftOutputBuffer)[i][1] * ((fftw_complex*)m_fftOutputBuffer)[i][1]));
        }
        m_fftMagnitudeBuffer[b] = (magnitudeSum / subBandWidth);
    }
}

void AudioCapture::run()
{
    m_userStop = false;

    while (!m_userStop)
    {
        m_mutex.lock();
        if (m_pause == false)
        {
            if (readAudio(m_captureSize) == true)
            {
                processData();
                emit dataProcessed(m_fftMagnitudeBuffer, m_signalPower);
            }
            else
                qDebug() << "Error reading data from audio source";

        }
        else
            usleep(15000);
        m_mutex.unlock();
    }
}