# WebUI Improvement Task

## Goal
Improve the WebUI by:
1. Moving terminal and chat tabs under the left panel (sidebar)
2. Adding mobile view support
3. Beautifying the chat window and overall WebUI

## Current Architecture Understanding

### Layout Structure:
- **Layout Context** (`/context/layout.tsx`): Manages layout state (sidebar width, terminal height, session tabs, views)
- **Layout Page** (`/pages/layout.tsx`): Main layout with sidebar shell, workspace sidebar, project sidebar, session tabs
- **Sidebar Shell** (`/pages/layout/sidebar-shell.tsx`): Left sidebar rail with project/workspace navigation
- **Session Page** (`/pages/session.tsx`): Main session view with chat surface, terminal panel, side panel
- **Session Side Panel** (`/pages/session/session-side-panel.tsx`): Right side panel with tabs (files, tasks, stats, notes, intelgraph, logs, agents, review)
- **Chat Surface** (`/pages/session/surface-tabs/chat-surface.tsx`): Chat interface with MessageTimeline and Composer
- **Terminal Panel** (`/pages/session/terminal-panel.tsx`): Terminal tabs panel at bottom of session

### Current Layout Issues:
1. Terminal tabs and Chat tabs are at the bottom of the session view (bottom panel)
2. Chat tabs and Terminal tabs should be moved to left panel (sidebar)
3. No mobile view support
4. Chat window needs beautification

## Tasks

### Phase 1: Understanding & Planning
- [x] Understand current layout architecture
- [x] Identify where terminal tabs and chat tabs are rendered
- [x] Identify sidebar structure
- [ ] Plan new layout structure
- [ ] Plan mobile responsive breakpoints

### Phase 2: Move Terminal and Chat Tabs to Left Panel
- [ ] Modify sidebar-shell.tsx to add terminal/chat tab sections
- [ ] Modify session-side-panel.tsx to remove terminal/chat from right panel
- [ ] Update layout context for new tab state management
- [ ] Update session.tsx to use new layout

### Phase 3: Mobile View Support
- [ ] Add mobile breakpoints in breakpoint context
- [ ] Create mobile layout variants
- [ ] Add mobile sidebar toggle
- [ ] Add mobile bottom navigation for tabs
- [ ] Add responsive CSS for chat and terminal

### Phase 4: Chat Window Beautification
- [ ] Improve MessageTimeline styling
- [ ] Improve SessionComposerRegion styling
- [ ] Add better message bubbles/styling
- [ ] Add better code block rendering
- [ ] Improve composer input styling
- [ ] Add animations/transitions

### Phase 5: General WebUI Beautification
- [ ] Improve sidebar styling
- [ ] Improve terminal panel styling
- [ ] Add consistent spacing/typography
- [ ] Add animations/transitions
- [ ] Improve dark/light theme support

### Phase 6: Testing & Polish
- [ ] Test desktop layout
- [ ] Test mobile layout
- [ ] Verify all tabs work correctly
- [ ] Polish animations and transitions

## Files to Modify
1. `/src/surface/web/official/packages/app/src/pages/layout/sidebar-shell.tsx` - Add terminal/chat tabs to sidebar
2. `/src/surface/web/official/packages/app/src/pages/session/session-side-panel.tsx` - Remove terminal/chat tabs from right panel
3. `/src/surface/web/official/packages/app/src/pages/session.tsx` - Update session layout
4. `/src/surface/web/official/packages/app/src/context/layout.tsx` - Update layout context for new tabs
5. `/src/surface/web/official/packages/app/src/pages/session/surface-tabs/chat-surface.tsx` - Beautify chat
6. `/src/surface/web/official/packages/app/src/pages/session/message-timeline.tsx` - Beautify messages
7. `/src/surface/web/official/packages/app/src/pages/session/composer.tsx` - Beautify composer
8. `/src/surface/web/official/packages/app/src/pages/session/terminal-panel.tsx` - Move to sidebar
9. `/src/surface/web/official/packages/app/src/context/breakpoint.tsx` - Add mobile breakpoints
10. CSS files for styling improvements
