#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <mph/core/HostClock.h>

MimicPhonProcessor::MimicPhonProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", MphParams::createLayout())
{
    startTimerHz (20);
}

MimicPhonProcessor::~MimicPhonProcessor()
{
    stopTimer();
}

void MimicPhonProcessor::switchOff (const char* parameterId)
{
    if (auto* p = apvts.getParameter (parameterId))
    {
        if (p->getValue() > 0.5f)
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (0.0f);
            p->endChangeGesture();
        }
    }
}

void MimicPhonProcessor::timerCallback()
{
    // Clear acts the moment it goes on; then it goes back off.
    if (engine.takeClearHandled())
        switchOff (MphParams::clear);

    // A project saved while Clear was on must not clear anything when it is opened.
    if (switchTransientButtonsOff.exchange (false))
        switchOff (MphParams::clear);
}

void MimicPhonProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock, getTotalNumInputChannels());
}

bool MimicPhonProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Mono in / mono out, or stereo in / stereo out.
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

mph::Params MimicPhonProcessor::readParams() const
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    auto on  = [&get] (const char* id) { return get (id) > 0.5f; };
    auto choiceOf = [&get] (const char* id, int maxIndex) { return juce::jlimit (0, maxIndex, (int) std::lround (get (id))); };

    mph::Params p;
    p.inputLevel    = get (MphParams::inputLevel);
    p.mix           = get (MphParams::mix);
    p.repeats       = get (MphParams::repeats);
    p.halo          = get (MphParams::halo);
    p.outputLevel   = get (MphParams::outputLevel);
    p.color         = get (MphParams::color);
    p.rate          = get (MphParams::rate);
    p.uRate         = get (MphParams::uRate);
    p.uRateLfoRate  = get (MphParams::uRateLfoRate);
    p.uRateLfoDepth = get (MphParams::uRateLfoDepth);

    p.zone          = choiceOf (MphParams::zone,          mph::kNumZones - 1);
    p.inputGainStep = choiceOf (MphParams::inputGain,     mph::kNumInputGainSteps - 1);
    p.uRateLfoShape = choiceOf (MphParams::uRateLfoShape, 1);
    p.clockDivision = choiceOf (MphParams::clockDivision, mph::core::kNumClockDivisions - 1);

    p.hold         = on (MphParams::hold);
    p.flip         = on (MphParams::flip);
    p.skew         = on (MphParams::skew);
    p.pingPong     = on (MphParams::pingPong);
    p.clear        = on (MphParams::clear);
    p.tempoSync    = on (MphParams::tempoSync);
    p.uRateLfoSync = on (MphParams::uRateLfoSync);
    p.holdFull     = on (MphParams::holdRange);
    return p;
}

void MimicPhonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    // Silence any output channels that have no matching input (not expected with the layouts above).
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, buffer.getNumSamples());

    // What Logic says about tempo and song position (Tempo Sync and the LFO Sync follow it).
    mph::core::HostInfo host;
    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto bpm = position->getBpm())
            {
                host.tempoValid = true;
                host.bpm = *bpm;
            }
            host.playing = position->getIsPlaying();
            if (auto ppq = position->getPpqPosition())
            {
                host.ppqValid = true;
                host.ppq = *ppq;
            }
            if (auto signature = position->getTimeSignature())
                if (signature->numerator > 0 && signature->denominator > 0)
                    host.beatsPerBar = (double) signature->numerator * 4.0 / (double) signature->denominator;
        }
    }
    engine.setHost (host);

    engine.setParams (readParams());
    engine.process (buffer.getArrayOfWritePointers(), juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels()),
                    buffer.getNumSamples());
}

juce::AudioProcessorEditor* MimicPhonProcessor::createEditor()
{
    return new MimicPhonEditor (*this);
}

void MimicPhonProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void MimicPhonProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            switchTransientButtonsOff.store (true);
        }
}

// This is what Logic calls to make the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MimicPhonProcessor();
}
