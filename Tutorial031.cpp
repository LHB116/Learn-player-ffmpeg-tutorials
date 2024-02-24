// https://blog.csdn.net/weixin_63572056/article/details/126116521

#include <Tutorial.h>
#include <iostream>
using namespace std;


#define MAX_AUDIO_FRAME_SIZE 192000
static AVFormatContext* ctx = nullptr;
static SDL_AudioSpec wanted_spec, spec;
static SwrContext* audio_s_ctx = nullptr;
static int i, videostream, audiostream;


typedef struct PacketQueue {
	AVPacketList* first_pkt, * last_pkt;
	int nb_packets;
	int size;
	SDL_mutex* mutex;
	SDL_cond* cond;
} PacketQueue;

static PacketQueue audioq;

static unsigned int audioLen = 0;
static unsigned char* audioChunk = NULL;
static unsigned char* audioPos = NULL;


static void packet_queue_init(PacketQueue* q) {
	memset(q, 0, sizeof(PacketQueue));
	q->mutex = SDL_CreateMutex();
	q->cond = SDL_CreateCond();
}

static int packet_queue_put(PacketQueue* q, AVPacket* pkts) {

	AVPacketList* pkt1;
	AVPacket* pkt = av_packet_alloc();
	if (av_packet_ref(pkt, pkts) < 0) {
		return -1;
	}
	pkt1 = (AVPacketList*)av_malloc(sizeof(AVPacketList));

	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	SDL_LockMutex(q->mutex);

	if (!q->last_pkt) {
		q->first_pkt = pkt1;
	}
	else {
		q->last_pkt->next = pkt1;
	}

	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);
	return 0;
}

static int packet_queue_get(PacketQueue* q, AVPacket* pkt, int block)
{
	AVPacketList* pkt1;
	int ret;

	SDL_LockMutex(q->mutex);

	for (;;) {

		pkt1 = q->first_pkt;
		if (pkt1) {
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;
			break;
		}
		else if (!block) {
			ret = 0;
			break;
		}
		else {
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

static int audio_decode_frame(AVCodecContext* aCodecCtx, uint8_t* audio_buf, int buf_size) {

	static AVPacket* pkt = av_packet_alloc();
	static uint8_t* audio_pkt_data = NULL;
	static int audio_pkt_size = 0;
	static AVFrame* frame;
	frame = av_frame_alloc();
	int len1, data_size = 0;

	for (;;) {
		while (audio_pkt_size > 0) {
			if (pkt->stream_index == audiostream)
			{
				int ret = avcodec_send_packet(aCodecCtx, pkt);
				if (ret != 0)
				{
					SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "avcodec_send_packet error");
					audio_pkt_size = 0;
					return ret;
				}
				len1 = pkt->size;
				audio_pkt_data += len1;
				audio_pkt_size -= len1;
				//cout << len1 << endl;
				data_size = 0;
				while (avcodec_receive_frame(aCodecCtx, frame) == 0)
				{
					//printf("audio_buf0: %d\n", audio_buf[0]);
					swr_convert(audio_s_ctx,
						&audio_buf,
						MAX_AUDIO_FRAME_SIZE,
						(const uint8_t**)frame->data,
						frame->nb_samples);

					//cout << frame->nb_samples << endl;
					data_size = 2 * 2 * frame->nb_samples;
				}

			}

			if (data_size <= 0) {
				/* No data yet, get more frames */
				continue;
			}
			/* We have data, return it and come back for more later */
			//cout << "11111111111111111111111111111111111" << endl;
			return data_size;
		}
		if (pkt->data)
			av_packet_unref(pkt);

		if (packet_queue_get(&audioq, pkt, 1) < 0) {
			return -1;
		}
		audio_pkt_data = pkt->data;
		audio_pkt_size = pkt->size;
	}
}

static void audio_callback(void* userdata, Uint8* stream, int len) {

	AVCodecContext* aCodecCtx = (AVCodecContext*)userdata;
	int len1, audio_size;

	static uint8_t audio_buf[MAX_AUDIO_FRAME_SIZE];
	static unsigned int audio_buf_size = 0;
	static unsigned int audio_buf_index = 0;


	while (len > 0) {
		if (audio_buf_index >= audio_buf_size) {
			/* We have already sent all our data; get more */
			audio_size = audio_decode_frame(aCodecCtx, audio_buf, sizeof(audio_buf));
			if (audio_size < 0) {
				//* If error, output silence 加静默音
				audio_buf_size = 1024; // arbitrary?
				memset(audio_buf, 0, audio_buf_size);
				cout << "ssssssssssssssssssssssssssssssssssssssssssssssssssssssssssss" << endl;
			}
			else {
				audio_buf_size = audio_size;
				cout << "+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++" << endl;
			}
			audio_buf_index = 0;
		}
		len1 = audio_buf_size - audio_buf_index;
		if (len1 > len)
			len1 = len;

		memcpy(stream, (uint8_t*)audio_buf + audio_buf_index, len1);
		cout << "-------------------------------------------------------len1    " << len1 << endl;
		len -= len1;
		cout << "-------------------------------------------------------len    " << len << endl;
		stream += len1;
		//cout << "-------------------------------------------------------stream    " << stream << endl;
		audio_buf_index += len1;
	}
}

static void audioset(AVCodecContext* acodec)
{
	wanted_spec.freq = acodec->sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = acodec->channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = 1024;
	wanted_spec.callback = audio_callback;
	wanted_spec.userdata = acodec;
}


int Tutorial31()
{
	const char* input = "./file/ba.mp4";

	int ret = -1;
	AVCodecParameters* codecctxOrig = nullptr;
	AVCodecParameters* codecctx = nullptr;//备份文件
	SwsContext* swsctx = nullptr;//图像转换上下文
	const AVCodec* codec = nullptr;
	AVFrame* pframe = nullptr;
	AVPacket pkt;

	AVFrame* pict = nullptr;
	int a = 0;

	//音频
	AVCodecParameters* acodecctxOrig = nullptr;
	AVCodecParameters* acodecctx = nullptr;//备份文件
	const AVCodec* acodec = nullptr;

	uint64_t in_channel_layout;
	uint64_t out_channel_layout;

	int out_buffer_size;//输出buff
	unsigned char* outBuff;

	SDL_Rect rect;
	uint32_t pixformat;


	SDL_Window* win = nullptr;
	SDL_Renderer* renderer = nullptr;//渲染器
	SDL_Texture* texture = nullptr;//纹理
	int w_width = 640;
	int w_height = 480;

	//打开输入文件
	if (avformat_open_input(&ctx, input, nullptr, nullptr) != 0)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "avformat_open_input error");
	}
	//查找流信息
	if (avformat_find_stream_info(ctx, nullptr) < 0)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "avformat_find_stream_info error");
	}
	av_dump_format(ctx, 0, input, 0);
	videostream = -1;
	audiostream = -1;
	for (i = 0; i < ctx->nb_streams; i++)
	{
		if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			videostream = i;

		}
		if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audiostream = i;

		}
	}
	if (videostream == -1)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "xxxxxxxxxxxxxxxxxxxxxxxxx");
	}
	if (audiostream == -1)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "xxxxxxxxxxxxxxxxxxxxxxxxx11111111111");
	}

	codecctxOrig = ctx->streams[videostream]->codecpar;//获取编码上下文
	codec = avcodec_find_decoder(codecctxOrig->codec_id);//寻找解码器

	acodecctxOrig = ctx->streams[audiostream]->codecpar;
	acodec = avcodec_find_decoder(acodecctxOrig->codec_id);

	AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);//给上下文分配内存空间
	AVCodecContext* acodec_ctx = avcodec_alloc_context3(acodec);
	// copy codecctxOrig-> > codec_ctx
	ret = avcodec_parameters_to_context(codec_ctx, codecctxOrig);
	if (ret < 0) {
		printf("Failed to copy in_stream codecpar to codec context\n");
		goto __FAIL;
	}

	ret = avcodec_parameters_to_context(acodec_ctx, acodecctxOrig);
	if (ret < 0) {
		printf("Failed to copy in_stream acodecpar to acodec context\n");
		goto __FAIL;
	}

	avcodec_open2(acodec_ctx, acodec, nullptr);
	packet_queue_init(&audioq);//设置声音队列

	//out_buffer_size = av_samples_get_buffer_size(NULL, acodec_ctx->channels, acodec_ctx->frame_size, AV_SAMPLE_FMT_S16, 1);   //输出buff
	//outBuff = (unsigned char*)av_malloc(MAX_AUDIO_FRAME_SIZE * acodec_ctx->channels);

	//音频重采样
	in_channel_layout = av_get_default_channel_layout(acodec_ctx->channels);
	out_channel_layout = in_channel_layout;
	audio_s_ctx = swr_alloc();
	swr_alloc_set_opts(audio_s_ctx, out_channel_layout, AV_SAMPLE_FMT_S16, acodec_ctx->sample_rate,
		in_channel_layout, acodec_ctx->sample_fmt, acodec_ctx->sample_rate,
		0, nullptr
	);
	swr_init(audio_s_ctx);
	SDL_Init(SDL_INIT_AUDIO);
	audioset(acodec_ctx);
	//if (SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, 1) < 0)
	if (SDL_OpenAudio(&wanted_spec, NULL) < 0)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_OpenAudio error");
	}
	SDL_PauseAudio(0);

	//打开解码器
	if (avcodec_open2(codec_ctx, codec, nullptr) < 0)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "avcodec_open2 error");
	}

	pframe = av_frame_alloc();
	pict = av_frame_alloc();
	// 重要操作，此函数的功能是按照指定的宽、高、像素格式来分析图像内存。
	av_image_alloc(pict->data, pict->linesize, codec_ctx->width, codec_ctx->height, AV_PIX_FMT_YUV420P, 1);
	w_width = codec_ctx->width;
	w_height = codec_ctx->height;
	//sdl窗口设置
	win = SDL_CreateWindow("video player", 200, 200,//窗口位置不确定
		w_width, w_height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);//opengl与窗口可变换大小

	if (!win)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow error");
		goto __FAIL;
	}

	renderer = SDL_CreateRenderer(win, -1, -0);
	if (!renderer)
	{
		SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer error");
		goto __FAIL;
	}

	pixformat = SDL_PIXELFORMAT_IYUV;
	texture = SDL_CreateTexture(renderer, pixformat, SDL_TEXTUREACCESS_STREAMING,//纹理访问模式 : 经常变化，可锁定
		w_width, w_height);
	//窗口信息初始化
	swsctx = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,//转换前格式
		codec_ctx->width, codec_ctx->height, AV_PIX_FMT_YUV420P,//转换后格式
		SWS_BILINEAR, nullptr, nullptr, nullptr//算法
	);

	while (av_read_frame(ctx, &pkt) >= 0)
	{
		if (pkt.stream_index == videostream)
		{
			int ret = avcodec_send_packet(codec_ctx, &pkt);
			if (ret != 0)
			{
				SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "avcodec_send_packet error");
				return ret;
			}
			while (avcodec_receive_frame(codec_ctx, pframe) == 0) {

				//图像缩放，格式转换
				sws_scale(swsctx, (uint8_t const* const*)pframe->data, pframe->linesize, 0, codec_ctx->height, pict->data, pict->linesize);
				//窗口渲染
				SDL_UpdateYUVTexture(texture, nullptr,
					pict->data[0], pict->linesize[0],
					pict->data[1], pict->linesize[1],
					pict->data[2], pict->linesize[2]// YUV三层，更新纹理
				);
				rect.x = 0;
				rect.y = 0;
				rect.w = codec_ctx->width;
				rect.h = codec_ctx->height;
				SDL_RenderClear(renderer);//刷一下屏
				SDL_RenderCopy(renderer, texture, nullptr, &rect);//将纹理拷贝到渲染器
				SDL_RenderPresent(renderer);//展示
				av_frame_unref(pframe);
			}

		}
		else if (pkt.stream_index == audiostream)
		{
			packet_queue_put(&audioq, &pkt);

		}

		av_packet_unref(&pkt);
		SDL_Event event;
		SDL_PollEvent(&event);
		switch (event.type)
		{
		case SDL_QUIT:goto __QUIT; break;
		}
	}

__QUIT:
	ret = 0;
__FAIL:
	if (pframe)
	{
		av_frame_free(&pframe);
	}
	if (pict)
	{
		av_frame_free(&pict);
	}

	if (codec_ctx)
	{
		avcodec_close(codec_ctx);
	}

	if (codecctxOrig)
	{
		avcodec_parameters_free(&codecctxOrig);
	}
	if (codecctx)
	{
		avcodec_parameters_free(&codecctx);
	}
	if (win)
	{
		SDL_DestroyWindow(win);
	}
	if (renderer)
	{
		SDL_DestroyRenderer(renderer);
	}
	if (texture)
	{
		SDL_DestroyTexture(texture);
	}
	SDL_QUIT;
}