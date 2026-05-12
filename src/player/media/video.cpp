#include "video.hpp"

#include <cassert>
#include <cstdio>

#include <libavutil/error.h>

#include "../util/util.hpp"

namespace daia { namespace player { namespace media {

// Packet
void Packet::Deleter::operator()(AVPacket* p)
{
  if (p)
  {
    av_packet_free(&p);
  }
}

AVPacket* Packet::get() const noexcept
{
  return _packet.get();
}

Packet::Packet()
{
  _packet = PacketPointer(av_packet_alloc());
}

int Packet::stream_index() const noexcept
{
  return _packet->stream_index;
}

void Packet::unref()
{
  if (_packet.get() != nullptr)
  {
    av_packet_unref(_packet.get());
  }
}

// Frame
void Frame::Deleter::operator()(AVFrame* p)
{
  if (p)
  {
    av_frame_free(&p);
  }
}

Frame::Frame()
{
  _frame = FramePointer(av_frame_alloc());
}

AVFrame* Frame::get() const noexcept
{
  return _frame.get();
}

uint8_t** Frame::data() const
{
  return _frame->data;
}

int* Frame::linesize() const noexcept
{
  return _frame->linesize;
}

// FormatContext
void FormatContext::Deleter::operator()(AVFormatContext* p) const noexcept
{
  if (p == nullptr)
  {
    return;
  }
  avformat_close_input(&p);
}

bool FormatContext::setup(const std::filesystem::path& filepath)
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

std::span<AVStream* const> FormatContext::streams()
{
  if (_context == nullptr || avformat_find_stream_info(_context.get(), nullptr) < 0)
  {
    return {};
  }
  return { _context->streams, _context->nb_streams };
}

FormatContext::ReadFrameResult FormatContext::read_frame(Packet& packet) const
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

// CodecContext
void CodecContext::Deleter::operator()(AVCodecContext* p) const noexcept
{
  avcodec_free_context(&p);
}

CodecContext::SendPacketResult CodecContext::_send_impl(const AVPacket* packetPtr) const
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

AVCodecContext* CodecContext::get() const noexcept
{
  return _context.get();
}

bool CodecContext::setup(const AVCodecParameters* params)
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

AVPixelFormat CodecContext::pixelFormat() const
{
  return _context.get()->pix_fmt;
}

int CodecContext::width() const noexcept
{
  return _context.get()->width;
}

int CodecContext::height() const noexcept
{
  return _context.get()->height;
}

CodecContext::SendPacketResult CodecContext::send_packet(const Packet& packet) const
{
  return _send_impl(packet.get());
}

CodecContext::SendPacketResult CodecContext::flush() const
{
  return _send_impl(nullptr);
}

CodecContext::ReceiveFrameResult CodecContext::receive_frame(const Frame& frame)
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

// DSwsContext
void DSwsContext::Deleter::operator()(SwsContext* p)
{
  sws_freeContext(p);
}

SwsContext* DSwsContext::get() const noexcept
{
  return _context.get();
}

void DSwsContext::setup(int width, int height, AVPixelFormat format)
{
  _context = ContextPointer(sws_getContext(width, height, format, width, height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr));
}

bool DSwsContext::scale(const Frame& frame, int width, int height, std::span<uint32_t> buffer)
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

// Decoder
bool Decoder::setup(int streamIndex, const AVCodecParameters* params)
{
  _streamIndex = streamIndex;
  if (streamIndex < 0)
  {
    return false;
  }
  return codecContext.setup(params);
}

int Decoder::stream_index() const noexcept
{
  return _streamIndex;
}

// Video
Video::ReadFrameResult Video::_read_frame(FormatContext& formatContext, Packet& packet, bool isPending) noexcept
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

void Video::_unref_packet()
{
  _isPacketPending = false;
  _packet.unref();
}

std::optional<std::reference_wrapper<Decoder>> Video::_find_decoder(const Packet& packet)
{
  if (packet.stream_index() == _videoDecoder.stream_index())
  {
    return _videoDecoder;
  }
  return std::nullopt;
}

bool Video::setup(const std::filesystem::path& filepath)
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

Video::FrameStatus Video::get_frame(int64_t frame, std::span<uint32_t> buffer)
{
  (void)frame;

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

int Video::width() const
{
  return _videoDecoder.codecContext.width();
}

int Video::height() const
{
  return _videoDecoder.codecContext.height();
}

}}} // namespace daia::player::media
