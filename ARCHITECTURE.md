# daia – Architecture Plan (rev 2)

> **Status: draft / planning** — nothing below has been implemented yet.  
> The goal is to agree on the overall structure before writing any code.
>
> **Change from rev 1:** Panes are no longer hardcoded as video panels.  
> Each pane has a swappable **role** (video, global timeline, playlist, config, …).

---

## Concept

`daia` is a **pane-based video player**, similar in spirit to tmux or VS Code's editor groups.  
The screen is divided into rectangular panes.  Panes can be attached and detached at any time.

Each pane can be assigned one of several **roles** that determine what it renders and how it handles input:

| Role | Description |
|---|---|
| **Video** | Decodes and displays a single video clip; can run in Synced or Independent play mode. |
| **GlobalTimeline** | Shows the shared scrubber / transport bar for synced playback. |
| **Playlist** | *(future)* Browses and queues video files. |
| **Config** | *(future)* Settings and preferences UI. |

A pane's role can be changed at any time without destroying the pane or its viewport.

---

## Proposed Class Hierarchy

```
App
 ├── Window                      (GLFW, already exists)
 ├── Pipeline                    (Vulkan, already exists)
 └── PaneManager                 (new – owns everything below)
      ├── GlobalTimeline          (new – shared timeline for synced video panes)
      └── Pane[]                  (new – one per screen region)
           └── PaneContent        (new – swappable role, one of:)
                ├── VideoContent        role = Video
                ├── TimelineContent     role = GlobalTimeline
                ├── PlaylistContent     role = Playlist  (future)
                └── ConfigContent       role = Config    (future)
```

---

## Class Sketches

### `PaneRole` (enum)

```
enum class PaneRole { Video, GlobalTimeline, Playlist, Config };
```

New roles can be added here as the application grows.

---

### `PaneContent` (abstract interface)

The base that every concrete role implements.

| Method | Description |
|---|---|
| `role()` | Returns the `PaneRole` tag of this content. |
| `tick(Δt)` | Per-frame logic (advance timelines, poll decoders, …). |
| `render(ctx)` | Emits draw commands for this pane's viewport. |
| `onAttach(GlobalTimeline&)` | Called when the content is placed into a pane; lets it subscribe to shared state. |
| `onDetach()` | Called before the content is replaced or the pane is removed; cleans up subscriptions. |

`Pane::setContent(unique_ptr<PaneContent>)` calls `onDetach` on the old content and `onAttach` on the new one, then stores the new content.

> **Implementation note (to agree on):** The interface can be realised either as a **virtual base class** (`unique_ptr<PaneContent>`) or a **`std::variant`** of the concrete types.  The virtual approach is more open to future roles; the variant avoids heap allocation and virtual dispatch on the hot path but requires recompiling when a new role is added.

---

### `VideoContent` (role = Video)

Holds everything needed to play one video clip.

| Member | Type | Description |
|---|---|---|
| `source` | `optional<VideoSource>` | metadata of the loaded clip |
| `playMode` | `PlayMode` | `Synced` or `Independent` |
| `localTimeline` | `LocalTimeline` | always present; active only in Independent mode |
| `globalTimeline*` | `GlobalTimeline*` | borrowed reference; non-null while attached |

Key operations:
- `loadVideo(VideoSource)` — stores metadata, updates local timeline duration.
- `setPlayMode(PlayMode)` — switches between Synced and Independent.  Subscribes to / unsubscribes from `GlobalTimeline` accordingly.  Carries current position across so there is no jump.
- `tick(Δt)` — advances `LocalTimeline` in Independent mode; no-op in Synced mode.
- `currentPlaybackState()` — delegates to the active timeline.

Lifecycle:
- `onAttach` → subscribes to `GlobalTimeline` (default: Synced).
- `onDetach` → unsubscribes from `GlobalTimeline`.

---

### `TimelineContent` (role = GlobalTimeline)

Renders the shared transport bar in a dedicated pane.

| Member | Type | Description |
|---|---|---|
| `globalTimeline*` | `GlobalTimeline*` | borrowed reference; non-null while attached |

Key operations:
- `tick(Δt)` — no-op; `GlobalTimeline` is ticked centrally by `PaneManager`.
- `render(ctx)` — draws the scrubber, play/pause, speed, loop controls.
- Input handling — seeks `GlobalTimeline`, toggles play/pause, adjusts speed.

---

### `PlaylistContent` (role = Playlist) — future

| Member | Type | Description |
|---|---|---|
| `items` | `vector<VideoSource>` | queued clips |
| `selected` | `size_t` | currently highlighted index |

Key operations:
- `render(ctx)` — scrollable list of filenames/thumbnails.
- Emits a signal / callback when the user activates an item so a `VideoContent` pane can load it.

---

### `ConfigContent` (role = Config) — future

Renders application settings (key bindings, decoder options, UI preferences).  No shared state beyond the settings store.

---

### `PlaybackState` (plain data struct)

Shared read-only snapshot used by both timeline types and by the UI.

| Field | Type | Notes |
|---|---|---|
| `position` | `double` | seconds from clip start |
| `duration` | `double` | total length in seconds (0 = unknown) |
| `speed` | `float` | playback rate multiplier (1.0 = normal) |
| `playing` | `bool` | true = advancing |

---

### `LocalTimeline`

Owned 1:1 by a `VideoContent`.  Plays independently.

Responsibilities:
- Maintain a `PlaybackState`.
- Advance `position` by `Δt × speed` on each `tick(Δt)`.
- Handle loop / clamp at end of clip.
- Expose `play()`, `pause()`, `toggle()`, `seek(seconds)`, `setSpeed()`.

---

### `GlobalTimeline`

**One instance**, held by `PaneManager`.  Drives all video panes in Synced mode and can be rendered by any pane assigned the `TimelineContent` role (multiple timeline-display panes are allowed).

Responsibilities:
- Same playback controls as `LocalTimeline`.
- Subscription API: contents call `subscribe(callback)` to be notified of any state change (play, pause, seek).  Returns a token used to `unsubscribe`.
- `tick(Δt)` called once per frame by `PaneManager`; advances position and fires callbacks.
- Duration kept as the **maximum** clip duration across all synced `VideoContent` instances (refreshed by `PaneManager` when clips load or play-modes change).

---

### `VideoSource` (plain data struct)

| Field | Type | Notes |
|---|---|---|
| `filePath` | `filesystem::path` | absolute path to the media file |
| `durationSeconds` | `double` | |
| `frameRate` | `double` | 0 = unknown |
| `width`, `height` | `uint32_t` | native resolution |

> A future decoder layer (e.g. FFmpeg) will own demuxing/decoding; `VideoSource` carries only the metadata.

---

### `Pane`

One rectangular region on screen.  Owns a `PaneContent` slot that can be swapped at any time.

| Member | Type | Description |
|---|---|---|
| `id` | `uint32_t` | stable identifier |
| `viewport` | `ViewportSet::ViewportSource` | normalised rect `(x, y, w, h)` + tint fed to Vulkan |
| `content` | `unique_ptr<PaneContent>` | currently active role |

Key operations:
- `setContent(unique_ptr<PaneContent>)` — replaces the role; calls `onDetach` / `onAttach`.
- `tick(Δt)` — forwards to `content->tick(Δt)`.
- `render(ctx)` — forwards to `content->render(ctx)`.
- `setViewport(…)` — repositions the pane without touching its content.

---

### `PaneManager`

The single owner of all panes and the global timeline.

| Operation | Notes |
|---|---|
| `addPane(viewport, content)` | Creates a `Pane`, calls `content->onAttach(globalTimeline)`. |
| `splitPane(id, direction)` | Splits existing pane; new pane starts with a default content (e.g. empty Video). |
| `removePane(id)` | Calls `onDetach`, destroys the pane. |
| `setContent(paneId, content)` | Delegates to `Pane::setContent`; manages `onDetach` / `onAttach` lifecycle. |
| `tick(Δt)` | Ticks `GlobalTimeline` first, then every `Pane`. |
| `buildViewportSet()` | Collects viewports from all panes for the `Pipeline`. |
| `refreshGlobalDuration()` | Scans synced `VideoContent` panes; updates `GlobalTimeline` duration. |

---

### `App` (updated)

`App` gains a `PaneManager` member and a wall-clock delta-time helper.

```
_setup()     → setupWindow, setupPipeline, setupPanes (via PaneManager)
_update(Δt)  → window.poll(), paneManager.tick(Δt)
_draw()      → pipeline.draw()  (uses ViewportSet from PaneManager)
```

---

## Data Flow (per frame)

```
steady_clock → Δt
     │
     ▼
PaneManager::tick(Δt)
     ├── GlobalTimeline::tick(Δt)
     │        └── fires onChange callbacks
     │                 └── VideoContent (synced) — signals decoder to seek/play/pause
     │
     └── Pane::tick(Δt)  ── for each pane
              └── PaneContent::tick(Δt)
                       VideoContent (independent) — advances LocalTimeline
                       TimelineContent            — no-op
                       PlaylistContent            — no-op / poll file scanner
     │
     ▼
PaneManager::buildViewportSet()
     │
     ▼
Pipeline::draw()
     └── for each pane: set Vulkan viewport/scissor, dispatch draw calls
```

---

## Open Questions

1. **`PaneContent` polymorphism** — virtual base class (`unique_ptr<PaneContent>`) vs. `std::variant` of concrete types?  Virtual is more open-ended; variant avoids heap allocation and indirect calls but requires a recompile when a new role is added.
2. **Decoder integration** — Does the decoder live inside `VideoContent`, behind a `VideoDecoder` interface owned by `VideoContent`, or in a shared thread-pool managed by `PaneManager`?
3. **Resize / reflow** — When a pane is split or removed, do adjacent panes expand to fill the gap (tmux style) or leave blank space?
4. **Frame-accurate seeking** — `GlobalTimeline` notifies via callbacks.  Is that enough, or do decoders need a dedicated seek queue to avoid stalling the render thread?
5. **Maximum pane count** — The existing `ViewportSet::BlankUboData` has a hard limit of 8 slots.  Should this become dynamic?
6. **Inter-pane communication** — When a `PlaylistContent` pane activates a clip, how does it tell a `VideoContent` pane to load it?  Options: direct `PaneManager` call, an event bus, or a signal/slot mechanism.
7. **Default content** — What role should a freshly split pane start with?  An empty video pane, or a picker UI?

---

## Files to Create (once plan is agreed)

| File | Purpose |
|---|---|
| `src/player/timeline.hpp` | `PlaybackState`, `LocalTimeline`, `GlobalTimeline` |
| `src/player/pane_content.hpp` | `PaneRole`, `PaneContent` interface |
| `src/player/video_content.hpp` | `VideoSource`, `PlayMode`, `VideoContent` |
| `src/player/timeline_content.hpp` | `TimelineContent` |
| `src/player/playlist_content.hpp` | `PlaylistContent` (future) |
| `src/player/pane.hpp` | `Pane` |
| `src/player/pane_manager.hpp` | `SplitDirection`, `PaneManager` |
| `src/player/app.hpp` | Add `PaneManager` member + delta-time helper |
