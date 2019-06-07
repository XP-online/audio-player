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

#include <stdio.h>
#include <tchar.h>
#include <string.h>
#include <assert.h>

#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio  

//swr
struct SwrContext* au_convert_ctx; // �ز���������
int out_buffer_size; // �ز�����Ļ�����
uint8_t* out_buffer; // sdl������Ƶ���ݵĻ�����

//audio decode
int au_stream_index = -1; // ��Ƶ�����ļ��е�λ��
AVFormatContext* pFormatCtx = nullptr; // �ļ�������
AVCodecParameters* audioCodecParameter; // ��Ƶ����������
AVCodecContext* audioCodecCtx = nullptr; // ��Ƶ������������
AVCodec* audioCodec = nullptr; // ��Ƶ������

// sdl
static Uint32 audio_len; // ��Ƶ���ݻ�������δ������ʣ��ĳ���
static Uint8* audio_pos; // ��Ƶ�������ж�ȡ��λ��
SDL_AudioSpec wanted_spec; // sdl������Ƶ�Ĳ���


void decode_audio_packet(AVCodecContext* dec_ctx, AVPacket* pkt, AVFrame* frame);
// sdl�����е�ϵͳ������Ƶ�Ļص���
// udata�������Լ����õĲ�����
// stream��ϵͳ��ȡ��Ƶ���ݵ�buffer����������������а���Ƶ���ݿ��������buffer�У�
// len��ϵͳϣ����ȡ�ĳ��ȣ����Ա����С�������ܸ��ࣩ
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

// ��ʼ�����������ز���������ĸ������
int init_audio_parameters() {
	// ��ȡ��Ƶ����������
	audioCodecParameter = pFormatCtx->streams[au_stream_index]->codecpar;
	// ��ȡ��Ƶ������
	audioCodec = avcodec_find_decoder(audioCodecParameter->codec_id);
	if (audioCodec == nullptr) {
		printf_s("audio avcodec_find_decoder failed.\n");
		return -1;
	}
	// ��ȡ������������
	audioCodecCtx = avcodec_alloc_context3(audioCodec);
	if (avcodec_parameters_to_context(audioCodecCtx, audioCodecParameter) < 0) {
		printf_s("audio avcodec_parameters_to_context failed\n");
		return -1;
	}
	// ����������������Ƶ������
	avcodec_open2(audioCodecCtx, audioCodec, nullptr);
	// -------------------�����ز�����ز���-------------------------//
	uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO; // ˫�������
	int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);

	AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16; // �������Ƶ��ʽ
	int out_sample_rate = 44100; // ������
	int64_t in_channel_layout = av_get_default_channel_layout(audioCodecCtx->channels); //����ͨ����
	audioCodecCtx->channel_layout = in_channel_layout;
	au_convert_ctx = swr_alloc(); // ��ʼ���ز����ṹ��
	au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
		in_channel_layout, audioCodecCtx->sample_fmt, audioCodecCtx->sample_rate, 0, nullptr); //�����ز�����
	swr_init(au_convert_ctx); // ��ʼ���ز�����

	int out_nb_samples = audioCodecCtx->frame_size;
	// ������ز�������Ҫ��buffer��С�����ڴ���ת�������Ƶ����ʱ��
	out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);
	out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
	// ���� SDL������Ƶʱ�Ĳ��� 
	wanted_spec.freq = out_sample_rate;//44100;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = out_channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = out_nb_samples;
	wanted_spec.callback = sdl_audio_callback; //sdlϵͳ�����������˵��
	wanted_spec.userdata = nullptr; // �ص�ʱ�����ȥ�Ĳ���

	if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
		printf_s("can't open audio.\n");
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
	//��ʼ��
	av_register_all();

	//��ȡ�ļ�ͷ���ļ�������Ϣ��pFormateCtx��
	pFormatCtx = avformat_alloc_context();
	if (avformat_open_input(&pFormatCtx, filePath, nullptr, nullptr) != 0) {
		printf_s("avformat_open_input failed\n");
		system("pause");
		return -1;
	}
	// ���ļ����ҵ��ļ��е���Ƶ������Ƶ���ȡ�������Ϣ
	if (avformat_find_stream_info(pFormatCtx, nullptr) < 0) {
		printf_s("avformat_find_stream_info failed\n");
		system("pause");
		return -1;
	}
	// ������������Ǳ���ģ�ֻ���������ӡ���ļ��Ļ�����Ϣ
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
	if (SDL_Init(/*SDL_INIT_VIDEO | */SDL_INIT_AUDIO /*| SDL_INIT_TIMER*/)) { //��������ֻ��Ҫ��Ƶ������ֻ������SDL_INIT_AUDIO
		printf_s("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	if (init_audio_parameters() < 0) {
		return -1;
	}

	AVPacket packet;
	AVFrame* pFrame = NULL;
	// ��ʼ��ȡ�ļ��б�������Ƶ���ݣ��������������ݴ�����
	while (av_read_frame(pFormatCtx, &packet) >= 0)
	{
		if (packet.stream_index == au_stream_index)
		{
			if (!pFrame)
			{
				if (!(pFrame = av_frame_alloc()))
				{
					printf_s("Could not allocate audio frame\n");
					system("pause");
					exit(1);
				}
			}
			if (packet.size) {
				// �Զ�ȡ����pkt���룬�������ݴ��ݸ���Ƶ���ݻ�����
				decode_audio_packet(audioCodecCtx, &packet, pFrame);
			}

			//av_frame_free(&pFrame);
			av_frame_unref(pFrame);
			av_packet_unref(&packet);
		}
	}

	// Close SDL  
	SDL_CloseAudio();
	SDL_Quit();

	avcodec_parameters_free(&audioCodecParameter);
	avcodec_free_context(&audioCodecCtx);
	av_free(pFrame);
	swr_free(&au_convert_ctx);
	av_free(audioCodecCtx);
	av_free(out_buffer);

	return 0;
}
// ����ȡ����һ����Ƶpkt�����avframe��avframe�е����ݾ���ԭʼ����Ƶ����
void decode_audio_packet(AVCodecContext * code_context, AVPacket * pkt, AVFrame * frame)
{
	int i, ch;
	int ret, data_size;
	// ffmpeg3.2�汾���Ƽ�ʹ�õķ�ʽ����һ��pkt���͸���������֮����avcodec_receive_frame��ȡ��������avframe
	ret = avcodec_send_packet(code_context, pkt);
	if (ret < 0)
	{
		printf_s("Error submitting the packet to the decoder\n");
		system("pause");
		exit(1);
	}
	// ���ϳ���ȡ����Ƶ���ݣ�ֱ���޷���ȡ��
	while (ret >= 0)
	{
		ret = avcodec_receive_frame(code_context, frame); // ǰ���Ѿ����ܣ��ڴ˴�ȡ��ԭʼ��Ƶ����

		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) //��֡Ŀǰ�޷��������Ҫ�ٷ���һ��pkt
			return;
		else if (ret < 0)
		{
			printf_s("Error during decoding\n");
			system("pause");
			exit(1);
		}
		// ����Ƶ�Ĳ�����ת���ɱ����ܲ����Ĳ�����
		swr_convert(au_convert_ctx, &out_buffer, out_buffer_size,
			(const uint8_t * *)frame->data, code_context->frame_size);

		while (audio_len > 0) // �ڴ˴��ȴ�sdl_audio_callback��֮ǰ���ݵ���Ƶ���ݲ������������з����µ�����
			SDL_Delay(1);

		//Set audio buffer (PCM data)  
		audio_len = out_buffer_size; // Audio buffer length 
		audio_pos = (Uint8*)out_buffer;
	}
}