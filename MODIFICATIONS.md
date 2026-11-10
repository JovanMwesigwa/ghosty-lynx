# Modifications

This is **Ghosty LYNX**, a modified version of **LYNX SHARE** (https://github.com/Ka1-S/LYNX-SHARE),
distributed under the same license, the GNU Affero General Public License v3.0 (see `LICENSE`).
The original copyright notices are kept. Modified by Jovan Mwesigwa, starting November 2026.

## What changed

- **Ghosty**, an AI music assistant in a new panel (Cmd/Ctrl+J): a chat where you describe what you want and
  Ghosty makes it in your project. The panel signs in to a Ghosty account (in the browser), shows the plan, and talks
  to Ghosty's server (https://api.officialghosty.app), which runs the AI and asks LYNX which commands to run.
  (`src/CommandRegistry/console/`)
- **A command bridge**: LYNX's features as commands (tempo, project, patterns, channels and plugin knobs/presets,
  playlist, transport, mixer and effects, audio settings, account), usable from the Ghosty panel, by the AI, and over
  a local HTTP port (127.0.0.1:7878) for tools. (`src/CommandRegistry/`)
- Smaller changes to support them: the panel in the workspace and the shortcuts, channel rack helpers, plugin preset
  listing, a Ghosty logo and icon (`src/ui/GhostyLogo.*`), and the translations of the new strings.

The Ghosty server is a separate program and is not part of this repository.
