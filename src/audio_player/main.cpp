extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

#include <SDL.h>
#include <SDL_thread.h>
}

//#include <Windows.h>
#include <stdio.h>
#include <tchar.h>
#include <string.h>
#include <assert.h>


#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio  

//swr
struct SwrContext* au_convert_ctx; // 重采样上下文
int out_buffer_size; // 重采样后的缓冲区
uint8_t* out_buffer; // sdl调用音频数据的缓冲区

//audio decode
int au_stream_index = -1; // 音频流在文件中的位置
AVFormatContext* pFormatCtx = nullptr; // 文件上下文
AVCodecParameters* audioCodecParameter; // 音频解码器参数
AVCodecContext* audioCodecCtx = nullptr; // 音频解码器上下文
AVCodec* audioCodec = nullptr; // 音频解码器

// sdl
static Uint32 audio_len; // 音频数据缓冲区中未读数据剩余的长度
static Uint8* audio_pos; // 音频缓冲区中读取的位置
SDL_AudioSpec wanted_spec; // sdl播放音频的参数


void decode_audio_packet(AVCodecContext* dec_ctx, AVPacket* pkt, AVFrame* frame);

void sdl_audio_callback(void* udata, Uint8* stream, int len)
{
	//SDL 2.0  
	SDL_memset(stream, 0, len);
	if (audio_len == 0)
		return;
	len = ((Uint32)len > audio_len ? audio_len : len); /*  Mix  as  much  data  as  possible  */

	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	audio_pos += len;
	audio_len -= len;
}

int init_audio_parameters() {
	// 获取音频解码器参数
	audioCodecParameter = pFormatCtx->streams[au_stream_index]->codecpar;
	// 获取音频解码器
	audioCodec = avcodec_find_decoder(audioCodecParameter->codec_id);
	if (audioCodec == nullptr) {
		printf_s("audio avcodec_find_decoder failed.\n");
		return -1;
	}
	// 获取解码器上下文
	audioCodecCtx = avcodec_alloc_context3(audioCodec);
	if (avcodec_parameters_to_context(audioCodecCtx, audioCodecParameter) < 0) {
		printf_s("audio avcodec_parameters_to_context failed\n");
		return -1;
	}
	// 根据上下文配置音频解码器
	avcodec_open2(audioCodecCtx, audioCodec, nullptr);
	// -------------------设置重采样相关参数-------------------------//
	uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO; // 双声道输出
	int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);

	AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16; // 输出的音频格式
	int out_sample_rate = 44100; // 采样率
	int64_t in_channel_layout = av_get_default_channel_layout(audioCodecCtx->channels); //输入通道数
	audioCodecCtx->channel_layout = in_channel_layout;
	au_convert_ctx = swr_alloc(); // 初始化重采样结构体
	au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
		in_channel_layout, audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate, 0, nullptr); //配置重采样率
	swr_init(au_convert_ctx); // 初始化重采样率

	int out_nb_samples = audioCodecCtx->frame_size;
	// 计算出重采样后需要的buffer大小，后期储存转换后的音频数据时用
	out_buffer_size = av_samples_get_buffer_size(NULL, out_channels/*audioCodecCtx->channels*/, out_nb_samples, out_sample_fmt, 1);
	out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
	// 设置 SDL播放音频时的参数 
	wanted_spec.freq = out_sample_rate;//44100;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = out_channels;//audioCodecCtx->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = out_nb_samples;//frame->nb_samples;
	wanted_spec.callback = sdl_audio_callback;
	wanted_spec.userdata = nullptr; // 回调时想带进去的参数

	if (SDL_OpenAudio(&wanted_spec, NULL) < 0)
	{
		printf("can't open audio.\n");
		return -1;
	}

	SDL_PauseAudio(0);
	return 0;
}

int _tmain(int argc, char** argv)
{
	char* base_path = SDL_GetBasePath();
	char filePath[256];
	strcpy_s(filePath, base_path);
	strcat_s(filePath, "Let Her Go-J.Fla.aac");
	//初始化
	av_register_all();

	//读取文件头和存储信息到pFormateCtx中
	pFormatCtx = avformat_alloc_context();
	if (avformat_open_input(&pFormatCtx, filePath, nullptr, nullptr) != 0) {
		printf_s("avformat_open_input failed\n");
		system("pause");
		return -1;
	}

	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
		printf_s("avformat_find_stream_info failed\n");
		system("pause");
		return -1;
	}
	av_dump_format(pFormatCtx, 0, filePath, 0);

	for (unsigned i = 0; i < pFormatCtx->nb_streams; ++i)
	{
		if (AVMEDIA_TYPE_AUDIO == pFormatCtx->streams[i]->codecpar->codec_type) {
			au_stream_index = i;
			continue;
		}
	}

	if (-1 == au_stream_index) {
		printf_s("Can't find audio stream\n");
		system("pause");
		return -1;
	}
	//Init SDL
	if (SDL_Init(/*SDL_INIT_VIDEO | */SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	if (init_audio_parameters() < 0) {
		return -1;
	}

	AVPacket packet;
	AVFrame* pFrame = NULL;
	while (av_read_frame(pFormatCtx, &packet) >= 0)
	{
		if (packet.stream_index == au_stream_index)
		{
			if (!pFrame)
			{
				if (!(pFrame = av_frame_alloc()))
				{
					fprintf(stderr, "Could not allocate audio frame\n");
					system("pause");
					exit(1);
				}
			}
			if (packet.size) {
				decode_audio_packet(audioCodecCtx, &packet, pFrame);
			}

			//av_frame_free(&pFrame);
			av_frame_unref(pFrame);
			av_packet_unref(&packet);
		}
	}

	SDL_CloseAudio();//Close SDL  
	SDL_Quit();

	avcodec_parameters_free(&audioCodecParameter);
	avcodec_free_context(&audioCodecCtx);
	av_free(pFrame);
	swr_free(&au_convert_ctx);
	av_free(audioCodecCtx);
	av_free(out_buffer);

	return 0;
}

void decode_audio_packet(AVCodecContext * code_context, AVPacket * pkt, AVFrame * frame)
{
	int i, ch;
	int ret, data_size;
	/* send the packet with the compressed data to the decoder */
	ret = avcodec_send_packet(code_context, pkt);
	if (ret < 0)
	{
		fprintf(stderr, "Error submitting the packet to the decoder\n");
		system("pause");
		exit(1);
	}
	/* read all the output frames (in general there may be any number of them */
	while (ret >= 0)
	{
		ret = avcodec_receive_frame(code_context, frame);

		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			return;
		else if (ret < 0)
		{
			fprintf(stderr, "Error during decoding\n");
			system("pause");
			exit(1);
		}
		data_size = av_get_bytes_per_sample(code_context->sample_fmt);
		if (data_size < 0)
		{
			/* This should not occur, checking just for paranoia */
			fprintf(stderr, "Failed to calculate data size\n");
			system("pause");
			exit(1);
		}
		swr_convert(au_convert_ctx, &out_buffer, out_buffer_size,
			(const uint8_t * *)frame->data, code_context->frame_size/*frame->nb_samples*/);

		while (audio_len > 0)//Wait until finish  
			SDL_Delay(1);

		//Set audio buffer (PCM data)  
		//Audio buffer length  
		audio_len = out_buffer_size;
		audio_pos = (Uint8*)out_buffer;
	}
}