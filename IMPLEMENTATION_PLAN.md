# DAIA Implementation Plan

> Last updated: 2026-03-18 (Session 10)
> 
> This document tracks the multi-phase implementation plan for Content-based rendering with Pane management.
> 
> **Current Phase**: Phase A - Content Layer Foundation  
> **Next Task**: Step 5b - video.hpp RAII ラッパー + Step 5c - VideoContent  
> **Status**: Step 0-5a 完了、Step 5b 進行中

---

## Overview

```
Phase A: Content Layer Foundation (Steps 0-9)
  ├─ Step 0: Restore Texture struct ✅
  ├─ Step 1: CMakeLists.txt - FFmpeg linking ✅（※実際は未リンク、Step 5a で修正）
  ├─ Step 2: Content base class + EmptyContent ✅
  ├─ Step 3: Pipeline content management (register/update) ✅
  ├─ Step 4a: コード品質の整理 ✅
  ├─ Step 5: VideoContent 実装 ← NOW
  │    ├─ 5a: CMakeLists.txt FFmpeg リンク + CMake 分割 ✅
  │    ├─ 5b: video.hpp RAII ラッパー + デコード整理 ← NOW
  │    ├─ 5c: video_content.hpp 作成
  │    └─ 5d: App 統合 + テスト
  ├─ Step 6: update/draw コマンド・同期の分離
  ├─ Step 7: Staging buffer 再利用
  ├─ Step 8: Pane class + PaneManager
  └─ Step 9: Test - Multi-pane + Content switching

Phase B: Multi-Content Rendering (Steps 10-14)
  ├─ Step 10: Multiple Pane support
  ├─ Step 11: Content switching at runtime
  ├─ Step 12: TimelineContent + UIContent
  ├─ Step 13: PipelineRegistry for multiple shaders
  └─ Step 14: Offscreen rendering for Content (shader/ImGui integration)

Phase C: Input & Advanced Features (Steps 15-20)
  ├─ Step 15: Input event system
  ├─ Step 16: Input routing to Content
  ├─ Step 17: Window resize handling
  ├─ Step 18: Dynamic Pane layout (add/remove/resize)
  ├─ Step 19: Content overlay support
  └─ Step 20: Hardware video decode (Vulkan Video)
```

---

## Phase A: Content Layer Foundation

### Architecture Overview

```
App
 ├─ Window (GLFW)
 └─ Pipeline (Vulkan rendering + content管理)
      ├─ WrappedContent = { shared_ptr<Content>, Texture }
      ├─ ViewportSet (複数ペイン描画)
      └─ update() / draw() サイクル

Content (abstract)
 ├─ EmptyContent (固定色テスト用)
 ├─ VideoContent (FFmpeg decode → CPU pixel data) [TODO]
 ├─ TimelineContent (Phase B)
 └─ UIContent (Phase B)
```

**Key Design Decisions (Session 6-7 で確定)**:
- **Content = CPU データ生成のみ**。`size()` と `data()` で pixel 列を提供。Vulkan API を直接触らない
- **Pipeline = Texture 所有 + upload + 同期 + 描画**。WrappedContent で content と texture を対にして管理
- **Texture = GPU リソース RAII** (image/view/sampler/memory)。create/destroy のみ
- Staging buffer は Pipeline::update() 内でフレームごとにアロケート（将来的に再利用へ）
- Descriptor set の texture binding は update() 末尾（waitIdle 後）で更新
- 1つの Content を複数ペインで共有可能（同一 descriptor を viewport ループで描画）
- Offscreen rendering は動画再生の基本動作確認後（Phase B Step 14）に導入

### Current Status
- [x] Step 0: Texture struct ✅ (player/texture.hpp)
- [x] Step 1: CMakeLists.txt FFmpeg linking ✅（※実際は未リンク、Step 5a で修正）
- [x] Step 2: Content base class + EmptyContent ✅ (player/content/{content_base,empty_content,content}.hpp)
- [x] Step 3: Pipeline content management ✅ (registerContent/unregisterContent/update)
- [x] Step 3a: Staging buffer upload ✅ (Pipeline::update() 内)
- [x] Step 3b: Sampler descriptor binding ✅ (binding 1 = CombinedImageSampler)
- [x] Step 4a: コード品質の整理 ✅
- [ ] Step 5: VideoContent 実装 ← NOW
  - [x] 5a: CMakeLists.txt FFmpeg リンク + CMake 分割 ✅
  - [ ] 5b: video.hpp RAII ラッパー + デコード整理 ← NOW
  - [ ] 5c: video_content.hpp 作成
  - [ ] 5d: App 統合 + テスト
- [ ] Step 6: update/draw コマンド・同期の分離
- [ ] Step 7: Staging buffer 再利用
- [ ] Step 8: Pane layer
- [ ] Step 9: Testing

### Step 0 Details: Restore Texture Struct

**File**: `src/player/pipeline.hpp`

**Location**: After `findMemoryType()` helper function, before `class Pipeline`

**Content**: Texture struct with RAII resource management

```cpp
struct Texture
{
	vk::UniqueImage image;
	vk::UniqueDeviceMemory memory;
	vk::UniqueImageView view;
	vk::UniqueSampler sampler;
	vk::Extent2D extent;

	void create(vk::Device device, vk::PhysicalDevice physicalDevice, uint32_t width, uint32_t height)
	{
		const auto format = vk::Format::eR8G8B8A8Unorm;

		image = device.createImageUnique({
			.imageType = vk::ImageType::e2D,
			.format = format,
			.extent = {width, height, 1},
			.mipLevels = 1,
			.arrayLayers = 1,
			.samples = vk::SampleCountFlagBits::e1,
			.usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
			.sharingMode = vk::SharingMode::eExclusive,
			.initialLayout = vk::ImageLayout::eUndefined,
		});

		const auto memReqs = device.getImageMemoryRequirements(*image);
		memory = device.allocateMemoryUnique({
			.allocationSize = memReqs.size,
			.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal),
		});
		device.bindImageMemory(*image, *memory, 0);

		view = device.createImageViewUnique({
			.image = *image,
			.viewType = vk::ImageViewType::e2D,
			.format = format,
			.subresourceRange = {
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		});

		sampler = device.createSamplerUnique({
			.magFilter = vk::Filter::eLinear,
			.minFilter = vk::Filter::eLinear,
			.mipmapMode = vk::SamplerMipmapMode::eLinear,
			.addressModeU = vk::SamplerAddressMode::eClampToEdge,
			.addressModeV = vk::SamplerAddressMode::eClampToEdge,
			.addressModeW = vk::SamplerAddressMode::eClampToEdge,
		});

		extent = {width, height};
	}

	void destroy()
	{
		sampler.reset();
		view.reset();
		memory.reset();
		image.reset();
	}
};
```

**Verification**: `bash .local/clean-build.sh` compiles successfully

### Step 1 Details: CMakeLists.txt - FFmpeg Linking

**File**: `src/player/CMakeLists.txt`

**Changes needed**:
```cmake
find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec swscale avutil)

target_link_libraries(daia
    # existing...
    Vulkan::Vulkan
    glfw
    # ADD:
    FFmpeg::avformat
    FFmpeg::avcodec
    FFmpeg::swscale
    FFmpeg::avutil
)

target_include_directories(daia PRIVATE ${FFMPEG_INCLUDE_DIR})
```

**Verification**: `bash .local/clean-build.sh` succeeds

---

### Step 2 Details: Content Base Class + EmptyContent ✅

**File**: `src/player/content/content.hpp`

**Implemented**:
- `SetupArgs` struct: `vk::UniqueDevice&`, `vk::PhysicalDevice&`
- `UpdateArgs` struct: `double time`, `float width`, `float height`（Vulkan 詳細なし）
- `Content` abstract class: `setup()`, `destroy()`, `update()` (returns bool), `size()`, `data()`
- `EmptyContent` class: コンストラクタで赤ピクセル生成、update() で初回のみ true を返す

```cpp
class Content {
public:
  virtual void setup(const SetupArgs info) = 0;
  virtual void destroy() = 0;
  virtual bool update(const UpdateArgs info) = 0;
  virtual util::uint2 size() const = 0;
  virtual std::span<const uint32_t> data() const = 0;
};
```

**既知の課題**（Step 4a で対応予定）:
- `virtual ~Content() = default;` が未宣言 → shared_ptr 経由の delete で UB
- `size()` 戻り値の不要な `const`
- `SetupArgs` の参照メンバ（代入不可 struct）

---

### Step 3 Details: Pipeline Content Management ✅

**File**: `src/player/pipeline.hpp`

**Implemented**:
- `WrappedContent` struct: `shared_ptr<Content>` + `shared_ptr<Texture>` を対にして管理
- `_contents`: `unordered_map<string, WrappedContent>`
- `registerContent(key, content)` → content->setup() + Texture 生成 + 登録
- `unregisterContent(key)` / `unregisterAllContents()` → destroy() + 削除
- `update(globalTime)`:
  - content->update() が true を返した場合のみ staging buffer 経由で texture upload
  - barrier: Undefined → TransferDstOptimal → ShaderReadOnlyOptimal
  - waitIdle 後に updateDescriptorSets() で binding 1 を更新
- `draw()`: recordCommand → submit → present（update とは別 submit）
- App: `_startTime` + `steady_clock` で globalTime を計算し `_pipeline.update()` に渡す

---

### Step 4a Details: コード品質の整理 ✅

**対象ファイル**: content.hpp, pipeline.hpp, texture.hpp

**完了した修正**:
1. `Content` に `virtual ~Content() = default;` を追加（UB 修正）
2. Descriptor Pool の poolSize を 2 種類に修正（UBO + CombinedImageSampler を type 指定で）
3. `WrappedContent::texture` を `shared_ptr` → 値メンバに変更
4. `ViewportSet::size()` に `const` 追加
5. `Texture::upload()` の dead code 削除
6. `registerContent` の key ムーブ
7. `updateDescriptorSets()` を毎フレームから `registerContent()` のみに移動
8. 重複キーガード追加

**残件**: `updateDescriptorSets()` 内の空 `_contents` ガード（現状 registerContent でのみ呼ぶので実害なし）

---

### Step 5 Details: VideoContent 実装 ← NOW

#### 5a: CMakeLists.txt FFmpeg リンク + CMake 分割 ✅

**完了済み** (Session 8-9):
- `src/player/CMakeLists.txt`: `player_core` INTERFACE ライブラリ化 + `PkgConfig::FFMPEG` リンク
- `src/app/CMakeLists.txt`: 新規作成、`daia` 実行ファイル + `player_core` リンク
- root `CMakeLists.txt`: `add_subdirectory(src/player)` + `add_subdirectory(src/app)`

#### 5b: video.hpp RAII ラッパー + デコード整理 ← NOW

**ファイル**: `src/player/video.hpp`（media.hpp からリネーム済み）

**現状**: Video クラスに setup/getFrame/destroy が実装済み。デコードロジックは動作するが、以下が未対応：

1. **RAII ラッパー作成**: FFmpeg 構造体ごとにスコープ解放可能なラッパー
   - `FormatContext`: `avformat_close_input()` で解放
   - `CodecContext`: `avcodec_free_context()` で解放
   - `Frame`: `av_frame_free()` で解放、`av_frame_unref()` で再利用
   - `Packet`: `av_packet_free()` で解放、`av_packet_unref()` で再利用
   - `SwsContext`: `sws_freeContext()` で解放
2. **メンバ化**: SwsContext / Frame / Packet をメンバに持たせて毎フレーム alloc/free を回避
3. **成功パスの frame リーク修正**: `ret == 0` → `break` の前に `av_frame_free` がない

**getFrame の将来設計**:
- Video 側はフレーム番号 `int64_t` で管理（FFmpeg の pts/seek API が `int64_t`）
- Content 側は秒（`UpdateArgs.time`）で要求
- **VideoContent が `time × fps` でフレーム番号に変換**し、前回と比較して `update()` の `bool` を返す
- 初回実装では「毎 update で次の 1 フレーム」でなとす
- 将来: `getFrame(int64_t n)` で指定フレームへのシーク対応

#### 5c: video_content.hpp 作成

**ファイル**: `src/player/content/video_content.hpp` （新規）

```cpp
class VideoContent : public Content {
    media::Video _video;
    std::vector<uint32_t> _pixels;  // getFrame() の結果を保持
    util::uint2 _size;
    int64_t _currentFrame = -1;     // 現在表示中のフレーム番号
    double _fps = 0;                // 動画のフレームレート

    void setup(const SetupArgs) override;   // Video::setup(filepath)
    void destroy() override;                // Video::destroy()
    bool update(const UpdateArgs) override; // time×fps→フレーム番号、変わったらgetFrame()
    util::uint2 size() const override;      // {video.width(), video.height()}
    std::span<const uint32_t> data() const override; // _pixels
};
```

**検討点**:
- ファイルパスの渡し方（コンストラクタで渡す）
- EOF 時の動作（まずは停止、最後のフレームを保持）
- 初回は毎 update で次の 1 フレームをデコード
- 将来: `getFrame(int64_t)` でシーク対応、VideoContent がフレーム番号を管理

#### 5d: App 統合 + テスト

- App::_setup() で `VideoContent` を登録して動画再生を確認
- テスト動画ファイルの配置（resources/ 等）

---

### Step 6 Details: update/draw コマンド・同期の分離

**目的**: 現在 update() と draw() が同じ `_drawFence` を共用していて危険。分離する。

**方針**:
- upload 用と描画用で fence を分ける（`_uploadFence` + `_drawFence`）
- upload 用の command buffer を分ける（または upload と draw を同一 command buffer にまとめる）
- `waitIdle()` の除去を目指す（fence wait のみで同期）

---

### Step 7 Details: Staging Buffer 再利用

**目的**: 現在は upload のたびに staging buffer + memory をアロケート/解放。再利用に切り替えてアロケーションを削減。

**方針**:
- Pipeline メンバに固定サイズ staging buffer を保持（最大テクスチャサイズ分）
- 複数 content で共有（シーケンシャル upload なので排他不要）
- content サイズが変わったら再アロケート

---

### Step 8 Details: Pane class + PaneManager

**注**: 現在の ViewportSet が Pane 的な役割を果たしている。必要に応じて Content と Viewport の対応関係を管理する Pane レイヤーを導入。

---

### Step 9 Details: Integration Test

**Verification**:
1. Video file loads without hanging
2. Frames extract successfully
3. Texture updates without crashes
4. Frame image displays on screen

---

## Phase B: Multi-Content Rendering

### Current Status
- [ ] Step 10: Multiple Pane support
- [ ] Step 11: Content switching at runtime
- [ ] Step 12: TimelineContent + UIContent
- [ ] Step 13: PipelineRegistry for multiple shaders
- [ ] Step 14: Offscreen rendering for Content (shader/ImGui integration)

=== Detailed descriptions deferred - see Phase C below ===

---

## Phase C: Input & Advanced Features

### Current Status
- [ ] Step 15: Input event system
- [ ] Step 16: Input routing to Content
- [ ] Step 17: Window resize handling
- [ ] Step 18: Dynamic Pane layout (add/remove/resize)
- [ ] Step 19: Content overlay support
- [ ] Step 20: Hardware video decode (Vulkan Video)

=== Detailed descriptions deferred ===

---

## Key Design Decisions

### Content Architecture (Session 6-7 で確定)
- **Content = CPU データ提供**。`update(UpdateArgs)` → bool, `size()` → uint2, `data()` → span<uint32_t>
- Content は Vulkan API を直接触らない。Texture も持たない
- Pipeline が WrappedContent = { Content, Texture } で対にして管理
- 将来の GPU ソース (HW decode) は `variant<CpuSource, GpuSource>` で拡張可能
- 1つの Content を複数ペインに表示可能（同一 texture を各ペインの shader に渡す）

### Pipeline Management
- Pipeline は描画実行 + Texture 所有 + staging upload + descriptor 管理を担当
- `update()`: content->update() → staging alloc → upload commands → submit → waitIdle → updateDescriptorSets
- `draw()`: acquireNextImage → recordCommand → submit → waitForFences → present
- update/draw で同じ `_drawFence` を共用中（Step 6 で分離予定）
- Staging buffer は現状フレームごとアロケート（Step 7 で再利用へ）

### Rendering
- Descriptor layout: binding 0 = UBO (viewport colors), binding 1 = CombinedImageSampler (content texture)
- Viewport ループで push constant (viewportIndex) を切り替えて複数ペイン描画
- Texture format: R8G8B8A8Unorm (content), B8G8R8A8Unorm (swapchain)

### Memory Layout
- Normalized Viewport coords (0-1) for resolution independence
- RAII via vk::Unique* handles for automatic cleanup
- Content ごとの shader/pipeline は段階導入
- Offscreen rendering は動画再生確認後に導入（Phase B Step 14）

---

## File Structure

```
src/
├── app/
│   ├── app.hpp              (App lifecycle - daia::app)
│   ├── CMakeLists.txt
│   ├── icon.png
│   └── main.cpp             (エントリポイント)
├── player/
│   ├── pipeline/
│   │   ├── pipeline.hpp     (Pipeline, SetupArgs, WrappedContent - daia::player::pipeline)
│   │   ├── viewport.hpp     (ViewportSet, PushConstant 等)
│   │   ├── helpers.hpp      (checkLayers, debug, shader ヘルパー)
│   │   └── shader/
│   │       ├── pane.vert
│   │       └── pane.frag
│   ├── common/
│   │   ├── util.hpp         (findMemoryType - daia::player::common)
│   │   └── texture.hpp      (Texture - daia::player::common)
│   ├── media/
│   │   └── video.hpp        (Video - daia::player::media)
│   ├── content/
│   │   ├── content.hpp      (集約ヘッダ: content_base + empty_content)
│   │   ├── content_base.hpp (Content base, SetupArgs, UpdateArgs - daia::player::content)
│   │   ├── empty_content.hpp(EmptyContent - daia::player::content)
│   │   └── video_content.hpp [TODO: Step 5c]
│   ├── window.hpp           (Window, Window::SetupInfo nested - daia::player)
│   └── CMakeLists.txt
└── util/
    ├── util.hpp             (float2/uint2 型エイリアス等)
    └── image.hpp            (stb_image ラッパー)
```

---

## Testing Checklist

### Phase A Verification
- [x] CMake finds FFmpeg libraries
- [x] video.hpp compiles and links
- [x] Staging buffer upload works (per-frame alloc)
- [x] Descriptor binding writes without errors (binding 0: UBO, binding 1: sampler)
- [x] EmptyContent red texture displays on screen
- [ ] Video file opens and frames decode
- [ ] Texture displays video frame on-screen correctly
- [ ] No memory leaks on shutdown

### Phase B Verification
- [ ] Multiple Pane rendering works
- [ ] Content switching at runtime works
- [ ] TimelineContent/UIContent render correctly
- [ ] Offscreen texture composition (shader/ImGui path) works

### Phase C Verification
- [ ] Multiple Viewports render simultaneously
- [ ] Content swapping works smoothly
- [ ] Input events route to correct Viewport/Content
- [ ] Window resize auto-adapts Viewport layout
- [ ] Dynamic Viewport add/remove functions
- [ ] No crashes under stress (rapid add/remove)

---

## Notes & Known Issues

### Current Limitations
- [ ] Video decode is software-first (CPU path)
- [ ] Texture format hardcoded to R8G8B8A8Unorm
- [ ] No frame rate synchronization
- [ ] Window is fixed-size (resizable in Phase C)
- [ ] Offscreen rendering not introduced yet (deferred to Phase B)

### Future Enhancements
- [ ] Frame rate limit (vsync, custom fps)
- [ ] Per-Viewport resolution override
- [ ] Viewport geometry persistence (save/load layouts)
- [ ] Content plugin system
- [ ] Advanced input (ImGui integration)
- [ ] Multi-file playlist support

---

## Session Log

### Session 2 (2026-03-06)

**Objective**: Plan FFmpeg integration + Content abstraction + Multi-viewport system architecture

**Actions Taken**:
1. Analyzed existing codebase (pipeline.hpp, media.hpp, app.hpp, window.hpp)
   - Found existing ViewportSet class (dynamic viewport/scissor approach)
   - Identified media.hpp (FFmpeg wrapper, previously restored)
   - Examined Texture struct (previously added in earlier session, now missing)
   - Analyzed Vulkan resource patterns (UniqueHandle RAII)

2. Designed complete system architecture:
   - **Phase A**: FFmpeg decoder integration (7 steps)
   - **Phase B**: Content abstraction layer (6 steps)
   - **Phase C**: Multi-viewport + input routing system (11 steps)

3. Clarified design decisions with user:
   - Each Viewport holds independent Content (not shared)
   - Each Content type has own shader/pipeline
   - Viewport layout dynamic (can add/remove at runtime)
   - Input events route through Viewport → Content hierarchy
   - Content can be swapped at runtime

4. Created IMPLEMENTATION_PLAN.md with detailed breakdown:
   - Step-by-step instructions for each phase
   - Code patterns and examples
   - Verification methods for each step

5. Identified missing Texture struct:
   - Was previously added but git checkout removed it
   - Step 0 added to plan to restore it

**Current Status**: 
- ✅ Architecture designed and documented
- ✅ Implementation plan created
- ⏳ Step 0 (Texture struct restoration) awaiting user implementation
- ⏳ Steps 1-7 (FFmpeg pipeline) ready for implementation

**Next**: User implements Step 0 (restore Texture struct), then proceeds with Step 1+

---

### Session 6 (2026-03-11)

**Objective**: アーキテクチャ整理 — Content/Pipeline/Texture の責務確定

**Actions Taken**:
1. Texture の所有者を Content → Pipeline へ移す方針を決定
   - Content は CPU ピクセルデータを提供するだけ
   - Pipeline が WrappedContent = { Content, Texture } で管理
2. 直接 viewport へ書く方式 (RenderPass 描画) も検討
   - アスペクト比フィットをシェーダー側で実現可能なことを確認
   - ただし将来的な再利用性を考え Texture 経由を維持
3. 1つの Content を複数ペインに表示する設計を確認
   - 同一 descriptor の共有で viewport ループ描画可能
4. staging buffer の管理責任を Pipeline に確定（bindBufferMemory 含む）

---

### Session 7 (2026-03-12)

**Objective**: Content インターフェース刷新 + レビュー + 計画更新

**Actions Taken**:
1. EmptyContent を大幅にリファクタリング
   - `UpdateInfo` → `UpdateArgs` に改名、Vulkan 依存を完全除去
   - `update()` が `bool` を返す形に変更（upload 要否の判定）
   - `size()` + `data()` でピクセル提供、`render()` を廃止
   - Texture メンバを EmptyContent から削除（Pipeline 側 WrappedContent で管理）
2. Pipeline::update() のアップロードロジック整理
   - content->update() が true のときのみ staging → copy
   - updateDescriptorSets() を waitIdle 後の末尾に移動
   - unregisterAllContents() が texture->destroy() も呼ぶように修正
3. App の globalTime を steady_clock で正しく計算するように修正
4. レビューで残存問題を特定:
   - Content に仮想デストラクタがない（UB）
   - Descriptor Pool の poolSize type/count 不足
   - WrappedContent::texture が不要な shared_ptr
   - ViewportSet::size() の const 欠落
   - Texture::upload() が dead code
   - update/draw が同じ fence を共用

**Current Status**:
- ✅ Content インターフェース整理完了（Vulkan 非依存に）
- ✅ Pipeline が Texture 所有 + upload + descriptor 更新を一括管理
- ⏳ Step 4a: コード品質の整理（仮想デストラクタ, descriptor pool, const 等）
- ⏳ Step 4b: update/draw のコマンド・同期分離

**Next**: Step 4a の修正 → Step 5 VideoContent → Step 6 同期分離

---

### Session 3-5 (2026-03-07)

**Objective**: Content abstraction layer の設計・実装

**Actions Taken**:
1. Step 0 (Texture struct) を texture.hpp に移動・復元 ✅
2. Step 1 (CMakeLists.txt FFmpeg linking) を pkg_check_modules で実装 ✅
3. Content base class 設計：
   - `content/content.hpp` に Content abstract class を定義
   - SetupInfo (device, physicalDevice), UpdateInfo (time, width, height)
   - 4つの pure virtual: setup(), destroy(), update(), render()
4. EmptyContent 実装：
   - Content を public 継承、Texture を create/destroy
5. Pipeline に content 管理を追加：
   - `_contents`: `unordered_map<string, shared_ptr<Content>>`
   - `registerContent(key, content)` / `unregisterContent(key)`
   - `update(globalTime)` で全 Content の update() を一括呼び出し
6. App の統合：
   - `_setup()` で EmptyContent を Pipeline に登録
   - `_update()` で `_pipeline.update(_globalTime)` に委譲
7. .vscode/tasks.json 作成（Ctrl+Shift+B で .local/build.sh 実行）

**レビューで指摘した既知の課題**:
- `_globalTime` 未初期化・未更新
- App の `_contents` ベクタが未使用
- Pipeline::destroy() で Content の destroy() 未呼び出し
- update() の viewport index ハードコード (0)

**Current Status**:
- ✅ Step 0-3 完了
- ⏳ Step 4 (VideoContent) 待ち
- 上記の既知課題は Step 8 (App Integration) で修正予定

**Next**: Step 4 (VideoContent) または Step 5 (Staging Buffer) に進む

---

### Session 8 (2026-03-16)

**Objective**: ディレクトリ構成の整理 + 計画更新

**Actions Taken**:
1. `player/common/` を解体して `player/` 直下へ統合
   - `common/texture.hpp` → `player/texture.hpp`
   - `common/util.hpp` → `player/util.hpp`
   - `common/` ディレクトリ削除
2. `src/app/` を新設し `app.hpp` と `main.cpp` を移動
   - `player/app.hpp` → `app/app.hpp`
   - `player/main.cpp` → `app/main.cpp`
3. include パスと CMakeLists.txt を新構成に合わせて修正
   - `app.hpp`: `#include "../player/pipeline.hpp"` 等
   - `pipeline.hpp`: `#include "texture.hpp"`（common/ 除去）
   - `CMakeLists.txt`: `add_executable(daia ../app/main.cpp)`
4. ビルド成功を確認
5. IMPLEMENTATION_PLAN.md のファイル構造・パス参照・ステップ番号を新構成に更新

**Current Status**:
- ✅ ディレクトリ構成整理完了
- ⏳ Step 5: VideoContent 実装開始待ち

**Next**: Step 5a (CMakeLists.txt FFmpeg リンク修正)

---

### Session 9 (2026-03-16〜17)

**Objective**: Step 5 VideoContent — FFmpeg リンク・CMake 分割・Video デコード実装

**Actions Taken**:
1. ディレクトリ構成整理
   - `player/common/` を解体し `player/` 直下へ統合
   - `src/app/` 新設（`app.hpp`, `main.cpp` を移動）
2. CMake 分割（案2 採用）
   - `src/player/CMakeLists.txt`: `player_core` INTERFACE ライブラリ + shader target
   - `src/app/CMakeLists.txt`: `daia` 実行ファイル + `player_core` リンク
   - root: 両方を `add_subdirectory`
3. FFmpeg リンク追加（`pkg_check_modules` + `PkgConfig::FFMPEG`）
4. `media.hpp` → `video.hpp` にリネーム、`Media` → `Video` クラスに変更
5. Video クラスにデコード機能実装
   - `setup()`: ファイルオープン + ストリーム検索 + デコーダオープン
   - `getFrame()`: パケット読み → デコード → sws_scale で RGBA 変換
   - `destroy()`: FFmpeg リソース解放
6. レビュー: バグ修正（`_videoStreamIndex` 未代入、全フレーム読み切り、linesize 型、avcodec_open2 チェック）
7. `.local/` スクリプト修正（`run.sh` のパス typo 修正、`build.sh` に `-S .` 追加）

**Design Decisions**:
- `getFrame()` は将来的にフレーム番号指定（Video 側はフレーム単位）
- Content 側は秒（`UpdateArgs.time`）で要求、VideoContent が `time × fps` でフレーム番号に変換
- 初回実装は「毎 update で次の 1 フレーム」で十分
- FFmpeg RAII ラッパーを各構造体ごとに作る方針（unique_ptr デフォルト deleter 問題の解消）

**Remaining**:
- 5b: RAII ラッパー作成 + SwsContext/Frame/Packet のメンバ化 + frame リーク修正
- 5c: `video_content.hpp` 作成
- 5d: App 統合 + テスト

**Next**: 5b RAII ラッパー完成 → 5c VideoContent → 5d テスト

### Session 10 (2026-03-17)

**Objective**: namespace とディレクトリ構造の一致 + 内部クラスの非公開化

**Actions Taken**:
1. `common/` ディレクトリ復活
   - `player/util.hpp` → `player/common/util.hpp` 移動（namespace `daia::player::common` そのまま）
   - `player/texture.hpp` → `player/common/texture.hpp` 移動 + namespace `daia::player` → `daia::player::common` に変更
2. `window.hpp` namespace 変更
   - `daia::window` → `daia::player::window`（ファイル位置は変更なし）
3. `media/` ディレクトリ新設
   - `player/video.hpp` → `player/media/video.hpp` 移動 + namespace `daia::media` → `daia::player::media`
4. `pipeline/` ディレクトリ新設・3ファイル分割
   - `pipeline/pipeline.hpp`: Pipeline 本体（WrappedContent, SetupInfo, checkDeviceExtensions）
   - `pipeline/viewport.hpp`: PushConstant, ViewportSource, ViewportSet
   - `pipeline/helpers.hpp`: checkLayers, debug messenger, createShaderModule
   - 旧 `player/pipeline.hpp` 削除
5. 全ファイルの include パス更新・ビルド成功確認

**Design Decisions**:
- Pipeline はディレクトリ分割（内部型が多く 1 ファイルが大きいため）
- Window は namespace 変更のみ（隠すクラスがない）
- video.hpp はクラス名 `Video` のまま、audio 対応時に再検討
- CMake 粒度は変更なし（`player_core` INTERFACE のまま）
- `content_base.hpp` を採用（`content_interface.hpp` ではなく。将来デフォルト実装追加の余地）
- ファイル命名規則は snake_case に統一

6. content.hpp 分割
   - `content_base.hpp`: Content base class, SetupArgs, UpdateArgs（`daia::player::content`）
   - `empty_content.hpp`: EmptyContent（`daia::player::content`）
   - `content.hpp`: 集約ヘッダ（上記 2 ファイルを include するだけ）
   - `pipeline.hpp` は `content_base.hpp` のみ include（具象クラス不要）
   - `app.hpp` は `content/content.hpp`（集約）を include
7. shader リネーム
   - `triangle.{vert,frag}` → `blit.{vert,frag}` → `pane.{vert,frag}`（UI content でも使うため）
   - shader ディレクトリを `player/shader/` → `player/pipeline/shader/` に移動
   - CMakeLists.txt のパスを更新
8. ファイル命名規則統一
   - `emptyContent.hpp` → `empty_content.hpp`（snake_case に統一）
9. ユーザーによる手動変更（namespace 整理）
   - `app.hpp`: namespace `daia::player` → `daia::app`
   - `window.hpp`: namespace `daia::player::window` → `daia::player`、`SetupInfo` を `Window::SetupInfo` にネスト
   - `Pipeline::SetupInfo` → `pipeline::SetupArgs` にリネーム（namespace スコープへ移動）

**Remaining**: Step 5b 以降は前回から変更なし

**Next**: 5b RAII ラッパー → 5c VideoContent → 5d テスト

---

## References

- [FFmpeg Documentation](https://ffmpeg.org/documentation.html)
- [Vulkan Specification](https://www.khronos.org/vulkan/)
- [vulkan-hpp RAII Wrappers](https://github.com/KhronosGroup/Vulkan-Hpp)
- [GLFW Input Guide](https://www.glfw.org/docs/latest/input.html)
