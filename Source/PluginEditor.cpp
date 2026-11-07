#include "PluginEditor.h"
#include "BinaryData.h"

using P = PhocusAudioProcessor;
using L = PhocusLookAndFeel;

//==============================================================================
// Look
PhocusLookAndFeel::PhocusLookAndFeel()
{
    semiBoldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchSemiBold_ttf, (size_t) BinaryData::ChakraPetchSemiBold_ttfSize);
    boldFace = juce::Typeface::createSystemTypefaceFor (BinaryData::ChakraPetchBold_ttf, (size_t) BinaryData::ChakraPetchBold_ttfSize);

    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff13292e));
    setColour (juce::PopupMenu::textColourId, text());
    setColour (juce::PopupMenu::highlightedBackgroundColourId, orange());
}

juce::Font PhocusLookAndFeel::font (float height, bool bold, float tracking) const
{
    const juce::Font base (juce::FontOptions (bold ? boldFace : semiBoldFace).withPointHeight (height));
    return tracking > 0.0f ? base.withExtraKerningFactor (tracking) : base;
}

juce::Colour PhocusLookAndFeel::bandColour (int band)
{
    switch (band)
    {
        case 0:  return orange();
        case 1:  return copper();
        case 2:  return mauve();
        case 3:  return teal();
        default: return juce::Colour (0xffb9cccf);
    }
}

juce::Path PhocusLookAndFeel::chamfered (juce::Rectangle<float> r, float cut)
{
    cut = juce::jmin (cut, r.getWidth() * 0.5f, r.getHeight() * 0.5f);
    juce::Path p;
    p.startNewSubPath (r.getX() + cut, r.getY());
    p.lineTo (r.getRight() - cut, r.getY());
    p.lineTo (r.getRight(), r.getY() + cut);
    p.lineTo (r.getRight(), r.getBottom() - cut);
    p.lineTo (r.getRight() - cut, r.getBottom());
    p.lineTo (r.getX() + cut, r.getBottom());
    p.lineTo (r.getX(), r.getBottom() - cut);
    p.lineTo (r.getX(), r.getY() + cut);
    p.closeSubPath();
    return p;
}

void PhocusLookAndFeel::drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, const juce::Font& f)
{
    const auto shape = chamfered (r, 8.0f);
    juce::DropShadow (juce::Colours::black.withAlpha (0.45f), 10, { 0, 3 }).drawForPath (g, shape);
    g.setGradientFill (juce::ColourGradient (panelTop(), 0.0f, r.getY(), panelBottom(), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    g.setColour (edge());
    g.strokePath (shape, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 8.0f, r.getRight() - 8.0f);

    if (title.isNotEmpty())
    {
        g.setFont (f);
        g.setColour (copper());
        g.drawText (title, r.withHeight (22.0f).translated (12.0f, 4.0f).withWidth (r.getWidth() - 24.0f), juce::Justification::centredLeft, false);
    }
}

void PhocusLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float start, float end, juce::Slider& s)
{
    const bool big = (bool) s.getProperties().getWithDefault ("big", false);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);
    const auto colour = s.getProperties().contains ("colour") ? juce::Colour ((juce::uint32) (juce::int64) s.getProperties()["colour"]) : orange();
    const bool enabled = s.isEnabled();

    auto area = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);

    // Nom en haut, valeur en bas
    const float nameH = big ? 18.0f : 13.0f, valueH = big ? 20.0f : 14.0f;
    auto nameArea = area.removeFromTop (nameH);
    auto valueArea = area.removeFromBottom (valueH);

    g.setColour (enabled ? dim() : dim().withAlpha (0.4f));
    g.setFont (font (big ? 12.0f : 9.5f, true, 0.18f));
    g.drawText (s.getName(), nameArea, juce::Justification::centred, false);

    g.setColour (enabled ? text() : text().withAlpha (0.35f));
    g.setFont (font (big ? 15.0f : 10.5f, true, 0.04f));
    g.drawText (s.getTextFromValue (s.getValue()), valueArea, juce::Justification::centred, false);

    const float radius = juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - (big ? 6.0f : 3.0f);
    const auto c = area.getCentre();
    const float arcR = radius, bodyR = radius - (big ? 10.0f : 5.5f);
    const float thick = big ? 6.0f : 3.5f;

    // Piste
    juce::Path track;
    track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xff0a1a1d));
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Valeur (dégradé orange -> mauve ; depuis le centre pour les réglages bipolaires)
    const float angle = start + pos * (end - start);
    const float from = bipolar ? start + 0.5f * (end - start) : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, juce::jmin (from, angle), juce::jmax (from, angle), true);
        juce::ColourGradient grad (colour, c.x - arcR, c.y + arcR, mauve(), c.x + arcR, c.y - arcR, false);
        if (colour != orange())
            grad = juce::ColourGradient (colour, c.x - arcR, c.y, colour.brighter (0.25f), c.x + arcR, c.y, false);
        g.setGradientFill (grad);
        g.setOpacity (enabled ? 1.0f : 0.35f);
        g.strokePath (value, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setOpacity (1.0f);
    }

    // Corps : anneau cuivre, centre turquoise sombre
    juce::DropShadow (juce::Colours::black.withAlpha (0.6f), big ? 12 : 6, { 0, big ? 4 : 2 })
        .drawForPath (g, [&] { juce::Path p; p.addEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c)); return p; }());
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a5961), c.x, c.y - bodyR, juce::Colour (0xff12292e), c.x, c.y + bodyR, false));
    g.fillEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c));
    g.setColour (copper().withAlpha (enabled ? 0.85f : 0.35f));
    g.drawEllipse (juce::Rectangle<float> (bodyR * 2.0f, bodyR * 2.0f).withCentre (c), big ? 1.6f : 1.0f);

    // Index
    const auto tip = c.getPointOnCircumference (bodyR - (big ? 6.0f : 3.0f), angle);
    const auto base = c.getPointOnCircumference (bodyR * (big ? 0.35f : 0.25f), angle);
    g.setColour (enabled ? text() : text().withAlpha (0.35f));
    g.drawLine ({ base, tip }, big ? 3.0f : 1.8f);
}

void PhocusLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto area = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto shape = chamfered (area, juce::jmin (5.0f, area.getHeight() * 0.25f));
    const bool on = b.getToggleState();
    const auto accent = b.getProperties().contains ("colour") ? juce::Colour ((juce::uint32) (juce::int64) b.getProperties()["colour"]) : orange();

    if (on)
    {
        juce::ColourGradient grad (accent, area.getX(), area.getY(), accent == orange() ? mauve() : accent.darker (0.3f), area.getRight(), area.getBottom(), false);
        g.setGradientFill (grad);
        g.fillPath (shape);
        g.setColour (juce::Colours::white.withAlpha (over ? 0.25f : 0.12f));
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }
    else
    {
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff163338), 0.0f, area.getY(), juce::Colour (0xff0f262b), 0.0f, area.getBottom(), false));
        g.fillPath (shape);
        g.setColour (over ? accent.withAlpha (0.8f) : edge());
        g.strokePath (shape, juce::PathStrokeType (1.0f));
    }

    if (down)
    {
        g.setColour (juce::Colours::black.withAlpha (0.2f));
        g.fillPath (shape);
    }
}

void PhocusLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    auto colour = on ? juce::Colour (0xff10191b) : text();
    if (! b.isEnabled())
        colour = colour.withAlpha (0.3f);
    g.setColour (colour);
    g.setFont (font (juce::jlimit (8.5f, 11.0f, (float) b.getHeight() * 0.42f), true, 0.14f));
    g.drawText (b.getButtonText(), b.getLocalBounds().reduced (2, 0), juce::Justification::centred, true);
}

void PhocusLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (juce::Colour (0xff13292e));
    g.setColour (edge());
    g.drawRect (0, 0, w, h, 1);
}

void PhocusLookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                           bool isHighlighted, bool isTicked, bool, const juce::String& itemText,
                                           const juce::String&, const juce::Drawable*, const juce::Colour*)
{
    if (isSeparator)
    {
        g.setColour (edge());
        g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
        return;
    }

    auto r = area.reduced (3, 1);
    if (isHighlighted && isActive)
    {
        g.setGradientFill (juce::ColourGradient (orange(), (float) r.getX(), 0.0f, mauve(), (float) r.getRight(), 0.0f, false));
        g.fillRect (r);
    }
    if (isTicked)
    {
        g.setColour (isHighlighted ? juce::Colours::white : orange());
        g.fillRect (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ (float) r.getX() + 12.0f, (float) r.getCentreY() }));
    }
    g.setColour (! isActive ? dim().withAlpha (0.6f) : isHighlighted ? juce::Colours::white : text());
    g.setFont (getPopupMenuFont());
    g.drawText (itemText, r.withTrimmedLeft (24).withTrimmedRight (10), juce::Justification::centredLeft, true);
}

void PhocusLookAndFeel::getIdealPopupMenuItemSize (const juce::String& itemText, bool isSeparator, int, int& w, int& h)
{
    if (isSeparator) { w = 60; h = 9; return; }
    h = 24;
    w = juce::jmax (150, (int) std::ceil (juce::GlyphArrangement::getStringWidth (getPopupMenuFont(), itemText)) + 44);
}

//==============================================================================
// Écran
namespace
{
    constexpr float sensRangeDb = 24.0f;
    constexpr float logSpan = 6.907755f;   // ln (20000 / 20)

    juce::String hzText (float hz)
    {
        if (hz >= 1000.0f)
        {
            const int hundreds = juce::roundToInt (hz / 100.0f);
            const int rest = hundreds % 10;
            return juce::String (hundreds / 10) + "k" + (rest != 0 ? juce::String (rest) : juce::String());
        }
        return juce::String (juce::roundToInt (hz));
    }

    const char* bandNames[] = { "BAND 1", "BAND 2", "BAND 3", "BAND 4", "LOW CUT", "HIGH CUT" };
}

GraphView::GraphView (PhocusAudioProcessor& p, PhocusLookAndFeel& l) : processor (p), lf (l)
{
    for (auto& s : spectrum) s = -120.0f;
    setComponentID ("graph");
    startTimerHz (30);
}

GraphView::~GraphView()
{
    stopTimer();
    processor.setBandListen (-1);
}

float GraphView::xForFreq (float hz) const
{
    const auto r = plotArea();
    return r.getX() + r.getWidth() * std::log (juce::jlimit (20.0f, 20000.0f, hz) / 20.0f) / logSpan;
}

float GraphView::freqForX (float x) const
{
    const auto r = plotArea();
    return 20.0f * std::exp (logSpan * juce::jlimit (0.0f, 1.0f, (x - r.getX()) / r.getWidth()));
}

float GraphView::yForSens (float db) const
{
    const auto r = plotArea();
    return r.getCentreY() - (db / sensRangeDb) * r.getHeight() * 0.36f;
}

float GraphView::sensForY (float y) const
{
    const auto r = plotArea();
    return (r.getCentreY() - y) / (r.getHeight() * 0.36f) * sensRangeDb;
}

juce::RangedAudioParameter* GraphView::param (int band, const char* what) const
{
    juce::String id;
    if (band < P::numBands)
        id = P::bandId (band, what);
    else
    {
        const bool low = band == P::numBands;
        const juce::String w (what);
        if (w == "Freq")      id = low ? P::lowCutFreqId : P::highCutFreqId;
        else if (w == "On")   id = low ? P::lowCutId : P::highCutId;
        else if (w == "Type") id = low ? P::lowCutSlopeId : P::highCutSlopeId;
        else return nullptr;
    }
    return processor.apvts.getParameter (id);
}

juce::Point<float> GraphView::nodePosition (int band) const
{
    const auto curve = processor.readCurve();
    if (band >= P::numBands)
    {
        const auto& cut = band == P::numBands ? curve.lowCut : curve.highCut;
        return { xForFreq (cut.freq), yForSens (0.0f) };
    }
    const auto& b = curve.bands[band];
    const float sensDb = b.type == phocus::reject ? 0.0f : b.sens;
    return { xForFreq (b.freq), yForSens (sensDb) };
}

int GraphView::hitNode (juce::Point<float> pt) const
{
    int best = -1;
    float bestDist = 12.0f;
    for (int b = 0; b < P::numBands + 2; ++b)
    {
        const float d = nodePosition (b).getDistanceFrom (pt);
        if (d < bestDist)
        {
            bestDist = d;
            best = b;
        }
    }
    return best;
}

void GraphView::timerCallback()
{
    const auto& engine = processor.getEngine();
    for (int i = 0; i < phocus::ResonanceEngine::displayPoints; ++i)
    {
        reduction[i] = engine.getDisplayReduction (i) * juce::jlimit (0.0f, 1.0f, processor.apvts.getRawParameterValue (P::mixId)->load() * 0.01f);
        const float lev = engine.getDisplayLevel (i);
        spectrum[i] = lev > spectrum[i] ? lev : spectrum[i] + 0.25f * (lev - spectrum[i]);
    }
    repaint();
}

void GraphView::paint (juce::Graphics& g)
{
    const auto r = plotArea();
    const auto shape = L::chamfered (r, 8.0f);

    // Fond de l'écran
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b1d21), 0.0f, r.getY(), L::screen(), 0.0f, r.getBottom(), false));
    g.fillPath (shape);
    g.saveState();
    g.reduceClipRegion (shape);

    // Grille
    g.setFont (lf.font (9.0f, true, 0.1f));
    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = xForFreq (hz);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
        g.setColour (L::dim().withAlpha (0.55f));
        g.drawText (hzText (hz), juce::Rectangle<float> (x + 3.0f, r.getBottom() - 16.0f, 40.0f, 12.0f), juce::Justification::centredLeft, false);
    }
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawHorizontalLine ((int) yForSens (0.0f), r.getX(), r.getRight());

    // Spectre d'entrée (en bas, léger)
    {
        juce::Path spec;
        spec.startNewSubPath (r.getX(), r.getBottom());
        for (int i = 0; i < phocus::ResonanceEngine::displayPoints; ++i)
        {
            const float x = xForFreq (phocus::ResonanceEngine::displayFrequency (i));
            const float t = juce::jlimit (0.0f, 1.0f, (spectrum[i] + 96.0f) / 96.0f);
            spec.lineTo (x, r.getBottom() - t * r.getHeight() * 0.62f);
        }
        spec.lineTo (r.getRight(), r.getBottom());
        spec.closeSubPath();
        g.setGradientFill (juce::ColourGradient (L::teal().withAlpha (0.16f), 0.0f, r.getY() + r.getHeight() * 0.4f,
                                                 L::teal().withAlpha (0.02f), 0.0f, r.getBottom(), false));
        g.fillPath (spec);
    }

    // Réduction (descend du haut)
    {
        juce::Path red;
        red.startNewSubPath (r.getX(), r.getY());
        for (int i = 0; i < phocus::ResonanceEngine::displayPoints; ++i)
        {
            const float x = xForFreq (phocus::ResonanceEngine::displayFrequency (i));
            red.lineTo (x, r.getY() + juce::jlimit (0.0f, 1.0f, reduction[i] / rangeDb) * r.getHeight() * 0.92f);
        }
        red.lineTo (r.getRight(), r.getY());
        red.closeSubPath();
        g.setGradientFill (juce::ColourGradient (L::orange().withAlpha (0.85f), 0.0f, r.getY(),
                                                 L::mauve().withAlpha (0.75f), 0.0f, r.getY() + r.getHeight() * 0.6f, false));
        g.fillPath (red);
        g.setColour (L::orange());
        g.strokePath (red, juce::PathStrokeType (1.2f));
    }

    // Échelle de la réduction
    g.setFont (lf.font (9.0f, true, 0.1f));
    g.setColour (L::orange().withAlpha (0.7f));
    for (float db : { 0.25f, 0.5f, 0.75f })
    {
        const float y = r.getY() + db * r.getHeight() * 0.92f;
        g.drawText ("-" + juce::String (juce::roundToInt (db * rangeDb)), juce::Rectangle<float> (r.getRight() - 34.0f, y - 6.0f, 30.0f, 12.0f),
                    juce::Justification::centredRight, false);
    }

    // Courbe de sensibilité
    const auto curve = processor.readCurve();
    juce::Path line;
    for (int i = 0; i <= 400; ++i)
    {
        const float x = r.getX() + r.getWidth() * (float) i / 400.0f;
        const auto pnt = phocus::evaluateCurve (curve, freqForX (x));
        const float sensDb = 0.5f * (pnt.sensA + pnt.sensB);
        const float w = 0.5f * (pnt.weightA + pnt.weightB);
        const float y = juce::jmin (r.getBottom() - 2.0f, yForSens (sensDb) + (1.0f - w) * r.getHeight() * 0.45f);
        if (i == 0) line.startNewSubPath (x, y);
        else        line.lineTo (x, y);
    }
    g.setColour (L::text().withAlpha (0.18f));
    g.strokePath (line, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (L::text().withAlpha (0.92f));
    g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Nœuds
    for (int b = 0; b < P::numBands + 2; ++b)
    {
        const auto pos = nodePosition (b);
        const bool on = b < P::numBands ? curve.bands[b].on : (b == P::numBands ? curve.lowCut.on : curve.highCut.on);
        const auto colour = L::bandColour (b);
        const float rad = b == selected ? 8.0f : 6.5f;
        auto circle = juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (pos);

        if (b == selected)
        {
            g.setColour (colour.withAlpha (0.25f));
            g.fillEllipse (circle.expanded (6.0f));
        }
        if (on)
        {
            g.setColour (colour);
            g.fillEllipse (circle);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawEllipse (circle, 1.0f);
        }
        else
        {
            g.setColour (L::screen());
            g.fillEllipse (circle);
            g.setColour (colour);
            g.drawEllipse (circle.reduced (0.6f), 1.6f);
        }

        if (processor.getBandListen() == b)
        {
            g.setColour (colour);
            g.drawEllipse (circle.expanded (5.0f), 1.2f);
        }

        g.setColour (on ? juce::Colour (0xff10191b) : colour);
        g.setFont (lf.font (8.5f, true));
        const juce::String label = b < P::numBands ? juce::String (b + 1) : (b == P::numBands ? "L" : "H");
        g.drawText (label, circle, juce::Justification::centred, false);
    }

    g.restoreState();

    // Fréquence sous la souris
    if (hoverX >= 0.0f)
    {
        const float hz = dragging >= 0 ? (dragging < P::numBands ? curve.bands[dragging].freq
                                                                  : (dragging == P::numBands ? curve.lowCut.freq : curve.highCut.freq))
                                       : freqForX (hoverX);
        const float x = dragging >= 0 ? xForFreq (hz) : hoverX;
        g.setColour (L::text().withAlpha (0.25f));
        g.drawVerticalLine ((int) x, r.getY() + 18.0f, r.getBottom());
        g.setColour (L::text());
        g.setFont (lf.font (11.0f, true, 0.06f));
        g.drawText (hzText (hz) + " Hz", juce::Rectangle<float> (x - 40.0f, r.getY() + 4.0f, 80.0f, 14.0f), juce::Justification::centred, false);
    }

    g.setColour (L::edge());
    g.strokePath (shape, juce::PathStrokeType (1.2f));
}

void GraphView::mouseMove (const juce::MouseEvent& e)
{
    hoverX = e.position.x;
    hover = hitNode (e.position);
    setMouseCursor (hover >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
    repaint();
}

void GraphView::mouseExit (const juce::MouseEvent&)
{
    hoverX = -1.0f;
    hover = -1;
    repaint();
}

void GraphView::mouseDown (const juce::MouseEvent& e)
{
    const int band = hitNode (e.position);
    if (band < 0)
        return;

    selected = band;
    if (onBandSelected)
        onBandSelected (band);

    if (e.mods.isPopupMenu())
    {
        showBandMenu (band);
        return;
    }

    if (e.mods.isAltDown())
    {
        processor.setBandListen (band);
        listeningFromDrag = true;
    }

    dragging = band;
    dragStart.mouse = e.position;
    if (auto* f = param (band, "Freq")) dragStart.freq = f->convertFrom0to1 (f->getValue());
    if (auto* s = param (band, "Sens")) dragStart.sens = s->convertFrom0to1 (s->getValue());
    if (auto* q = param (band, "Q"))    dragStart.q = q->convertFrom0to1 (q->getValue());

    gestureParams.clear();
    for (const char* what : { "Freq", "Sens", "Q" })
        if (auto* prm = param (band, what))
        {
            prm->beginChangeGesture();
            gestureParams.push_back (prm);
        }
    repaint();
}

void GraphView::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0)
        return;

    hoverX = e.position.x;
    const auto r = plotArea();
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto delta = (e.position - dragStart.mouse) * fine;
    auto set = [] (juce::RangedAudioParameter* prm, float value) { if (prm != nullptr) prm->setValueNotifyingHost (prm->convertTo0to1 (value)); };

    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        set (param (dragging, "Q"), juce::jlimit (0.1f, 10.0f, dragStart.q * std::pow (2.0f, -delta.y / 60.0f)));
    }
    else
    {
        const float hz = dragStart.freq * std::exp (delta.x / r.getWidth() * logSpan);
        set (param (dragging, "Freq"), juce::jlimit (20.0f, 20000.0f, hz));

        if (dragging < P::numBands)
        {
            const auto curve = processor.readCurve();
            if (curve.bands[dragging].type != phocus::reject)
                set (param (dragging, "Sens"), juce::jlimit (-24.0f, 24.0f, dragStart.sens - delta.y / (r.getHeight() * 0.36f) * sensRangeDb));
        }
    }
    repaint();
}

void GraphView::mouseUp (const juce::MouseEvent&)
{
    for (auto* prm : gestureParams)
        prm->endChangeGesture();
    gestureParams.clear();
    dragging = -1;

    if (listeningFromDrag)
    {
        processor.setBandListen (-1);
        listeningFromDrag = false;
    }
    repaint();
}

void GraphView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int band = hitNode (e.position);
    if (band < 0)
        return;
    if (auto* on = param (band, "On"))
    {
        on->beginChangeGesture();
        on->setValueNotifyingHost (on->getValue() > 0.5f ? 0.0f : 1.0f);
        on->endChangeGesture();
    }
    repaint();
}

void GraphView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    int band = hitNode (e.position);
    if (band < 0)
        band = selected;
    if (band >= P::numBands)
        return;

    const float step = (std::abs (w.deltaY) > 0.0f ? w.deltaY : w.deltaX) * (e.mods.isShiftDown() ? 0.2f : 1.0f);
    if (e.mods.isCommandDown() || e.mods.isCtrlDown())
    {
        if (auto* b = param (band, "Balance"))
        {
            b->beginChangeGesture();
            b->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, b->getValue() + step * 0.1f));
            b->endChangeGesture();
        }
    }
    else if (auto* q = param (band, "Q"))
    {
        const float value = juce::jlimit (0.1f, 10.0f, q->convertFrom0to1 (q->getValue()) * std::pow (2.0f, step));
        q->beginChangeGesture();
        q->setValueNotifyingHost (q->convertTo0to1 (value));
        q->endChangeGesture();
    }
    repaint();
}

void GraphView::showBandMenu (int band)
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&lf);
    auto* on = param (band, "On");
    auto* type = param (band, "Type");

    menu.addSectionHeader (bandNames[band]);
    menu.addItem (1, "ENABLED", true, on != nullptr && on->getValue() > 0.5f);
    menu.addItem (2, "LISTEN", true, processor.getBandListen() == band);
    menu.addSeparator();

    const auto& names = band < P::numBands ? P::bandTypeNames() : P::slopeNames();
    const int current = type != nullptr ? juce::roundToInt (type->convertFrom0to1 (type->getValue())) : -1;
    for (int i = 0; i < names.size(); ++i)
        menu.addItem (10 + i, names[i], true, i == current);

    juce::Component::SafePointer<GraphView> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ juce::Desktop::getMousePosition().x, juce::Desktop::getMousePosition().y, 1, 1 }),
                        [safe, band, on, type] (int result)
                        {
                            if (safe == nullptr || result == 0)
                                return;
                            if (result == 1 && on != nullptr)
                            {
                                on->beginChangeGesture();
                                on->setValueNotifyingHost (on->getValue() > 0.5f ? 0.0f : 1.0f);
                                on->endChangeGesture();
                            }
                            else if (result == 2)
                            {
                                safe->processor.setBandListen (safe->processor.getBandListen() == band ? -1 : band);
                            }
                            else if (result >= 10 && type != nullptr)
                            {
                                type->beginChangeGesture();
                                type->setValueNotifyingHost (type->convertTo0to1 ((float) (result - 10)));
                                type->endChangeGesture();
                            }
                            safe->repaint();
                        });
}

//==============================================================================
// Façade
namespace
{
    // Zones (façade 1180 x 740)
    const juce::Rectangle<int> graphArea   { 24, 76, 808, 404 };
    const juce::Rectangle<int> bandArea    { 24, 492, 808, 116 };
    const juce::Rectangle<int> mainArea    { 848, 76, 308, 532 };
    const juce::Rectangle<int> speedArea   { 24, 620, 186, 104 };
    const juce::Rectangle<int> stereoArea  { 222, 620, 262, 104 };
    const juce::Rectangle<int> qualityArea { 496, 620, 268, 104 };
    const juce::Rectangle<int> sideArea    { 776, 620, 128, 104 };
    const juce::Rectangle<int> outputArea  { 916, 620, 240, 104 };
}

PhocusFacade::PhocusFacade (PhocusAudioProcessor& p)
    : audioProcessor (p),
      graph (p, lookAndFeel),
      presetBar (p.presets, [this] (float size, bool bold, float tracking) { return lookAndFeel.font (size, bold, tracking); })
{
    setLookAndFeel (&lookAndFeel);
    setSize (width, height);

    addAndMakeVisible (graph);
    graph.setBounds (graphArea);
    graph.onBandSelected = [this] (int band) { selectBand (band); };

    // Principal
    choiceSwitch (modeSwitch, P::modeId, { "SOFT", "HARD" }, { mainArea.getX() + 24, mainArea.getY() + 40, mainArea.getWidth() - 48, 30 }, false);
    knob (depthKnob, P::depthId, { mainArea.getX() + 54, mainArea.getY() + 86, 200, 210 }, false, true);
    knob (sharpnessKnob, P::sharpnessId, { mainArea.getX() + 30, mainArea.getY() + 312, 116, 120 });
    knob (selectivityKnob, P::selectivityId, { mainArea.getRight() - 146, mainArea.getY() + 312, 116, 120 });

    // Vitesse
    knob (attackKnob, P::attackId, { speedArea.getX() + 12, speedArea.getY() + 26, 76, 74 });
    knob (releaseKnob, P::releaseId, { speedArea.getX() + 98, speedArea.getY() + 26, 76, 74 });

    // Stéréo
    choiceSwitch (stereoSwitch, P::stereoModeId, { "L/R", "M/S" }, { stereoArea.getX() + 12, stereoArea.getY() + 34, 70, 60 }, true);
    knob (linkKnob, P::linkId, { stereoArea.getX() + 92, stereoArea.getY() + 26, 76, 74 });
    knob (balanceKnob, P::balanceId, { stereoArea.getX() + 174, stereoArea.getY() + 26, 80, 74 }, true);

    // Qualité
    choiceMenu (oversampleMenu, P::oversampleId,               { qualityArea.getX() + 92,  qualityArea.getY() + 34, 74, 26 });
    choiceMenu (resolutionMenu, P::resolutionId,               { qualityArea.getX() + 174, qualityArea.getY() + 34, 82, 26 });
    choiceMenu (offlineOversampleMenu, P::offlineOversampleId, { qualityArea.getX() + 92,  qualityArea.getY() + 68, 74, 26 });
    choiceMenu (offlineResolutionMenu, P::offlineResolutionId, { qualityArea.getX() + 174, qualityArea.getY() + 68, 82, 26 });

    // Sidechain
    toggle (sidechainButton, P::sidechainId, { sideArea.getX() + 12, sideArea.getY() + 34, sideArea.getWidth() - 24, 28 }, "sidechain");
    toggle (sidechainListenButton, P::sidechainListenId, { sideArea.getX() + 12, sideArea.getY() + 68, sideArea.getWidth() - 24, 28 }, "sidechainListen");
    sidechainListenButton.getProperties().set ("colour", (juce::int64) L::mauve().getARGB());

    // Sortie
    knob (mixKnob, P::mixId, { outputArea.getX() + 8, outputArea.getY() + 26, 74, 74 });
    knob (trimKnob, P::trimId, { outputArea.getX() + 82, outputArea.getY() + 26, 74, 74 });
    toggle (deltaButton, P::deltaId, { outputArea.getRight() - 78, outputArea.getY() + 34, 66, 28 }, "delta");
    toggle (bypassButton, P::bypassId, { outputArea.getRight() - 78, outputArea.getY() + 68, 66, 28 }, "bypass");
    deltaButton.getProperties().set ("colour", (juce::int64) L::mauve().getARGB());

    // Bande choisie
    for (int b = 0; b < 6; ++b)
    {
        auto& chip = bandChips[b];
        chip.setButtonText (b < 4 ? juce::String (b + 1) : (b == 4 ? "LC" : "HC"));
        chip.setComponentID ("chip" + juce::String (b));
        chip.getProperties().set ("colour", (juce::int64) L::bandColour (b).getARGB());
        chip.setClickingTogglesState (false);
        chip.onClick = [this, b] { selectBand (b); };
        chip.setBounds (bandArea.getX() + 14 + b * 38, bandArea.getY() + 34, 34, 28);
        addAndMakeVisible (chip);
    }

    bandOnButton.setComponentID ("bandOn");
    bandOnButton.setClickingTogglesState (true);
    bandOnButton.setBounds (bandArea.getX() + 14, bandArea.getY() + 72, 108, 30);
    addAndMakeVisible (bandOnButton);

    bandListenButton.setComponentID ("bandListen");
    bandListenButton.setClickingTogglesState (false);
    bandListenButton.getProperties().set ("colour", (juce::int64) L::mauve().getARGB());
    bandListenButton.setBounds (bandArea.getX() + 130, bandArea.getY() + 72, 106, 30);
    bandListenButton.onClick = [this]
    {
        const int band = graph.getSelectedBand();
        audioProcessor.setBandListen (audioProcessor.getBandListen() == band ? -1 : band);
    };
    addAndMakeVisible (bandListenButton);

    for (auto* k : { &bandFreqKnob, &bandSensKnob, &bandQKnob, &bandBalanceKnob })
    {
        k->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k->setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        k->setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        k->setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        addAndMakeVisible (*k);
    }
    bandFreqKnob.setComponentID ("bandFreq");
    bandFreqKnob.setName ("FREQ");
    bandSensKnob.setComponentID ("bandSens");
    bandSensKnob.setName ("SENS");
    bandQKnob.setComponentID ("bandQ");
    bandQKnob.setName ("Q");
    bandBalanceKnob.setComponentID ("bandBalance");
    bandBalanceKnob.setName ("BAL");
    bandSensKnob.getProperties().set ("bipolar", true);
    bandBalanceKnob.getProperties().set ("bipolar", true);
    const int kx = bandArea.getRight() - 4 * 80 - 10;
    bandFreqKnob.setBounds (kx, bandArea.getY() + 22, 80, 88);
    bandSensKnob.setBounds (kx + 80, bandArea.getY() + 22, 80, 88);
    bandQKnob.setBounds (kx + 160, bandArea.getY() + 22, 80, 88);
    bandBalanceKnob.setBounds (kx + 240, bandArea.getY() + 22, 80, 88);

    // En-tête : annuler, rétablir, A / B, copie, plage de l'écran
    int hx = 772;
    for (auto* b : { &undoButton, &redoButton, &abButton, &copyButton })
    {
        const int w = b == &abButton ? 40 : (b == &copyButton ? 58 : 56);
        b->setBounds (hx, 24, w, 28);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
        hx += w + 6;
    }
    undoButton.setComponentID ("undo");
    redoButton.setComponentID ("redo");
    abButton.setComponentID ("ab");
    copyButton.setComponentID ("abCopy");
    undoButton.onClick = [this] { audioProcessor.undo(); };
    redoButton.onClick = [this] { audioProcessor.redo(); };
    abButton.onClick = [this] { audioProcessor.switchAb(); abButton.setButtonText (audioProcessor.getAbSlot() == 0 ? "A" : "B");
                                copyButton.setButtonText (audioProcessor.getAbSlot() == 0 ? "A > B" : "B > A"); };
    copyButton.onClick = [this] { audioProcessor.copyAbToOther(); };
    abButton.setButtonText (audioProcessor.getAbSlot() == 0 ? "A" : "B");
    copyButton.setButtonText (audioProcessor.getAbSlot() == 0 ? "A > B" : "B > A");

    rangeButton.setComponentID ("range");
    rangeButton.setBounds (graphArea.getRight() - 66, graphArea.getY() + 8, 56, 22);
    rangeButton.onClick = [this]
    {
        const float next = graph.getRange() < 20.0f ? 30.0f : graph.getRange() < 40.0f ? 60.0f : 12.0f;
        graph.setRange (next);
        rangeButton.setButtonText (juce::String (juce::roundToInt (next)) + " dB");
        audioProcessor.apvts.state.setProperty ("displayRange", next, nullptr);
    };
    const float savedRange = (float) audioProcessor.apvts.state.getProperty ("displayRange", 30.0f);
    graph.setRange (savedRange);
    rangeButton.setButtonText (juce::String (juce::roundToInt (savedRange)) + " dB");
    addAndMakeVisible (rangeButton);

    // Presets
    addAndMakeVisible (presetBar);
    presetBar.setBounds (772 - 16 - phragma::PresetBar::idealWidth, 24, phragma::PresetBar::idealWidth, 28);
    presetBar.attachSavePanelTo (*this);

    selectBand ((int) audioProcessor.apvts.state.getProperty ("selectedBand", 2));
    startTimerHz (15);
}

PhocusFacade::~PhocusFacade()
{
    stopTimer();
    bandFreqAttachment.reset();
    bandSensAttachment.reset();
    bandQAttachment.reset();
    bandBalanceAttachment.reset();
    bandOnAttachment.reset();
    setLookAndFeel (nullptr);
}

juce::Component* PhocusFacade::findControl (const juce::String& componentId)
{
    for (auto* c : getChildren())
        if (c->getComponentID() == componentId)
            return c;
    return nullptr;
}

void PhocusFacade::knob (juce::Slider& s, const char* id, juce::Rectangle<int> area, bool bipolar, bool big)
{
    auto* prm = audioProcessor.apvts.getParameter (id);
    s.setName (prm->getName (20).toUpperCase());
    if (juce::String (id) == P::balanceId)  s.setName ("BALANCE");
    if (juce::String (id) == P::linkId)     s.setName ("LINK");
    s.setComponentID (id);
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    s.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    s.setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    s.getProperties().set ("big", big);
    s.getProperties().set ("bipolar", bipolar);
    s.setBounds (area);
    addAndMakeVisible (s);
    sliderAttachments.push_back (std::make_unique<APVTS::SliderAttachment> (audioProcessor.apvts, id, s));
}

void PhocusFacade::toggle (juce::TextButton& b, const char* id, juce::Rectangle<int> area, const juce::String& componentId)
{
    b.setComponentID (componentId);
    b.setClickingTogglesState (true);
    b.setBounds (area);
    b.setWantsKeyboardFocus (false);
    addAndMakeVisible (b);
    buttonAttachments.push_back (std::make_unique<APVTS::ButtonAttachment> (audioProcessor.apvts, id, b));
}

void PhocusFacade::choiceSwitch (ChoiceSwitch& sw, const juce::String& id, const juce::StringArray& texts, juce::Rectangle<int> area, bool vertical)
{
    sw.attachment.reset();
    sw.buttons.clear();

    const int n = texts.size();
    for (int i = 0; i < n; ++i)
    {
        auto* b = sw.buttons.add (new juce::TextButton (texts[i]));
        b->setComponentID (id + "_" + juce::String (i));
        b->setClickingTogglesState (false);
        b->setWantsKeyboardFocus (false);
        const int gap = 4;
        if (vertical)
        {
            const int h = (area.getHeight() - gap * (n - 1)) / n;
            b->setBounds (area.getX(), area.getY() + i * (h + gap), area.getWidth(), h);
        }
        else
        {
            const int w = (area.getWidth() - gap * (n - 1)) / n;
            b->setBounds (area.getX() + i * (w + gap), area.getY(), w, area.getHeight());
        }
        addAndMakeVisible (b);
    }

    auto* prm = audioProcessor.apvts.getParameter (id);
    auto* swPtr = &sw;
    sw.attachment = std::make_unique<juce::ParameterAttachment> (*prm, [swPtr] (float value)
    {
        const int index = juce::roundToInt (value);
        for (int i = 0; i < swPtr->buttons.size(); ++i)
            swPtr->buttons[i]->setToggleState (i == index, juce::dontSendNotification);
    });

    for (int i = 0; i < n; ++i)
        sw.buttons[i]->onClick = [swPtr, i] { swPtr->attachment->setValueAsCompleteGesture ((float) i); };

    sw.attachment->sendInitialUpdate();
}

void PhocusFacade::choiceMenu (ChoiceMenu& cm, const char* id, juce::Rectangle<int> area)
{
    auto* prm = dynamic_cast<juce::AudioParameterChoice*> (audioProcessor.apvts.getParameter (id));
    cm.button.setComponentID (id);
    cm.button.setBounds (area);
    cm.button.setWantsKeyboardFocus (false);
    addAndMakeVisible (cm.button);

    auto* cmPtr = &cm;
    cm.attachment = std::make_unique<juce::ParameterAttachment> (*prm, [cmPtr, prm] (float value)
    {
        cmPtr->button.setButtonText (prm->choices[juce::jlimit (0, prm->choices.size() - 1, juce::roundToInt (value))] + juce::String (juce::CharPointer_UTF8 (" \xe2\x96\xbe")));
    });
    cm.attachment->sendInitialUpdate();

    cm.button.onClick = [this, cmPtr, prm]
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&lookAndFeel);
        for (int i = 0; i < prm->choices.size(); ++i)
            menu.addItem (i + 1, prm->choices[i], true, i == prm->getIndex());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&cmPtr->button),
                            [cmPtr] (int r) { if (r > 0) cmPtr->attachment->setValueAsCompleteGesture ((float) (r - 1)); });
    };
}

void PhocusFacade::selectBand (int band)
{
    band = juce::jlimit (0, 5, band);
    graph.setSelectedBand (band);
    audioProcessor.apvts.state.setProperty ("selectedBand", band, nullptr);
    if (audioProcessor.getBandListen() >= 0 && audioProcessor.getBandListen() != band)
        audioProcessor.setBandListen (band);
    bindBandControls();
}

void PhocusFacade::bindBandControls()
{
    const int band = graph.getSelectedBand();
    const bool isCut = band >= P::numBands;
    const auto colour = (juce::int64) L::bandColour (band).getARGB();

    for (int b = 0; b < 6; ++b)
        bandChips[b].setToggleState (b == band, juce::dontSendNotification);

    bandFreqAttachment.reset();
    bandSensAttachment.reset();
    bandQAttachment.reset();
    bandBalanceAttachment.reset();
    bandOnAttachment.reset();

    auto& apvts = audioProcessor.apvts;
    const juce::String freqId = isCut ? juce::String (band == P::numBands ? P::lowCutFreqId : P::highCutFreqId) : P::bandId (band, "Freq");
    const juce::String onId   = isCut ? juce::String (band == P::numBands ? P::lowCutId : P::highCutId) : P::bandId (band, "On");

    bandFreqAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, freqId, bandFreqKnob);
    bandOnAttachment = std::make_unique<APVTS::ButtonAttachment> (apvts, onId, bandOnButton);
    bandOnButton.getProperties().set ("colour", colour);
    bandFreqKnob.getProperties().set ("colour", colour);

    bandSensKnob.setVisible (! isCut);
    bandQKnob.setVisible (! isCut);
    bandBalanceKnob.setVisible (! isCut && stereo);

    if (! isCut)
    {
        bandSensAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, P::bandId (band, "Sens"), bandSensKnob);
        bandQAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, P::bandId (band, "Q"), bandQKnob);
        bandBalanceAttachment = std::make_unique<APVTS::SliderAttachment> (apvts, P::bandId (band, "Balance"), bandBalanceKnob);
        for (auto* k : { &bandSensKnob, &bandQKnob, &bandBalanceKnob })
            k->getProperties().set ("colour", colour);
    }

    // Types (bandes) ou pentes (coupures), sur deux rangées
    const juce::Rectangle<int> typeArea { bandArea.getX() + 250, bandArea.getY() + 34, 222, 68 };
    if (isCut)
    {
        bandTypeSwitch.attachment.reset();
        bandTypeSwitch.buttons.clear();
        choiceSwitch (slopeSwitch, band == P::numBands ? P::lowCutSlopeId : P::highCutSlopeId,
                      { "6 dB", "12 dB", "24 dB", "48 dB" }, typeArea.withHeight (32), false);
        for (auto* b : slopeSwitch.buttons)
            b->getProperties().set ("colour", colour);
    }
    else
    {
        slopeSwitch.attachment.reset();
        slopeSwitch.buttons.clear();
        choiceSwitch (bandTypeSwitch, P::bandId (band, "Type"), { "PEAK", "LO SHELF", "HI SHELF", "BAND SH", "REJECT", "TILT" },
                      typeArea, false);
        // deux rangées de trois
        for (int i = 0; i < bandTypeSwitch.buttons.size(); ++i)
        {
            const int w = (typeArea.getWidth() - 8) / 3, h = 32;
            bandTypeSwitch.buttons[i]->setBounds (typeArea.getX() + (i % 3) * (w + 4), typeArea.getY() + (i / 3) * (h + 4), w, h);
            bandTypeSwitch.buttons[i]->getProperties().set ("colour", colour);
        }
    }

    repaint();
}

void PhocusFacade::timerCallback()
{
    undoButton.setEnabled (audioProcessor.canUndo());
    redoButton.setEnabled (audioProcessor.canRedo());

    const bool connected = audioProcessor.isSidechainConnected();
    sidechainListenButton.setEnabled (connected && sidechainButton.getToggleState());
    sidechainListenButton.setButtonText (connected ? "LISTEN" : "NO INPUT");

    const bool isStereo = audioProcessor.getTotalNumOutputChannels() > 1;
    if (isStereo != stereo)
    {
        stereo = isStereo;
        for (auto* b : stereoSwitch.buttons) b->setEnabled (stereo);
        linkKnob.setEnabled (stereo);
        balanceKnob.setEnabled (stereo);
        bindBandControls();
    }

    bandListenButton.setToggleState (audioProcessor.getBandListen() == graph.getSelectedBand(), juce::dontSendNotification);

    // Typ de la bande REJECT : la sensibilité ne compte pas
    if (graph.getSelectedBand() < P::numBands)
    {
        const auto curve = audioProcessor.readCurve();
        bandSensKnob.setEnabled (curve.bands[graph.getSelectedBand()].type != phocus::reject);
    }

    shownReduction = audioProcessor.getPeakReduction();
    const auto toDb = [] (float peak) { return juce::jlimit (-60.0f, 6.0f, juce::Decibels::gainToDecibels (peak, -60.0f)); };
    inLevel = juce::jmax (toDb (audioProcessor.takeInputPeak()), inLevel - 3.0f);
    outLevel = juce::jmax (toDb (audioProcessor.takeOutputPeak()), outLevel - 3.0f);
    repaint (mainArea.getX(), mainArea.getBottom() - 92, mainArea.getWidth(), 92);
}

void PhocusFacade::paint (juce::Graphics& g)
{
    // Fond
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff12292e), 0.0f, 0.0f, L::ground(), 0.0f, (float) height, false));
    g.fillAll();

    // En-tête
    g.setColour (L::copper());
    g.setFont (lookAndFeel.font (30.0f, true, 0.18f));
    g.drawText ("PHOCUS", juce::Rectangle<float> (26.0f, 14.0f, 190.0f, 40.0f), juce::Justification::centredLeft, false);
    g.setColour (L::dim());
    g.setFont (lookAndFeel.font (9.0f, true, 0.22f));
    g.drawText ("RESONANCE SUPPRESSOR", juce::Rectangle<float> (204.0f, 20.0f, 186.0f, 14.0f), juce::Justification::centredLeft, false);
    g.drawText ("BY PHRAGMA", juce::Rectangle<float> (204.0f, 36.0f, 186.0f, 14.0f), juce::Justification::centredLeft, false);
    g.setColour (L::dim().withAlpha (0.8f));
    g.drawText ("V" PHOCUS_VERSION, juce::Rectangle<float> ((float) width - 100.0f, 24.0f, 76.0f, 28.0f), juce::Justification::centredRight, false);

    juce::ColourGradient line (L::orange(), 24.0f, 0.0f, L::mauve(), (float) width - 24.0f, 0.0f, false);
    g.setGradientFill (line);
    g.fillRect (24.0f, 64.0f, (float) width - 48.0f, 1.5f);

    const auto titleFont = lookAndFeel.font (10.5f, true, 0.28f);
    L::drawPanel (g, bandArea.toFloat(), {}, titleFont);
    L::drawPanel (g, mainArea.toFloat(), "RESONANCE", titleFont);
    L::drawPanel (g, speedArea.toFloat(), "SPEED", titleFont);
    L::drawPanel (g, stereoArea.toFloat(), "STEREO", titleFont);
    L::drawPanel (g, qualityArea.toFloat(), "QUALITY", titleFont);
    L::drawPanel (g, sideArea.toFloat(), "SIDECHAIN", titleFont);
    L::drawPanel (g, outputArea.toFloat(), "OUTPUT", titleFont);

    // Bande choisie : nom et couleur
    const int band = graph.getSelectedBand();
    g.setColour (L::bandColour (band));
    g.setFont (titleFont);
    g.drawText (bandNames[band], juce::Rectangle<float> ((float) bandArea.getX() + 14.0f, (float) bandArea.getY() + 8.0f, 200.0f, 20.0f),
                juce::Justification::centredLeft, false);
    g.setColour (L::dim());
    g.setFont (lookAndFeel.font (9.0f, true, 0.18f));
    g.drawText (band < P::numBands ? "SHAPE" : "SLOPE", juce::Rectangle<float> ((float) bandArea.getX() + 250.0f, (float) bandArea.getY() + 12.0f, 120.0f, 14.0f),
                juce::Justification::centredLeft, false);

    // Qualité : étiquettes
    g.setFont (lookAndFeel.font (9.5f, true, 0.18f));
    g.setColour (L::dim());
    g.drawText ("REAL-TIME", juce::Rectangle<float> ((float) qualityArea.getX() + 12.0f, (float) qualityArea.getY() + 34.0f, 80.0f, 26.0f), juce::Justification::centredLeft, false);
    g.drawText ("OFFLINE", juce::Rectangle<float> ((float) qualityArea.getX() + 12.0f, (float) qualityArea.getY() + 68.0f, 80.0f, 26.0f), juce::Justification::centredLeft, false);
}

void PhocusFacade::paintOverChildren (juce::Graphics& g)
{
    // Lecture de la réduction et des niveaux, en bas du panneau principal
    auto area = juce::Rectangle<float> ((float) mainArea.getX() + 24.0f, (float) mainArea.getBottom() - 84.0f, (float) mainArea.getWidth() - 48.0f, 68.0f);
    const auto screenShape = L::chamfered (area, 6.0f);
    g.setColour (L::screen());
    g.fillPath (screenShape);
    g.setColour (L::edge());
    g.strokePath (screenShape, juce::PathStrokeType (1.0f));

    g.setFont (lookAndFeel.font (9.0f, true, 0.2f));
    g.setColour (L::dim());
    g.drawText ("MAX REDUCTION", area.withHeight (20.0f).translated (10.0f, 6.0f), juce::Justification::centredLeft, false);
    g.setColour (L::orange());
    g.setFont (lookAndFeel.font (22.0f, true, 0.04f));
    g.drawText ((shownReduction > 0.05f ? "-" : "") + juce::String (shownReduction, 1) + " dB",
                area.withTrimmedTop (24.0f).withTrimmedLeft (10.0f).withWidth (130.0f), juce::Justification::centredLeft, false);

    // Crêtes entrée / sortie
    auto meters = area.withTrimmedLeft (150.0f).reduced (8.0f, 12.0f);
    for (int i = 0; i < 2; ++i)
    {
        auto row = meters.removeFromTop (meters.getHeight() * (i == 0 ? 0.5f : 1.0f)).reduced (0.0f, 3.0f);
        g.setColour (L::dim());
        g.setFont (lookAndFeel.font (8.5f, true, 0.2f));
        g.drawText (i == 0 ? "IN" : "OUT", row.removeFromLeft (26.0f), juce::Justification::centredLeft, false);
        g.setColour (juce::Colour (0xff10282d));
        g.fillRect (row);
        const float level = i == 0 ? inLevel : outLevel;
        const float t = juce::jlimit (0.0f, 1.0f, (level + 60.0f) / 66.0f);
        g.setGradientFill (juce::ColourGradient (L::teal(), row.getX(), 0.0f, L::orange(), row.getRight(), 0.0f, false));
        g.fillRect (row.withWidth (row.getWidth() * t));
    }

}

//==============================================================================
PhocusAudioProcessorEditor::PhocusAudioProcessorEditor (PhocusAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), facade (p)
{
    addAndMakeVisible (facade);

    const int savedWidth = (int) audioProcessor.apvts.state.getProperty ("editorWidth", PhocusFacade::width);

    setResizable (true, true);
    setResizeLimits (juce::roundToInt (PhocusFacade::width * 0.6), juce::roundToInt (PhocusFacade::height * 0.6),
                     juce::roundToInt (PhocusFacade::width * 1.5), juce::roundToInt (PhocusFacade::height * 1.5));
    getConstrainer()->setFixedAspectRatio ((double) PhocusFacade::width / (double) PhocusFacade::height);

    facade.getPresetBar().getScale = [this] { return getUiScale(); };
    facade.getPresetBar().setScale = [this] (float scale) { setUiScale (scale); };

    const int shownWidth = juce::jlimit (juce::roundToInt (PhocusFacade::width * 0.6), juce::roundToInt (PhocusFacade::width * 1.5), savedWidth);
    setSize (shownWidth, juce::roundToInt ((double) shownWidth * PhocusFacade::height / PhocusFacade::width));
    sized = true;
    resized();
}

PhocusAudioProcessorEditor::~PhocusAudioProcessorEditor() = default;

void PhocusAudioProcessorEditor::setUiScale (float scale)
{
    scale = juce::jlimit (0.6f, 1.5f, scale);
    setSize (juce::roundToInt ((float) PhocusFacade::width * scale), juce::roundToInt ((float) PhocusFacade::height * scale));
}

void PhocusAudioProcessorEditor::resized()
{
    facade.setTransform (juce::AffineTransform::scale (getUiScale()));
    facade.getPresetBar().refresh();

    if (sized)
        audioProcessor.apvts.state.setProperty ("editorWidth", getWidth(), nullptr);
}
