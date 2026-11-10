#include "CommandRegistry/console/SignInView.h"

#include "CommandRegistry/console/Account.h"
#include "ui/GhostyLogo.h"
#include "ui/Theme.h"

namespace lynx::commands
{
namespace
{
juce::Font textFont (float size, bool bold = false) { return juce::Font (theme::font (size, bold)); }

void styleButton (juce::TextButton& b, bool filled)
{
    using namespace theme;
    b.setColour (juce::TextButton::buttonColourId, filled ? colour::accent : colour::bg2);
    b.setColour (juce::TextButton::buttonOnColourId, filled ? colour::accentPressed : colour::bg3);
    b.setColour (juce::TextButton::textColourOffId, filled ? juce::Colours::white : colour::textPrimary);
    b.setColour (juce::TextButton::textColourOnId, filled ? juce::Colours::white : colour::textPrimary);
    b.setColour (juce::ComboBox::outlineColourId, colour::bg4);
    b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

// one centred line of text (wraps to two)
void line (juce::Graphics& g, const juce::String& text, juce::Rectangle<int>& area, int height, juce::Font font, juce::Colour colour)
{
    g.setColour (colour);
    g.setFont (font);
    g.drawFittedText (text, area.removeFromTop (height), juce::Justification::centred, 2);
}
} // namespace

SignInView::SignInView()
{
    create.setButtonText ("Create account");
    signIn.setButtonText ("Sign in");
    openAgain.setButtonText ("Open the page again");
    cancel.setButtonText ("Cancel");
    styleButton (create, true);
    styleButton (signIn, false);
    styleButton (openAgain, false);
    styleButton (cancel, false);
    create.onClick = [] { Account::get().signIn (true); };
    signIn.onClick = [] { Account::get().signIn (false); };
    openAgain.onClick = [] { Account::get().openPageAgain(); };
    cancel.onClick = [] { Account::get().cancel(); };
    for (auto* b : { &create, &signIn, &openAgain, &cancel })
        addChildComponent (*b);

    Account::get().addChangeListener (this);
    refresh();
}

SignInView::~SignInView()
{
    Account::get().removeChangeListener (this);
}

void SignInView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();
}

void SignInView::refresh()
{
    const auto state = Account::get().state();
    const auto out = state == Account::State::signedOut;
    create.setVisible (out);
    signIn.setVisible (out);
    openAgain.setVisible (state == Account::State::waiting);
    cancel.setVisible (state == Account::State::starting || state == Account::State::waiting);
    resized();
    repaint();
}

void SignInView::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::bg1);
    const auto& account = Account::get();
    auto area = content;

    // Ghosty's logo
    ghosty::draw (g, area.removeFromTop (44).withSizeKeepingCentre (44, 44).toFloat(), colour::textPrimary);
    area.removeFromTop (14);

    switch (account.state())
    {
        case Account::State::signedOut:
        case Account::State::signedIn:
            line (g, "Welcome to Ghosty", area, 26, textFont (type::title + 4.0f, true), colour::textPrimary);
            line (g, "Sign in or create an account to start making music with Ghosty, LYNX's AI.", area, 40, textFont (type::body), colour::textSecondary);
            if (account.error().isNotEmpty())
            {
                area.removeFromTop (54); // (the buttons)
                line (g, account.error(), area, 36, textFont (type::small), colour::record);
            }
            break;

        case Account::State::starting:
            line (g, "Opening your browser...", area, 26, textFont (type::title + 2.0f, true), colour::textPrimary);
            break;

        case Account::State::waiting:
        {
            line (g, "Finish in your browser", area, 26, textFont (type::title + 2.0f, true), colour::textPrimary);
            line (g, "Sign in or create your account there, then approve this code:", area, 38, textFont (type::body), colour::textSecondary);
            area.removeFromTop (8);
            auto code = account.code();
            if (code.length() == 8)
                code = code.substring (0, 4) + "-" + code.substring (4); // (ABCD-EFGH)
            const auto box = area.removeFromTop (52).withSizeKeepingCentre (juce::jmin (area.getWidth(), 220), 52).toFloat();
            g.setColour (colour::bg2);
            g.fillRoundedRectangle (box, 8.0f);
            g.setColour (colour::bg4);
            g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
            g.setColour (colour::textPrimary);
            g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 24.0f, juce::Font::bold)));
            g.drawText (code, box, juce::Justification::centred);
            break;
        }
    }

    // the footer: why the browser
    auto footer = getLocalBounds().removeFromBottom (44).reduced (16, 0);
    g.setColour (colour::textDisabled);
    g.setFont (textFont (type::small));
    g.drawFittedText ("You sign in in your browser: Ghosty never sees your password.", footer, juce::Justification::centred, 2);
}

void SignInView::resized()
{
    // a centred column, a little above the middle
    const auto width = juce::jmin (getWidth() - 32, 340);
    const auto height = 260;
    content = juce::Rectangle<int> ((getWidth() - width) / 2, juce::jmax (16, getHeight() / 2 - height / 2 - 30), width, height);

    const auto state = Account::get().state();
    auto buttons = content.withTrimmedTop (44 + 14 + 26 + 40 + 6).withHeight (34);
    if (state == Account::State::waiting)
        buttons = content.withTrimmedTop (44 + 14 + 26 + 38 + 8 + 52 + 16).withHeight (34);
    else if (state == Account::State::starting)
        buttons = content.withTrimmedTop (44 + 14 + 26 + 12).withHeight (34);

    auto pair = [] (juce::Rectangle<int> row, juce::Button& left, juce::Button& right, int leftWidth, int rightWidth) {
        const auto total = leftWidth + 8 + rightWidth;
        auto r = row.withSizeKeepingCentre (total, row.getHeight());
        left.setBounds (r.removeFromLeft (leftWidth));
        r.removeFromLeft (8);
        right.setBounds (r);
    };
    if (state == Account::State::signedOut || state == Account::State::signedIn)
        pair (buttons, create, signIn, 140, 100);
    else if (state == Account::State::waiting)
        pair (buttons, openAgain, cancel, 160, 80);
    else
        cancel.setBounds (buttons.withSizeKeepingCentre (90, buttons.getHeight()));
}
} // namespace lynx::commands
