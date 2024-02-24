#include <Tutorial.h>

char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

#define MAX_VIDEO_PIC_NUM  1  //��󻺴����ͼƬ��
#define WINDOW_W 1280  // SDL���ڿ��
#define WINDOW_H 720
#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define BREAK_EVENT  (SDL_USEREVENT + 2)

//����
typedef struct PacketQueue {
	AVPacketList* first_pkt, * last_pkt;
	int nb_packets;
	SDL_mutex* mutex;
} PacketQueue;

static  Uint8* audio_chunk;
static  Uint32  audio_len;
static  Uint8* audio_pos;

SDL_Window* sdlWindow = nullptr;
SDL_Renderer* sdlRender = nullptr;
SDL_Texture* sdlTtexture = nullptr;

AVFormatContext* ifmt_ctx = NULL;
AVPacket* pkt;
AVFrame* video_frame, * audio_frame;
int ret;
int videoindex = -1, audioindex = -1;
// const char* in_filename = "./file/IronMan.mp4";
const char* in_filename = "./file/ba.mp4";

int frame_width = 1280;
int frame_height = 720;

//��Ƶ����
const AVCodec* video_codec = nullptr;
AVCodecContext* video_codecContent = nullptr;

//��Ƶ����
const AVCodec* audio_codec = nullptr;
AVCodecContext* audio_codecContent = nullptr;

//��Ƶ֡����
PacketQueue video_pkt_queue;
PacketQueue audio_pkt_queue;

float audio_clock;
float video_clock;

// ������ͣ  ����
bool g_exit = false;
bool g_pause = false;

//SDL��Ƶ�����ṹ
SDL_AudioSpec wanted_spec;

static int initSdl();
static void closeSDL();
static void fill_audio_pcm2(void* udata, Uint8* stream, int len);

//fltpתΪpacked��ʽ
static void fltp_convert_to_f32le(float* f32le, float* fltp_l, float* fltp_r, int nb_samples, int channels)
{
	for (int i = 0; i < nb_samples; i++)
	{
		f32le[i * channels] = fltp_l[i];
		f32le[i * channels + 1] = fltp_r[i];
	}
}

//��һ��ѹ�����ݰ�������Ӧ�Ķ�����
static void put_AVPacket_into_queue(PacketQueue* q, AVPacket* packet)
{
	SDL_LockMutex(q->mutex);
	AVPacketList* temp = nullptr;
	temp = (AVPacketList*)av_malloc(sizeof(AVPacketList));
	if (!temp)
	{
		printf("malloc a AVPacketList error\n");
		return;
	}

	temp->pkt = *packet;
	temp->next = NULL;

	if (!q->last_pkt)
		q->first_pkt = temp;
	else
		q->last_pkt->next = temp;

	q->last_pkt = temp;
	q->nb_packets++;

	SDL_UnlockMutex(q->mutex);
}

static void packet_queue_get(PacketQueue* q, AVPacket* pkt2)
{
	while (true)
	{
		AVPacketList* pkt1;
		//һֱȡ��ֱ�������������ݣ��ͷ���
		pkt1 = q->first_pkt;
		if (pkt1)
		{
			SDL_LockMutex(q->mutex);
			q->first_pkt = pkt1->next;

			if (!q->first_pkt)
				q->last_pkt = NULL;

			q->nb_packets--;
			SDL_UnlockMutex(q->mutex);

			*pkt2 = pkt1->pkt;

			av_free(pkt1);

			break;

		}
		else
			SDL_Delay(1);

	}

	return;
}

static int video_play_thread(void* data)
{
	AVPacket video_packt = { 0 };
	//ȡ����
	//SDL_Event sdl_event;

	while (1)
	{
		//ȡ���ݰ�
		if (g_pause) continue;
		if (g_exit) return 0;

		packet_queue_get(&video_pkt_queue, &video_packt);

		ret = avcodec_send_packet(video_codecContent, &video_packt);
		if (ret < 0) {
			fprintf(stderr, "Error sending a packet for decoding\n", av_err2str(ret));
			continue;
		}

		while (ret >= 0)
		{
			ret = avcodec_receive_frame(video_codecContent, video_frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			else if (ret < 0)
			{
				fprintf(stderr, "Error during decoding\n");
				break;
			}
			//printf(" frame num %3d\n", video_codecContent->frame_number);
			fflush(stdout);

			video_clock = av_q2d(video_codecContent->time_base) * video_codecContent->ticks_per_frame * 1000 * video_codecContent->frame_number;
			//printf("��Ƶ֡pts: %f ms\n", video_clock);
			float duration = av_q2d(video_codecContent->time_base) * video_codecContent->ticks_per_frame * 1000;

			SDL_UpdateYUVTexture(sdlTtexture, nullptr,
				video_frame->data[0], video_frame->linesize[0],
				video_frame->data[1], video_frame->linesize[1],
				video_frame->data[2], video_frame->linesize[2]);

			//printf("%d %d %d\n", video_frame->linesize[0], video_frame->linesize[1], video_frame->linesize[2]);
			//exit(0);
			//if (video_codecContent->frame_number == 60) {
			//	for (int i = 1000; i < 1400; ++i) {
			//		printf("%d ", video_frame->data[2][i]);
			//	}
			//	printf("\n");
			//	exit(0);
			//}

			//������Ⱦ��������
			SDL_RenderClear(sdlRender);
			//����������������Ⱦƽ����
			SDL_RenderCopy(sdlRender, sdlTtexture, NULL, nullptr);
			//��ת��������ǰ̨��ʾ
			SDL_RenderPresent(sdlRender);

			//��ʱ����
			float delay = duration;
			float diff = video_clock - audio_clock;
			if (fabs(diff) <= duration) //ʱ�����һ֡��Χ�ڱ�ʾ��������ʱ����ʱ��
				delay = duration;
			else if (diff > duration) //��Ƶʱ�ӱ���Ƶʱ�ӿ죬�Ҵ���һ֡��ʱ�䣬��ʱ2��
				delay *= 2;
			else if (diff < -duration) //��Ƶʱ�ӱ���Ƶʱ�������ҳ���һ֡ʱ�䣬�������ŵ�ǰ֡
				delay = 1;

			printf("֡����%d ��ʱ: %f\n", video_codecContent->frame_number, delay);

			SDL_Delay(delay);
		}
	}
	return 0;
}

static int audio_play_thread(void* data)
{
	AVPacket audio_packt = { 0 };

	while (1)
	{
		if (g_pause) continue;  // ��ͣ�����˳��߳�
		if (g_exit) return 0;

		packet_queue_get(&audio_pkt_queue, &audio_packt);

		ret = avcodec_send_packet(audio_codecContent, &audio_packt);
		if (ret < 0) {
			fprintf(stderr, "Error submitting the packet to the decoder\n");
			exit(1);
		}

		/* read all the output frames (in general there may be any number of them */
		while (ret >= 0) {
			//�ӵ�������<AVPacket����>����ȡ��AVFrame��
			ret = avcodec_receive_frame(audio_codecContent, audio_frame);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			else if (ret < 0) {
				fprintf(stderr, "Error during decoding\n");
				break;
			}
			/*�����ǵõ��������������ݽ��д��������������ݵ���������Ӧ�Ĵ�����AAC�������PCM��h264�������YUV�ȡ�*/
			int data_size = av_get_bytes_per_sample(audio_codecContent->sample_fmt);
			if (data_size < 0) {
				/* This should not occur, checking just for paranoia */
				fprintf(stderr, "Failed to calculate data size\n");
				break;
			}

			//ת��Ϊpackedģʽ
			int pcm_buffer_size = data_size * audio_frame->nb_samples * audio_codecContent->channels;
			uint8_t* pcm_buffer = (uint8_t*)malloc(pcm_buffer_size);
			memset(pcm_buffer, 0, pcm_buffer_size);

			fltp_convert_to_f32le((float*)pcm_buffer, (float*)audio_frame->data[0], (float*)audio_frame->data[1],
				audio_frame->nb_samples, audio_codecContent->channels);
			//ʹ��SDL����
			//Set audio buffer (PCM data)
			audio_chunk = pcm_buffer;//(Uint8*)audio_frame->data[0];
			audio_len = pcm_buffer_size;
			audio_pos = audio_chunk;

			audio_clock = audio_frame->pts * av_q2d(audio_codecContent->time_base) * 1000;
			//printf("��Ƶ֡pts: %f ms\n", audio_clock);

			while (audio_len > 0)//Wait until finish
				SDL_Delay(1);

			//SDL_Delay(1000);
			free(pcm_buffer);
		}
	}

	return 0;
}

static int open_file_thread(void* data)
{
	//��ȡ
	if (g_exit) {
		//av_packet_unref(pkt);
		return 0;
	}

	while (av_read_frame(ifmt_ctx, pkt) >= 0)
	{
		if (pkt->stream_index == videoindex) {
			//������Ƶ����
			put_AVPacket_into_queue(&video_pkt_queue, pkt);
		}
		else if (pkt->stream_index == audioindex)
		{
			//������Ƶ����
			put_AVPacket_into_queue(&audio_pkt_queue, pkt);
		}
		else
			av_packet_unref(pkt);
	}

	return 0;
}

int SDLPlayer1()
{
	if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {
		printf("Could not open input file.");
		return -1;
	}

	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
		printf("Failed to retrieve input stream information");
		return -1;
	}

	videoindex = -1;
	for (int i = 0; i < ifmt_ctx->nb_streams; i++) { //nb_streams������Ƶ���ĸ���
		if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
			videoindex = i;
		else if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
			audioindex = i;
	}

	printf("\nInput Video===========================\n");
	av_dump_format(ifmt_ctx, 0, in_filename, 0);  // ��ӡ��Ϣ
	printf("\n======================================\n");

	//���ݱ������id��ѯ�����������ؽ������ṹ
	video_codec = avcodec_find_decoder(ifmt_ctx->streams[videoindex]->codecpar->codec_id);

	//���������������
	video_codecContent = avcodec_alloc_context3(video_codec);
	//������Ƶ����Ϣ����Ƶ��������������
	avcodec_parameters_to_context(video_codecContent, ifmt_ctx->streams[videoindex]->codecpar);

	frame_width = ifmt_ctx->streams[videoindex]->codecpar->width;
	frame_height = ifmt_ctx->streams[videoindex]->codecpar->height;

	//printf("%d, %d", frame_width, frame_height);
	//exit(0);

	//����Ƶ�������͹���������������
	if (avcodec_open2(video_codecContent, video_codec, nullptr))
	{
		printf("could not open codec!\n");
		return -1;
	}

	//���ݱ������id��ѯ�����������ؽ������ṹ
	audio_codec = avcodec_find_decoder(ifmt_ctx->streams[audioindex]->codecpar->codec_id);

	//���������������
	audio_codecContent = avcodec_alloc_context3(audio_codec);
	//������Ƶ����Ϣ����Ƶ��������������
	avcodec_parameters_to_context(audio_codecContent, ifmt_ctx->streams[audioindex]->codecpar);
	//����Ƶ�������͹���������������
	if (avcodec_open2(audio_codecContent, audio_codec, nullptr))
	{
		printf("could not open codec!\n");
		return -1;
	}

	//����һ��AVPacket�ṹ
	pkt = av_packet_alloc();

	//����һ��AVFrame �ṹ������Ž���������
	video_frame = av_frame_alloc();
	audio_frame = av_frame_alloc();

	//��ʼ��SDL
	initSdl();

	video_pkt_queue.mutex = SDL_CreateMutex();
	audio_pkt_queue.mutex = SDL_CreateMutex();
	if (!video_pkt_queue.mutex ||!audio_pkt_queue.mutex) {
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		return AVERROR(ENOMEM);
	}


	//����SDL��Ƶ���Ų���
	wanted_spec.freq = audio_codecContent->sample_rate; //������
	wanted_spec.format = AUDIO_F32LSB; // ������Ҫת��Ϊsdl�Ĳ�����ʽ  AUDIO_F32LSB  AUDIO_S16SYS
	wanted_spec.channels = audio_codecContent->channels; //ͨ����
	wanted_spec.silence = 0;
	wanted_spec.samples = audio_codecContent->frame_size;   //ÿһ֡�Ĳ���������
	wanted_spec.callback = fill_audio_pcm2; //��Ƶ���Żص�

	//��ϵͳ��Ƶ�豸
	if (SDL_OpenAudio(&wanted_spec, NULL) < 0) {
		printf("can't open audio.\n");
		return -1;
	}
	//Play
	SDL_PauseAudio(0);

	SDL_CreateThread(open_file_thread, "open_file", nullptr);
	SDL_CreateThread(audio_play_thread, "audio_play", nullptr);
	SDL_CreateThread(video_play_thread, "video_play", nullptr);

	SDL_Event event;
	while (!g_exit) {
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_KEYDOWN:
				if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
				{
					g_exit = true;
					SDL_Log("quit");
					break;
				}
				else if (event.key.keysym.scancode == SDL_SCANCODE_SPACE)
				{
					g_pause = !g_pause;
					SDL_Log(g_pause == true ? "pause" : "continue");
				}
				break;

			case SDL_QUIT:
				SDL_Log("quit");
				g_exit = true;
				break;

			default:
				break;
			}
		}
	}

	SDL_DestroyMutex(video_pkt_queue.mutex);
	SDL_DestroyMutex(audio_pkt_queue.mutex);

	//�ͷ�ffmpegָ��
	avformat_close_input(&ifmt_ctx);
	avcodec_free_context(&video_codecContent);
	avcodec_free_context(&audio_codecContent);
	av_frame_free(&audio_frame);
	av_frame_free(&video_frame);
	av_packet_free(&pkt);
	return 0;
}

static int initSdl()
{
	bool success = true;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS))
	{
		printf("init sdl error:%s\n", SDL_GetError());
		success = false;
	}

	//����window
	sdlWindow = SDL_CreateWindow("simple video player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
	if (sdlWindow == nullptr)
	{
		printf("create window error: %s\n", SDL_GetError());
		success = false;
	}

	//������Ⱦ��
	sdlRender = SDL_CreateRenderer(sdlWindow, -1, 0);
	if (sdlRender == nullptr)
	{
		printf("init window Render error: %s\n", SDL_GetError());
		success = false;
	}

	//����������
	sdlTtexture = SDL_CreateTexture(sdlRender, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, frame_width, frame_height);

	return success;
}

static void fill_audio_pcm2(void* udata, Uint8* stream, int len) {
	//SDL 2.0

	SDL_memset(stream, 0, len);

	if (g_pause || g_exit) return;

	if (audio_len == 0)
		return;
	len = (len > audio_len ? audio_len : len);

	//printf("audio_pos %d, len %d\n", audio_pos[0], len);

	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	audio_len -= len;
}

static void closeSDL()
{
	SDL_DestroyWindow(sdlWindow);
	sdlWindow = nullptr;
	SDL_DestroyRenderer(sdlRender);
	sdlRender = nullptr;
	SDL_DestroyTexture(sdlTtexture);
	sdlTtexture = nullptr;
}
