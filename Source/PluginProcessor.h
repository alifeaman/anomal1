#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <atomic>
#include <vector>

// Barındırılan 4 plugin yuvası
enum Slot { SlotDrum = 0, SlotAcid, SlotDrive, SlotComp, NumSlots };

static constexpr int kNumMacros = 8;

struct MacroTarget
{
    int slot = 0;
    int paramIndex = -1;
    juce::String paramName;
    float lo = 0.0f; // makro 0 iken parametre değeri (normalize)
    float hi = 1.0f; // makro 1 iken parametre değeri (normalize)
};

struct MacroDef
{
    std::vector<MacroTarget> targets;
};

class AnomalProcessor : public juce::AudioProcessor,
                        public juce::ChangeBroadcaster
{
public:
    //==========================================================================
    struct SlotListener
    {
        virtual ~SlotListener() = default;
        virtual void slotWillChange (int slot) = 0; // eski instance yok edilmeden önce
        virtual void slotChanged (int slot) = 0;    // yeni instance yerleştirildikten sonra
    };

    AnomalProcessor();
    ~AnomalProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ANOMAL-01"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    // Plugin yönetimi (mesaj thread'i)
    void startScan();
    bool isScanning() const { return scanning.load(); }
    void autoAssignMissingSlots();
    juce::AudioPluginInstance* getSlot (int slot) const { return slots[(size_t) slot].get(); }
    bool isSlotLoading (int slot) const { return loading[(size_t) slot]; }
    juce::File dumpParameters();

    // Macro eşleme (mesaj thread'i)
    void addMacroTarget (int macro, int slot, int paramIndex);
    void clearMacro (int macro);
    juce::String describeMacro (int macro) const;
    static const char* macroTitle (int i);
    static const char* macroSubtitle (int i);
    static const char* slotName (int slot);
    static juce::StringArray acidNoteNames(); // MIDI 24..96

    juce::AudioProcessorValueTreeState apvts;

    juce::ListenerList<SlotListener> slotListeners;

    // Scope / metre (audio thread yazar, editor okur)
    static constexpr int scopeSize = 512;
    std::array<std::atomic<float>, scopeSize> scope {};
    std::atomic<int> scopeWritePos { 0 };
    std::atomic<float> peakLevel { 0.0f };
    std::atomic<double> currentBpm { 0.0 };

    JUCE_DECLARE_WEAK_REFERENCEABLE (AnomalProcessor)

private:
    //==========================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static juce::File getAppDataDir();
    static bool descMatchesSlot (const juce::PluginDescription& d, int slot);

    void loadSlot (int slot, const juce::PluginDescription& desc);
    void configureInstance (juce::AudioPluginInstance& inst, int slot);
    void applyDefaultMacroMapping (int onlyForSlot);
    int findParam (int slot, const std::vector<juce::String>& tokens) const;
    void syncMacroFromTarget (int macro);
    void updateMacros();
    void scanTargeted(); // sadece 4 plugin dosyasını bulur, ana thread'de

    //==========================================================================
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;

    std::array<std::unique_ptr<juce::AudioPluginInstance>, NumSlots> slots;
    std::array<bool, NumSlots> loading {};
    std::array<juce::MemoryBlock, NumSlots> pendingState;

    mutable juce::CriticalSection lock; // slot swap + macro tablosu
    std::array<MacroDef, kNumMacros> macros;
    std::array<std::atomic<float>*, kNumMacros> macroRaw {};
    std::array<std::atomic<float>, kNumMacros> lastMacro {};
    std::atomic<float>* routeRaw = nullptr;
    std::atomic<float>* acidAutoRaw = nullptr;
    std::atomic<float>* acidRootRaw = nullptr;
    std::atomic<float>* masterRaw = nullptr;
    std::atomic<float>* fxDriveRaw = nullptr;
    std::atomic<float>* fxCompRaw = nullptr;
    std::atomic<float>* bpmFollowRaw = nullptr;
    std::atomic<float>* bpmRaw = nullptr;

    // Barındırılan pluginlere verilen (tempo'su değiştirilmiş) transport bilgisi
    struct OverrideHead : public juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    };
    OverrideHead overrideHead;
    double ownPpq = 0.0;
    bool acidNoteHeld = false;
    int acidHeldNote = 60;
    bool macrosCustomised = false; // kullanıcı/state haritayı değiştirdiyse varsayılanlar uygulanmaz

    std::atomic<bool> scanning { false };
    bool targetedScanDone = false;

    double currentSR = 44100.0;
    int currentBS = 512;

    juce::AudioBuffer<float> mixBuf, tmpBuf;
    juce::MidiBuffer drumMidi, acidMidi, emptyMidi;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnomalProcessor)
};
