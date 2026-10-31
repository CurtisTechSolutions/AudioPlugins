#include "ap_ui/EditorBase.h"

namespace ap::ui
{

namespace
{
    constexpr const char* kScaleProperty = "uiScale";
    constexpr float kMinScale = 0.7f, kMaxScale = 2.0f;
}

/** The unscaled drawing surface: background, header and every content child. */
class EditorBase::Canvas : public juce::Component
{
public:
    explicit Canvas (EditorBase& e) : owner (e) { setOpaque (true); }

    void paint (juce::Graphics& g) override
    {
        const auto& th = owner.lookAndFeel.theme();
        auto bounds = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (th.background.brighter (0.03f), 0.0f, 0.0f,
                                                 th.background.darker (0.15f), 0.0f, bounds.getBottom(), false));
        g.fillAll();
        // A faint wash of the accent from the top-left corner gives each plugin its own tint.
        g.setGradientFill (juce::ColourGradient (th.accent.withAlpha (0.07f), 0.0f, 0.0f,
                                                 th.accent.withAlpha (0.0f), bounds.getWidth() * 0.55f, bounds.getHeight() * 0.6f, true));
        g.fillAll();

        // Header.
        auto header = bounds.removeFromTop ((float) headerHeight);
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.025f), 0.0f, 0.0f,
                                                 juce::Colours::transparentWhite, 0.0f, header.getBottom(), false));
        g.fillRect (header);
        g.setColour (th.panelEdge);
        g.fillRect (header.getX(), header.getBottom() - 1.0f, header.getWidth(), 1.0f);
        g.setGradientFill (juce::ColourGradient (th.accent, 0.0f, 0.0f, th.accent.withAlpha (0.0f), header.getWidth() * 0.6f, 0.0f, false));
        g.fillRect (header.getX(), header.getBottom() - 1.0f, header.getWidth(), 1.0f);

        auto left = header.reduced ((float) margin, 0.0f);
        const auto badge = left.removeFromLeft (36.0f).withSizeKeepingCentre (36.0f, 36.0f);
        owner.paintLogo (g, badge);
        left.removeFromLeft (12.0f);

        auto text = left.withHeight (40.0f).withCentre ({ left.getCentreX(), header.getCentreY() });
        g.setColour (th.text);
        g.setFont (Type::title());
        g.drawText (owner.titleText, text.removeFromTop (23.0f), juce::Justification::bottomLeft);
        g.setColour (th.textDim);
        g.setFont (Type::label());
        g.drawText (owner.taglineText, text.withTrimmedTop (1.0f), juce::Justification::topLeft);
    }

private:
    EditorBase& owner;
};

//==============================================================================
EditorBase::EditorBase (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& apvts,
                        const juce::String& title, const juce::String& tagline, juce::Colour accent,
                        int baseWidth, int baseHeight)
    : juce::AudioProcessorEditor (p),
      state (apvts),
      lookAndFeel (Theme::withAccent (accent)),
      titleText (title),
      taglineText (tagline),
      baseW (baseWidth),
      baseH (baseHeight)
{
    setLookAndFeel (&lookAndFeel);
    canvas = std::make_unique<Canvas> (*this);
    addAndMakeVisible (*canvas);
    canvas->setBounds (0, 0, baseW, baseH);
    setSize (baseW, baseH);
}

EditorBase::~EditorBase()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void EditorBase::finishSetup()
{
    ready = true;
    setResizable (true, true);
    setResizeLimits (juce::roundToInt ((float) baseW * kMinScale), juce::roundToInt ((float) baseH * kMinScale),
                     juce::roundToInt ((float) baseW * kMaxScale), juce::roundToInt ((float) baseH * kMaxScale));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) baseW / (double) baseH);

    const float saved = (float) state.state.getProperty (kScaleProperty, 1.0);
    const float scale = juce::jlimit (kMinScale, kMaxScale, saved);
    setSize (juce::roundToInt ((float) baseW * scale), juce::roundToInt ((float) baseH * scale));
    resized();
    tick();
    startTimerHz (30);
}

void EditorBase::paint (juce::Graphics& g)
{
    g.fillAll (lookAndFeel.theme().background);
}

void EditorBase::resized()
{
    if (! ready)   // the subclass is not constructed yet; finishSetup() lays out
        return;

    const float scale = juce::jlimit (0.1f, 4.0f, (float) getWidth() / (float) baseW);
    canvas->setTransform (juce::AffineTransform::scale (scale));
    canvas->setBounds (0, 0, baseW, baseH);

    // Header components, right to left.
    int x = baseW - margin;
    for (auto& h : headerItems)
    {
        x -= h.w;
        h.c->setBounds (x, (headerHeight - h.h) / 2, h.w, h.h);
        x -= gap;
    }

    layoutContent ({ margin, headerHeight + margin, baseW - 2 * margin, baseH - headerHeight - 2 * margin });

    if (std::abs (scale - (float) state.state.getProperty (kScaleProperty, 1.0)) > 0.01f)
        state.state.setProperty (kScaleProperty, scale, nullptr);
}

void EditorBase::tick()
{
    for (auto* a : animated)
        a->tick();
}

void EditorBase::addContent (juce::Component& c)
{
    canvas->addAndMakeVisible (c);
    if (auto* a = dynamic_cast<Animated*> (&c))
        addAnimated (*a);
}

ParamSection& EditorBase::addSection (const juce::String& title, const juce::StringArray& paramIds)
{
    sections.push_back (std::make_unique<ParamSection> (state, title, paramIds));
    addContent (*sections.back());
    return *sections.back();
}

void EditorBase::addAnimated (Animated& a)
{
    if (std::find (animated.begin(), animated.end(), &a) != animated.end())
        return;
    a.driveExternally();
    animated.push_back (&a);
}

void EditorBase::addHeaderComponent (juce::Component& c, int width, int height)
{
    headerItems.push_back ({ &c, width, height });
    addContent (c);
}

void EditorBase::paintLogo (juce::Graphics& g, juce::Rectangle<float> badge)
{
    const auto& th = lookAndFeel.theme();
    juce::Path shape;
    shape.addRoundedRectangle (badge, 9.0f);
    juce::DropShadow (th.accent.withAlpha (0.35f), 10, { 0, 2 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (th.accent.brighter (0.25f), badge.getX(), badge.getY(),
                                             th.accent.darker (0.35f), badge.getRight(), badge.getBottom(), false));
    g.fillPath (shape);
    g.setColour (juce::Colours::white.withAlpha (0.25f));
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    g.setColour (th.background.withAlpha (0.9f));
    g.setFont (Type::big (20.0f));
    g.drawText (titleText.substring (0, 1), badge, juce::Justification::centred);
}

} // namespace ap::ui
