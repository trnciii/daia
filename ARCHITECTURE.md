# daia \u2013 Architecture Plan (rev 3)

> **Status: draft / planning** \u2014 nothing below has been implemented yet.  
> The goal is to agree on the overall structure before writing any code.
>
> **Change from rev 1:** Panes are no longer hardcoded as video panels.  
> Each pane has a swappable **role** (video, global timeline, playlist, config, \u2026).
>
> **Change from rev 2:** `PaneContent` shared interface clarified for both polymorphism approaches;
> `EmptyContent` added as the default role for a new pane;
> playlist \u2192 video assignment deferred;
> decoder ownership options compared by feature.

---

## Concept

`daia` is a **pane-based video player**, similar in spirit to tmux or VS Code's editor groups.  
The screen is divided into rectangular panes.  Panes can be attached and detached at any time.

Each pane can be assigned one of several **roles** that determine what it renders and how it handles input:

| Role | Description |
|---|---|
| **Empty** | Blank placeholder \u2014 the default for a newly created pane. |
| **Video** | Decodes and displays a single video clip; can run in Synced or Independent play mode. |
| **GlobalTimeline** | Shows the shared scrubber / transport bar for synced playback. |
| **Playlist** | *(future)* Browses and queues video files. |
| **Config** | *(future)* Settings and preferences UI. |

A pane's role can be changed at any time without destroying the pane or its viewport.

---

## Proposed Class Hierarchy

```
App
 \u251c\u2500\u2500 Window                      (GLFW, already exists)
 \u251c\u2500\u2500 Pipeline                    (Vulkan, already exists)
 \u2514\u2500\u2500 PaneManager                 (new \u2013 owns everything below)
      \u251c\u2500\u2500 GlobalTimeline          (new \u2013 shared timeline for synced video panes)
      \u2514\u2500\u2500 Pane[]                  (new \u2013 one per screen region)
           \u2514\u2500\u2500 PaneContent        (new \u2013 swappable role, one of:)
                \u251c\u2500\u2500 EmptyContent        role = Empty          (default)
                \u251c\u2500\u2500 VideoContent        role = Video
                \u251c\u2500\u2500 TimelineContent     role = GlobalTimeline
                \u251c\u2500\u2500 PlaylistContent     role = Playlist  (future)
                \u2514\u2500\u2500 ConfigContent       role = Config    (future)
```

---

## Class Sketches

### `PaneRole` (enum)

```
enum class PaneRole { Empty, Video, GlobalTimeline, Playlist, Config };
```

New roles can be added here as the application grows.

---

### `PaneContent` \u2014 shared interface contract

Every concrete content type must provide **exactly these five operations**, regardless of whether
polymorphism is realised as a virtual base class or a `std::variant`:

| Operation | Signature | Description |
|---|---|---|
| `role` | `PaneRole role() const` | Returns the role tag for runtime inspection / switching UI. |
| `tick` | `void tick(double dt)` | Per-frame update: advance timelines, poll decoders, etc. |
| `render` | `void render(RenderCtx&)` | Emit draw commands for this pane's viewport. |
| `onAttach` | `void onAttach(GlobalTimeline&)` | Called when the content is seated in a pane; subscribe to shared state. |
| `onDetach` | `void onDetach()` | Called before removal; unsubscribe and release borrowed references. |

These five operations are the **contract** the rest of the system relies on.  
How they are dispatched is an implementation detail:

| Approach | Dispatch mechanism | Adding a new role |
|---|---|---|
| **Virtual base class** | vtable (indirect call per operation) | Add a new subclass; no changes to call sites |
| **`std::variant`** | `std::visit` (switch over known types) | Add to the variant list; all `visit` sites must be updated |

Both are valid. The virtual approach is more open-ended; the variant approach eliminates heap
allocation and indirect calls at the cost of a closed type set.

`Pane::setContent(\u2026)` calls `onDetach` on the old content and `onAttach` on the new one,
then stores the new content.

---

### `EmptyContent` (role = Empty)

The default content for every newly created or split pane.  A visible blank slate \u2014 renders a
dark/neutral background with no controls.

| Method | Behaviour |
|---|---|
| `role()` | Returns `PaneRole::Empty` |
| `tick(dt)` | no-op |
| `render(ctx)` | Draws a solid background colour (signals "this pane has no role yet"). |
| `onAttach(gt)` | no-op |
| `onDetach()` | no-op |

When the user assigns a real role to a pane, `PaneManager::setContent` replaces `EmptyContent`
with the chosen concrete content.

---

### `VideoContent` (role = Video)

Holds everything needed to play one video clip.

| Member | Type | Description |
|---|---|---|
| `source` | `optional<VideoSource>` | metadata of the loaded clip |
| `playMode` | `PlayMode` | `Synced` or `Independent` |
| `localTimeline` | `LocalTimeline` | always present; active only in Independent mode |
| `globalTimeline*` | `GlobalTimeline*` | borrowed reference; non-null while attached |
| `decoder` | *(see Decoder Ownership section)* | owned or borrowed; decodes frames |

Key operations:
- `loadVideo(VideoSource)` \u2014 stores metadata, updates local timeline duration.
- `setPlayMode(PlayMode)` \u2014 switches between Synced and Independent.  Subscribes to /
  unsubscribes from `GlobalTimeline` accordingly.  Carries current position across so there is no jump.
- `tick(dt)` \u2014 advances `LocalTimeline` in Independent mode; no-op in Synced mode.
- `currentPlaybackState()` \u2014 delegates to the active timeline.

Lifecycle:
- `onAttach` \u2192 subscribes to `GlobalTimeline` (default: Synced).
- `onDetach` \u2192 unsubscribes from `GlobalTimeline`.

---

### `TimelineContent` (role = GlobalTimeline)

Renders the shared transport bar in a dedicated pane.

| Member | Type | Description |
|---|---|---|
| `globalTimeline*` | `GlobalTimeline*` | borrowed reference; non-null while attached |

Key operations:
- `tick(dt)` \u2014 no-op; `GlobalTimeline` is ticked centrally by `PaneManager`.
- `render(ctx)` \u2014 draws the scrubber, play/pause, speed, loop controls.
- Input handling \u2014 seeks `GlobalTimeline`, toggles play/pause, adjusts speed.

---

### `PlaylistContent` (role = Playlist) \u2014 future

| Member | Type | Description |
|---|---|---|
| `items` | `vector<VideoSource>` | queued clips |
| `selected` | `size_t` | currently highlighted index |

Key operations:
- `render(ctx)` \u2014 scrollable list of filenames/thumbnails.
- When the user activates an item, the playlist must somehow tell a `VideoContent` pane to load it.
  **The exact assignment mechanism is deferred (TBD).**

---

### `ConfigContent` (role = Config) \u2014 future

Renders application settings (key bindings, decoder options, UI preferences).  No shared state
beyond the settings store.

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
- Advance `position` by `\u0394t \u00d7 speed` on each `tick(\u0394t)`.
- Handle loop / clamp at end of clip.
- Expose `play()`, `pause()`, `toggle()`, `seek(seconds)`, `setSpeed()`.

---

### `GlobalTimeline`

**One instance**, held by `PaneManager`.  Drives all video panes in Synced mode and can be rendered
by any pane assigned the `TimelineContent` role (multiple timeline-display panes are allowed).

Responsibilities:
- Same playback controls as `LocalTimeline`.
- Subscription API: contents call `subscribe(callback)` to be notified of any state change
  (play, pause, seek).  Returns a token used to `unsubscribe`.
- `tick(\u0394t)` called once per frame by `PaneManager`; advances position and fires callbacks.
- Duration kept as the **maximum** clip duration across all synced `VideoContent` instances
  (refreshed by `PaneManager` when clips load or play-modes change).

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
- `setContent(unique_ptr<PaneContent>)` \u2014 replaces the role; calls `onDetach` / `onAttach`.
- `tick(\u0394t)` \u2014 forwards to `content->tick(\u0394t)`.
- `render(ctx)` \u2014 forwards to `content->render(ctx)`.
- `setViewport(\u2026)` \u2014 repositions the pane without touching its content.

---

### `PaneManager`

The single owner of all panes and the global timeline.

| Operation | Notes |
|---|---|
| `addPane(viewport)` | Creates a `Pane` with `EmptyContent` as the initial role. |
| `splitPane(id, direction)` | Splits existing pane; new pane starts with `EmptyContent`. |
| `removePane(id)` | Calls `onDetach`, destroys the pane. |
| `setContent(paneId, content)` | Delegates to `Pane::setContent`; manages `onDetach` / `onAttach` lifecycle. |
| `tick(\u0394t)` | Ticks `GlobalTimeline` first, then every `Pane`. |
| `buildViewportSet()` | Collects viewports from all panes for the `Pipeline`. |
| `refreshGlobalDuration()` | Scans synced `VideoContent` panes; updates `GlobalTimeline` duration. |

---

### `App` (updated)

`App` gains a `PaneManager` member and a wall-clock delta-time helper.

```
_setup()     \u2192 setupWindow, setupPipeline, setupPanes (via PaneManager)
_update(\u0394t)  \u2192 window.poll(), paneManager.tick(\u0394t)
_draw()      \u2192 pipeline.draw()  (uses ViewportSet from PaneManager)
```

---

## Decoder Ownership Options

The decoder (e.g. an FFmpeg wrapper) must be placed somewhere in the ownership hierarchy.  
Three options are possible; each grants different features.

### Option A \u2014 Decoder embedded directly in `VideoContent`

```
VideoContent
 \u2514\u2500\u2500 FfmpegDecoder   (concrete type, owned by value or unique_ptr)
```

| Feature | Available? |
|---|---|
| Simple ownership \u2014 no sharing needed | \u2705 |
| `VideoContent` constructs and destroys the decoder | \u2705 |
| Seek / play / pause controlled directly inside `tick` | \u2705 |
| Reuse one decoder across multiple panes | \u274c |
| Hot-swap decoder backend (e.g. swap FFmpeg for a hardware decoder) | \u274c (recompile required) |
| Decode on a background thread isolated from the content | needs extra work |

Best when: each pane always owns exactly one clip, and simplicity matters more than flexibility.

---

### Option B \u2014 `VideoDecoder` interface owned by `VideoContent`

```
VideoContent
 \u2514\u2500\u2500 unique_ptr<VideoDecoder>   (abstract interface)
          \u251c\u2500\u2500 FfmpegDecoder      (concrete implementation)
          \u2514\u2500\u2500 HardwareDecoder    (future alternative)
```

`VideoDecoder` interface (minimum surface):

| Method | Description |
|---|---|
| `open(VideoSource)` | Open and probe the file; populate duration/fps metadata. |
| `decodeFrameAt(double seconds)` | Seek and decode the nearest frame to the given position. |
| `close()` | Release file handles and codec state. |
| `isOpen() const` | Whether a file is currently open. |

| Feature | Available? |
|---|---|
| Simple 1:1 ownership per pane | \u2705 |
| Hot-swap decoder backend without touching `VideoContent` | \u2705 |
| Unit-test `VideoContent` with a mock decoder | \u2705 |
| Reuse one decoder across multiple panes | \u274c |
| Offload decoding to a background thread | needs a wrapper / async adapter |

Best when: decoder backends may change (hardware acceleration, custom codecs) and testability matters.

---

### Option C \u2014 Shared decoder thread-pool in `PaneManager`

```
PaneManager
 \u2514\u2500\u2500 DecoderPool
      \u2514\u2500\u2500 DecoderJob[]   (one active job per VideoContent pane)
VideoContent              (holds a handle / ticket into the pool)
```

`DecoderPool` public surface:

| Method | Description |
|---|---|
| `acquire(VideoSource) \u2192 handle` | Allocates a decoder worker for a clip; returns an opaque handle. |
| `release(handle)` | Returns the worker to the pool. |
| `requestFrame(handle, seconds)` | Posts an async decode request. |
| `latestFrame(handle) \u2192 Frame` | Returns the most recently decoded frame (non-blocking). |

| Feature | Available? |
|---|---|
| Background decoding on worker threads | \u2705 |
| Bounded resource usage (pool size caps parallel decoders) | \u2705 |
| Render thread never blocks on decode | \u2705 |
| Reuse a single decoded stream across multiple synced panes | \u2705 (possible with shared handles) |
| Simple to implement and reason about | \u274c (most complex option) |
| `VideoContent` can be tested independently of decoding | \u274c (pool is a global dependency) |

Best when: multiple panes play simultaneously, performance matters, and the render thread must never stall.

---

## Data Flow (per frame)

```
steady_clock \u2192 \u0394t
     \u2502
     \u25bc
PaneManager::tick(\u0394t)
     \u251c\u2500\u2500 GlobalTimeline::tick(\u0394t)
     \u2502        \u2514\u2500\u2500 fires onChange callbacks
     \u2502                 \u2514\u2500\u2500 VideoContent (synced) \u2014 signals decoder to seek/play/pause
     \u2502
     \u2514\u2500\u2500 Pane::tick(\u0394t)  \u2500\u2500 for each pane
               \u2514\u2500\u2500 PaneContent::tick(\u0394t)
                        EmptyContent               \u2014 no-op
                        VideoContent (independent) \u2014 advances LocalTimeline
                        TimelineContent            \u2014 no-op
                        PlaylistContent            \u2014 no-op / poll file scanner
     \u2502
     \u25bc
PaneManager::buildViewportSet()
     \u2502
     \u25bc
Pipeline::draw()
     \u2514\u2500\u2500 for each pane: set Vulkan viewport/scissor, dispatch draw calls
```

---

## Open Questions

1. **`PaneContent` polymorphism** \u2014 virtual base class or `std::variant`?  The shared interface
   contract (5 operations above) is the same either way; only dispatch and extensibility differ.
   See the comparison table in the `PaneContent` section.
2. **Decoder ownership** \u2014 Option A (embedded), B (interface), or C (thread-pool)?
   See the Decoder Ownership section for a feature comparison.
3. **Resize / reflow** \u2014 When a pane is split or removed, do adjacent panes expand to fill the
   gap (tmux style) or leave blank space?
4. **Frame-accurate seeking** \u2014 `GlobalTimeline` notifies via callbacks.  Is that enough, or do
   decoders need a dedicated seek queue to avoid stalling the render thread?
5. **Maximum pane count** \u2014 The existing `ViewportSet::BlankUboData` has a hard limit of 8 slots.
   Should this become dynamic?
6. **Playlist \u2192 Video assignment** \u2014 When a `PlaylistContent` pane activates a clip, how does
   it tell a `VideoContent` pane to load it?  **Deferred (TBD).**

---

## Files to Create (once plan is agreed)

| File | Purpose |
|---|---|
| `src/player/timeline.hpp` | `PlaybackState`, `LocalTimeline`, `GlobalTimeline` |
| `src/player/pane_content.hpp` | `PaneRole`, `PaneContent` interface |
| `src/player/empty_content.hpp` | `EmptyContent` (default role) |
| `src/player/video_content.hpp` | `VideoSource`, `PlayMode`, `VideoContent` |
| `src/player/video_decoder.hpp` | `VideoDecoder` interface (if Option B or C) |
| `src/player/timeline_content.hpp` | `TimelineContent` |
| `src/player/playlist_content.hpp` | `PlaylistContent` (future) |
| `src/player/pane.hpp` | `Pane` |
| `src/player/pane_manager.hpp` | `SplitDirection`, `PaneManager` |
| `src/player/app.hpp` | Add `PaneManager` member + delta-time helper |
