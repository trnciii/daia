#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

namespace daia { namespace player { namespace media {

class Packet
{
public:
  Packet();
  int stream_index() const noexcept;
  void unref();

private:
  struct Deleter
  {
    void operator()(AVPacket* p);
  };

  using PacketPointer = std::unique_ptr<AVPacket, Deleter>;
  PacketPointer _packet;

  friend class FormatContext;
  friend class CodecContext;

  AVPacket* get() const noexcept;
};

class Frame
{
public:
  Frame();
  AVFrame* get() const noexcept;
  uint8_t** data() const;
  int* linesize() const noexcept;

private:
  struct Deleter
  {
    void operator()(AVFrame* p);
  };

  using FramePointer = std::unique_ptr<AVFrame, Deleter>;
  FramePointer _frame;
};

class FormatContext
{
public:
  enum class ReadFrameResult
  {
    Read,
    Eof,
    Error,
  };

  bool setup(const std::filesystem::path& filepath);
  std::span<AVStream* const> streams();
  ReadFrameResult read_frame(Packet& packet) const;

private:
  struct Deleter
  {
    void operator()(AVFormatContext* p) const noexcept;
  };

  using ContextPointer = std::unique_ptr<AVFormatContext, Deleter>;
  ContextPointer _context;
};

class CodecContext
{
public:
  enum class SendPacketResult
  {
    Success,
    Eagain,
    Error,
  };

  enum class ReceiveFrameResult
  {
    Success,
    Eagain,
    Eof,
    Error,
  };

  AVCodecContext* get() const noexcept;
  bool setup(const AVCodecParameters* params);
  AVPixelFormat pixelFormat() const;
  int width() const noexcept;
  int height() const noexcept;
  SendPacketResult send_packet(const Packet& packet) const;
  SendPacketResult flush() const;
  ReceiveFrameResult receive_frame(const Frame& frame);

private:
  struct Deleter
  {
    void operator()(AVCodecContext* p) const noexcept;
  };

  using ContextPointer = std::unique_ptr<AVCodecContext, Deleter>;
  ContextPointer _context;

  SendPacketResult _send_impl(const AVPacket* packetPtr) const;
};

class DSwsContext
{
public:
  SwsContext* get() const noexcept;
  void setup(int width, int height, AVPixelFormat format);
  bool scale(const Frame& frame, int width, int height, std::span<uint32_t> buffer);

private:
  struct Deleter
  {
    void operator()(SwsContext* p);
  };

  using ContextPointer = std::unique_ptr<SwsContext, Deleter>;
  ContextPointer _context;
};

class Decoder
{
public:
  CodecContext codecContext;

  bool setup(int streamIndex, const AVCodecParameters* params);
  int stream_index() const noexcept;

private:
  int _streamIndex = -1;
};

class Video
{
public:
  enum class FrameStatus
  {
    Success,
    InvalidVideoSize,
    NoPacket,
    SwsError,
    SendError,
    ReceiveError,
    Eof,
    Unknown,
  };

  bool setup(const std::filesystem::path& filepath);
  FrameStatus get_frame(int64_t frame, std::span<uint32_t> buffer);
  int width() const;
  int height() const;

private:
  enum class ReadFrameResult
  {
    Read,
    Skip,
    Eof,
    Error,
  };

  static ReadFrameResult _read_frame(FormatContext& formatContext, Packet& packet, bool isPending) noexcept;
  void _unref_packet();
  std::optional<std::reference_wrapper<Decoder>> _find_decoder(const Packet& packet);

  FormatContext _formatContext;
  Packet _packet;
  bool _isPacketPending = false;
  Decoder _videoDecoder;
  Frame _frame;
  DSwsContext _swsContext;
};

}}} // namespace daia::player::media
