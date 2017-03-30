/*
 * Copyright (C) 2010, Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "modules/webaudio/BiquadDSPKernel.h"
#include "platform/FloatConversion.h"
#include "platform/audio/AudioUtilities.h"
#include "wtf/Vector.h"
#include <limits.h>

namespace blink {

// FIXME: As a recursive linear filter, depending on its parameters, a biquad filter can have
// an infinite tailTime. In practice, Biquad filters do not usually (except for very high resonance values)
// have a tailTime of longer than approx. 200ms. This value could possibly be calculated based on the
// settings of the Biquad.
static const double MaxBiquadDelayTime = 0.2;

void BiquadDSPKernel::updateCoefficientsIfNecessary(int framesToProcess)
{
    if (biquadProcessor()->filterCoefficientsDirty()) {
        float cutoffFrequency[AudioUtilities::kRenderQuantumFrames];
        float Q[AudioUtilities::kRenderQuantumFrames];
        float gain[AudioUtilities::kRenderQuantumFrames];
        float detune[AudioUtilities::kRenderQuantumFrames]; // in Cents

        RELEASE_ASSERT_WITH_SECURITY_IMPLICATION(
            static_cast<unsigned>(framesToProcess) <= AudioUtilities::kRenderQuantumFrames);

        if (biquadProcessor()->hasSampleAccurateValues()) {
            biquadProcessor()->parameter1().calculateSampleAccurateValues(cutoffFrequency, framesToProcess);
            biquadProcessor()->parameter2().calculateSampleAccurateValues(Q, framesToProcess);
            biquadProcessor()->parameter3().calculateSampleAccurateValues(gain, framesToProcess);
            biquadProcessor()->parameter4().calculateSampleAccurateValues(detune, framesToProcess);
            updateCoefficients(framesToProcess, cutoffFrequency, Q, gain, detune);
        } else {
            cutoffFrequency[0] = biquadProcessor()->parameter1().smoothedValue();
            Q[0] = biquadProcessor()->parameter2().smoothedValue();
            gain[0] = biquadProcessor()->parameter3().smoothedValue();
            detune[0] = biquadProcessor()->parameter4().smoothedValue();
            updateCoefficients(1, cutoffFrequency, Q, gain, detune);
        }
    }
}

void BiquadDSPKernel::updateCoefficients(int numberOfFrames, const float* cutoffFrequency, const float* Q, const float* gain, const float* detune)
{
    // Convert from Hertz to normalized frequency 0 -> 1.
    double nyquist = this->nyquist();

    m_biquad.setHasSampleAccurateValues(numberOfFrames > 1);

    for (int k = 0; k < numberOfFrames; ++k) {
        double normalizedFrequency = cutoffFrequency[k] / nyquist;

        // Offset frequency by detune.
        if (detune[k])
            normalizedFrequency *= pow(2, detune[k] / 1200);

        // Configure the biquad with the new filter parameters for the appropriate type of filter.
        switch (biquadProcessor()->type()) {
        case BiquadProcessor::LowPass:
            m_biquad.setLowpassParams(k, normalizedFrequency, Q[k]);
            break;

        case BiquadProcessor::HighPass:
            m_biquad.setHighpassParams(k, normalizedFrequency, Q[k]);
            break;

        case BiquadProcessor::BandPass:
            m_biquad.setBandpassParams(k, normalizedFrequency, Q[k]);
            break;

        case BiquadProcessor::LowShelf:
            m_biquad.setLowShelfParams(k, normalizedFrequency, gain[k]);
            break;

        case BiquadProcessor::HighShelf:
            m_biquad.setHighShelfParams(k, normalizedFrequency, gain[k]);
            break;

        case BiquadProcessor::Peaking:
            m_biquad.setPeakingParams(k, normalizedFrequency, Q[k], gain[k]);
            break;

        case BiquadProcessor::Notch:
            m_biquad.setNotchParams(k, normalizedFrequency, Q[k]);
            break;

        case BiquadProcessor::Allpass:
            m_biquad.setAllpassParams(k, normalizedFrequency, Q[k]);
            break;
        }
    }
}

void BiquadDSPKernel::process(const float* source, float* destination, size_t framesToProcess)
{
    ASSERT(source);
    ASSERT(destination);
    ASSERT(biquadProcessor());

    // Recompute filter coefficients if any of the parameters have changed.
    // FIXME: as an optimization, implement a way that a Biquad object can simply copy its internal filter coefficients from another Biquad object.
    // Then re-factor this code to only run for the first BiquadDSPKernel of each BiquadProcessor.


    // The audio thread can't block on this lock; skip updating the coefficients for this block if
    // necessary. We'll get them the next time around.
    {
        MutexTryLocker tryLocker(m_processLock);
        if (tryLocker.locked())
            updateCoefficientsIfNecessary(framesToProcess);
    }

    m_biquad.process(source, destination, framesToProcess);
}

void BiquadDSPKernel::getFrequencyResponse(int nFrequencies, const float* frequencyHz, float* magResponse, float* phaseResponse)
{
    bool isGood = nFrequencies > 0 && frequencyHz && magResponse && phaseResponse;
    ASSERT(isGood);
    if (!isGood)
        return;

    Vector<float> frequency(nFrequencies);

    double nyquist = this->nyquist();

    // Convert from frequency in Hz to normalized frequency (0 -> 1),
    // with 1 equal to the Nyquist frequency.
    for (int k = 0; k < nFrequencies; ++k)
        frequency[k] = narrowPrecisionToFloat(frequencyHz[k] / nyquist);

    float cutoffFrequency;
    float Q;
    float gain;
    float detune; // in Cents

    {
        // Get a copy of the current biquad filter coefficients so we can update the biquad with
        // these values. We need to synchronize with process() to prevent process() from updating
        // the filter coefficients while we're trying to access them. The process will update it
        // next time around.
        //
        // The BiquadDSPKernel object here (along with it's Biquad object) is for querying the
        // frequency response and is NOT the same as the one in process() which is used for
        // performing the actual filtering. This one is is created in
        // BiquadProcessor::getFrequencyResponse for this purpose. Both, however, point to the same
        // BiquadProcessor object.
        //
        // FIXME: Simplify this: crbug.com/390266
        MutexLocker processLocker(m_processLock);

        cutoffFrequency = biquadProcessor()->parameter1().value();
        Q = biquadProcessor()->parameter2().value();
        gain = biquadProcessor()->parameter3().value();
        detune = biquadProcessor()->parameter4().value();
    }

    updateCoefficients(1, &cutoffFrequency, &Q, &gain, &detune);

    m_biquad.getFrequencyResponse(nFrequencies, frequency.data(), magResponse, phaseResponse);
}

double BiquadDSPKernel::tailTime() const
{
    return MaxBiquadDelayTime;
}

double BiquadDSPKernel::latencyTime() const
{
    return 0;
}

} // namespace blink
