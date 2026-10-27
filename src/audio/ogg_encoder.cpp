#include "audio/ogg_encoder.hpp"

#include <algorithm>
#include <cstddef>
#include <random>
#include <stdexcept>

#include <vorbis/vorbisenc.h>

namespace m2n::audio {

std::vector<uint8_t> encodeOggVorbis(const float* interleaved, size_t frameCount, int sampleRate,
                                     float quality) {
  vorbis_info vi;
  vorbis_info_init(&vi);
  if (vorbis_encode_init_vbr(&vi, 2, sampleRate, quality) != 0) {
    vorbis_info_clear(&vi);
    throw std::runtime_error("Vorbis 编码器初始化失败");
  }

  vorbis_dsp_state dsp;
  vorbis_block block;
  vorbis_analysis_init(&dsp, &vi);
  vorbis_block_init(&dsp, &block);

  vorbis_comment comment;
  vorbis_comment_init(&comment);
  vorbis_comment_add_tag(&comment, "ENCODER", "Luxumbra's M2N");

  ogg_stream_state stream;
  std::mt19937 seededRng(0x4D324E);  // 确定性串号
  std::uniform_int_distribution<int> serialDist(0, 0x7FFFFFFF);
  ogg_stream_init(&stream, serialDist(seededRng));

  std::vector<uint8_t> output;

  auto writePages = [&](ogg_stream_state* s, bool flush) {
    ogg_page page;
    while ((flush ? ogg_stream_flush(s, &page) : ogg_stream_pageout(s, &page)) != 0) {
      output.insert(output.end(), page.header, page.header + page.header_len);
      output.insert(output.end(), page.body, page.body + page.body_len);
    }
  };

  // 头部
  ogg_packet header, headerComm, headerCode;
  vorbis_analysis_headerout(&dsp, &comment, &header, &headerComm, &headerCode);
  ogg_stream_packetin(&stream, &header);
  ogg_stream_packetin(&stream, &headerComm);
  ogg_stream_packetin(&stream, &headerCode);
  writePages(&stream, true);

  // 数据
  auto encodeBlock = [&]() {
    while (vorbis_analysis_blockout(&dsp, &block) == 1) {
      vorbis_analysis(&block, nullptr);
      vorbis_bitrate_addblock(&block);
      ogg_packet packet;
      while (vorbis_bitrate_flushpacket(&dsp, &packet)) {
        ogg_stream_packetin(&stream, &packet);
      }
    }
  };

  const size_t kChunk = 4096;
  size_t done = 0;
  while (done < frameCount) {
    size_t n = std::min(kChunk, frameCount - done);
    float** buf = vorbis_analysis_buffer(&dsp, static_cast<int>(n));
    for (size_t f = 0; f < n; ++f) {
      buf[0][f] = interleaved[2 * (done + f)];
      buf[1][f] = interleaved[2 * (done + f) + 1];
    }
    vorbis_analysis_wrote(&dsp, static_cast<int>(n));
    done += n;
    encodeBlock();
    writePages(&stream, false);
  }
  vorbis_analysis_wrote(&dsp, 0);  // EOF
  encodeBlock();
  writePages(&stream, true);

  ogg_stream_clear(&stream);
  vorbis_block_clear(&block);
  vorbis_dsp_clear(&dsp);
  vorbis_comment_clear(&comment);
  vorbis_info_clear(&vi);
  return output;
}

}  // namespace m2n::audio
