#include "PluginEditor.h"

namespace
{
    namespace C
    {
        const juce::Colour bg      { 0xff0e1014 };
        const juce::Colour panel   { 0xff14171d };
        const juce::Colour border  { 0xff262b34 };
        const juce::Colour text    { 0xffe8ebf0 };
        const juce::Colour dim     { 0xff7d8594 };
        const juce::Colour green   { 0xff7cfc3c };
        const juce::Colour orange  { 0xffffb020 };
        const juce::Colour red     { 0xffff4d4d };
    }

    juce::Font mono (float h, bool bold = false)
    {
        return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), h,
                                              bold ? juce::Font::bold : juce::Font::plain));
    }

    //==========================================================================
    struct DeckLnF : public juce::LookAndFeel_V4
    {
        void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                               juce::Slider::SliderStyle, juce::Slider&) override
        {
            const float cy = (float) y + (float) h * 0.5f;
            g.setColour (juce::Colour (0xff1d2128));
            g.fillRoundedRectangle ((float) x, cy - 3.0f, (float) w, 6.0f, 3.0f);

            g.setColour (C::green.withAlpha (0.35f));
            g.fillRoundedRectangle ((float) x, cy - 3.0f, pos - (float) x, 6.0f, 3.0f);

            g.setColour (juce::Colour (0xffd8dbe0));
            g.fillRoundedRectangle (pos - 6.0f, cy - 12.0f, 12.0f, 24.0f, 2.5f);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawRoundedRectangle (pos - 6.0f, cy - 12.0f, 12.0f, 24.0f, 2.5f, 1.0f);
        }
    };

    //==========================================================================
    class MacroCell : public juce::Component
    {
    public:
        MacroCell (AnomalProcessor& p, int idx, juce::LookAndFeel& lnf)
            : proc (p), index (idx)
        {
            slider.setSliderStyle (juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            slider.setLookAndFeel (&lnf);
            slider.setPopupMenuEnabled (false);
            slider.addMouseListener (this, false);
            slider.onValueChange = [this] { repaint(); };
            addAndMakeVisible (slider);

            attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                proc.apvts, "macro" + juce::String (index), slider);
        }

        ~MacroCell() override { slider.setLookAndFeel (nullptr); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (C::panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (C::border);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

            g.setColour (C::dim);
            g.setFont (mono (10.5f));
            g.drawText ("M." + juce::String (index + 1).paddedLeft ('0', 2) + " // " + AnomalProcessor::macroSubtitle (index),
                        getLocalBounds().removeFromTop (30).withTrimmedTop (10), juce::Justification::centred);

            g.setColour (C::text);
            g.setFont (mono (15.0f, true));
            g.drawText (AnomalProcessor::macroTitle (index),
                        getLocalBounds().withTrimmedTop (28).removeFromTop (24), juce::Justification::centred);

            const float v = (float) slider.getValue();
            g.setColour (v < 0.7f ? C::green : (v < 0.9f ? C::orange : C::red));
            g.setFont (mono (15.0f, true));
            g.drawText (juce::String (juce::roundToInt (v * 100.0f)) + "%",
                        getLocalBounds().withTrimmedTop (getHeight() - 52).removeFromTop (22), juce::Justification::centred);

            const auto desc = proc.describeMacro (index);
            g.setColour (desc.startsWith ("unmapped") ? C::red.withAlpha (0.8f) : C::dim);
            g.setFont (mono (9.5f));
            g.drawText (desc, getLocalBounds().removeFromBottom (24).reduced (6, 0),
                        juce::Justification::centred, true);
        }

        void resized() override
        {
            slider.setBounds (getLocalBounds().withTrimmedTop (58).removeFromTop (30).reduced (22, 0));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                showMapMenu();
        }

    private:
        void showMapMenu()
        {
            juce::PopupMenu menu;
            menu.addSectionHeader (juce::String ("MAP: ") + AnomalProcessor::macroTitle (index));
            menu.addItem (1, "Clear mapping");
            menu.addSeparator();

            for (int s = 0; s < NumSlots; ++s)
            {
                auto* inst = proc.getSlot (s);
                juce::PopupMenu slotMenu;

                if (inst == nullptr)
                {
                    menu.addItem (-1, juce::String (AnomalProcessor::slotName (s)) + " (not loaded)", false);
                    continue;
                }

                auto& params = inst->getParameters();
                const int chunk = 40;
                for (int start = 0; start < params.size(); start += chunk)
                {
                    juce::PopupMenu sub;
                    const int end = juce::jmin (start + chunk, params.size());
                    for (int i = start; i < end; ++i)
                        sub.addItem (100 + s * 100000 + i, params[i]->getName (60));
                    slotMenu.addSubMenu (juce::String (start) + " - " + juce::String (end - 1), sub);
                }
                menu.addSubMenu (AnomalProcessor::slotName (s), slotMenu);
            }

            juce::Component::SafePointer<MacroCell> safe (this);
            menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                                [safe] (int result)
                                {
                                    if (safe == nullptr || result == 0)
                                        return;

                                    if (result == 1)
                                    {
                                        safe->proc.clearMacro (safe->index);
                                    }
                                    else if (result >= 100)
                                    {
                                        const int code = result - 100;
                                        safe->proc.addMacroTarget (safe->index, code / 100000, code % 100000);
                                    }
                                    safe->repaint();
                                });
        }

        AnomalProcessor& proc;
        int index;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    //==========================================================================
    class DeckView : public juce::Component,
                     public juce::ChangeListener,
                     private juce::Timer
    {
    public:
        explicit DeckView (AnomalProcessor& p) : proc (p)
        {
            for (int i = 0; i < kNumMacros; ++i)
            {
                cells[(size_t) i] = std::make_unique<MacroCell> (proc, i, lnf);
                addAndMakeVisible (*cells[(size_t) i]);
            }
            acidAuto.setButtonText ("ACID FOLLOWS LIVE PLAY");
            acidAuto.setColour (juce::ToggleButton::textColourId, C::text);
            acidAuto.setColour (juce::ToggleButton::tickColourId, C::green);
            addAndMakeVisible (acidAuto);
            acidAutoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                proc.apvts, "acidAuto", acidAuto);

            rootLabel.setText ("ROOT", juce::dontSendNotification);
            rootLabel.setFont (mono (11.0f));
            rootLabel.setColour (juce::Label::textColourId, C::dim);
            addAndMakeVisible (rootLabel);

            acidRoot.setSliderStyle (juce::Slider::IncDecButtons);
            acidRoot.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 52, 22);
            acidRoot.setIncDecButtonsMode (juce::Slider::incDecButtonsNotDraggable);
            acidRoot.textFromValueFunction = [] (double v)
            {
                return juce::MidiMessage::getMidiNoteName ((int) v, true, true, 3);
            };
            acidRoot.setColour (juce::Slider::textBoxTextColourId, C::text);
            acidRoot.setColour (juce::Slider::textBoxBackgroundColourId, C::panel);
            acidRoot.setColour (juce::Slider::textBoxOutlineColourId, C::border);
            addAndMakeVisible (acidRoot);
            acidRootAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                proc.apvts, "acidRoot", acidRoot);

            masterLabel.setText ("MASTER", juce::dontSendNotification);
            masterLabel.setFont (mono (11.0f));
            masterLabel.setColour (juce::Label::textColourId, C::dim);
            addAndMakeVisible (masterLabel);

            master.setSliderStyle (juce::Slider::LinearHorizontal);
            master.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
            master.setTextValueSuffix (" dB");
            master.setColour (juce::Slider::textBoxTextColourId, C::text);
            master.setColour (juce::Slider::textBoxBackgroundColourId, C::panel);
            master.setColour (juce::Slider::textBoxOutlineColourId, C::border);
            master.setColour (juce::Slider::trackColourId, C::green.withAlpha (0.5f));
            master.setColour (juce::Slider::thumbColourId, juce::Colour (0xffd8dbe0));
            addAndMakeVisible (master);
            masterAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
                proc.apvts, "master", master);

            proc.addChangeListener (this);
            startTimerHz (30);
        }

        ~DeckView() override
        {
            proc.removeChangeListener (this);
            for (auto& c : cells)
                c.reset();
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
        void timerCallback() override { repaint (scopeRect.getUnion (meterRect).getUnion (tempoRect).expanded (2)); }

        void resized() override
        {
            auto area = getLocalBounds().reduced (24, 12);
            auto right = area.removeFromRight (380);
            area.removeFromRight (20);

            // Sol: 4x2 macro ızgarası
            auto grid = area.withTrimmedTop (34);
            const int gap = 14;
            const int cw = (grid.getWidth() - gap * 3) / 4;
            const int ch = juce::jmin (190, (grid.getHeight() - gap) / 2);
            const int startY = grid.getY() + (grid.getHeight() - (ch * 2 + gap)) / 2;

            for (int i = 0; i < kNumMacros; ++i)
            {
                const int col = i % 4, row = i / 4;
                cells[(size_t) i]->setBounds (grid.getX() + col * (cw + gap), startY + row * (ch + gap), cw, ch);
            }

            // Sağ: scope, metre, log
            right.removeFromTop (34);
            scopeRect = right.removeFromTop (230);
            right.removeFromTop (14);
            meterRect = right.removeFromTop (64);
            right.removeFromTop (14);
            logRect = right.removeFromTop (juce::jmin (150, right.getHeight()));
            tempoRect = juce::Rectangle<int> (area.getX(), getHeight() - 30, 260, 22);

            int x = tempoRect.getRight() + 14;
            acidAuto.setBounds (x, getHeight() - 32, 210, 24);
            x += 220;
            rootLabel.setBounds (x, getHeight() - 32, 40, 24);
            acidRoot.setBounds (x + 42, getHeight() - 32, 110, 24);
            x += 168;
            masterLabel.setBounds (x, getHeight() - 32, 60, 24);
            master.setBounds (x + 62, getHeight() - 32, 200, 24);
        }

        void paint (juce::Graphics& g) override
        {
            g.setFont (mono (12.0f, true));
            g.setColour (C::dim);
            g.drawText ("HYBRID MACRO DECK  //  8-POINT SUMMATION", 24, 12, 600, 22, juce::Justification::left);
            g.drawText ("VECTOR GLITCH SCOPE", scopeRect.getX(), 12, 260, 22, juce::Justification::left);

            // Scope
            g.setColour (juce::Colour (0xff0b0d11));
            g.fillRoundedRectangle (scopeRect.toFloat(), 6.0f);
            g.setColour (C::border);
            g.drawRoundedRectangle (scopeRect.toFloat().reduced (0.5f), 6.0f, 1.0f);

            auto sr = scopeRect.toFloat().reduced (6.0f);
            g.setColour (juce::Colour (0xff1a1e25));
            for (int i = 1; i < 8; ++i)
                g.drawVerticalLine ((int) (sr.getX() + sr.getWidth() * (float) i / 8.0f), sr.getY(), sr.getBottom());
            g.drawHorizontalLine ((int) sr.getCentreY(), sr.getX(), sr.getRight());

            juce::Path path;
            const int w = proc.scopeWritePos.load();
            for (int i = 0; i < AnomalProcessor::scopeSize; ++i)
            {
                const float v = juce::jlimit (-1.0f, 1.0f,
                                              proc.scope[(size_t) ((w + i) % AnomalProcessor::scopeSize)].load());
                const float x = sr.getX() + sr.getWidth() * (float) i / (float) (AnomalProcessor::scopeSize - 1);
                const float y = sr.getCentreY() - v * sr.getHeight() * 0.48f;
                if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
            }
            g.setColour (C::green);
            g.strokePath (path, juce::PathStrokeType (1.4f));

            g.setColour (C::green.withAlpha (0.8f));
            g.setFont (mono (10.0f));
            g.drawText ("CH: SUMMED_STEREO", scopeRect.reduced (10, 6), juce::Justification::bottomRight);

            // Metre
            g.setColour (C::panel);
            g.fillRoundedRectangle (meterRect.toFloat(), 6.0f);
            g.setColour (C::border);
            g.drawRoundedRectangle (meterRect.toFloat().reduced (0.5f), 6.0f, 1.0f);

            const float db = juce::Decibels::gainToDecibels (proc.peakLevel.load(), -60.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            auto mr = meterRect.reduced (14, 0);
            g.setColour (C::dim);
            g.setFont (mono (11.0f));
            g.drawText ("OUTPUT PEAK", mr.removeFromTop (meterRect.getHeight() / 2 - 2).withTrimmedTop (10), juce::Justification::left);
            g.setColour (C::text);
            g.drawText (db <= -59.9f ? "-inf dB" : juce::String (db, 1) + " dB",
                        meterRect.reduced (14, 0).withTrimmedTop (10).removeFromTop (meterRect.getHeight() / 2 - 2),
                        juce::Justification::right);

            auto bar = juce::Rectangle<float> ((float) meterRect.getX() + 14.0f, (float) meterRect.getBottom() - 22.0f,
                                               (float) meterRect.getWidth() - 28.0f, 8.0f);
            g.setColour (juce::Colour (0xff1d2128));
            g.fillRoundedRectangle (bar, 4.0f);
            g.setColour (frac < 0.85f ? C::green : (frac < 0.97f ? C::orange : C::red));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * frac), 4.0f);

            // Log
            g.setColour (C::panel);
            g.fillRoundedRectangle (logRect.toFloat(), 6.0f);
            g.setColour (C::border);
            g.drawRoundedRectangle (logRect.toFloat().reduced (0.5f), 6.0f, 1.0f);

            g.setFont (mono (11.0f));
            int y = logRect.getY() + 10;
            auto line = [&] (const juce::String& s, juce::Colour c)
            {
                g.setColour (c);
                g.drawText ("> " + s, logRect.getX() + 12, y, logRect.getWidth() - 20, 16, juce::Justification::left);
                y += 17;
            };

            line ("ANOMAL-01 INITIALIZED.", C::dim);
            for (int s = 0; s < NumSlots; ++s)
            {
                juce::String name (AnomalProcessor::slotName (s));
                if (proc.getSlot (s) != nullptr)         line (name + ": LOADED", C::green);
                else if (proc.isSlotLoading (s))         line (name + ": LOADING...", C::orange);
                else                                     line (name + ": NOT LOADED", C::red);
            }
            if (proc.isScanning())
                line ("SCANNING VST3...", C::orange);

            // Alt bilgi
            const double bpm = proc.currentBpm.load();
            g.setColour (C::dim);
            g.setFont (mono (11.0f));
            g.drawText ("TEMPO: " + (bpm > 0.0 ? juce::String (bpm, 1) + " BPM" : juce::String ("--")),
                        tempoRect, juce::Justification::left);
        }

    private:
        AnomalProcessor& proc;
        DeckLnF lnf;
        std::array<std::unique_ptr<MacroCell>, kNumMacros> cells;
        juce::Rectangle<int> scopeRect, meterRect, logRect, tempoRect;

        juce::ToggleButton acidAuto;
        juce::Label rootLabel, masterLabel;
        juce::Slider acidRoot, master;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> acidAutoAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> acidRootAtt, masterAtt;
    };
}

//==============================================================================
AnomalEditor::AnomalEditor (AnomalProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p)
{
    const char* names[NumSlots + 1] = { "DECK", "DRUMAZON", "ACID V", "CYBERDRIVE", "I HEART NY" };

    for (size_t i = 0; i < tabButtons.size(); ++i)
    {
        auto& b = tabButtons[i];
        b.setButtonText (names[i]);
        b.setClickingTogglesState (true);
        b.setRadioGroupId (1001);
        b.setColour (juce::TextButton::buttonColourId, C::panel);
        b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff1f3a16));
        b.setColour (juce::TextButton::textColourOffId, C::text);
        b.setColour (juce::TextButton::textColourOnId, C::green);
        const int tab = (int) i;
        b.onClick = [this, tab] { if (tabButtons[(size_t) tab].getToggleState()) showTab (tab); };
        addAndMakeVisible (b);
    }
    tabButtons[0].setToggleState (true, juce::dontSendNotification);

    for (auto* b : { &scanButton, &paramsButton })
    {
        b->setColour (juce::TextButton::buttonColourId, C::panel);
        b->setColour (juce::TextButton::textColourOffId, C::orange);
        addAndMakeVisible (*b);
    }
    scanButton.onClick = [this] { proc.startScan(); };
    paramsButton.onClick = [this] { proc.dumpParameters().revealToUser(); };

    routeBox.addItemList ({ "MIDI: Split (ch10 = Drums)", "MIDI: Both", "MIDI: Drums only", "MIDI: Acid only" }, 1);
    routeBox.setColour (juce::ComboBox::backgroundColourId, C::panel);
    routeBox.setColour (juce::ComboBox::textColourId, C::text);
    routeBox.setColour (juce::ComboBox::outlineColourId, C::border);
    addAndMakeVisible (routeBox);
    routeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "midiRoute", routeBox);

    deck = std::make_unique<DeckView> (proc);
    addAndMakeVisible (*deck);

    proc.addChangeListener (this);
    proc.slotListeners.add (this);

    setSize (deckW, deckH);
    showTab (0);
}

AnomalEditor::~AnomalEditor()
{
    proc.slotListeners.remove (this);
    proc.removeChangeListener (this);

    for (auto& e : childEditors)
        if (e != nullptr)
        {
            e->removeComponentListener (this);
            e.reset();
        }
}

juce::Rectangle<int> AnomalEditor::contentArea() const
{
    return getLocalBounds().withTrimmedTop (headerH).withTrimmedBottom (footerH);
}

void AnomalEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);

    // Başlık şeridi
    g.setColour (C::panel);
    g.fillRect (0, 0, getWidth(), headerH);
    g.setColour (C::border);
    g.drawHorizontalLine (headerH - 1, 0.0f, (float) getWidth());

    int loaded = 0, loading = 0;
    for (int s = 0; s < NumSlots; ++s)
    {
        if (proc.getSlot (s) != nullptr) ++loaded;
        else if (proc.isSlotLoading (s)) ++loading;
    }
    g.setColour (loaded == NumSlots ? C::green : (loading > 0 || proc.isScanning() ? C::orange : C::red));
    g.fillEllipse (18.0f, 14.0f, 12.0f, 12.0f);

    g.setColour (C::text);
    g.setFont (mono (17.0f, true));
    g.drawText ("ANOMAL-01", 40, 8, 140, 24, juce::Justification::left);
    g.setColour (C::green);
    g.drawText ("//", 150, 8, 24, 24, juce::Justification::left);
    g.setColour (C::text);
    g.drawText ("DECONSTRUCT CORE", 176, 8, 260, 24, juce::Justification::left);

    g.setColour (C::dim);
    g.setFont (mono (10.5f));
    g.drawText ("DRUMAZON 2 + ACID V > CYBERDRIVE > I HEART NY  |  " + juce::String (loaded) + "/4 LOADED",
                40, 32, 560, 16, juce::Justification::left);

    // Footer
    g.setColour (C::border);
    g.drawHorizontalLine (getHeight() - footerH, 0.0f, (float) getWidth());

    // Yüklenmemiş plugin sekmesi mesajı
    if (currentTab > 0 && childEditors[(size_t) (currentTab - 1)] == nullptr)
    {
        g.setColour (C::dim);
        g.setFont (mono (13.0f));
        g.drawFittedText (juce::String (AnomalProcessor::slotName (currentTab - 1))
                              + " yuklu degil.\nSCAN'e bas veya plugin'in kurulu oldugunu (VST3, arm64) kontrol et.",
                          contentArea(), juce::Justification::centred, 4);
    }
}

void AnomalEditor::resized()
{
    // Sekmeler başlığın alt satırında
    int x = 620;
    for (size_t i = 0; i < tabButtons.size(); ++i)
    {
        const int w = i == 0 ? 70 : (i == 3 ? 100 : 92);
        tabButtons[i].setBounds (x, 34, w, 26);
        x += w + 6;
    }

    // Sağ üst: kontrol butonları
    paramsButton.setBounds (getWidth() - 90, 6, 80, 24);
    scanButton.setBounds (getWidth() - 176, 6, 80, 24);
    routeBox.setBounds (getWidth() - 440, 6, 252, 24);

    const auto content = contentArea();
    deck->setBounds (content);

    for (auto& e : childEditors)
        if (e != nullptr)
            e->setTopLeftPosition (content.getX() + (content.getWidth() - e->getWidth()) / 2,
                                   content.getY() + juce::jmax (0, (content.getHeight() - e->getHeight()) / 2));
}

void AnomalEditor::updateSizeForTab()
{
    int w = deckW, h = deckH;

    if (currentTab > 0)
        if (auto* e = childEditors[(size_t) (currentTab - 1)].get())
        {
            w = juce::jmax (900, e->getWidth());
            h = headerH + e->getHeight() + footerH;
        }

    if (getWidth() != w || getHeight() != h)
        setSize (w, h);
    else
        resized();
}

void AnomalEditor::showTab (int tab)
{
    currentTab = tab;
    deck->setVisible (tab == 0);

    for (size_t i = 0; i < childEditors.size(); ++i)
        if (childEditors[i] != nullptr)
            childEditors[i]->setVisible ((int) i == tab - 1);

    if (tab > 0)
    {
        const size_t s = (size_t) (tab - 1);
        if (childEditors[s] == nullptr)
            if (auto* inst = proc.getSlot ((int) s))
                if (inst->hasEditor())
                    if (auto* ed = inst->createEditorIfNeeded())
                    {
                        childEditors[s].reset (ed);
                        addAndMakeVisible (*ed);
                        ed->addComponentListener (this);
                    }

        if (childEditors[s] != nullptr)
            childEditors[s]->setVisible (true);
    }

    updateSizeForTab();
    repaint();
}

void AnomalEditor::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    if (! wasResized || currentTab <= 0)
        return;

    if (&c == childEditors[(size_t) (currentTab - 1)].get())
        updateSizeForTab();
}

void AnomalEditor::slotWillChange (int slot)
{
    if (auto& e = childEditors[(size_t) slot])
    {
        e->removeComponentListener (this);
        e.reset();
    }
}

void AnomalEditor::slotChanged (int slot)
{
    if (currentTab == slot + 1)
        showTab (currentTab);
    repaint();
}

void AnomalEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    repaint();
}
