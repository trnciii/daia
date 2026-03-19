#pragma once

#include <memory>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <filesystem>
#include <vector>

namespace daia { namespace player { namespace media {

class Video
{
public:
  bool setup(const std::filesystem::path& filepath)
  {
    {
      auto fc = avformat_alloc_context();
      if (avformat_open_input(&fc, reinterpret_cast<const char*>(filepath.u8string().c_str()), nullptr, nullptr) != 0)
      {
        fprintf(stderr, "Could not open input file.\n");
        return false;
      }
      _formatContext = std::unique_ptr<AVFormatContext>(fc);
    }

    if (avformat_find_stream_info(_formatContext.get(), nullptr) < 0)
    {
      fprintf(stderr, "Failed to retrieve stream info.\n");
      return false;
    }

    for (int i = 0; i < _formatContext->nb_streams; i++)
    {
      if (_formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
      {
        _videoStreamIndex = i;
        break;
      }
    }

    if (_videoStreamIndex < 0)
    {
      fprintf(stderr, "Failed to find video stream\n");
      return false;
    }

    const auto videoStream = _formatContext->streams[_videoStreamIndex];

    auto codec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    _codecContext = std::unique_ptr<AVCodecContext>(avcodec_alloc_context3(codec));
    avcodec_parameters_to_context(_codecContext.get(), videoStream->codecpar);
    if (avcodec_open2(_codecContext.get(), codec, nullptr) < 0)
    {
      fprintf(stderr, "Failed to open codec\n");
      return false;
    }

    return true;
  }

  std::vector<uint32_t> getFrame(int64_t frame) const
  {
    std::vector<uint32_t> buffer(width() * height());

    auto packet = av_packet_alloc();

    while (av_read_frame(_formatContext.get(), packet) >= 0)
    {
      if (packet->stream_index == _videoStreamIndex)
      {
        avcodec_send_packet(_codecContext.get(), packet);
        av_packet_unref(packet);

        auto frame = av_frame_alloc();
        int ret = avcodec_receive_frame(_codecContext.get(), frame);

        if (ret == AVERROR(EAGAIN))
        {
          // pass
        }
        else if (ret == 0)
        {
          auto swsContext = sws_getContext(
            width(),
            height(),
            _codecContext.get()->pix_fmt,
            width(),
            height(),
            AV_PIX_FMT_RGBA,
            SWS_BILINEAR,
            nullptr,
            nullptr,
            nullptr);
          int linesize = width() * 4;
          auto data = reinterpret_cast<uint8_t* const>(buffer.data());
          sws_scale(swsContext, frame->data, frame->linesize, 0, height(), &data, &linesize);

          sws_freeContext(swsContext);
          break;
        }
        else
        {
          // error
          break;
        }
        av_frame_free(&frame);
      }
      av_packet_unref(packet);
    }

    av_packet_free(&packet);

    return buffer;
  }

  int width() const
  {
    return _codecContext.get()->width;
  }

  int height() const
  {
    return _codecContext.get()->height;
  }

  void destroy()
  {
    {
      auto cc = _codecContext.release();
      avcodec_free_context(&cc);
    }
    {
      auto fc = _formatContext.release();
      avformat_close_input(&fc);
      avformat_free_context(fc);
    }
  }

private:
  std::unique_ptr<AVFormatContext> _formatContext;
  std::unique_ptr<AVCodecContext> _codecContext;
  int _videoStreamIndex = -1;
};

}}} // namespace daia::player::media
