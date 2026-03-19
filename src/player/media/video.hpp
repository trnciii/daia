#pragma once

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <libavutil/error.h>
#include <libavutil/pixfmt.h>
#include <memory>
#include <optional>
#include <span>
#include <vulkan/vulkan_core.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include "../util/util.hpp"

namespace daia { namespace player { namespace media {

class Packet
{
  struct Deleter
  {
    void operator()(AVPacket* p)
    {
      if (p)
      {
        av_packet_free(&p);
      }
    }
  };

  using PacketPointer = std::unique_ptr<AVPacket, Deleter>;
  PacketPointer _packet;

  friend class FormatContext;
  friend class CodecContext;

  AVPacket* get() const noexcept
  {
    return _packet.get();
  }

public:
  int stream_index() const noexcept
  {
    return _packet->stream_index;
  }

  void unref()
  {
    if (_packet.get() != nullptr)
    {
      av_packet_unref(_packet.get());
    }
  }

  Packet()
  {
    _packet = PacketPointer(av_packet_alloc());
  }
};

class Frame
{
  struct Deleter
  {
    void operator()(AVFrame* p)
    {
      if (p)
      {
        av_frame_free(&p);
      }
    }
  };

  using FramePointer = std::unique_ptr<AVFrame, Deleter>;
  FramePointer _frame;

public:
  Frame()
  {
    _frame = FramePointer(av_frame_alloc());
  }

  AVFrame* get() const noexcept
  {
    return _frame.get();
  }

  uint8_t** data() const
  {
    return _frame->data;
  }

  int* linesize() const noexcept
  {
    return _frame->linesize;
  }
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

private:
  struct Deleter
  {
    void operator()(AVFormatContext* p) const noexcept
    {
      if (p == nullptr)
      {
        return;
      }
      avformat_close_input(&p);
    }
  };

  using ContextPointer = std::unique_ptr<AVFormatContext, Deleter>;
  ContextPointer _context;

public:
  bool setup(const std::filesystem::path& filepath)
  {
    auto fc = avformat_alloc_context();
    if (fc == nullptr || avformat_open_input(&fc, reinterpret_cast<const char*>(filepath.u8string().c_str()), nullptr, nullptr) != 0)
    {
      fprintf(stderr, "Could not open input file.\n");
      return false;
    }
    _context = ContextPointer(fc);
    return true;
  }

  std::span<AVStream* const> streams()
  {
    if (_context == nullptr || avformat_find_stream_info(_context.get(), nullptr) < 0)
    {
      return {};
    }
    return { _context->streams, _context->nb_streams };
  }

  ReadFrameResult read_frame(Packet& packet) const
  {
    packet.unref();
    auto result = av_read_frame(_context.get(), packet.get());
    if (result == 0)
    {
      return ReadFrameResult::Read;
    }
    if (result == AVERROR_EOF)
    {
      return ReadFrameResult::Eof;
    }
    return ReadFrameResult::Error;
  }
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

private:
  struct Deleter
  {
    void operator()(AVCodecContext* p) const noexcept
    {
      avcodec_free_context(&p);
    }
  };

  using ContextPointer = std::unique_ptr<AVCodecContext, Deleter>;
  ContextPointer _context;

  SendPacketResult _send_impl(const AVPacket* packetPtr) const
  {
    auto result = avcodec_send_packet(_context.get(), packetPtr);
    if (result == 0)
    {
      return SendPacketResult::Success;
    }
    if (result == AVERROR(EAGAIN))
    {
      return SendPacketResult::Eagain;
    }
    return SendPacketResult::Error;
  }

public:
  AVCodecContext* get() const noexcept
  {
    return _context.get();
  }

  bool setup(const AVCodecParameters* params)
  {
    auto _codec = avcodec_find_decoder(params->codec_id);
    _context = ContextPointer(avcodec_alloc_context3(_codec));
    avcodec_parameters_to_context(_context.get(), params);
    if (avcodec_open2(_context.get(), _codec, nullptr) < 0)
    {
      fprintf(stderr, "Failed to open codec\n");
      return false;
    }

    return true;
  }

  AVPixelFormat pixelFormat() const
  {
    return _context.get()->pix_fmt;
  }

  int width() const noexcept
  {
    return _context.get()->width;
  }

  int height() const noexcept
  {
    return _context.get()->height;
  }

  SendPacketResult send_packet(const Packet& packet) const
  {
    return _send_impl(packet.get());
  }

  SendPacketResult flush() const
  {
    return _send_impl(nullptr);
  }

  ReceiveFrameResult receive_frame(const Frame& frame)
  {
    auto result = avcodec_receive_frame(_context.get(), frame.get());
    if (result == 0)
    {
      return ReceiveFrameResult::Success;
    }
    if (result == AVERROR(EAGAIN))
    {
      return ReceiveFrameResult::Eagain;
    }
    if (result == AVERROR_EOF)
    {
      return ReceiveFrameResult::Eof;
    }
    return ReceiveFrameResult::Error;
  }
};

class DSwsContext
{
  struct Deleter
  {
    void operator()(SwsContext* p)
    {
      sws_freeContext(p);
    }
  };

  using ContextPointer = std::unique_ptr<SwsContext, Deleter>;

  ContextPointer _context;

public:
  SwsContext* get() const noexcept
  {
    return _context.get();
  }

  void setup(int width, int height, AVPixelFormat format)
  {
    _context = ContextPointer(sws_getContext(width, height, format, width, height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr));
  }

  bool scale(const Frame& frame, int width, int height, std::span<uint32_t> buffer)
  {
    if (buffer.size() < width * height)
    {
      return false;
    }

    int linesize = width * 4;
    auto data = reinterpret_cast<uint8_t* const>(buffer.data());
    sws_scale(_context.get(), frame.data(), frame.linesize(), 0, height, &data, &linesize);
    return true;
  }
};

class Decoder
{
  int _streamIndex = -1;

public:
  CodecContext codecContext;

  bool setup(int streamIndex, const AVCodecParameters* params)
  {
    _streamIndex = streamIndex;
    if (streamIndex < 0)
    {
      return false;
    }
    return codecContext.setup(params);
  }

  int stream_index() const noexcept
  {
    return _streamIndex;
  }
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

private:
  enum class ReadFrameResult
  {
    Read,
    Skip,
    Eof,
    Error,
  };

  static ReadFrameResult _read_frame(FormatContext& formatContext, Packet& packet, bool isPending) noexcept
  {
    if (isPending)
    {
      return ReadFrameResult::Skip;
    }

    auto result = formatContext.read_frame(packet);
    if (result == FormatContext::ReadFrameResult::Read)
    {
      return ReadFrameResult::Read;
    }

    if (result == FormatContext::ReadFrameResult::Eof)
    {
      return ReadFrameResult::Eof;
    }
    return ReadFrameResult::Error;
  }

  void _unref_packet()
  {
    _isPacketPending = false;
    _packet.unref();
  }

  std::optional<std::reference_wrapper<Decoder>> _find_decoder(const Packet& packet)
  {
    if (packet.stream_index() == _videoDecoder.stream_index())
    {
      return _videoDecoder;
    }
    return std::nullopt;
  }

  FormatContext _formatContext;
  Packet _packet;
  bool _isPacketPending = false;
  Decoder _videoDecoder;
  Frame _frame;
  DSwsContext _swsContext;

public:
  bool setup(const std::filesystem::path& filepath)
  {
    if (!_formatContext.setup(filepath))
    {
      return false;
    }

    const auto& streams = _formatContext.streams();
    int videoStreamIndex = -1;
    for (auto i = 0; i < streams.size(); i++)
    {
      if (streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
      {
        videoStreamIndex = i;
        break;
      }
    }

    if (!_videoDecoder.setup(videoStreamIndex, streams[videoStreamIndex]->codecpar))
    {
      return false;
    }

    _swsContext.setup(_videoDecoder.codecContext.width(), _videoDecoder.codecContext.height(), _videoDecoder.codecContext.pixelFormat());

    return true;
  }

  FrameStatus get_frame(int64_t frame, std::span<uint32_t> buffer)
  {
    auto width = _videoDecoder.codecContext.width();
    auto height = _videoDecoder.codecContext.height();

    if (width <= 0 || height <= 0)
    {
      return FrameStatus::InvalidVideoSize;
    }

    while (true)
    {
      auto readResult = _read_frame(_formatContext, _packet, _isPacketPending);
      if (readResult == ReadFrameResult::Error)
      {
        return FrameStatus::NoPacket;
      }

      std::optional<std::reference_wrapper<Decoder>> decoder = std::nullopt;
      if (readResult == ReadFrameResult::Eof)
      {
        decoder = _videoDecoder; // audio が増えたら全部flush する必要がある
        const auto result = decoder.value().get().codecContext.flush();
        if (result == CodecContext::SendPacketResult::Error)
        {
          return FrameStatus::SendError;
        }
        if (result != CodecContext::SendPacketResult::Eagain)
        {
          _isPacketPending = false;
        }
        util::println("eof"); // pass to drain
      }
      else // packet ready
      {
        decoder = _find_decoder(_packet);
        if (!decoder.has_value())
        {
          _unref_packet();
          continue;
        }

        auto sendResult = decoder.value().get().codecContext.send_packet(_packet);
        if (sendResult == CodecContext::SendPacketResult::Eagain)
        {
          _isPacketPending = true;
          util::println("send eagain");
          // pass to drain
        }
        else
        {
          _unref_packet();
          if (sendResult == CodecContext::SendPacketResult::Error)
          {
            return FrameStatus::SendError;
          }
        }
      }

      while (true)
      {
        assert(decoder.has_value());

        auto receiveResult = decoder.value().get().codecContext.receive_frame(_frame);
        if (receiveResult == CodecContext::ReceiveFrameResult::Eagain)
        {
          util::println("receive eagain");
          break; // send again
        }
        else if (receiveResult == CodecContext::ReceiveFrameResult::Eof)
        {
          return FrameStatus::Eof;
        }
        else if (receiveResult == CodecContext::ReceiveFrameResult::Success)
        {
          if (_swsContext.scale(_frame, width, height, buffer))
          {
            return FrameStatus::Success;
          }
          else
          {
            return FrameStatus::SwsError;
          }
        }
        else
        {
          return FrameStatus::ReceiveError;
        }
      }
    }
  }

  int width() const
  {
    return _videoDecoder.codecContext.width();
  }

  int height() const
  {
    return _videoDecoder.codecContext.height();
  }
};

}}} // namespace daia::player::media
