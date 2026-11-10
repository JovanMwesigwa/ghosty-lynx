#include "CommandRegistry/console/ConsolePanel.h"

#include "CommandRegistry/console/Account.h"
#include "CommandRegistry/console/AgentClient.h"
#include "CommandRegistry/console/AiSettings.h"
#include "CommandRegistry/console/SettingsView.h"
#include "CommandRegistry/console/SignInView.h"
#include "CommandRegistry/core/CommandBridge.h"
#include "CommandRegistry/core/CommandLine.h"
#include "ui/GhostyLogo.h"
#include "ui/Theme.h"
#include "ui/Workspace.h"

#include <cmath>
#include <set>

namespace lynx::commands
{
namespace
{
// (non-ASCII text is spelled as UTF-8: a plain "..." literal is read as ASCII)
const juce::String cross = juce::String::fromUTF8 ("\xe2\x9c\x97");      // ✗
const juce::String pencil = juce::String::fromUTF8 ("\xe2\x9c\x8e");     // ✎
const juce::String rangeDash = juce::String::fromUTF8 ("\xe2\x80\x93");  // –
const juce::String dot = juce::String::fromUTF8 ("\xc2\xb7");            // ·
const juce::String tick = juce::String::fromUTF8 ("\xe2\x9c\x93");      // ✓
const juce::Colour okGreen { 0xff8fc97a };

// the look: full-width rows, top to bottom (like Claude Code). Every message's text starts after a narrow marker
// column ("›" on your messages, a dot on LYNX's), so replies get the whole width of the panel.
constexpr int sidePad = 12, messageGap = 10;
constexpr float markerWidth = 18.0f;               // the column for "›" / the dot
constexpr float userPadX = 8.0f, userPadY = 7.0f;  // inside your message's row
constexpr float userRadius = 6.0f;                 // (Tailwind's rounded-md, as the composer)
constexpr float composerRadius = 6.0f;

juce::Font monoFont (float size) { return juce::Font (theme::monoFont (size)); }
juce::Font textFont (float size, bool bold = false) { return juce::Font (theme::font (size, bold)); }

juce::String number (double v)
{
    return v == std::floor (v) && std::abs (v) < 1e15 ? juce::String ((juce::int64) v) : juce::String (v);
}

juce::String flagText (const juce::var& arg)
{
    const auto type = arg["type"].toString();
    if (type == "boolean")
        return "--" + arg["name"].toString();
    const auto kind = type == "number" && arg.hasProperty ("min") ? number (arg["min"]) + rangeDash + number (arg["max"]) : type;
    return "--" + arg["name"].toString() + " <" + kind + ">";
}

// "tempo set --bpm <10–522>"; more than two optional flags fold into [options] (as the CLI)
juce::String usage (const juce::var& command)
{
    juce::StringArray parts { command["name"].toString().replaceCharacter ('.', ' ') }, optional;
    if (const auto* args = command["args"].getArray())
        for (const auto& a : *args)
        {
            if ((bool) a["required"])
                parts.add (flagText (a));
            else
                optional.add ("[" + flagText (a) + "]");
        }
    if (optional.size() <= 2)
        parts.addArray (optional);
    else
        parts.add ("[options]");
    return parts.joinIntoString (" ");
}

// Ghosty's avatar: its logo (the ghost with headphones)
void drawAvatar (juce::Graphics& g, juce::Rectangle<float> r)
{
    ghosty::draw (g, r, theme::colour::textPrimary);
}

// a pill-shaped suggestion on the empty state
class Chip final : public juce::Button
{
public:
    explicit Chip (const juce::String& command) : juce::Button (command) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    int preferredWidth() const { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (textFont (theme::type::small), getName())) + 28; }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (down ? colour::bg4 : over ? colour::bg3 : colour::bg2);
        g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
        g.setColour (over ? colour::accent.withAlpha (0.6f) : colour::bg4);
        g.drawRoundedRectangle (r, r.getHeight() / 2.0f, 1.0f);
        g.setColour (over ? colour::textPrimary : colour::textSecondary);
        g.setFont (textFont (type::small));
        g.drawText (getName(), getLocalBounds(), juce::Justification::centred);
    }
};
} // namespace

// ---- the conversation ---------------------------------------------------------------------------------------------

class ConsolePanel::MessageList final : public juce::Component
{
public:
    std::function<void (const juce::String&)> onSuggestion; // a chip was clicked: send it
    std::function<void()> onSetUpAi;                        // the "Set up AI" chip (no AI model picked)
    std::function<void (const juce::String&)> onEditAgain;  // "Edit again" on one of your messages
    std::function<void()> onBackgroundClick;                // (give the input the keys back)

    MessageList() { setSuggestions (false); }

    // The empty state's suggestions: things to ask in plain words (an AI model is picked), or a way to set one up.
    void setSuggestions (bool ai)
    {
        if (ai == aiSuggestions && ! chips.isEmpty())
            return;
        aiSuggestions = ai;
        chips.clear();
        const juce::StringArray prompts { "Make a 4-bar trap beat at 140 BPM", "Write chords for a warm piano", "Add a melody over my chords",
                                          "Make the drums punchier", "What's in this project?" };
        for (const auto& s : ai ? prompts : juce::StringArray { "Set up AI" })
        {
            auto* chip = chips.add (new Chip (s));
            chip->onClick = [this, text = s, ai] {
                if (! ai)
                {
                    if (onSetUpAi)
                        onSetUpAi();
                }
                else if (onSuggestion)
                    onSuggestion (text);
            };
            addChildComponent (chip);
        }
        resized();
        repaint();
    }

    void resized() override
    {
        for (auto* chip : chips)
            chip->setVisible (messages.empty());
        if (messages.empty())
            layoutChips (getWidth(), getHeight());
    }

    bool isEmpty() const noexcept { return messages.empty(); }

    // Adds a message; returns its id (to update it while it streams: replace / append).
    int add (Kind kind, std::vector<Span> spans)
    {
        Message m;
        m.kind = kind;
        m.id = ++lastId;
        for (auto& s : spans)
            m.plain += s.text;
        m.spans = std::move (spans);
        messages.push_back (std::move (m));
        if (messages.size() > 400) // (a long session keeps the last 300)
            messages.erase (messages.begin(), messages.begin() + 100);
        return lastId;
    }

    // A message's whole text again (a command row turning ✓ / ✗). False when it's gone.
    bool replace (int id, std::vector<Span> spans)
    {
        for (auto& m : messages)
            if (m.id == id)
            {
                m.plain.clear();
                for (auto& s : spans)
                    m.plain += s.text;
                m.spans = std::move (spans);
                m.builtWidth = -1.0f; // (lay it out again)
                return true;
            }
        return false;
    }

    // More text at the end of a message (a reply streaming in). False when it's gone.
    bool append (int id, const juce::String& text)
    {
        for (auto& m : messages)
            if (m.id == id && ! m.spans.empty())
            {
                m.spans.back().text += text;
                m.plain += text;
                m.builtWidth = -1.0f;
                return true;
            }
        return false;
    }

    void clear() { messages.clear(); }

    // A command row inside the fold `group` (a toolGroup message): shown only while the fold is open.
    int addToGroup (int group, Kind kind, std::vector<Span> spans)
    {
        const auto id = add (kind, std::move (spans));
        messages.back().group = group;
        return id;
    }

    std::function<void()> onFoldToggled; // (the panel lays the list out again)

    // Lays everything out for `width` and sets this component's size (at least `minHeight`: the empty state fills it).
    void layoutFor (int width, int minHeight)
    {
        auto y = (float) messageGap;
        const auto w = (float) width;

        const auto rowX = (float) sidePad, rowW = juce::jmax (60.0f, w - 2.0f * sidePad);
        std::set<int> open; // the folds that are open (a fold comes before its rows)
        for (auto& m : messages)
        {
            if (m.kind == Kind::toolGroup && m.expanded)
                open.insert (m.id);
            m.shown = m.group == 0 || open.count (m.group) > 0;
            if (! m.shown)
            {
                m.card = m.text = {};
                continue;
            }
            if (m.group != 0)
            {
                // a command inside an open fold: indented under it, close to its neighbours
                const auto textW = rowW - 2.0f * markerWidth;
                buildIfNeeded (m, textW);
                y -= (float) messageGap * 0.5f;
                m.card = { rowX + markerWidth, y, rowW - markerWidth, m.layout.getHeight() };
                m.text = { rowX + 2.0f * markerWidth, y, textW, m.layout.getHeight() };
            }
            else if (m.kind == Kind::user)
            {
                // your message: a full-width row with a soft background, "›" then the text
                const auto textW = rowW - 2.0f * userPadX - markerWidth;
                buildIfNeeded (m, textW);
                m.card = { rowX, y, rowW, m.layout.getHeight() + 2.0f * userPadY };
                m.text = { rowX + userPadX + markerWidth, y + userPadY, textW, m.layout.getHeight() };
            }
            else
            {
                // LYNX's reply (and notes): no box, the marker column then the text across the whole width
                const auto textW = rowW - markerWidth;
                buildIfNeeded (m, textW);
                m.card = { rowX, y, rowW, m.layout.getHeight() };
                m.text = { rowX + markerWidth, y, textW, m.layout.getHeight() };
            }
            y = m.card.getBottom() + (float) messageGap;
        }

        // the empty state: the welcome and the chips, in the middle
        for (auto* chip : chips)
            chip->setVisible (messages.empty());
        if (messages.empty())
        {
            const auto height = juce::jmax (minHeight, 260);
            layoutChips (width, height);
            setSize (width, height);
            return;
        }
        setSize (width, juce::jmax (minHeight, (int) std::ceil (y)));
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        if (messages.empty())
        {
            paintWelcome (g);
            return;
        }

        const auto visible = g.getClipBounds().toFloat();
        for (auto& m : messages)
        {
            if (! m.shown || ! m.card.intersects (visible))
                continue;

            // the marker sits on the first line of text
            const auto firstLine = m.layout.getNumLines() > 0 ? m.layout.getLine (0).getLineBoundsY().getLength() : 16.0f;
            const auto markerCentreY = m.text.getY() + firstLine / 2.0f;
            const auto markerX = m.text.getX() - markerWidth;

            switch (m.kind)
            {
                case Kind::user:
                    g.setColour (colour::bg2);
                    g.fillRoundedRectangle (m.card, userRadius);
                    g.setColour (colour::accent);
                    g.setFont (monoFont (type::body + 1.0f).boldened());
                    g.drawText (juce::String::fromUTF8 ("\xe2\x80\xba"), // ›
                                juce::Rectangle<float> (markerX, markerCentreY - 9.0f, markerWidth, 18.0f), juce::Justification::centredLeft);
                    break;
                case Kind::assistant:
                    // Ghosty's words: its little ghost
                    ghosty::draw (g, juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ markerX + 5.0f, markerCentreY }), colour::textPrimary);
                    break;
                case Kind::reply:
                case Kind::help:
                case Kind::error:
                    g.setColour (m.kind == Kind::error ? colour::record : colour::accent);
                    g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ markerX + 4.5f, markerCentreY }));
                    break;
                case Kind::tool:
                    // a command the agent ran: a small hollow marker, quieter than a reply's
                    g.setColour (colour::textDisabled);
                    g.drawEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre ({ markerX + 4.5f, markerCentreY }), 1.2f);
                    break;
                case Kind::toolGroup:
                {
                    // the fold's chevron: ▸ closed, ▾ open
                    g.setColour (colour::textSecondary);
                    juce::Path chevron;
                    const juce::Point<float> c { markerX + 4.5f, markerCentreY };
                    if (m.expanded)
                        chevron.addTriangle (c.x - 4.0f, c.y - 2.0f, c.x + 4.0f, c.y - 2.0f, c.x, c.y + 3.0f);
                    else
                        chevron.addTriangle (c.x - 2.0f, c.y - 4.0f, c.x - 2.0f, c.y + 4.0f, c.x + 3.0f, c.y);
                    g.fillPath (chevron);
                    break;
                }
                case Kind::info:
                    break;
            }
            m.layout.draw (g, m.text);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (foldAt (e.position) != nullptr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu())
        {
            if (auto* fold = foldAt (e.position))
            {
                fold->expanded = ! fold->expanded;
                if (onFoldToggled)
                    onFoldToggled();
                return;
            }
            if (onBackgroundClick)
                onBackgroundClick();
            return;
        }
        // right-click on a message: copy it (and edit one of yours again)
        for (const auto& m : messages)
            if (m.shown && m.card.contains (e.position) && m.kind != Kind::info)
            {
                juce::PopupMenu menu;
                auto text = m.plain;
                if (m.kind == Kind::toolGroup) // (a fold: copy the commands in it)
                {
                    juce::StringArray lines;
                    for (const auto& r : messages)
                        if (r.group == m.id)
                            lines.add (r.plain);
                    text = lines.joinIntoString ("\n");
                }
                menu.addItem ("Copy", [text] { juce::SystemClipboard::copyTextToClipboard (text); });
                if (m.kind == Kind::user)
                    menu.addItem ("Edit again", [this, text = m.plain] {
                        if (onEditAgain)
                            onEditAgain (text);
                    });
                menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
                return;
            }
    }

private:
    struct Message
    {
        int id = 0;
        Kind kind = Kind::reply;
        int group = 0;                // a command inside a fold: the fold's id (0: not in one)
        bool expanded = false;        // a fold: open
        bool shown = true;            // (false: inside a closed fold)
        std::vector<Span> spans;
        juce::String plain;           // (for Copy)
        juce::TextLayout layout;
        float builtWidth = -1.0f;     // the width `layout` was made for (-1: the text changed since)
        juce::Rectangle<float> card;  // the bubble / card
        juce::Rectangle<float> text;  // where the text goes inside it
    };

    // (laying out text is the slow part: a message is laid out again only when its text or the width changed)
    static void buildIfNeeded (Message& m, float maxWidth)
    {
        if (m.builtWidth == maxWidth)
            return;
        build (m, maxWidth);
        m.builtWidth = maxWidth;
    }

    static void build (Message& m, float maxWidth)
    {
        using namespace theme;
        juce::AttributedString text;
        text.setWordWrap (juce::AttributedString::WordWrap::byWord);
        text.setJustification (juce::Justification::topLeft);
        text.setLineSpacing (2.0f);
        for (auto& s : m.spans)
        {
            const auto size = m.kind == Kind::user || m.kind == Kind::assistant ? type::body
                              : m.kind == Kind::info || m.kind == Kind::tool || m.kind == Kind::toolGroup ? type::small
                                                                             : 12.5f;
            text.append (s.text, s.mono ? monoFont (size) : textFont (size, s.bold), s.colour);
        }
        m.layout.createLayout (text, juce::jmax (20.0f, maxWidth));
    }

    void layoutChips (int width, int height)
    {
        // rows of chips, centred, below the welcome text
        const int chipHeight = 28, gap = 8, maxRow = juce::jmax (120, width - 2 * sidePad);
        std::vector<std::vector<Chip*>> rows (1);
        int rowWidth = 0;
        for (auto* c : chips)
        {
            auto* chip = static_cast<Chip*> (c);
            const auto cw = chip->preferredWidth();
            if (! rows.back().empty() && rowWidth + gap + cw > maxRow)
            {
                rows.emplace_back();
                rowWidth = 0;
            }
            rowWidth += (rows.back().empty() ? 0 : gap) + cw;
            rows.back().push_back (chip);
        }
        auto y = height / 2 + 6;
        for (auto& row : rows)
        {
            int total = 0;
            for (auto* chip : row)
                total += chip->preferredWidth() + (total > 0 ? gap : 0);
            auto x = (width - total) / 2;
            for (auto* chip : row)
            {
                chip->setBounds (x, y, chip->preferredWidth(), chipHeight);
                x += chip->preferredWidth() + gap;
            }
            y += chipHeight + gap;
        }
    }

    void paintWelcome (juce::Graphics& g)
    {
        using namespace theme;
        const auto centreY = (float) getHeight() / 2.0f;
        const auto disc = juce::Rectangle<float> (36.0f, 36.0f).withCentre ({ (float) getWidth() / 2.0f, centreY - 70.0f });
        drawAvatar (g, disc);

        g.setColour (colour::textPrimary);
        g.setFont (textFont (type::title + 3.0f, true));
        g.drawText ("What are we making today?", juce::Rectangle<float> (0.0f, centreY - 44.0f, (float) getWidth(), 26.0f), juce::Justification::centred);
        g.setColour (colour::textSecondary);
        g.setFont (textFont (type::body));
        g.drawFittedText (aiSuggestions ? "I'm Ghosty. Tell me what you want, in your own words. For example:"
                                        : "I'm Ghosty. Add a Claude or OpenAI key, pick a model below, and tell me what you want in your own words.",
                          juce::Rectangle<int> (sidePad, juce::roundToInt (centreY - 18.0f), getWidth() - 2 * sidePad, 22), juce::Justification::centred, 2);
    }

    // the fold under the mouse (its row), or nullptr
    Message* foldAt (juce::Point<float> p)
    {
        for (auto& m : messages)
            if (m.kind == Kind::toolGroup && m.shown && m.card.contains (p))
                return &m;
        return nullptr;
    }

    std::vector<Message> messages;
    bool aiSuggestions = false;
    int lastId = 0;
    juce::OwnedArray<juce::Button> chips;
};

// ---- the composer -------------------------------------------------------------------------------------------------

class ConsolePanel::Composer final : public juce::Component
{
public:
    // the text: Enter / Shift+Enter / Up / Down / Tab / Escape are the console's before the editor's
    struct Input final : juce::TextEditor
    {
        std::function<bool (const juce::KeyPress&)> onKey;
        bool keyPressed (const juce::KeyPress& key) override { return (onKey && onKey (key)) || juce::TextEditor::keyPressed (key); }
        void focusGained (FocusChangeType cause) override
        {
            juce::TextEditor::focusGained (cause);
            repaintParent();
        }
        void focusLost (FocusChangeType cause) override
        {
            juce::TextEditor::focusLost (cause);
            repaintParent();
        }
        void repaintParent()
        {
            if (auto* p = getParentComponent())
                p->repaint();
        }
    };

    // the round buttons at the bottom right
    class RoundButton final : public juce::Button
    {
    public:
        enum class Glyph { send, mic, gear, stop };
        RoundButton (const juce::String& name, Glyph g) : juce::Button (name), glyph (g) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace theme;
            const auto r = getLocalBounds().toFloat().reduced (1.0f);
            const auto c = r.getCentre();
            juce::Path p;

            if (glyph == Glyph::stop)
            {
                // the agent is working: a filled disc with a square (click: stop it)
                g.setColour (down ? colour::accentPressed : over ? colour::accentHover : colour::accent);
                g.fillEllipse (r);
                g.setColour (juce::Colours::white);
                g.fillRoundedRectangle (juce::Rectangle<float> (9.0f, 9.0f).withCentre (c), 1.5f);
                return;
            }
            if (glyph == Glyph::send)
            {
                // a filled disc with an arrow pointing up (dim until there's something to send)
                const auto on = isEnabled();
                g.setColour (! on ? colour::bg4 : down ? colour::accentPressed : over ? colour::accentHover : colour::accent);
                g.fillEllipse (r);
                p.startNewSubPath (c.x, c.y + 6.0f);
                p.lineTo (c.x, c.y - 6.0f);
                p.startNewSubPath (c.x - 5.0f, c.y - 1.0f);
                p.lineTo (c.x, c.y - 6.0f);
                p.lineTo (c.x + 5.0f, c.y - 1.0f);
                g.setColour (on ? juce::Colours::white : colour::textDisabled);
                g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }

            if (over || down)
            {
                g.setColour (down ? colour::bg4 : colour::bg3);
                g.fillEllipse (r);
            }
            if (glyph == Glyph::gear)
            {
                // a cog: a ring and eight teeth
                p.addEllipse (c.x - 4.0f, c.y - 4.0f, 8.0f, 8.0f);
                for (int i = 0; i < 8; ++i)
                {
                    const auto a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                    p.startNewSubPath (c.x + 5.5f * std::sin (a), c.y - 5.5f * std::cos (a));
                    p.lineTo (c.x + 8.0f * std::sin (a), c.y - 8.0f * std::cos (a));
                }
                g.setColour (over ? colour::textPrimary : colour::textSecondary);
                g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                return;
            }

            // the microphone: a capsule on a stand
            p.addRoundedRectangle (c.x - 3.5f, c.y - 8.0f, 7.0f, 11.0f, 3.5f);
            p.addCentredArc (c.x, c.y - 1.5f, 6.5f, 6.5f, 0.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3.0f, true);
            p.startNewSubPath (c.x, c.y + 5.0f);
            p.lineTo (c.x, c.y + 8.0f);
            p.startNewSubPath (c.x - 3.0f, c.y + 8.0f);
            p.lineTo (c.x + 3.0f, c.y + 8.0f);
            g.setColour (over ? colour::textPrimary : colour::textSecondary);
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void setGlyph (Glyph g)
        {
            glyph = g;
            repaint();
        }

    private:
        Glyph glyph;
    };

    // the model picker: the chosen model's name and a chevron (click: the menu)
    class ModelButton final : public juce::Button
    {
    public:
        ModelButton() : juce::Button ("Model") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        int preferredWidth() const { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (textFont (theme::type::small), getButtonText())) + 34; }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace theme;
            const auto r = getLocalBounds().toFloat().reduced (0.0f, 4.0f);
            if (over || down)
            {
                g.setColour (down ? colour::bg4 : colour::bg3);
                g.fillRoundedRectangle (r, 6.0f);
            }
            g.setColour (over ? colour::textPrimary : colour::textSecondary);
            g.setFont (textFont (type::small));
            g.drawText (getButtonText(), r.withTrimmedLeft (10.0f).withTrimmedRight (22.0f), juce::Justification::centredLeft, true);
            juce::Path chevron;
            const auto c = juce::Point<float> (r.getRight() - 12.0f, r.getCentreY());
            chevron.startNewSubPath (c.x - 3.5f, c.y - 1.5f);
            chevron.lineTo (c.x, c.y + 2.0f);
            chevron.lineTo (c.x + 3.5f, c.y - 1.5f);
            g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };

    Input input;
    // you: a disc with your initials (click: the account menu)
    class AccountButton final : public juce::Button
    {
    public:
        AccountButton() : juce::Button ("Account") { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        void setPerson (const juce::String& name, const juce::String& email)
        {
            juce::String letters;
            for (const auto& word : juce::StringArray::fromTokens (name.trim(), " ", ""))
                if (letters.length() < 2 && word.isNotEmpty())
                    letters << word.substring (0, 1).toUpperCase();
            initials = letters.isNotEmpty() ? letters : email.substring (0, 1).toUpperCase();
            setTooltip (name.isNotEmpty() ? name + " (" + email + ")" : email);
            repaint();
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace theme;
            const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (24.0f, 24.0f);
            g.setColour (down ? colour::bg5 : over ? colour::bg4 : colour::bg3);
            g.fillEllipse (r);
            g.setColour (colour::textPrimary);
            g.setFont (textFont (initials.length() > 1 ? 10.0f : 11.5f, true));
            g.drawText (initials.isNotEmpty() ? initials : juce::String ("?"), r, juce::Justification::centred);
        }

    private:
        juce::String initials;
    };

    AccountButton account;
    RoundButton settings { "Settings", RoundButton::Glyph::gear };
    ModelButton model;
    RoundButton mic { "Voice input", RoundButton::Glyph::mic };
    RoundButton send { "Send", RoundButton::Glyph::send };
    std::function<void()> onHeightChanged;
    bool busy = false; // (the agent is working: send is Stop)

    // the send button turns into Stop while the agent works
    void setBusy (bool isBusy)
    {
        busy = isBusy;
        send.setGlyph (busy ? RoundButton::Glyph::stop : RoundButton::Glyph::send);
        send.setTooltip (busy ? "Stop (Esc)" : "Send (Enter)");
        send.setEnabled (busy || input.getText().trim().isNotEmpty());
    }

    Composer()
    {
        using namespace theme;
        input.setMultiLine (true, true);
        input.setReturnKeyStartsNewLine (false);
        input.setScrollbarsShown (true);
        input.setFont (monoFont (type::body));
        input.setTextToShowWhenEmpty ("Ask Ghosty...  (Enter sends, Shift+Enter: new line)", colour::textDisabled);
        input.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        input.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        input.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        input.setColour (juce::TextEditor::textColourId, colour::textPrimary);
        input.setColour (juce::TextEditor::highlightColourId, colour::accent.withAlpha (0.35f));
        input.setColour (juce::CaretComponent::caretColourId, colour::accent);
        input.setEscapeAndReturnKeysConsumed (true);
        input.onTextChange = [this] {
            send.setEnabled (busy || input.getText().trim().isNotEmpty());
            const auto height = preferredHeight();
            if (height != lastHeight && onHeightChanged)
            {
                lastHeight = height;
                onHeightChanged(); // (the box grows with its lines; not on every key)
            }
        };
        addAndMakeVisible (input);

        mic.setTooltip ("Voice input (coming soon)");
        send.setTooltip ("Send (Enter)");
        settings.setTooltip ("Settings: API keys");
        model.setTooltip ("Which model answers in the chat");
        send.setEnabled (false);
        for (juce::Button* b : { (juce::Button*) &account, (juce::Button*) &settings, (juce::Button*) &model, (juce::Button*) &mic, (juce::Button*) &send })
        {
            b->setWantsKeyboardFocus (false);
            b->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
    }

    int lastHeight = 0; // (preferredHeight() when the panel last laid out)

    // the input as tall as its lines (one to eight, then it scrolls), the button row below it
    int preferredHeight() const
    {
        const auto line = juce::roundToInt (monoFont (theme::type::body).getHeight());
        const auto text = juce::jlimit (line + 8, 8 * line + 8, input.getTextHeight() + 8);
        return 10 + text + 4 + 32 + 8;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (colour::bg0);
        g.fillRoundedRectangle (r, composerRadius);
        g.setColour (input.hasKeyboardFocus (true) ? colour::accent.withAlpha (0.65f) : colour::bg4);
        g.drawRoundedRectangle (r, composerRadius, 1.0f);
    }

    // the bottom row: [you] [⚙] [model ⌄] ............ [mic] [send]
    void resized() override
    {
        auto area = getLocalBounds().reduced (12, 0);
        area.removeFromTop (10);
        area.removeFromBottom (8);
        auto row = area.removeFromBottom (32);
        area.removeFromBottom (4);
        input.setBounds (area.expanded (4, 0)); // (the editor has its own small indent)
        send.setBounds (row.removeFromRight (32));
        row.removeFromRight (4);
        mic.setBounds (row.removeFromRight (32));
        row.removeFromRight (8);
        account.setBounds (row.removeFromLeft (30).translated (-4, 0));
        settings.setBounds (row.removeFromLeft (32).translated (-4, 0));
        model.setBounds (row.removeFromLeft (juce::jmin (model.preferredWidth(), row.getWidth())));
    }
};

// ---- the header's plan button ----------------------------------------------------------------------------------------

class ConsolePanel::PlanButton final : public juce::Button
{
public:
    PlanButton() : juce::Button ("Plan")
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (false);
        setMouseClickGrabsKeyboardFocus (false);
    }

    // filled: the accent call to action (Upgrade); otherwise outlined in blue (the plan you're on)
    void show (const juce::String& text, bool isFilled)
    {
        setButtonText (text);
        filled = isFilled;
        repaint();
    }

    int preferredWidth() const
    {
        return juce::roundToInt (juce::GlyphArrangement::getStringWidth (textFont (theme::type::small, true), getButtonText())) + (filled ? 34 : 22);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        auto text = getLocalBounds();
        if (filled)
        {
            g.setColour (down ? colour::accentPressed : over ? colour::accentHover : colour::accent);
            g.fillRoundedRectangle (r, 6.0f);
            // a small spark before the words
            const auto c = juce::Point<float> ((float) text.removeFromLeft (22).getCentreX() + 3.0f, r.getCentreY());
            juce::Path spark;
            spark.addStar (c, 4, 1.6f, 5.0f);
            g.setColour (juce::Colours::white);
            g.fillPath (spark);
            text.removeFromRight (10);
        }
        else
        {
            g.setColour (down ? colour::bg4 : over ? colour::bg3.brighter (0.05f) : colour::bg2);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (colour::info.withAlpha (over ? 0.9f : 0.6f)); // (blue: the plan you're on, not a call to action)
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
            text = text.reduced (10, 0);
        }
        g.setColour (filled ? juce::Colours::white : colour::info);
        g.setFont (textFont (type::small, true));
        g.drawText (getButtonText(), text, juce::Justification::centred);
    }

private:
    bool filled = true;
};

// ---- the panel ----------------------------------------------------------------------------------------------------

ConsolePanel::ConsolePanel()
    : list (std::make_unique<MessageList>()), composer (std::make_unique<Composer>()), settingsView (std::make_unique<SettingsView>()),
      agent (std::make_unique<AgentClient>())
{
    list->onSuggestion = [this] (const juce::String& prompt) {
        composer->input.setText (prompt, true);
        submit();
    };
    list->onSetUpAi = [this] { showSettings (true); };
    list->onEditAgain = [this] (const juce::String& text) {
        composer->input.setText (text, true);
        composer->input.moveCaretToEnd();
        composer->input.grabKeyboardFocus();
    };
    list->onBackgroundClick = [this] { composer->input.grabKeyboardFocus(); };

    viewport.setViewedComponent (list.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    viewport.setWantsKeyboardFocus (false);
    addAndMakeVisible (viewport);

    composer->input.onKey = [this] (const juce::KeyPress& key) { return handleKey (key); };
    composer->send.onClick = [this] {
        if (agent->isRunning())
            agent->stop();
        else
            submit();
    };
    composer->mic.onClick = [this] {
        post (Kind::info, { { "Voice input is coming soon: type your command for now.", theme::colour::textDisabled, false } });
        composer->input.grabKeyboardFocus();
    };
    composer->onHeightChanged = [this] { resized(); };
    composer->settings.onClick = [this] { showSettings (true); };
    composer->model.onClick = [this] { showModelMenu(); };
    addAndMakeVisible (*composer);

    settingsView->onBack = [this] { showSettings (false); };
    addChildComponent (*settingsView);

    agent->onEvent = [this] (const juce::var& e) { agentEvent (e); };
    agent->onFinished = [this] {
        assistantMessage = -1;
        if (toolGroup >= 0)
            updateToolGroup (true);
        toolGroup = -1;
        composer->setBusy (false);
        layoutConversation (true);
    };
    list->onFoldToggled = [this] { layoutConversation (false); };

    signInView = std::make_unique<SignInView>();
    addChildComponent (*signInView);
    composer->account.onClick = [this] { showAccountMenu (composer->account); };

    planButton = std::make_unique<PlanButton>();
    addChildComponent (*planButton);
    planButton->onClick = [this] {
        auto& account = Account::get();
        if (account.isPaid() || account.upgrading())
            showAccountMenu (*planButton);
        else
            showUpgradeMenu();
    };
    Account::get().addChangeListener (this);
    accountChanged();

    AiSettings::get().addChangeListener (this);
    refreshModelPicker();
    setWantsKeyboardFocus (true);
}

ConsolePanel::~ConsolePanel()
{
    Account::get().removeChangeListener (this);
    AiSettings::get().removeChangeListener (this);
    viewport.setViewedComponent (nullptr, false); // (the list is owned here, not by the viewport)
}

void ConsolePanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &Account::get())
        accountChanged();
    else
        refreshModelPicker();
}

// ---- the account ------------------------------------------------------------------------------------------------------

void ConsolePanel::accountChanged()
{
    const auto& account = Account::get();
    const auto now = account.state() == Account::State::signedIn;
    if (signedIn && ! now)
    {
        // signed out: the conversation was theirs, so it goes (and the agent forgets it too)
        if (agent->isRunning())
            agent->stop();
        agent->reset();
        list->clear();
        toolRows.clear();
        assistantMessage = toolGroup = -1;
        showingSettings = false;
    }
    signedIn = now;
    composer->account.setPerson (account.name(), account.email());
    refreshPlanButton();
    updateGate();
}

// Signed out: only the sign-in screen. Signed in: the chat (or Settings, if that's open).
void ConsolePanel::updateGate()
{
    signInView->setVisible (! signedIn);
    settingsView->setVisible (signedIn && showingSettings);
    viewport.setVisible (signedIn && ! showingSettings);
    composer->setVisible (signedIn && ! showingSettings);
    resized();
    if (signedIn && ! showingSettings && isShowing())
        composer->input.grabKeyboardFocus();
}

void ConsolePanel::refreshPlanButton()
{
    const auto& account = Account::get();
    if (! signedIn)
        planButton->setVisible (false);
    else if (account.upgrading())
    {
        planButton->show ("Waiting for payment...", false);
        planButton->setVisible (true);
    }
    else if (account.isPaid())
    {
        planButton->show (account.onTrial() ? juce::String::fromUTF8 ("Launch \xc2\xb7 Trial") : juce::String ("Launch"), false);
        planButton->setVisible (true);
    }
    else
    {
        planButton->show (account.trialDays() > 0 ? "Start free trial" : "Upgrade", true);
        planButton->setVisible (account.billingAvailable() && ! account.offers().empty());
    }
    planButton->setTooltip (account.planText());
    resized();
}

// The free plan's choice: Launch monthly or yearly (each starts a checkout), or the plans compared in the browser.
void ConsolePanel::showUpgradeMenu()
{
    const auto& account = Account::get();
    juce::PopupMenu menu;
    menu.addSectionHeader ("Launch plan");
    if (account.trialDays() > 0)
        menu.addItem (juce::String (account.trialDays()) + "-day free trial, then:", false, false, [] {});
    for (const auto& offer : account.offers())
        menu.addItem ((offer.interval == "year" ? "Yearly: " : "Monthly: ") + offer.label, [interval = offer.interval] { Account::get().upgrade (interval); });
    menu.addSeparator();
    menu.addItem ("Compare plans in the browser...", [] { Account::get().openUpgradePage(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (planButton.get()));
}

void ConsolePanel::showAccountMenu (juce::Component& target)
{
    const auto& account = Account::get();
    juce::PopupMenu menu;
    menu.addSectionHeader (account.name().isNotEmpty() ? account.name() : juce::String ("Signed in"));
    if (account.email().isNotEmpty())
        menu.addItem (account.email(), false, false, [] {});
    if (account.planText().isNotEmpty())
        menu.addItem (account.planText(), false, false, [] {});
    if (account.error().isNotEmpty())
        menu.addItem (account.error(), false, false, [] {});
    menu.addSeparator();
    if (account.upgrading())
    {
        menu.addItem ("Waiting for the payment...", false, false, [] {});
        menu.addItem ("Stop waiting", [] { Account::get().cancelUpgrade(); });
    }
    else if (account.isPaid() || account.hasSubscription())
        menu.addItem ("Manage subscription...", [] { Account::get().manageSubscription(); });
    else if (account.billingAvailable())
        for (const auto& offer : account.offers()) // ("Upgrade to Launch: $50 / month...", "... $490 / year...")
            menu.addItem ((account.trialDays() > 0 ? "Try Launch free: then " : "Upgrade to Launch: ") + offer.label + "...",
                          [interval = offer.interval] { Account::get().upgrade (interval); });
    menu.addItem ("Account page...", [] { juce::URL (Account::serverUrl() + "/account").launchInDefaultBrowser(); });
    menu.addItem ("Sign out", [] { Account::get().signOut(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&target).withPreferredPopupDirection (
        &target == &composer->account ? juce::PopupMenu::Options::PopupDirection::upwards : juce::PopupMenu::Options::PopupDirection::downwards));
}

void ConsolePanel::setTitleBarInsets (int left, int right)
{
    titleLeft = left;
    titleRight = right;
    resized();
}

void ConsolePanel::refreshModelPicker()
{
    list->setSuggestions (AiSettings::get().selectedIsAi());
    composer->model.setButtonText (AiSettings::get().selectedLabel());
    composer->resized();
    composer->repaint();
}

void ConsolePanel::showSettings (bool show)
{
    showingSettings = show;
    updateGate();
}

// The model picker's menu: the models by provider (the ones without a key greyed out, saying why), then Settings.
void ConsolePanel::showModelMenu()
{
    auto& settings = AiSettings::get();
    juce::PopupMenu menu;
    settings.fetchModelsIfNeeded (AiProvider::openai); // (a key but no list yet: ask OpenAI, for next time)
    juce::String group;
    for (const auto& m : settings.models())
    {
        if (m.group != group)
        {
            group = m.group;
            menu.addSectionHeader (group == "LYNX" ? juce::String ("No AI") : group);
        }
        menu.addItem (m.label + (m.usable ? juce::String() : "  (" + m.why + ")"), m.usable, m.id == settings.selectedModel() && m.usable,
                      [this, id = m.id, label = m.label] {
                          AiSettings::get().select (id);
                          if (id.startsWith ("google:")) // (the one provider the agent can't talk to yet)
                              post (Kind::info, { { label + " selected. Chat with Google models isn't connected yet: commands still run as before.",
                                                    theme::colour::textDisabled, false } });
                      });
    }
    menu.addSeparator();
    menu.addItem ("API keys and settings...", [this] { showSettings (true); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&composer->model).withPreferredPopupDirection (
        juce::PopupMenu::Options::PopupDirection::upwards));
}

// ---- the agent ------------------------------------------------------------------------------------------------------

// Every line is a command the registry knows (or help / clear / a # comment): run it as a script, not as a chat message.
bool ConsolePanel::isCommandScript (const juce::String& text) const
{
    auto* registry = CommandBridge::activeRegistry();
    if (registry == nullptr)
        return false;
    bool any = false;
    for (auto line : juce::StringArray::fromLines (text))
    {
        line = line.trim();
        if (line.isEmpty() || line.startsWithChar ('#'))
            continue;
        if (line.startsWithIgnoreCase ("lynx "))
            line = line.substring (5).trim();
        any = true;
        if (line == "clear")
            continue;
        const auto parsed = parseCommandLine (line);
        if (parsed.error.isNotEmpty())
            return false;
        if (parsed.name.isEmpty() || parsed.name == "help")
            continue;
        const auto known = parsed.help ? registry->exists (parsed.name.upToFirstOccurrenceOf (".", false, false)) : registry->exists (parsed.name);
        if (! known)
            return false;
    }
    return any;
}

void ConsolePanel::sendToAgent (const juce::String& text)
{
    using namespace theme;
    post (Kind::user, { { text, colour::textPrimary } });

    auto& settings = AiSettings::get();
    const auto& model = settings.selectedModel();
    if (! model.startsWith ("claude-") && ! model.startsWith ("openai:"))
    {
        post (Kind::error, { { cross + " Chat with Google models isn't connected yet: pick a Claude or OpenAI model.", colour::textPrimary, false } });
        return;
    }
    const auto provider = model.startsWith ("openai:") ? AiProvider::openai : AiProvider::anthropic;
    const auto key = settings.key (provider);
    if (! key)
    {
        post (Kind::error, { { cross + " No " + AiSettings::providerName (provider) + " key: add yours in Settings (the gear).", colour::textPrimary, false } });
        return;
    }
    const auto& options = settings.provider (provider);
    const auto baseUrl = provider == AiProvider::openai && options.overrideBaseUrl ? options.baseUrl.trim() : juce::String();

    assistantMessage = -1;
    toolRows.clear();
    toolGroup = -1;
    composer->setBusy (true);
    auto* registry = CommandBridge::activeRegistry();
    agent->start (text, model, *key, registry != nullptr ? registry->describe() : juce::var(), baseUrl);
}

// One event of the agent's reply: its words stream into one message; each command it runs is a row (✓ / ✗ when done).
void ConsolePanel::agentEvent (const juce::var& e)
{
    using namespace theme;
    const auto type = e["t"].toString();

    if (type == "text")
    {
        const auto delta = e["d"].toString();
        if (assistantMessage < 0 || ! list->append (assistantMessage, delta))
        {
            if (delta.trim().isEmpty())
                return; // (no empty message for leading whitespace)
            if (toolGroup >= 0)
                updateToolGroup (true); // (words after the commands: that fold is complete)
            toolGroup = -1;
            assistantMessage = list->add (Kind::assistant, { { delta.trimStart(), colour::textPrimary, false } });
        }
    }
    else if (type == "tool")
    {
        assistantMessage = -1; // (words after a command start a new message below it)
        const auto id = e["id"].toString();
        const auto command = e["command"].toString();
        if (toolGroup < 0) // the first command after some words: a new fold, closed
        {
            toolGroup = list->add (Kind::toolGroup, {});
            groupCount = groupDone = groupFailed = 0;
        }
        ++groupCount;
        toolRows[id] = { list->addToGroup (toolGroup, Kind::tool, { { command, colour::textSecondary, true } }), command };
        updateToolGroup (false);

        // the agent (on Ghosty's server) asks LYNX to run it: run it on the open project, send the result back
        // (just after this, so the row shows first)
        if (const auto& run = e["run"]; run.isObject())
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ConsolePanel> (this), id, name = run["name"].toString(),
                                              args = run["args"]] {
                if (safe != nullptr)
                    safe->runForAgent (id, name, args);
            });
    }
    else if (type == "tool_end")
    {
        const auto found = toolRows.find (e["id"].toString());
        if (found == toolRows.end())
            return;
        const auto ok = (bool) e["ok"];
        const auto summary = e["summary"].toString();
        list->replace (found->second.first, { { found->second.second, colour::textSecondary, true },
                                              { ok ? "  " + tick + (summary.isNotEmpty() ? " " + summary : juce::String()) : "  " + cross + " " + summary,
                                                ok ? okGreen : colour::record, false } });
        ++groupDone;
        if (! ok)
            ++groupFailed;
        if (toolGroup >= 0)
            updateToolGroup (false);
    }
    else if (type == "error")
    {
        assistantMessage = -1;
        list->add (Kind::error, { { cross + " " + e["message"].toString(), colour::textPrimary, false } });
    }
    else
        return; // ("done": the finish handler resets the button)

    layoutConversation (true);
}

// A command Ghosty's agent asked for: run here, on the open project, and its result posted back to the agent. LYNX
// keeps the last word on what may run: never sign-in / billing, never --discard / --overwrite (they'd lose work), and
// every argument is checked by the command itself.
void ConsolePanel::runForAgent (const juce::String& callId, const juce::String& name, const juce::var& args)
{
    auto* registry = CommandBridge::activeRegistry();
    juce::String refusal;
    if (registry == nullptr)
        refusal = "LYNX's commands aren't ready yet";
    else if (name.startsWith ("account.") || name.startsWith ("commands.") || ! registry->exists (name))
        refusal = "Ghosty can't run " + name;
    else if (auto* o = args.getDynamicObject(); o != nullptr && (o->hasProperty ("discard") || o->hasProperty ("overwrite")))
        refusal = "Ghosty can't discard unsaved work or overwrite files";
    if (refusal.isNotEmpty())
    {
        agent->sendResult (callId, false, {}, refusal);
        return;
    }
    const auto result = registry->run (name, args.isObject() ? args : juce::var (new juce::DynamicObject()));
    agent->sendResult (callId, result.ok, result.value, result.error);
}

// The fold's line: "Running 2 commands..." while they run, then "Ran 5 commands" (and how many failed: the AI usually
// fixes those itself, so it's a quiet note, not an error).
void ConsolePanel::updateToolGroup (bool finished)
{
    using namespace theme;
    const auto plural = [] (int n) { return juce::String (n) + (n == 1 ? " command" : " commands"); };
    std::vector<Span> spans;
    if (! finished && groupDone < groupCount)
        spans.push_back ({ "Running " + plural (groupCount) + "...", colour::textSecondary, false });
    else
        spans.push_back ({ "Ran " + plural (groupCount), colour::textSecondary, false });
    if (groupFailed > 0)
        spans.push_back ({ "  " + juce::String (groupFailed) + " didn't work", colour::textDisabled, false });
    list->replace (toolGroup, std::move (spans));
}

void ConsolePanel::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::bg1);
    // the header: the workspace's title row (the frame's own look)
    auto header = getLocalBounds().removeFromTop (Workspace::titleHeight - 1);
    g.setColour (colour::bg3);
    g.fillRect (header);
    g.setColour (colour::border);
    g.fillRect (header.removeFromBottom (1));
}

void ConsolePanel::resized()
{
    // the header row: the plan button at the right, before the window buttons
    auto body = getLocalBounds();
    auto header = body.removeFromTop (Workspace::titleHeight - 1).withTrimmedLeft (titleLeft).withTrimmedRight (titleRight + 4);
    if (planButton != nullptr && planButton->isVisible())
    {
        const auto w = juce::jmin (planButton->preferredWidth(), juce::jmax (0, header.getWidth()));
        planButton->setBounds (header.removeFromRight (w).withSizeKeepingCentre (w, 22));
    }

    if (! signedIn)
    {
        signInView->setBounds (body);
        return;
    }
    if (showingSettings)
    {
        settingsView->setBounds (body);
        return;
    }
    auto area = body.reduced (10);
    composer->setBounds (area.removeFromBottom (composer->preferredHeight()));
    area.removeFromBottom (8);

    // stay at the end of the conversation if that's where we were
    const auto atEnd = viewport.getViewPositionY() + viewport.getHeight() >= list->getHeight() - 4;
    viewport.setBounds (area);
    layoutConversation (atEnd);
}

void ConsolePanel::layoutConversation (bool scrollToEnd)
{
    list->layoutFor (viewport.getMaximumVisibleWidth(), viewport.getMaximumVisibleHeight());
    list->repaint();
    if (scrollToEnd)
        viewport.setViewPosition (0, juce::jmax (0, list->getHeight() - viewport.getMaximumVisibleHeight()));
}

void ConsolePanel::focusGained (FocusChangeType)
{
    composer->input.grabKeyboardFocus();
}

void ConsolePanel::mouseDown (const juce::MouseEvent&)
{
    composer->input.grabKeyboardFocus();
}

void ConsolePanel::post (Kind kind, std::vector<Span> spans)
{
    // (no trailing blank lines inside a card)
    while (! spans.empty() && spans.back().text.trimEnd().isEmpty())
        spans.pop_back();
    if (! spans.empty())
        spans.back().text = spans.back().text.trimEnd();
    if (spans.empty())
        return;

    list->add (kind, std::move (spans));
    layoutConversation (true);
}

bool ConsolePanel::handleKey (const juce::KeyPress& key)
{
    auto& input = composer->input;
    const auto code = key.getKeyCode();
    const auto text = input.getText();
    const auto caret = input.getCaretPosition();

    if (code == juce::KeyPress::returnKey)
    {
        if (key.getModifiers().isShiftDown())
            input.insertTextAtCaret ("\n"); // a new line
        else
            submit();                       // send them all
        return true;
    }
    // Up / Down: the history from the first / last line of the input; between lines they move the caret
    if (key == juce::KeyPress::upKey)
    {
        if (text.substring (0, caret).containsChar ('\n'))
            return false;
        recallHistory (-1);
        return true;
    }
    if (key == juce::KeyPress::downKey)
    {
        if (text.substring (caret).containsChar ('\n'))
            return false;
        recallHistory (1);
        return true;
    }
    if (key == juce::KeyPress::tabKey)
    {
        complete();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        if (agent->isRunning())
        {
            agent->stop();
            return true;
        }
        input.clear();
        historyIndex = history.size();
        return true;
    }
    return false;
}

void ConsolePanel::recallHistory (int delta)
{
    auto& input = composer->input;
    if (history.isEmpty())
        return;
    if (historyIndex == history.size())
        draft = input.getText();
    const auto next = juce::jlimit (0, history.size(), historyIndex + delta);
    if (next == historyIndex)
        return;
    historyIndex = next;
    input.setText (historyIndex == history.size() ? draft : history[historyIndex], true);
    input.moveCaretToEnd();
}

void ConsolePanel::complete()
{
    auto& input = composer->input;
    auto* registry = CommandBridge::activeRegistry();
    if (registry == nullptr)
        return;

    // the line the caret is on
    const auto text = input.getText();
    const auto caret = input.getCaretPosition();
    const auto lineStart = text.substring (0, caret).lastIndexOfChar ('\n') + 1;
    const auto newline = text.indexOfChar (caret, '\n');
    const auto lineEnd = newline < 0 ? text.length() : newline;
    const auto typed = text.substring (lineStart, lineEnd).trimStart().replaceCharacter ('.', ' ');
    if (typed.contains ("--"))
        return;

    auto replaceLine = [&] (const juce::String& with) {
        input.setHighlightedRegion ({ lineStart, lineEnd });
        input.insertTextAtCaret (with);
    };

    // every group ("tempo") and command ("tempo get") that starts with what was typed
    juce::StringArray matches;
    if (const auto* groups = registry->describe()["groups"].getArray())
        for (const auto& g : *groups)
        {
            if (g["name"].toString().startsWithIgnoreCase (typed))
                matches.add (g["name"].toString());
            if (const auto* commands = g["commands"].getArray())
                for (const auto& c : *commands)
                    if (const auto name = c["name"].toString().replaceCharacter ('.', ' '); name.startsWithIgnoreCase (typed))
                        matches.add (name);
        }

    if (matches.isEmpty())
        return;
    if (matches.size() == 1)
    {
        replaceLine (matches[0] + " ");
        return;
    }
    // several: extend to what they share, or list them when that adds nothing
    auto shared = matches[0];
    for (const auto& m : matches)
        while (! m.startsWithIgnoreCase (shared))
            shared = shared.dropLastCharacters (1);
    if (shared.length() > typed.length())
        replaceLine (shared);
    else
        post (Kind::info, { { matches.joinIntoString ("    "), theme::colour::textSecondary } });
}

void ConsolePanel::submit()
{
    auto& input = composer->input;
    const auto text = input.getText();
    if (text.trim().isEmpty())
        return;

    if (agent->isRunning())
        return; // (one message at a time: Stop the running one first)

    if (history.isEmpty() || history[history.size() - 1] != text.trim())
        history.add (text.trim());
    historyIndex = history.size();
    draft.clear();
    input.clear();
    resized();

    // an AI model is picked and this isn't a command (or a script of them): the agent takes it
    if (AiSettings::get().selectedIsAi() && ! isCommandScript (text))
    {
        sendToAgent (text.trim());
        return;
    }

    // each line in order; blank lines and # comments are skipped, a failing line stops the rest
    juce::StringArray commands;
    for (const auto& line : juce::StringArray::fromLines (text))
        if (const auto l = line.trim(); l.isNotEmpty() && ! l.startsWithChar ('#'))
            commands.add (l);

    for (int i = 0; i < commands.size(); ++i)
        if (! runLine (commands[i]))
        {
            if (const auto left = commands.size() - i - 1; left > 0)
                post (Kind::info, { { "Stopped: " + juce::String (left) + (left == 1 ? " line" : " lines") + " after this one weren't run.",
                                      theme::colour::warning, false } });
            break;
        }
    input.grabKeyboardFocus();
}

bool ConsolePanel::runLine (const juce::String& typed)
{
    using namespace theme;

    // a line pasted from the terminal: "lynx tempo get" → "tempo get"
    auto text = typed;
    if (text.equalsIgnoreCase ("lynx"))
        text = "help";
    else if (text.startsWithIgnoreCase ("lynx "))
        text = text.substring (5).trim();

    if (text == "clear")
    {
        if (! agent->isRunning())
            agent->reset(); // (a new conversation for the agent too)
        list->clear();
        layoutConversation (true);
        return true;
    }

    post (Kind::user, { { typed, colour::textPrimary } });

    auto* registry = CommandBridge::activeRegistry();
    if (registry == nullptr)
    {
        post (Kind::error, { { cross + " the command bridge isn't running", colour::textPrimary } });
        return false;
    }

    const auto parsed = parseCommandLine (text);
    if (parsed.error.isNotEmpty())
    {
        post (Kind::error, { { cross + " " + parsed.error, colour::textPrimary } });
        return false;
    }

    // help: "help", "tempo --help", "tempo set --help"
    if (parsed.name.isEmpty() || parsed.name == "help")
    {
        postHelp (registry->describe());
        return true;
    }
    if (parsed.help)
    {
        const auto group = parsed.name.upToFirstOccurrenceOf (".", false, false);
        if (! registry->exists (group) || (parsed.name.containsChar ('.') && ! registry->exists (parsed.name)))
        {
            post (Kind::error, { { cross + " unknown command \"" + parsed.name.replaceCharacter ('.', ' ') + "\" (type help)", colour::textPrimary } });
            return false;
        }
        postHelp (registry->describe (group), parsed.name.containsChar ('.') ? parsed.name : juce::String());
        return true;
    }

    // run it: straight into the registry, on this (the UI) thread
    const auto result = registry->run (parsed.name, parsed.args);
    if (! result.ok)
    {
        std::vector<Span> spans { { cross + " " + result.error, colour::textPrimary } };
        if (parsed.name.containsChar ('.') && registry->exists (parsed.name))
            spans.push_back ({ "\nsee: " + parsed.name.replaceCharacter ('.', ' ') + " --help", colour::textSecondary });
        post (Kind::error, std::move (spans));
        return false;
    }
    if (! parsed.name.containsChar ('.')) // a group alone: its help
        postHelp (result.value);
    else
        post (Kind::reply, { { juce::JSON::toString (result.value, juce::JSON::FormatOptions {}.withMaxDecimalPlaces (6)), colour::textPrimary } });
    return true;
}

void ConsolePanel::postHelp (const juce::var& description, const juce::String& onlyCommand)
{
    using namespace theme;
    std::vector<Span> spans;
    auto say = [&spans] (const juce::String& text, juce::Colour colour, bool mono = true, bool bold = false) {
        spans.push_back ({ text, colour, mono, bold });
    };

    auto group = [&] (const juce::var& g, bool detailed) {
        if (onlyCommand.isEmpty())
        {
            say (g["name"].toString(), colour::accent, true, true);
            say ("  " + g["description"].toString() + "\n", colour::textSecondary, false);
        }
        if (const auto* commands = g["commands"].getArray())
            for (const auto& c : *commands)
            {
                if (onlyCommand.isNotEmpty() && c["name"].toString() != onlyCommand)
                    continue;
                say ("  " + usage (c) + "\n", colour::textPrimary);
                say ("    " + c["description"].toString() + ((bool) c["changesProject"] ? "  " + pencil : juce::String()) + "\n",
                     colour::textSecondary, false);
                if (! detailed)
                    continue;
                if (const auto* args = c["args"].getArray())
                    for (const auto& a : *args)
                    {
                        say ("      " + flagText (a), colour::accent.withAlpha (0.85f));
                        say ("  " + a["description"].toString() + ((bool) a["required"] ? juce::String() : "  (optional)") + "\n",
                             colour::textSecondary, false);
                    }
                if (const auto example = c["example"].toString(); example.isNotEmpty())
                {
                    say ("    e.g.  ", colour::textDisabled, false);
                    say (example + "\n", colour::accent);
                }
                say ("\n", colour::textSecondary); // (a gap between commands)
            }
    };

    if (const auto* groups = description["groups"].getArray())
    {
        for (const auto& g : *groups)
        {
            group (g, false);
            say ("\n", colour::textSecondary);
        }
        say (pencil + " changes the project   " + dot + "   type a section's name + help for its details, e.g. project help",
             colour::textDisabled, false);
    }
    else
    {
        group (description, true);
        if (onlyCommand.isEmpty())
            say ("type a command + help for just that one, e.g. " + description["name"].toString() + " "
                     + description["commands"][0]["name"].toString().fromFirstOccurrenceOf (".", false, false).replaceCharacter ('.', ' ') + " help",
                 colour::textDisabled, false);
    }
    post (Kind::help, std::move (spans));
}
} // namespace lynx::commands
