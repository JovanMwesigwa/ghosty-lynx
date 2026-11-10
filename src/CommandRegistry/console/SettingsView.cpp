#include "CommandRegistry/console/SettingsView.h"

#include "CommandRegistry/console/Account.h"
#include "CommandRegistry/console/AiSettings.h"
#include "ui/Theme.h"

#include <cmath>

namespace lynx::commands
{
namespace
{
const juce::String bullet = juce::String::fromUTF8 ("\xe2\x80\xa2"); // •
const juce::Colour toggleOn { 0xff8fc97a };                          // (the switches' "on" green)

// the look: compact (the panel is often a narrow sidebar)
constexpr int side = 12, rowHeight = 44, footerHeight = 42, cardRadius = 8, fieldWidth = 200, fieldHeight = 28, headerBar = 36;
constexpr float titleSize = 12.5f, labelSize = 12.0f, smallSize = 11.0f;

juce::Font textFont (float size, bool bold = false) { return juce::Font (theme::font (size, bold)); }

// A pill switch: green with the knob right when on.
class ToggleSwitch final : public juce::Button
{
public:
    ToggleSwitch() : juce::Button ("toggle")
    {
        setClickingTogglesState (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().withSizeKeepingCentre (36.0f, 20.0f);
        const auto on = getToggleState();
        const auto alpha = isEnabled() ? 1.0f : 0.4f;
        g.setColour ((on ? (over ? toggleOn.brighter (0.1f) : toggleOn) : (over ? colour::bg5 : colour::bg4)).withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, r.getHeight() / 2.0f);
        const auto knob = juce::Rectangle<float> (15.0f, 15.0f).withCentre ({ on ? r.getRight() - 10.0f : r.getX() + 10.0f, r.getCentreY() });
        g.setColour (juce::Colours::white.withMultipliedAlpha (alpha));
        g.fillEllipse (knob);
    }
};

// A single-line field in the cards' look.
void styleField (juce::TextEditor& field, const juce::String& placeholder)
{
    using namespace theme;
    field.setFont (textFont (labelSize));
    field.setIndents (9, 6);
    field.setColour (juce::TextEditor::backgroundColourId, colour::bg1);
    field.setColour (juce::TextEditor::outlineColourId, colour::bg4);
    field.setColour (juce::TextEditor::focusedOutlineColourId, colour::accent.withAlpha (0.7f));
    field.setColour (juce::TextEditor::textColourId, colour::textPrimary);
    field.setColour (juce::CaretComponent::caretColourId, colour::accent);
    field.setTextToShowWhenEmpty (placeholder, colour::textDisabled);
    field.setSelectAllWhenFocused (true);
}

// A small flat text button ("Remove") or a filled one ("Save").
void styleButton (juce::TextButton& b, bool filled)
{
    using namespace theme;
    b.setColour (juce::TextButton::buttonColourId, filled ? colour::accent : juce::Colours::transparentBlack);
    b.setColour (juce::TextButton::buttonOnColourId, filled ? colour::accentPressed : colour::bg3);
    b.setColour (juce::TextButton::textColourOffId, filled ? juce::Colours::white : colour::textSecondary);
    b.setColour (juce::TextButton::textColourOnId, filled ? juce::Colours::white : colour::textPrimary);
    b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

// One provider's card: its title and description, then rows (a label, maybe a second line, and a control on the right),
// and a footer with Save.
class ProviderCard final : public juce::Component, private juce::Timer
{
public:
    explicit ProviderCard (AiProvider p) : provider (p)
    {
        const auto name = AiSettings::providerName (p);
        title = name + " API Key";
        description = p == AiProvider::anthropic
                          ? "Your Anthropic key, to use Claude in Ghosty. When it's on, the Claude models in the picker use it."
                          : p == AiProvider::openai ? "Your OpenAI key, to use OpenAI models in Ghosty."
                                                    : "Your Google AI Studio key, to use Google models in Ghosty.";

        // the key: saved on Save, Enter or leaving the field, then cleared (the field never shows it back)
        styleField (keyField, "Enter API key");
        keyField.setPasswordCharacter ((juce::juce_wchar) 0x2022);
        keyField.onReturnKey = [this] { saveKey(); };
        keyField.onFocusLost = [this] { saveKey(); };
        addAndMakeVisible (keyField);

        removeKey.setButtonText ("Remove");
        styleButton (removeKey, false);
        removeKey.onClick = [this] { AiSettings::get().removeKey (provider); };
        addChildComponent (removeKey);

        use.onClick = [this] { AiSettings::get().setEnabled (provider, use.getToggleState()); };
        addAndMakeVisible (use);

        if (p == AiProvider::google) // (OpenAI's models are listed from the API; Google's is typed)
        {
            styleField (model, "the model's id");
            model.onReturnKey = [this] { saveModel(); };
            model.onFocusLost = [this] { saveModel(); };
            addAndMakeVisible (model);
        }

        save.setButtonText ("Save");
        styleButton (save, true);
        save.onClick = [this] { saveAll(); };
        addAndMakeVisible (save);
        refresh();
    }

    // Follows AiSettings (and the field texts, unless one is being typed in).
    void refresh()
    {
        const auto& settings = AiSettings::get();
        const auto& s = settings.provider (provider);
        const auto saved = settings.hasKey (provider);
        keyField.setTextToShowWhenEmpty (saved ? juce::String::repeatedString (bullet, 18) : juce::String ("Enter API key"),
                                         saved ? theme::colour::textSecondary : theme::colour::textDisabled);
        removeKey.setVisible (saved);
        use.setToggleState (s.enabled, juce::dontSendNotification);
        use.setEnabled (saved);
        if (provider == AiProvider::google && ! model.hasKeyboardFocus (true))
            model.setText (s.model, false);
        rows = buildRows();
        resized();
        repaint();
    }

    // How tall it is at `width` (its description wraps).
    int heightFor (int width) { return headerHeight (width) + (int) rows.size() * rowHeight + footerHeight; }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto w = getWidth();
        g.setColour (colour::textPrimary);
        g.setFont (textFont (titleSize, true));
        g.drawText (title, 2, 0, w, 16, juce::Justification::centredLeft);
        descriptionLayout (w).draw (g, juce::Rectangle<float> (2.0f, 18.0f, (float) w - 4.0f, (float) headerHeight (w) - 18.0f));

        const auto card = cardArea().toFloat();
        g.setColour (colour::bg2);
        g.fillRoundedRectangle (card, (float) cardRadius);

        for (int i = 0; i < (int) rows.size(); ++i)
        {
            const auto row = rowArea (i);
            if (i > 0)
                divider (g, row.getY());
            const auto& r = rows[(std::size_t) i];
            auto text = r.control == &keyField ? row.withTrimmedLeft (14).withWidth (keyLabelWidth)
                                               : row.withTrimmedLeft (14).withTrimmedRight (fieldWidth + 30);
            g.setColour (colour::textPrimary);
            g.setFont (textFont (labelSize));
            if (r.subtitle.isEmpty())
                g.drawText (r.label, text, juce::Justification::centredLeft);
            else
            {
                g.drawText (r.label, text.removeFromTop (row.getHeight() / 2).withTrimmedTop (3), juce::Justification::bottomLeft);
                g.setColour (r.subtitleColour);
                g.setFont (textFont (smallSize));
                g.drawText (r.subtitle, text.withTrimmedTop (1), juce::Justification::topLeft);
            }
        }

        // the footer: Save, and a word on what happened
        const auto footer = footerArea();
        divider (g, footer.getY());
        g.setColour (savedFlash ? toggleOn : colour::textDisabled);
        g.setFont (textFont (smallSize));
        g.drawText (savedFlash ? juce::String::fromUTF8 ("Saved \xe2\x9c\x93") : juce::String ("Keys go to your Keychain"),
                    footer.withTrimmedLeft (14).withTrimmedRight (90), juce::Justification::centredLeft);
    }

    void resized() override
    {
        for (int i = 0; i < (int) rows.size(); ++i)
        {
            auto row = rowArea (i).reduced (14, 0);
            auto* control = rows[(std::size_t) i].control;
            if (control == &use)
                control->setBounds (row.removeFromRight (44).withSizeKeepingCentre (44, 26));
            else if (control == &keyField)
            {
                // the key: wide, from after its label (and Remove, when a key is saved) to the card's right edge
                row.removeFromLeft (keyLabelWidth);
                if (removeKey.isVisible())
                    removeKey.setBounds (row.removeFromLeft (62).withSizeKeepingCentre (58, fieldHeight - 4));
                keyField.setBounds (row.withSizeKeepingCentre (row.getWidth(), fieldHeight));
            }
            else if (control != nullptr)
            {
                const auto right = row.removeFromRight (juce::jmin (fieldWidth, row.getWidth() / 2)).withSizeKeepingCentre (juce::jmin (fieldWidth, row.getWidth() / 2), fieldHeight);
                control->setBounds (right);
            }
        }
        save.setBounds (footerArea().reduced (14, 0).removeFromRight (64).withSizeKeepingCentre (64, 26));
    }

private:
    struct Row
    {
        juce::String label, subtitle;
        juce::Colour subtitleColour;
        juce::Component* control = nullptr;
    };

    std::vector<Row> buildRows()
    {
        using namespace theme;
        const auto& settings = AiSettings::get();
        const auto name = AiSettings::providerName (provider);
        std::vector<Row> out;
        out.push_back ({ "API Key", {}, {}, &keyField });
        if (provider == AiProvider::google)
            out.push_back ({ "Model", "The model the picker offers", colour::textSecondary, &model });

        const auto status = errorText.isNotEmpty() ? errorText
                            : settings.hasKey (provider) ? juce::String ("Secret saved")
                                                         : juce::String ("No key saved yet");
        out.push_back ({ "Use " + name + " API Key", status, errorText.isNotEmpty() ? colour::record : colour::textSecondary, &use });
        return out;
    }

    // ---- saving
    bool saveKey()
    {
        const auto text = keyField.getText().trim();
        if (text.isEmpty())
            return false;
        keyField.clear(); // (it's never kept or shown again)
        errorText.clear();
        juce::String error;
        if (! AiSettings::get().setKey (provider, text, error))
        {
            errorText = "Couldn't save it: " + error;
            refresh();
            return false;
        }
        return true;
    }

    void saveModel()
    {
        if (model.getText().trim() != AiSettings::get().provider (provider).model)
            AiSettings::get().setModel (provider, model.getText());
    }

    // Save: whatever is typed in this card (the toggles save as they're flipped)
    void saveAll()
    {
        saveKey();
        if (provider == AiProvider::google)
            saveModel();
        if (errorText.isNotEmpty())
            return;
        savedFlash = true;
        repaint();
        startTimer (2000);
    }

    void timerCallback() override
    {
        stopTimer();
        savedFlash = false;
        repaint();
    }

    // ---- layout
    void divider (juce::Graphics& g, int y) const
    {
        g.setColour (theme::colour::bg4.withAlpha (0.6f));
        g.fillRect (14, y, getWidth() - 28, 1);
    }

    juce::TextLayout descriptionLayout (int width) const
    {
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::WordWrap::byWord);
        s.append (description, textFont (smallSize), theme::colour::textSecondary);
        juce::TextLayout layout;
        layout.createLayout (s, (float) juce::jmax (40, width - 4));
        return layout;
    }

    int headerHeight (int width) const { return 18 + (int) std::ceil (descriptionLayout (width).getHeight()) + 8; }
    juce::Rectangle<int> cardArea() const
    {
        return { 0, headerHeight (getWidth()), getWidth(), (int) rows.size() * rowHeight + footerHeight };
    }
    juce::Rectangle<int> rowArea (int i) const { return cardArea().withHeight (rowHeight).translated (0, i * rowHeight); }
    juce::Rectangle<int> footerArea() const { return cardArea().removeFromBottom (footerHeight); }

    AiProvider provider;
    juce::String title, description, errorText;
    juce::TextEditor keyField, model;
    juce::TextButton removeKey, save;
    ToggleSwitch use;
    static constexpr int keyLabelWidth = 72; // ("API Key")
    std::vector<Row> rows;
    bool savedFlash = false;
};
} // namespace

// ---- the scrolling content ------------------------------------------------------------------------------------------

// The LYNX account: signed out → "Sign in"; signing in → the code to approve in the browser; signed in → who.
class AccountCard final : public juce::Component, private juce::ChangeListener
{
public:
    std::function<void()> onHeightChanged; // (the code row comes and goes)

    AccountCard()
    {
        for (auto* b : { &primary, &secondary })
        {
            addChildComponent (*b);
            b->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }
        styleButton (primary, true);
        styleButton (secondary, false);
        primary.onClick = [this] {
            auto& account = Account::get();
            if (account.state() == Account::State::waiting)
                account.openPageAgain();
            else if (account.state() == Account::State::signedIn)
            {
                if (account.isPaid() || account.hasSubscription())
                    account.manageSubscription();
                else
                    showOffers();
            }
            else
                account.signIn();
        };
        secondary.onClick = [] {
            auto& account = Account::get();
            if (account.state() == Account::State::signedIn)
                account.signOut();
            else
                account.cancel();
        };
        Account::get().addChangeListener (this);
        refresh();
    }

    ~AccountCard() override { Account::get().removeChangeListener (this); }

    int heightFor (int width) const { return headerHeight (width) + rowCount() * rowHeight; }

    void refresh()
    {
        const auto& account = Account::get();
        const auto state = account.state();
        const auto manage = account.isPaid() || account.hasSubscription();
        const auto canUpgrade = account.billingAvailable() && ! account.upgrading() && ! account.offers().empty();
        primary.setVisible (state == Account::State::signedOut || state == Account::State::waiting
                            || (state == Account::State::signedIn && (manage || canUpgrade)));
        primary.setButtonText (state == Account::State::waiting ? "Open page"
                               : state == Account::State::signedIn ? (manage ? "Manage" : "Upgrade")
                                                                   : "Sign in");
        secondary.setVisible (state != Account::State::signedOut);
        secondary.setButtonText (state == Account::State::signedIn ? "Sign out" : "Cancel");
        resized();
        repaint();
        if (onHeightChanged)
            onHeightChanged();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto& account = Account::get();
        const auto w = getWidth();
        g.setColour (colour::textPrimary);
        g.setFont (textFont (titleSize, true));
        g.drawText ("Ghosty Account", 2, 0, w, 16, juce::Justification::centredLeft);
        descriptionLayout (w).draw (g, juce::Rectangle<float> (2.0f, 18.0f, (float) w - 4.0f, (float) headerHeight (w) - 18.0f));

        const auto card = juce::Rectangle<int> (0, headerHeight (w), w, rowCount() * rowHeight);
        g.setColour (colour::bg2);
        g.fillRoundedRectangle (card.toFloat(), (float) cardRadius);

        // the first row: where things stand
        juce::String label, subtitle;
        auto subtitleColour = colour::textSecondary;
        switch (account.state())
        {
            case Account::State::signedOut:
                label = "Not signed in";
                subtitle = account.error().isNotEmpty() ? account.error() : juce::String ("Sign in with your browser to use Ghosty.");
                subtitleColour = account.error().isNotEmpty() ? colour::record : colour::textSecondary;
                break;
            case Account::State::starting:
                label = "Starting the sign-in...";
                break;
            case Account::State::waiting:
                label = "Approve this code in your browser";
                subtitle = "The page opened in your browser (Open page shows it again)";
                break;
            case Account::State::signedIn:
                label = account.name().isNotEmpty() ? account.name() : juce::String ("Signed in");
                subtitle = account.error().isNotEmpty() ? account.error()
                           : account.upgrading()        ? juce::String ("Finish the payment in your browser...")
                                                        : account.email()
                                                              + (account.planText().isNotEmpty() ? juce::String::fromUTF8 (" \xc2\xb7 ") + account.planText()
                                                                                                 : juce::String());
                subtitleColour = account.error().isNotEmpty() ? colour::warning : colour::textSecondary;
                break;
        }
        auto row = card.withHeight (rowHeight).withTrimmedLeft (14).withTrimmedRight (buttonsWidth() + 20);
        g.setColour (colour::textPrimary);
        g.setFont (textFont (labelSize));
        if (subtitle.isEmpty())
            g.drawText (label, row, juce::Justification::centredLeft);
        else
        {
            g.drawText (label, row.removeFromTop (rowHeight / 2).withTrimmedTop (3), juce::Justification::bottomLeft);
            g.setColour (subtitleColour);
            g.setFont (textFont (smallSize));
            g.drawText (subtitle, row.withTrimmedTop (1), juce::Justification::topLeft, true);
        }

        // waiting: the code, big
        if (account.state() == Account::State::waiting)
        {
            const auto codeRow = card.withTrimmedTop (rowHeight).withHeight (rowHeight);
            g.setColour (colour::bg4.withAlpha (0.6f));
            g.fillRect (14, codeRow.getY(), w - 28, 1);
            auto code = account.code();
            if (code.length() == 8)
                code = code.substring (0, 4) + "-" + code.substring (4); // (easier to read: ABCD-EFGH)
            g.setColour (colour::textPrimary);
            g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 20.0f, juce::Font::bold)));
            g.drawText (code, codeRow.withTrimmedLeft (14), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto row = juce::Rectangle<int> (0, headerHeight (getWidth()), getWidth(), rowHeight).reduced (14, 0);
        if (secondary.isVisible())
        {
            secondary.setBounds (row.removeFromRight (secondaryWidth()).withSizeKeepingCentre (secondaryWidth(), 26));
            row.removeFromRight (6);
        }
        if (primary.isVisible())
            primary.setBounds (row.removeFromRight (78).withSizeKeepingCentre (78, 26));
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

    // Upgrade: the plan, monthly or yearly
    void showOffers()
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Launch plan");
        for (const auto& offer : Account::get().offers())
            menu.addItem ((offer.interval == "year" ? "Yearly: " : "Monthly: ") + offer.label,
                          [interval = offer.interval] { Account::get().upgrade (interval); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&primary));
    }

    int rowCount() const { return Account::get().state() == Account::State::waiting ? 2 : 1; }
    int secondaryWidth() const { return 68; }
    int buttonsWidth() const { return (primary.isVisible() ? 78 + 6 : 0) + (secondary.isVisible() ? secondaryWidth() : 0); }

    juce::TextLayout descriptionLayout (int width) const
    {
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::WordWrap::byWord);
        s.append ("Your Ghosty account and plan. You sign in in your browser; LYNX keeps the sign-in in your Keychain.",
                  textFont (smallSize), theme::colour::textSecondary);
        juce::TextLayout layout;
        layout.createLayout (s, (float) juce::jmax (40, width - 4));
        return layout;
    }
    int headerHeight (int width) const { return 18 + (int) std::ceil (descriptionLayout (width).getHeight()) + 8; }

    juce::TextButton primary, secondary;
};
class SettingsView::Content final : public juce::Component
{
public:
    Content()
    {
        addAndMakeVisible (account);
        account.onHeightChanged = [this] { layoutFor (getWidth()); };
        for (auto p : { AiProvider::anthropic, AiProvider::openai, AiProvider::google })
            addAndMakeVisible (cards.add (new ProviderCard (p)));
    }

    void refresh()
    {
        for (auto* c : cards)
            c->refresh();
        layoutFor (getWidth());
    }

    void layoutFor (int width)
    {
        auto y = 8;
        accountHeader = { side, y, width - 2 * side, 24 };
        y += 30;
        {
            const auto w = width - 2 * side - 8;
            const auto h = account.heightFor (w);
            account.setBounds (side + 4, y, w, h);
            y += h + 18;
        }
        sectionHeader = { side, y, width - 2 * side, 24 };
        y += 30;
        for (auto* c : cards)
        {
            c->setVisible (expanded);
            if (! expanded)
                continue;
            const auto w = width - 2 * side - 8;
            const auto h = c->heightFor (w);
            c->setBounds (side + 4, y, w, h);
            y += h + 14;
        }
        setSize (width, y + 8);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        // "⌄ API Keys" (click folds it)
        juce::Path chevron;
        const auto c = juce::Point<float> ((float) sectionHeader.getX() + 6.0f, (float) sectionHeader.getCentreY());
        if (expanded)
        {
            chevron.startNewSubPath (c.x - 4.0f, c.y - 2.0f);
            chevron.lineTo (c.x, c.y + 2.0f);
            chevron.lineTo (c.x + 4.0f, c.y - 2.0f);
        }
        else
        {
            chevron.startNewSubPath (c.x - 2.0f, c.y - 4.0f);
            chevron.lineTo (c.x + 2.0f, c.y);
            chevron.lineTo (c.x - 2.0f, c.y + 4.0f);
        }
        g.setColour (colour::textSecondary);
        g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (colour::textPrimary);
        g.setFont (textFont (13.0f, true));
        g.drawText ("API Keys", sectionHeader.withTrimmedLeft (18), juce::Justification::centredLeft);
        g.drawText ("Account", accountHeader.withTrimmedLeft (18), juce::Justification::centredLeft);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (sectionHeader.contains (e.getPosition()))
        {
            expanded = ! expanded;
            layoutFor (getWidth());
        }
    }

private:
    AccountCard account;
    juce::OwnedArray<ProviderCard> cards;
    juce::Rectangle<int> sectionHeader, accountHeader;
    bool expanded = true;
};

// ---- the view -------------------------------------------------------------------------------------------------------

SettingsView::SettingsView() : content (std::make_unique<Content>())
{
    using namespace theme;
    back.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xb9 Chat")); // ‹ Chat
    styleButton (back, false);
    back.onClick = [this] {
        if (onBack)
            onBack();
    };
    addAndMakeVisible (back);

    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (6);
    addAndMakeVisible (viewport);

    AiSettings::get().addChangeListener (this);
}

SettingsView::~SettingsView()
{
    AiSettings::get().removeChangeListener (this);
    viewport.setViewedComponent (nullptr, false);
}

void SettingsView::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::bg1);
    g.setColour (colour::textPrimary);
    g.setFont (textFont (14.0f, true));
    g.drawText ("Settings", getLocalBounds().removeFromTop (headerBar).withTrimmedLeft (side + 58), juce::Justification::centredLeft);
    g.setColour (colour::bg4.withAlpha (0.6f));
    g.fillRect (0, headerBar, getWidth(), 1);
}

void SettingsView::resized()
{
    auto area = getLocalBounds();
    auto header = area.removeFromTop (headerBar);
    back.setBounds (header.removeFromLeft (side + 54).reduced (6, 6));
    area.removeFromTop (1);
    viewport.setBounds (area);
    content->layoutFor (viewport.getMaximumVisibleWidth());
}

void SettingsView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    content->refresh();
}
} // namespace lynx::commands
