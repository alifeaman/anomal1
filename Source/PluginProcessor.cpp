#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
// Yardımcılar
//==============================================================================
const char* AnomalProcessor::macroTitle (int i)
{
    static const char* t[kNumMacros] = { "KICK SNAP", "ACID CUTOFF", "ACID RESO", "GLUE SPANK",
                                         "CRUSH DRIVE", "TAPE FLUTTER", "GLITCH JITTER", "OUTPUT CEILING" };
    return t[juce::jlimit (0, kNumMacros - 1, i)];
}

const char* AnomalProcessor::macroSubtitle (int i)
{
    static const char* t[kNumMacros] = { "DRUMAZON", "ACID V", "ACID V", "I HEART NY",
                                         "CYBERDRIVE", "CYBERDRIVE", "DRUMAZON", "I HEART NY" };
    return t[juce::jlimit (0, kNumMacros - 1, i)];
}

const char* AnomalProcessor::slotName (int slot)
{
    static const char* t[NumSlots] = { "DRUMAZON 2", "ACID V", "CYBERDRIVE", "I HEART NY" };
    return t[juce::jlimit (0, (int) NumSlots - 1, slot)];
}

juce::File AnomalProcessor::getAppDataDir()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("ANOMAL-01");
    dir.createDirectory();
    return dir;
}

bool AnomalProcessor::descMatchesSlot (const juce::PluginDescription& d, int slot)
{
    auto n = d.name.toLowerCase();
    switch (slot)
    {
        case SlotDrum:  return n.contains ("drumazon");
        case SlotAcid:  return n.contains ("acid v");
        case SlotDrive: return n.contains ("cyberdrive");
        case SlotComp:  return n.contains ("heart ny");
        default:        return false;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AnomalProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int i = 0; i < kNumMacros; ++i)
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { "macro" + juce::String (i), 1 },
            juce::String ("Macro ") + juce::String (i + 1) + " " + macroTitle (i),
            juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "midiRoute", 1 }, "MIDI Route",
        juce::StringArray { "Split (ch10 = Drums)", "Both", "Drums only", "Acid only" }, 0));

    return layout;
}

//==============================================================================
struct AnomalProcessor::ScanThread : public juce::Thread
{
    explicit ScanThread (AnomalProcessor& o) : juce::Thread ("ANOMAL scan"), owner (o) {}
    void run() override { owner.runScan (*this); }
    AnomalProcessor& owner;
};

//==============================================================================
AnomalProcessor::AnomalProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    formatManager.addDefaultFormats();

    for (int i = 0; i < kNumMacros; ++i)
    {
        macroRaw[(size_t) i] = apvts.getRawParameterValue ("macro" + juce::String (i));
        lastMacro[(size_t) i].store (-1.0f);
    }
    routeRaw = apvts.getRawParameterValue ("midiRoute");

    mixBuf.setSize (2, 512);
    tmpBuf.setSize (16, 512);

    if (auto xml = juce::parseXML (getAppDataDir().getChildFile ("plugins.xml")))
        knownPlugins.recreateFromXml (*xml);

    // Host, setStateInformation'ı genelde hemen çağırır; o yüzden otomatik atamayı ertele.
    juce::WeakReference<AnomalProcessor> weak (this);
    juce::MessageManager::callAsync ([weak]
    {
        if (weak != nullptr)
            weak->autoAssignMissingSlots();
    });
}

AnomalProcessor::~AnomalProcessor()
{
    if (scanThread != nullptr)
        scanThread->stopThread (8000);
}

//==============================================================================
// Tarama
//==============================================================================
void AnomalProcessor::startScan()
{
    if (scanning.load())
        return;

    if (scanThread != nullptr)
        scanThread->stopThread (2000);

    scanning = true;
    sendChangeMessage();

    scanThread = std::make_unique<ScanThread> (*this);
    scanThread->startThread();
}

void AnomalProcessor::runScan (juce::Thread& t)
{
    for (int i = 0; i < formatManager.getNumFormats(); ++i)
    {
        auto* format = formatManager.getFormat (i);

        // Arka plan thread'inde sadece VST3 taranır (AU taraması mesaj thread'i ister).
        if (format->getName() != "VST3")
            continue;

        juce::PluginDirectoryScanner scanner (knownPlugins, *format,
                                              format->getDefaultLocationsToSearch(), true,
                                              getAppDataDir().getChildFile ("scan.deadmans"), false);
        juce::String name;
        while (! t.threadShouldExit() && scanner.scanNextFile (true, name))
        {
        }
    }

    if (auto xml = knownPlugins.createXml())
        xml->writeTo (getAppDataDir().getChildFile ("plugins.xml"));

    juce::WeakReference<AnomalProcessor> weak (this);
    juce::MessageManager::callAsync ([weak]
    {
        if (weak != nullptr)
        {
            weak->scanning = false;
            weak->autoAssignMissingSlots();
            weak->sendChangeMessage();
        }
    });
}

void AnomalProcessor::autoAssignMissingSlots()
{
    if (knownPlugins.getNumTypes() == 0 && ! scanning.load())
    {
        startScan();
        return;
    }

    for (int s = 0; s < NumSlots; ++s)
    {
        if (slots[(size_t) s] != nullptr || loading[(size_t) s])
            continue;

        const juce::PluginDescription* best = nullptr;
        auto types = knownPlugins.getTypes();

        for (auto& d : types)
        {
            if (! descMatchesSlot (d, s))
                continue;
            if (best == nullptr || d.pluginFormatName == "VST3")
                best = &d;
        }

        if (best != nullptr)
            loadSlot (s, *best);
    }

    sendChangeMessage();
}

//==============================================================================
// Plugin yükleme
//==============================================================================
void AnomalProcessor::configureInstance (juce::AudioPluginInstance& inst, int)
{
    auto layout = inst.getBusesLayout();

    if (inst.getBusCount (false) > 0)
        layout.getChannelSet (false, 0) = juce::AudioChannelSet::stereo();
    if (inst.getBusCount (true) > 0)
        layout.getChannelSet (true, 0) = juce::AudioChannelSet::stereo();

    inst.setBusesLayout (layout); // olmazsa plugin'in varsayılanı kalır

    inst.setRateAndBufferSizeDetails (currentSR, currentBS);
    inst.prepareToPlay (currentSR, currentBS);
}

void AnomalProcessor::loadSlot (int slot, const juce::PluginDescription& desc)
{
    loading[(size_t) slot] = true;
    sendChangeMessage();

    juce::WeakReference<AnomalProcessor> weak (this);

    formatManager.createPluginInstanceAsync (desc, currentSR, currentBS,
        [weak, slot] (std::unique_ptr<juce::AudioPluginInstance> inst, const juce::String& error)
        {
            if (weak == nullptr)
                return;

            auto& p = *weak;
            p.loading[(size_t) slot] = false;

            if (inst == nullptr)
            {
                DBG ("ANOMAL-01: plugin yuklenemedi: " << error);
                p.sendChangeMessage();
                return;
            }

            p.slotListeners.call (&SlotListener::slotWillChange, slot);
            p.configureInstance (*inst, slot);

            {
                const juce::ScopedLock sl (p.lock);
                p.slots[(size_t) slot].swap (inst);
            }
            inst.reset(); // eski instance (varsa) burada, mesaj thread'inde yok edilir

            auto& ps = p.pendingState[(size_t) slot];
            if (ps.getSize() > 0)
            {
                p.slots[(size_t) slot]->setStateInformation (ps.getData(), (int) ps.getSize());
                ps.reset();
            }

            p.applyDefaultMacroMapping (slot);

            // Macro'ların son bilinen değerlerini güncelle: yüklemede plugin preset'ini ezme.
            for (int m = 0; m < kNumMacros; ++m)
            {
                bool hasTargetInSlot = false;
                {
                    const juce::ScopedLock sl (p.lock);
                    for (auto& t : p.macros[(size_t) m].targets)
                        if (t.slot == slot)
                            hasTargetInSlot = true;
                }
                if (! hasTargetInSlot)
                    continue;

                if (p.macrosCustomised)
                    p.lastMacro[(size_t) m].store (p.macroRaw[(size_t) m]->load());
                else
                    p.syncMacroFromTarget (m);
            }

            p.slotListeners.call (&SlotListener::slotChanged, slot);
            p.sendChangeMessage();
        });
}

//==============================================================================
// Macro eşleme
//==============================================================================
int AnomalProcessor::findParam (int slot, const std::vector<juce::String>& tokens) const
{
    auto* inst = slots[(size_t) slot].get();
    if (inst == nullptr)
        return -1;

    auto& params = inst->getParameters();
    for (int i = 0; i < params.size(); ++i)
    {
        auto name = params[i]->getName (100).toLowerCase();
        bool all = true;
        for (auto& tk : tokens)
            if (! name.contains (tk))
                all = false;
        if (all)
            return i;
    }
    return -1;
}

void AnomalProcessor::applyDefaultMacroMapping (int onlySlot)
{
    if (macrosCustomised)
        return;

    struct Def { int slot; std::vector<std::vector<juce::String>> candidates; };
    static const Def defs[kNumMacros] = {
        { SlotDrum,  { { "bd", "attack" }, { "bass", "attack" }, { "kick", "attack" } } },
        { SlotAcid,  { { "cutoff" } } },
        { SlotAcid,  { { "reson" } } },
        { SlotComp,  { { "spank" } } },
        { SlotDrive, { { "dist 1", "drive" }, { "dist1", "drive" }, { "drive" } } },
        { SlotDrive, { { "motion", "rate" }, { "chorus", "rate" }, { "rate" } } },
        { SlotDrum,  { { "shuffle" } } },
        { SlotComp,  { { "output" } } },
    };

    for (int m = 0; m < kNumMacros; ++m)
    {
        if (defs[m].slot != onlySlot)
            continue;

        {
            const juce::ScopedLock sl (lock);
            if (! macros[(size_t) m].targets.empty())
                continue;
        }

        for (auto& cand : defs[m].candidates)
        {
            int idx = findParam (onlySlot, cand);
            if (idx >= 0)
            {
                MacroTarget t;
                t.slot = onlySlot;
                t.paramIndex = idx;
                t.paramName = slots[(size_t) onlySlot]->getParameters()[idx]->getName (100);

                const juce::ScopedLock sl (lock);
                macros[(size_t) m].targets.push_back (t);
                break;
            }
        }
    }
}

void AnomalProcessor::syncMacroFromTarget (int macro)
{
    MacroTarget t;
    {
        const juce::ScopedLock sl (lock);
        auto& tg = macros[(size_t) macro].targets;
        if (tg.empty())
            return;
        t = tg.front();
    }

    auto* inst = slots[(size_t) t.slot].get();
    if (inst == nullptr || t.paramIndex < 0 || t.paramIndex >= inst->getParameters().size())
        return;

    float v = inst->getParameters()[t.paramIndex]->getValue();
    float range = t.hi - t.lo;
    float norm = std::abs (range) > 1.0e-6f ? juce::jlimit (0.0f, 1.0f, (v - t.lo) / range) : 0.0f;

    lastMacro[(size_t) macro].store (norm);
    if (auto* p = apvts.getParameter ("macro" + juce::String (macro)))
        p->setValueNotifyingHost (norm);
}

void AnomalProcessor::addMacroTarget (int macro, int slot, int paramIndex)
{
    auto* inst = slots[(size_t) slot].get();
    if (inst == nullptr || paramIndex < 0 || paramIndex >= inst->getParameters().size())
        return;

    MacroTarget t;
    t.slot = slot;
    t.paramIndex = paramIndex;
    t.paramName = inst->getParameters()[paramIndex]->getName (100);

    {
        const juce::ScopedLock sl (lock);
        macros[(size_t) macro].targets.push_back (t);
    }
    macrosCustomised = true;
    sendChangeMessage();
}

void AnomalProcessor::clearMacro (int macro)
{
    {
        const juce::ScopedLock sl (lock);
        macros[(size_t) macro].targets.clear();
    }
    macrosCustomised = true;
    sendChangeMessage();
}

juce::String AnomalProcessor::describeMacro (int macro) const
{
    const juce::ScopedLock sl (lock);
    auto& tg = macros[(size_t) macro].targets;
    if (tg.empty())
        return "unmapped (right-click)";

    juce::String s = juce::String (slotName (tg.front().slot)) + ": " + tg.front().paramName;
    if (tg.size() > 1)
        s << " +" << juce::String ((int) tg.size() - 1);
    return s;
}

void AnomalProcessor::updateMacros()
{
    // 'lock' çağıran tarafından tutuluyor
    for (int m = 0; m < kNumMacros; ++m)
    {
        float v = macroRaw[(size_t) m]->load();
        if (v == lastMacro[(size_t) m].load())
            continue;

        lastMacro[(size_t) m].store (v);

        for (auto& t : macros[(size_t) m].targets)
        {
            auto* inst = slots[(size_t) t.slot].get();
            if (inst == nullptr)
                continue;

            auto& params = inst->getParameters();
            if (t.paramIndex < 0 || t.paramIndex >= params.size())
                continue;

            params[t.paramIndex]->setValue (juce::jmap (v, t.lo, t.hi));
        }
    }
}

juce::File AnomalProcessor::dumpParameters()
{
    juce::String out;
    for (int s = 0; s < NumSlots; ++s)
    {
        out << "=== " << slotName (s) << " ===\n";
        if (auto* inst = slots[(size_t) s].get())
        {
            auto& params = inst->getParameters();
            for (int i = 0; i < params.size(); ++i)
                out << i << ": " << params[i]->getName (100) << "  [" << params[i]->getValue() << "]\n";
        }
        else
            out << "(yuklu degil)\n";
    }

    auto f = getAppDataDir().getChildFile ("parameters.txt");
    f.replaceWithText (out);
    return f;
}

//==============================================================================
// Ses
//==============================================================================
bool AnomalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AnomalProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSR = sampleRate;
    currentBS = samplesPerBlock;

    mixBuf.setSize (2, samplesPerBlock);
    tmpBuf.setSize (16, samplesPerBlock);

    const juce::ScopedLock sl (lock);
    for (auto& s : slots)
        if (s != nullptr)
        {
            s->setRateAndBufferSizeDetails (sampleRate, samplesPerBlock);
            s->prepareToPlay (sampleRate, samplesPerBlock);
        }
}

void AnomalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int n = buffer.getNumSamples();
    buffer.clear();

    juce::ScopedTryLock tryLock (lock);
    if (! tryLock.isLocked())
        return;

    if (n > mixBuf.getNumSamples() || n > tmpBuf.getNumSamples())
    {
        mixBuf.setSize (2, n, false, false, true);
        tmpBuf.setSize (16, n, false, false, true);
    }

    updateMacros();

    // Transport bilgisi
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                currentBpm.store (*bpm);

    // MIDI yönlendirme
    drumMidi.clear();
    acidMidi.clear();
    const int route = (int) routeRaw->load();

    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const int ch = msg.getChannel(); // kanal mesajı değilse 0

        bool toDrum = true, toAcid = true;
        if (ch != 0)
        {
            switch (route)
            {
                case 0:  toDrum = (ch == 10); toAcid = (ch != 10); break;
                case 1:  break;
                case 2:  toAcid = false; break;
                case 3:  toDrum = false; break;
                default: break;
            }
        }
        if (toDrum) drumMidi.addEvent (msg, meta.samplePosition);
        if (toAcid) acidMidi.addEvent (msg, meta.samplePosition);
    }

    juce::AudioBuffer<float> mix (mixBuf.getArrayOfWritePointers(), 2, n);
    mix.clear();

    auto runInstrument = [&] (int slot, juce::MidiBuffer& m)
    {
        auto* inst = slots[(size_t) slot].get();
        if (inst == nullptr)
            return;

        inst->setPlayHead (getPlayHead());

        const int outs = inst->getTotalNumOutputChannels();
        if (outs <= 0)
            return;

        const int ch = juce::jmin (tmpBuf.getNumChannels(),
                                   juce::jmax (2, juce::jmax (outs, inst->getTotalNumInputChannels())));
        juce::AudioBuffer<float> v (tmpBuf.getArrayOfWritePointers(), ch, n);
        v.clear();
        inst->processBlock (v, m);

        for (int c = 0; c < 2; ++c)
            mix.addFrom (c, 0, v, juce::jmin (c, outs - 1), 0, n);
    };

    auto runEffect = [&] (int slot)
    {
        auto* inst = slots[(size_t) slot].get();
        if (inst == nullptr)
            return;

        inst->setPlayHead (getPlayHead());

        const int outs = inst->getTotalNumOutputChannels();
        const int ins = inst->getTotalNumInputChannels();
        const int ch = juce::jmin (tmpBuf.getNumChannels(), juce::jmax (2, juce::jmax (outs, ins)));

        juce::AudioBuffer<float> v (tmpBuf.getArrayOfWritePointers(), ch, n);
        v.clear();
        for (int c = 0; c < 2; ++c)
            v.copyFrom (c, 0, mix, c, 0, n);

        emptyMidi.clear();
        inst->processBlock (v, emptyMidi);

        if (outs > 0)
            for (int c = 0; c < 2; ++c)
                mix.copyFrom (c, 0, v, juce::jmin (c, outs - 1), 0, n);
    };

    runInstrument (SlotDrum, drumMidi);
    runInstrument (SlotAcid, acidMidi);
    runEffect (SlotDrive);
    runEffect (SlotComp);

    const int outCh = juce::jmin (2, buffer.getNumChannels());
    for (int c = 0; c < outCh; ++c)
        buffer.copyFrom (c, 0, mix, c, 0, n);

    // Metre + scope
    float peak = 0.0f;
    int pos = scopeWritePos.load();
    for (int i = 0; i < n; ++i)
    {
        const float l = mix.getSample (0, i);
        const float r = mix.getSample (1, i);
        peak = juce::jmax (peak, std::abs (l), std::abs (r));

        if ((i & 7) == 0)
        {
            scope[(size_t) pos].store (0.5f * (l + r));
            pos = (pos + 1) % scopeSize;
        }
    }
    scopeWritePos.store (pos);
    peakLevel.store (juce::jmax (peak, peakLevel.load() * 0.9f));
}

//==============================================================================
// Editor
//==============================================================================
juce::AudioProcessorEditor* AnomalProcessor::createEditor()
{
    return new AnomalEditor (*this);
}

//==============================================================================
// Durum kaydı
//==============================================================================
void AnomalProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("ANOMAL");

    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        root.addChildElement (xml.release());

    // Macro haritası
    {
        auto* map = root.createNewChildElement ("MACROMAP");
        map->setAttribute ("custom", macrosCustomised ? 1 : 0);

        const juce::ScopedLock sl (lock);
        for (int m = 0; m < kNumMacros; ++m)
        {
            auto* me = map->createNewChildElement ("MACRO");
            me->setAttribute ("i", m);
            for (auto& t : macros[(size_t) m].targets)
            {
                auto* te = me->createNewChildElement ("T");
                te->setAttribute ("slot", t.slot);
                te->setAttribute ("idx", t.paramIndex);
                te->setAttribute ("name", t.paramName);
                te->setAttribute ("lo", (double) t.lo);
                te->setAttribute ("hi", (double) t.hi);
            }
        }
    }

    // Barındırılan pluginler + kendi durumları
    for (int s = 0; s < NumSlots; ++s)
    {
        auto* inst = slots[(size_t) s].get();
        if (inst == nullptr)
            continue;

        auto* se = root.createNewChildElement ("SLOT");
        se->setAttribute ("index", s);

        if (auto desc = inst->getPluginDescription().createXml())
            se->addChildElement (desc.release());

        juce::MemoryBlock mb;
        inst->getStateInformation (mb);
        se->setAttribute ("state", mb.toBase64Encoding());
    }

    copyXmlToBinary (root, destData);
}

void AnomalProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("ANOMAL"))
        return;

    if (auto* st = xml->getChildByName (apvts.state.getType().toString()))
        apvts.replaceState (juce::ValueTree::fromXml (*st));

    if (auto* map = xml->getChildByName ("MACROMAP"))
    {
        macrosCustomised = map->getIntAttribute ("custom", 0) != 0;

        const juce::ScopedLock sl (lock);
        for (auto& m : macros)
            m.targets.clear();

        for (auto* me : map->getChildWithTagNameIterator ("MACRO"))
        {
            const int i = me->getIntAttribute ("i", -1);
            if (i < 0 || i >= kNumMacros)
                continue;

            for (auto* te : me->getChildWithTagNameIterator ("T"))
            {
                MacroTarget t;
                t.slot = te->getIntAttribute ("slot");
                t.paramIndex = te->getIntAttribute ("idx", -1);
                t.paramName = te->getStringAttribute ("name");
                t.lo = (float) te->getDoubleAttribute ("lo", 0.0);
                t.hi = (float) te->getDoubleAttribute ("hi", 1.0);
                macros[(size_t) i].targets.push_back (t);
            }
        }
    }

    for (auto* se : xml->getChildWithTagNameIterator ("SLOT"))
    {
        const int s = se->getIntAttribute ("index", -1);
        if (s < 0 || s >= NumSlots)
            continue;

        juce::PluginDescription desc;
        bool ok = false;
        for (auto* child : se->getChildIterator())
            if (desc.loadFromXml (*child))
            {
                ok = true;
                break;
            }

        if (! ok)
            continue;

        pendingState[(size_t) s].reset();
        pendingState[(size_t) s].fromBase64Encoding (se->getStringAttribute ("state"));
        loadSlot (s, desc);
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnomalProcessor();
}
