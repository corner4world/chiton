/**************************************************************************
 *
 *     This file is part of Chiton.
 *
 *   Chiton is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Chiton is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Chiton.  If not, see <https://www.gnu.org/licenses/>.
 *
 *   Copyright 2020 Ed Martin <edman007@edman007.com>
 *
 **************************************************************************
 */
#include "stream_writer.hpp"
#include "util.hpp"
#include "chiton_ffmpeg.hpp"
#include <assert.h>

StreamWriter::StreamWriter(Config& cfg) : cfg(cfg) {
    file_opened = false;
}

bool StreamWriter::open(void){
    int error;
    if (file_opened){
        return true;//already opened
    }
    
    AVOutputFormat *ofmt = output_format_context->oformat;
    stream_offset.clear();
    last_dts.clear();
    
    av_dump_format(output_format_context, 0, path.c_str(), 1);
    
    if (!(ofmt->flags & AVFMT_NOFILE)) {
        error = avio_open(&output_format_context->pb, path.c_str(), AVIO_FLAG_WRITE);
        if (error < 0) {
            LERROR("Could not open output file '" + path + "'");
            LERROR("Error occurred: " + std::string(av_err2str(error)));
            return false;
        }
    }

    error = avformat_write_header(output_format_context, NULL);
    if (error < 0) {
        LERROR("Error occurred when opening output file");
        LERROR("Error occurred: " + std::string(av_err2str(error)));
        return false;
    }
    file_opened = true;
    return true;

}


StreamWriter::~StreamWriter(){
    if (file_opened){
        close();
    }
    free_context();
}

void StreamWriter::close(void){
    if (!file_opened){
        LWARN("Attempted to close a output stream that wasn't open");
    }
    //flush it...
    if (0 > av_interleaved_write_frame(output_format_context, NULL)){
        LERROR("Error flushing muxing output for camera " + cfg.get_value("camera-id"));
    }

    av_write_trailer(output_format_context);
    file_opened = false;
}

bool StreamWriter::write(const AVPacket &packet, const AVStream *in_stream){
    AVStream *out_stream;
    AVPacket out_pkt;

    if (!file_opened){
        return false;
    }
    if (av_packet_ref(&out_pkt, &packet)){
        LERROR("Could not allocate new output packet for writing");
        return false;
    }
    
    if (out_pkt.stream_index >= static_cast<int>(stream_mapping.size()) ||
        stream_mapping[out_pkt.stream_index] < 0) {
        av_packet_unref(&out_pkt);
        return true;//we processed the stream we don't care about
    }
    //log_packet(unwrap.get_format_context(), packet, "in: " + path);
    out_pkt.stream_index = stream_mapping[out_pkt.stream_index];
    out_stream = output_format_context->streams[out_pkt.stream_index];

    /* copy packet */
    out_pkt.pts = av_rescale_q_rnd(out_pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    out_pkt.dts = av_rescale_q_rnd(out_pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    out_pkt.duration = av_rescale_q(out_pkt.duration, in_stream->time_base, out_stream->time_base);
    out_pkt.pos = -1;

    /* This should actually be used only for exporting video
    //correct for the offset, it is intentional that we base the offset on DTS (always first), and subtract it from DTS and PTS to
    //preserve any difference between them
    if (stream_offset[stream_mapping[out_pkt.stream_index]] < 0){
        stream_offset[stream_mapping[out_pkt.stream_index]] = out_pkt.dts;
    }
    out_pkt.dts -= stream_offset[stream_mapping[out_pkt.stream_index]];
    out_pkt.pts -= stream_offset[stream_mapping[out_pkt.stream_index]];
    */
    
    //guarentee that they have an increasing DTS
    if (out_pkt.dts < last_dts[stream_mapping[out_pkt.stream_index]]){
        LWARN("Shifting frame timestamp due to out of order issue in camera " + cfg.get_value("camera-id") +", old dts was: " + std::to_string(out_pkt.dts));
        last_dts[stream_mapping[out_pkt.stream_index]]++;
        long pts_delay = out_pkt.pts - out_pkt.dts;
        out_pkt.dts = last_dts[stream_mapping[out_pkt.stream_index]];
        out_pkt.pts = out_pkt.dts + pts_delay;
    } else if (out_pkt.dts == last_dts[stream_mapping[out_pkt.stream_index]]) {
        LWARN("Received duplicate frame from camera " + cfg.get_value("camera-id") +" at dts: " + std::to_string(out_pkt.dts) + ". Dropping Frame");
        av_packet_unref(&out_pkt);
        return true;
    }

    last_dts[stream_mapping[out_pkt.stream_index]] = out_pkt.dts;
    //log_packet(output_format_context, out_pkt, "out: "+path);

    int ret = av_interleaved_write_frame(output_format_context, &out_pkt);
    if (ret < 0) {
        LERROR("Error muxing packet for camera " + cfg.get_value("camera-id"));
        return false;
    }

    av_packet_unref(&out_pkt);
    return true;
}

void StreamWriter::log_packet(const AVFormatContext *fmt_ctx, const AVPacket &pkt, const std::string &tag){
    AVRational *time_base = &fmt_ctx->streams[pkt.stream_index]->time_base;
    LINFO(tag + ": pts:" + std::string(av_ts2str(pkt.pts)) +
          " pts_time:"+std::string(av_ts2timestr(pkt.pts, time_base))+
          " dts:" + std::string(av_ts2str(pkt.dts)) +
          " dts_time:"+ std::string(av_ts2timestr(pkt.dts, time_base)) +
          " duration:" +std::string(av_ts2str(pkt.duration)) +
          " duration_time:" + std::string(av_ts2timestr(pkt.duration, time_base))+
          " stream_index:" + std::to_string(pkt.stream_index)
    );
}

void StreamWriter::change_path(std::string &new_path){
    if (!new_path.empty()){
        path = new_path;
        free_context();
    }
}

void StreamWriter::free_context(void){
    if (output_format_context && !(output_format_context->flags & AVFMT_NOFILE)){
        avio_closep(&output_format_context->pb);
    }
    avformat_free_context(output_format_context);

    output_format_context = NULL;
    stream_mapping.clear();

    for (auto &encoder :  encode_ctx){
        avcodec_free_context(&encoder.second);
    }
}

bool StreamWriter::alloc_context(void){
    if (output_format_context){
        return true;
    }
    avformat_alloc_output_context2(&output_format_context, NULL, NULL, path.c_str());
    if (!output_format_context) {
        LERROR("Could not create output context");
        int error = AVERROR_UNKNOWN;
        LERROR("Error occurred: " + std::string(av_err2str(error)));
        return false;
    }
    return true;
}

bool StreamWriter::add_stream(const AVStream *in_stream){
    AVStream *out_stream = init_stream(in_stream);
    if (out_stream == NULL){
        return false;
    }
    int error = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    if (error < 0) {
        LERROR("Failed to copy codec parameters\n");
        LERROR("Error occurred: " + std::string(av_err2str(error)));
        return false;
    }
    out_stream->codecpar->codec_tag = 0;
    return true;
}

bool StreamWriter::copy_streams(StreamUnwrap &unwrap){
    bool ret = false;
    for (unsigned int i = 0; i < unwrap.get_stream_count(); i++) {
        ret |= add_stream(unwrap.get_format_context()->streams[i]);
    }
    return ret;
}

bool StreamWriter::add_encoded_stream(const AVStream *in_stream, const AVCodecContext *dec_ctx){
    if (dec_ctx == NULL){
        return false;
    }

    AVStream *out_stream = init_stream(in_stream);
    if (out_stream == NULL){
        return false;
    }

    AVCodec *encoder = NULL;
    //Audio
    if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO){
        if (cfg.get_value("encode-format-audio") == "ac3"){
            encoder = avcodec_find_encoder(AV_CODEC_ID_AC3);
        }
        if (!encoder) {//default or above option(s) failed
            encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
        }
        if (encoder){
            encode_ctx[out_stream->index] = avcodec_alloc_context3(encoder);
            if (!encode_ctx[out_stream->index]){
                LERROR("Could not alloc audio encoding context");
                return false;
            }
            encode_ctx[out_stream->index]->sample_rate = dec_ctx->sample_rate;
            encode_ctx[out_stream->index]->channel_layout = dec_ctx->channel_layout;
            encode_ctx[out_stream->index]->channels = av_get_channel_layout_nb_channels(encode_ctx[out_stream->index]->channel_layout);
            encode_ctx[out_stream->index]->sample_fmt = encoder->sample_fmts[0];
            encode_ctx[out_stream->index]->time_base = (AVRational){1, encode_ctx[out_stream->index]->sample_rate};
        } else {
            LWARN("Could not find audio encoder");
            return false;
        }

    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO){//video
        if (cfg.get_value("encode-format-video") == "hevc"){
            encoder = avcodec_find_encoder(AV_CODEC_ID_HEVC);
        }
        if (!encoder) {//default or above option(s) failed
            encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
        }
        if (encoder){
            encode_ctx[out_stream->index] = avcodec_alloc_context3(encoder);
            if (!encode_ctx[out_stream->index]){
                LERROR("Could not alloc video encoding context");
                return false;
            }
            encode_ctx[out_stream->index]->width = dec_ctx->width;
            encode_ctx[out_stream->index]->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;

            //take first pixel format
            if (encoder->pix_fmts) {
                encode_ctx[out_stream->index]->pix_fmt = encoder->pix_fmts[0];
            } else {
                encode_ctx[out_stream->index]->pix_fmt = dec_ctx->pix_fmt;
            }

            encode_ctx[out_stream->index]->time_base = in_stream->time_base;//av_inv_q(dec_ctx->framerate);
        } else {
            LWARN("Could not find video encoder");
            return false;
        }
    } else {
        return false;//not audio or video, can't encode it
    }

    
    if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER){
        encode_ctx[out_stream->index]->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    int ret = avcodec_open2(encode_ctx[out_stream->index], encoder, NULL);
    if (ret < 0) {
        LERROR("Cannot open encoder for stream " + std::to_string(out_stream->index));
        return false;
    }

    ret = avcodec_parameters_from_context(out_stream->codecpar, encode_ctx[out_stream->index]);
    if (ret < 0) {
        LERROR("Failed to copy encoder parameters to output stream " + std::to_string(out_stream->index));
        return false;
    }

    out_stream->time_base = encode_ctx[out_stream->index]->time_base;
    return true;
}

bool StreamWriter::write(const AVFrame *frame, const AVStream *in_stream){
    int ret = 0;
    AVPacket enc_pkt;
    av_init_packet(&enc_pkt);
    enc_pkt.data = NULL;
    enc_pkt.size = 0;
    ret = avcodec_send_frame(encode_ctx[stream_mapping[in_stream->index]], frame);
    if (ret < 0){
        LWARN("Error during encoding. Error code: " +  std::string(av_err2str(ret)));
        return false;
    }
    while (1) {
        ret = avcodec_receive_packet(encode_ctx[stream_mapping[in_stream->index]], &enc_pkt);
        if (ret){
            break;
        }
        enc_pkt.stream_index = in_stream->index;//revert stream index because write will adjust this

        bool write_ret = write(enc_pkt, in_stream);
        if (!write_ret){
            return false;
        }
    }
    return true;
}

AVStream *StreamWriter::init_stream(const AVStream *in_stream){
    if (!output_format_context){
        if (!alloc_context()){
            return NULL;
        }
    }

    if (in_stream == NULL){
        return NULL;
    }

    AVStream *out_stream = NULL;
    AVCodecParameters *in_codecpar = in_stream->codecpar;

    if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO &&
        in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO) {
        stream_mapping[in_stream->index] = -1;
        return NULL;
    }

    stream_mapping[in_stream->index] = output_format_context->nb_streams;

    //set the offset to -1, indicating unknown
    stream_offset.push_back(-1);
    last_dts.push_back(-1);

    out_stream = avformat_new_stream(output_format_context, NULL);
    if (!out_stream) {
        LERROR("Failed allocating output stream");
        LERROR("Error occurred: " + std::string(av_err2str(AVERROR_UNKNOWN)));
        stream_mapping[in_stream->index] = -1;
        return NULL;
    }
    return out_stream;
}
