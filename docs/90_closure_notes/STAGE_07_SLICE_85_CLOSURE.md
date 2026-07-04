# Stage 07 · Slice 85 — Assistant console (chat panel redesign)

## Goal (user direction)
The bottom-dock Chat tab looked like a generic consumer AI app (sparkle icon,
"Dolphin AI", left/right message bubbles, centered "Ask me anything" splash
with suggestion chips, round ↑ send button). Redesign it to suit a
professional survey workstation.

## Design
The pane now reads as a **query console** in the same visual family as its
dock neighbours (Problems / Output / Jobs / Terminal):

- **Header strip** like `#terminalHeader`: "Assistant" + a bordered mono
  `on-device` badge (tooltip explains the local-Ollama privacy story), flat
  borderless model combo, flat "Clear" text button. No sparkle icon.
- **Transcript, not bubbles**: queries are monospace log lines with an accent
  `›` prompt glyph and a right-aligned mono timestamp; answers are full-width
  proportional-font blocks set off by a 2px accent left rule. Text in both is
  mouse-selectable. Full-width like the Output pane — no width caps.
- **Banner state** instead of a chat splash: top-left console MOTD
  ("SURVEY ASSISTANT" + on-device note) with four `›`-prefixed flat mono
  suggestion lines (hover → accent). Replaces the centered icon + chip grid.
- **Input row** like `#terminalInputRow`: accent `›` prompt, borderless
  transparent mono input (auto-grows 1→3 lines), flat accent "Send" text
  button. Enter sends, Shift+Enter newline (now in the placeholder, replacing
  the hint label row).

## Implementation notes
- `PanelChatWidget.cpp` rebuilt; the Ollama backend
  (`PanelChatWidget.Ollama.cpp`) is untouched — it only uses
  `appendMessage`, `m_stream_label`, `m_stream_row`, whose contracts are
  preserved (`makeAnswerRow` helper feeds both paths).
- The constructor greeting bubble was removed. It had made the entire empty
  state DEAD CODE since the panel shipped (first `appendMessage` cleared the
  empty state before it was ever seen) — the banner is now the greeting.
- QSS: the `panelChat*` section in AppStyleShell.cpp replaced wholesale;
  removed dead ids (Icon, NewBtn, Sep, InputBox, Hint, Chip, EmptyIcon/Title/
  Sub). The old right-panel `convBubble*` styles remain (ChatPanel.cpp).
- Layout lesson: a max-width-capped label inside a stretch cell gets
  *centered* by Qt layouts (alignment 0 = expand; can't expand → centered).
  First cut capped text at 720px and both message kinds drifted to center /
  wrapped at sizeHint. Full-width (console convention) avoids the trap.

## Verification
Build green; 16/16 tests. Rendered via in-app widget grab (the VM display
can't screenshot the GL-covered app) in BOTH themes: dark and light show the
banner, mono query rows with timestamps, accent-ruled answers, streaming
cursor, and the prompt input correctly.
