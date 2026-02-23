# daia – Architecture Plan

> **Status: draft / planning** — nothing below has been implemented yet.  
> The goal is to agree on the overall structure before writing any code.

---

## Concept

`daia` is a **pane-based video player**, similar in spirit to tmux or VS Code's editor groups.  
The screen is divided into rectangular panes.  Each pane shows one video clip and owns its own render pass so panes can be attached and detached at any time without affecting the others.

Two play modes are supported:

| Mode | Description |
|---|---|
| **Synced** | All participating panes share a single Global Timeline — useful for frame-accurate comparison of multiple clips. |
| **Independent** | Each pane runs its own Local Timeline — useful for free-form playback and looping of individual clips. |

---

## Proposed Class Hierarchy

```
App
 ├── Window                   (GLFW, already exists)
 ├── Pipeline                 (Vulkan, already exists)
 └── PaneManager              (new – owns everything below)
      ├── GlobalTimeline       (new – shared timeline for synced mode)
      └── Pane[]               (new – one per active video panel)
           ├── VideoSource      (new – metadata: path, duration, fps, …)
           └── LocalTimeline    (new – timeline for independent mode)
```

---

## Class Sketches

### `PlaybackState` (plain data struct)

Carries the runtime state of any timeline so the UI and renderer can read it without knowing which timeline type produced it.

| Field | Type | Notes |
|---|---|---|
| `position` | `double` | seconds from the start of the clip |
| `duration` | `double` | total length in seconds (0 = unknown) |
| `speed` | `float` | playback rate multiplier (1.0 = normal) |
| `playing` | `bool` | true = advancing, false = paused |

---

### `LocalTimeline`

Owned **1:1 by a `Pane`**.  Plays independently.

Responsibilities:
- Maintain a `PlaybackState`.
- Advance `position` by `Δt × speed` on each `tick(Δt)` call.
- Handle loop and clamp at end of clip.
- Expose `play()`, `pause()`, `toggle()`, `seek(seconds)`, `setSpeed()`.

---

### `GlobalTimeline`

**One instance**, held by `PaneManager`.  Drives all panes that are in Synced mode.

Responsibilities:
- Same playback controls as `LocalTimeline`.
- Subscription API: panes call `subscribe(callback)` to receive a notification whenever play state or position changes (e.g. a UI seek).  `unsubscribe(id)` removes the callback.
- `tick(Δt)` is called once per frame by `PaneManager`; it advances position and fires all registered callbacks.
- Duration is set to the **maximum** clip duration across all synced panes (updated by `PaneManager` whenever a clip is loaded or a pane changes mode).

---

### `VideoSource` (plain data struct)

| Field | Type | Notes |
|---|---|---|
| `filePath` | `filesystem::path` | absolute path to the media file |
| `durationSeconds` | `double` | |
| `frameRate` | `double` | 0 = unknown |
| `width`, `height` | `uint32_t` | native resolution |

> A future **decoder layer** (e.g. FFmpeg) will own the actual demuxing/decoding; `VideoSource` is only the metadata that the rest of the architecture needs right now.

---

### `Pane`

One cell in the tiled layout.

| Member | Type | Description |
|---|---|---|
| `id` | `uint32_t` | stable identifier |
| `viewportSource` | `ViewportSet::ViewportSource` | normalised rect `(x, y, w, h)` + tint color fed to Vulkan |
| `source` | `optional<VideoSource>` | the loaded clip, if any |
| `playMode` | `PlayMode` (enum) | `Synced` or `Independent` |
| `localTimeline` | `LocalTimeline` | always present; active only in Independent mode |

Key operations:
- `loadVideo(VideoSource)` — stores the source, updates timeline durations.
- `setPlayMode(PlayMode)` — switches between Synced and Independent.  On switch to Independent, carries the current global position across so there is no visible jump.
- `tick(Δt)` — advances the local timeline if in Independent mode (no-op in Synced mode; GlobalTimeline handles that).
- `currentPlaybackState()` — returns the state from whichever timeline is currently active.

Lifecycle with `GlobalTimeline`:
- On construction: subscribes to `GlobalTimeline` (starts in Synced mode).
- On switch to Independent: unsubscribes.
- On switch back to Synced: re-subscribes.
- On destruction: unsubscribes automatically.

---

### `PaneManager`

The single owner of all panes and the global timeline.

Responsibilities:

| Operation | Notes |
|---|---|
| `addPane(viewport)` | Creates a new `Pane` with the given normalised rectangle. |
| `splitPane(id, direction)` | Splits an existing pane horizontally or vertically. The original pane shrinks to the first half; a new pane fills the second half. |
| `removePane(id)` | Destroys the pane (subscription is cleaned up by the pane's destructor). |
| `tick(Δt)` | Ticks the `GlobalTimeline` first, then calls `tick(Δt)` on every pane. |
| `buildViewportSet()` | Constructs the `ViewportSet` that the `Pipeline` consumes each frame. |
| `refreshGlobalDuration()` | Scans synced panes and sets `GlobalTimeline` duration to the max clip length. |

---

### `App` (updated)

`App` gets a `PaneManager` member.

Main loop change:
```
_setup()            → setupWindow, setupPipeline, setupPanes (via PaneManager)
_update(Δt)         → window.poll(), paneManager.tick(Δt)
_draw()             → pipeline.draw()   (uses ViewportSet from PaneManager)
```

A **wall-clock delta-time** (`std::chrono::steady_clock`) is computed each frame and passed to `tick`.

---

## Data Flow (per frame)

```
steady_clock → Δt
     │
     ▼
PaneManager::tick(Δt)
     ├── GlobalTimeline::tick(Δt)  →  fires callbacks on synced Panes
     └── Pane::tick(Δt)           →  advances LocalTimeline (independent panes only)
     │
     ▼
PaneManager::buildViewportSet()
     │
     ▼
Pipeline::draw()   (sets Vulkan viewports/scissors per pane, dispatches draw calls)
```

---

## Open Questions

1. **Decoder integration** — Where exactly does an FFmpeg (or similar) decoder live?  Options: inside `Pane`, behind a `VideoDecoder` interface owned by `Pane`, or in a separate decoding thread pool managed by `PaneManager`.
2. **Resize / reflow** — When a pane is split or removed, should adjacent panes expand to fill the gap (tmux behaviour) or leave blank space?
3. **Timeline UI widget** — The global timeline bar and per-pane local bars are UI concerns.  Should they be ImGui overlays drawn on top of the Vulkan swapchain, or separate GLFW child windows?
4. **Frame-accurate seeking** — `GlobalTimeline` notifies panes via callbacks. Is a callback sufficient, or do decoders need a dedicated seek queue to avoid blocking the render thread?
5. **Maximum pane count** — The current `ViewportSet::BlankUboData` has a hard limit of 8 viewports (`float4 colors[8]`).  Should this be dynamic?

---

## Files to Create (once plan is agreed)

| File | Purpose |
|---|---|
| `src/player/timeline.hpp` | `PlaybackState`, `LocalTimeline`, `GlobalTimeline` |
| `src/player/pane.hpp` | `VideoSource`, `PlayMode`, `Pane` |
| `src/player/pane_manager.hpp` | `SplitDirection`, `PaneManager` |
| `src/player/app.hpp` | Add `PaneManager` member + delta-time helper |
