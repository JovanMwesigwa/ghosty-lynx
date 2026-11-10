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

// a quiet text link ("Compare plans", "Sign out")
void styleLink (juce::TextButton& b)
{
    using namespace theme;
    b.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    b.setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
    b.setColour (juce::TextButton::textColourOffId, colour::textSecondary);
    b.setColour (juce::TextButton::textColourOnId, colour::textPrimary);
    b.setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    b.setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

// centred text (wraps to `lines`); returns the height used
int line (juce::Graphics& g, const juce::String& text, juce::Rectangle<int>& area, int height, juce::Font font, juce::Colour colour, int lines = 2)
{
    g.setColour (colour);
    g.setFont (font);
    g.drawFittedText (text, area.removeFromTop (height), juce::Justification::centred, lines);
    return height;
}
} // namespace

SignInView::Screen SignInView::screen()
{
    const auto& a = Account::get();
    switch (a.state())
    {
        case Account::State::signedOut: return Screen::signIn;
        case Account::State::starting: return Screen::starting;
        case Account::State::waiting: return Screen::waiting;
        case Account::State::signedIn: break;
    }
    if (a.upgrading())
        return Screen::paying;
    return a.planKnown() ? Screen::plan : Screen::checking;
}

SignInView::SignInView()
{
    create.setButtonText ("Create account");
    signIn.setButtonText ("Sign in");
    openAgain.setButtonText ("Open the page again");
    cancel.setButtonText ("Cancel");
    retry.setButtonText ("Try again");
    seePlans.setButtonText ("See plans");
    signOut.setButtonText ("Sign out");
    checkNow.setButtonText ("I've paid: check now");
    stopWaiting.setButtonText ("Stop waiting");
    for (auto* b : { &create, &seePlans, &checkNow })
        styleButton (*b, true);
    for (auto* b : { &signIn, &openAgain, &cancel, &retry, &stopWaiting })
        styleButton (*b, false);
    styleLink (signOut);

    create.onClick = [] { Account::get().signIn (true); };
    signIn.onClick = [] { Account::get().signIn (false); };
    openAgain.onClick = [] { Account::get().openPageAgain(); };
    cancel.onClick = [] { Account::get().cancel(); };
    retry.onClick = [] { Account::get().refresh(); };
    seePlans.onClick = [] { Account::get().openUpgradePage(); };
    signOut.onClick = [] { Account::get().signOut(); };
    checkNow.onClick = [] { Account::get().checkPlan(); };
    stopWaiting.onClick = [] { Account::get().cancelUpgrade(); };
    for (auto* b : { &create, &signIn, &openAgain, &cancel, &retry, &seePlans, &signOut, &checkNow, &stopWaiting })
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
    const auto s = screen();
    const auto& a = Account::get();
    create.setVisible (s == Screen::signIn);
    signIn.setVisible (s == Screen::signIn);
    openAgain.setVisible (s == Screen::waiting);
    cancel.setVisible (s == Screen::starting || s == Screen::waiting);
    retry.setVisible (s == Screen::checking && a.error().isNotEmpty());

    seePlans.setVisible (s == Screen::plan && a.billingAvailable());
    signOut.setVisible (a.state() == Account::State::signedIn);
    retry.setVisible (retry.isVisible() || (s == Screen::plan && ! a.billingAvailable()));
    checkNow.setVisible (s == Screen::paying);
    stopWaiting.setVisible (s == Screen::paying);
    resized();
    repaint();
}

void SignInView::paint (juce::Graphics& g)
{
    using namespace theme;
    g.fillAll (colour::bg1);
    const auto& account = Account::get();
    auto area = content;
    const auto title = textFont (type::title + 4.0f, true), body = textFont (type::body);

    // Ghosty's logo
    ghosty::draw (g, area.removeFromTop (44).withSizeKeepingCentre (44, 44).toFloat(), colour::textPrimary);
    area.removeFromTop (14);

    auto error = [&] {
        if (account.error().isNotEmpty())
            line (g, account.error(), area, 36, textFont (type::small), colour::record);
    };

    switch (screen())
    {
        case Screen::signIn:
            line (g, "Welcome to Ghosty", area, 26, title, colour::textPrimary);
            line (g, "Sign in or create an account to start making music with Ghosty, LYNX's AI.", area, 40, body, colour::textSecondary);
            area.removeFromTop (54);
            error();
            break;

        case Screen::starting:
            line (g, "Opening your browser...", area, 26, title, colour::textPrimary);
            break;

        case Screen::waiting:
        {
            line (g, "Finish in your browser", area, 26, title, colour::textPrimary);
            line (g, "Sign in or create your account there, then approve this code:", area, 38, body, colour::textSecondary);
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

        case Screen::checking:
            line (g, "Checking your plan...", area, 26, title, colour::textPrimary);
            line (g, account.email(), area, 22, body, colour::textSecondary, 1);
            area.removeFromTop (54);
            error();
            break;

        case Screen::plan:
        {
            const auto trial = account.trialDays();
            line (g, trial > 0 ? "Start your " + juce::String (trial) + "-day free trial" : juce::String ("Upgrade to Launch"), area, 26, title,
                  colour::textPrimary);
            line (g, "Ghosty is part of the Launch plan.", area, 24, body, colour::textSecondary, 1);
            area.removeFromTop (8 + 34 + 12); // (See plans)
            if (! account.billingAvailable())
                line (g, "Plans aren't available right now. Try again in a moment.", area, 36, textFont (type::small), colour::warning);
            error();
            break;
        }

        case Screen::paying:
            line (g, "Finish the checkout in your browser", area, 26, title, colour::textPrimary);
            line (g, "Ghosty opens here by itself as soon as your plan starts.", area, 40, body, colour::textSecondary);
            area.removeFromTop (54);
            error();
            break;
    }

    // the footer: who's signed in (then the small "Sign out" link, a child), or why the browser
    g.setColour (colour::textDisabled);
    g.setFont (textFont (type::small));
    if (account.state() == Account::State::signedIn)
        g.drawText (footerText(), footerTextArea, juce::Justification::centredRight);
    else
        g.drawFittedText ("You sign in in your browser: Ghosty never sees your password.", getLocalBounds().removeFromBottom (44).reduced (16, 0),
                          juce::Justification::centred, 2);
}

void SignInView::resized()
{
    // a centred column, a little above the middle
    const auto width = juce::jmin (getWidth() - 32, 360);
    const auto height = 300;
    content = juce::Rectangle<int> ((getWidth() - width) / 2, juce::jmax (16, getHeight() / 2 - height / 2 - 30), width, height);

    // where each screen's text ends (the same heights paint uses: the logo 44 + 14, then the title and the lines)
    const auto s = screen();
    const auto text = s == Screen::signIn || s == Screen::paying ? 26 + 40 + 6
                    : s == Screen::starting                      ? 26 + 12
                    : s == Screen::waiting                       ? 26 + 38 + 8 + 52 + 16
                    : s == Screen::checking                      ? 26 + 22 + 10
                                                                 : 26 + 24 + 8; // (plan)
    const auto top = content.getY() + 44 + 14 + text;
    auto row = [&] (int y, std::initializer_list<std::pair<juce::Button*, int>> buttons) {
        int total = 0;
        for (const auto& [b, w] : buttons)
            total += w + (total > 0 ? 8 : 0);
        auto r = juce::Rectangle<int> (0, y, total, 34).withCentre ({ content.getCentreX(), y + 17 });
        for (const auto& [b, w] : buttons)
        {
            b->setBounds (r.removeFromLeft (w));
            r.removeFromLeft (8);
        }
    };

    switch (s)
    {
        case Screen::signIn: row (top, { { &create, 140 }, { &signIn, 100 } }); break;
        case Screen::starting: row (top, { { &cancel, 90 } }); break;
        case Screen::waiting: row (top, { { &openAgain, 160 }, { &cancel, 80 } }); break;
        case Screen::checking: row (top, { { &retry, 100 } }); break;
        case Screen::paying: row (top, { { &checkNow, 170 }, { &stopWaiting, 110 } }); break;
        case Screen::plan:
            if (seePlans.isVisible())
                row (top, { { &seePlans, 140 } });
            else if (retry.isVisible())
                row (top, { { &retry, 100 } });
            break;
    }

    // the footer line: "Signed in as you@example.com  ·  " then the Sign out link, centred together
    const auto font = textFont (theme::type::small);
    const auto textWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, footerText())) + 2;
    const auto linkWidth = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font, "Sign out")) + 8;
    auto footer = getLocalBounds().removeFromBottom (44).withSizeKeepingCentre (juce::jmin (getWidth() - 32, textWidth + linkWidth), 22);
    footerTextArea = footer.removeFromLeft (juce::jmax (0, footer.getWidth() - linkWidth));
    signOut.setBounds (footer);
}

juce::String SignInView::footerText()
{
    return "Signed in as " + Account::get().email() + "  " + juce::String::fromUTF8 ("\xc2\xb7");
}
} // namespace lynx::commands
