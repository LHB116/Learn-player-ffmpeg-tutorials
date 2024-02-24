#include <Tutorial.h>

// �����и�ϸ������Ҫע��ģ�����av_read_frame���ļ�β������
// ��Ҫ�ٴε���avcodec_send_packet����NULL���߿հ�flush������Ļ���֡
// 
// ��������Ƶ��ʽ������ʡ������������Ŀ�겻һ��ʱ����Ҫ�����ز���
// 
// https://blog.csdn.net/yinshipin007/article/details/131216846  push
// https://blog.csdn.net/qq21497936/article/details/108596396
// https://blog.csdn.net/yunxiaobaobei/article/details/130531382  ����push��pull
// ��׼CD���ֵ���������16bit��44.1KHz����
// SDL������Ƶ���ݵ����ַ�ʽ 
// 1.�ص���ʽpull SDL_Init->SDL_OpenAudio->SDL_PauseAudio,�����ݾͻص�����
// ���ûص��ķ�ʽ���ţ���Ȼ��Ҫһ������������д���������ݣ���Ƶ�豸�ص�ʱ�ٽ����ݶ���

// 2.��������Ƶ���ӵ����ݶ�����  push�� SDL_Init->SDL_OpenAudioDevice->SDL_PauseAudioDevice->SDL_QueueAudio
// ʹ��push��SDL_QueueAudio���ķ�ʽ������Ƶ��ͨ��������һ�����⣺Ӧ����ʲôƵ��������д��������ݣ�
// ��α��ֶ��г����ȶ����Ҳ�����Ϊ���ݹ��ٵ����������١�
// ͨ���Զ����ķ�ʽ�ǲ����еģ�������������������ٿ��ٻ���г��Ȳ���������
// ��ʱ��������Ҫ�ܹ���̬�Ŀ��ƶ��г��ȣ��������˾�д���һЩ�����ݹ����д����һЩ��


static AVAudioFifo* fifo = NULL;
static SDL_AudioSpec wanted_spec, spec;
static SDL_mutex* mtx = NULL;

static void audioCallback(void* userdata, Uint8* stream, int len) {
	//����AVAudioFifo���̰߳�ȫ���������̴߳����˻ص���������Ҫ����
	SDL_LockMutex(mtx);
	//��ȡ�����е���Ƶ����
	av_audio_fifo_read(fifo, (void**)&stream, spec.samples);
	SDL_UnlockMutex(mtx);
};


int Tutorial31_pull() {
	const char* input = "./ba.mp4";

	enum AVSampleFormat forceFormat;
	AVFormatContext* pFormatCtx = NULL;
	AVCodecContext* pCodecCtx = NULL;
	const AVCodec* pCodec = NULL;
	AVDictionary* opts = NULL;
	AVPacket packet;
	AVFrame* pFrame = NULL;
	struct SwrContext* swr_ctx = NULL;
	uint8_t* outBuffer = NULL;
	int	 audioindex = -1;
	int exitFlag = 0;
	int isLoop = 1;
	double framerate;
	int audioId = 0;
	memset(&packet, 0, sizeof(AVPacket));
	//��ʼ��SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	//��������
	if (avformat_open_input(&pFormatCtx, input, NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		goto end;
	}
	//������������Ϣ
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		printf("Couldn't find stream information.\n");
		goto end;
	}
	//��ȡ��Ƶ��
	for (unsigned i = 0; i < pFormatCtx->nb_streams; i++)
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioindex = i;
			break;
		}
	if (audioindex == -1) {
		printf("Didn't find a audio stream.\n");
		goto end;
	}
	//��������������
	pCodecCtx = avcodec_alloc_context3(NULL);
	if (pCodecCtx == NULL)
	{
		printf("Could not allocate AVCodecContext\n");
		goto end;
	}
	//��ȡ������
	if (avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[audioindex]->codecpar) < 0)
	{
		printf("Could not init AVCodecContext\n");
		goto end;
	}
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		goto end;
	}
	//ʹ�ö��߳̽���
	if (!av_dict_get(opts, "threads", NULL, 0))
		av_dict_set(&opts, "threads", "auto", 0);
	//�򿪽�����
	if (avcodec_open2(pCodecCtx, pCodec, &opts) < 0) {
		printf("Could not open codec.\n");
		goto end;
	}

	if (pCodecCtx->sample_fmt == AV_SAMPLE_FMT_NONE)
	{
		printf("Unknown sample foramt.\n");
		goto end;
	}
	if (pCodecCtx->sample_rate <= 0 || pFormatCtx->streams[audioindex]->codecpar->channels <= 0)
	{
		printf("Invalid sample rate or channel count!\n");
		goto end;
	}

	//���豸
	wanted_spec.channels = pFormatCtx->streams[audioindex]->codecpar->channels;
	wanted_spec.freq = pCodecCtx->sample_rate;
	wanted_spec.format = AUDIO_F32SYS;
	wanted_spec.silence = 0;
	wanted_spec.samples = FFMAX(512, 2 << av_log2(wanted_spec.freq / 30));
	wanted_spec.callback = audioCallback;
	wanted_spec.userdata = NULL;
	audioId = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &spec, 1);
	if (audioId < 2)
	{
		printf("Open audio device error!\n");
		goto end;
	}
	switch (spec.format)
	{
	case	AUDIO_S16SYS:
		forceFormat = AV_SAMPLE_FMT_S16;
		break;
	case	AUDIO_S32SYS:
		forceFormat = AV_SAMPLE_FMT_S32;
		break;
	case	AUDIO_F32SYS:
		forceFormat = AV_SAMPLE_FMT_FLT;
		break;
	default:
		printf("audio device format was not surported!\n");
		goto end;
		break;
	}
	pFrame = av_frame_alloc();
	if (!pFrame)
	{
		printf("alloc frame failed!\n");
		goto end;
	}
	//ʹ����Ƶ����
	fifo = av_audio_fifo_alloc(forceFormat, spec.channels, spec.samples * 10);
	if (!fifo)
	{
		printf("alloc fifo failed!\n");
		goto end;
	}
	mtx = SDL_CreateMutex();
	if (!mtx)
	{
		printf("alloc mutex failed!\n");
		goto end;
	}
	SDL_PauseAudioDevice(audioId, 0);
start:
	while (!exitFlag)
	{
		//��ȡ��
		int gotPacket = av_read_frame(pFormatCtx, &packet) == 0;
		if (!gotPacket || packet.stream_index == audioindex)
			//!gotPacket:δ��ȡ��packet��Ҫ���������Ļ���flush�����Ի���Ҫ��һ�ν������̡�
		{
			//���Ͱ�
			if (avcodec_send_packet(pCodecCtx, &packet) < 0)
			{
				printf("Decode error.\n");
				av_packet_unref(&packet);
				goto end;
			}
			//���ս����֡
			while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
				uint8_t* data;
				size_t dataSize;
				size_t samples;
				if (forceFormat != pCodecCtx->sample_fmt || spec.freq != pFrame->sample_rate || spec.channels != pFrame->channels)
					//�ز���
				{
					//�������������
					int out_count = (int64_t)pFrame->nb_samples * spec.freq / pFrame->sample_rate + 256;
					//����������ݴ�С
					int out_size = av_samples_get_buffer_size(NULL, spec.channels, out_count, forceFormat, 0);
					//��������ָ��
					const uint8_t** in = (const uint8_t**)pFrame->extended_data;
					//���������ָ��
					uint8_t** out = &outBuffer;
					int len2 = 0;
					if (out_size < 0) {
						av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
						goto end;
					}
					if (!swr_ctx)
						//��ʼ���ز�������
					{
						swr_ctx = swr_alloc_set_opts(NULL, av_get_default_channel_layout(spec.channels), forceFormat, spec.freq, av_get_default_channel_layout(pFormatCtx->streams[audioindex]->codecpar->channels), pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, NULL);
						if (!swr_ctx || swr_init(swr_ctx) < 0) {
							av_log(NULL, AV_LOG_ERROR, "swr_alloc_set_opts() failed\n");
							goto end;
						}
					}
					if (!outBuffer)
						//�������������
					{
						outBuffer = (uint8_t*)av_mallocz(out_size);
					}
					//ִ���ز���
					len2 = swr_convert(swr_ctx, out, out_count, in, pFrame->nb_samples);
					if (len2 < 0) {
						av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
						goto end;
					}
					//ȡ���������
					data = outBuffer;
					//������ݳ���
					dataSize = av_samples_get_buffer_size(0, spec.channels, len2, forceFormat, 1);
					samples = len2;
				}
				else
				{
					data = pFrame->data[0];
					dataSize = av_samples_get_buffer_size(pFrame->linesize, pFrame->channels, pFrame->nb_samples, forceFormat, 0);
					samples = pFrame->nb_samples;
				}
				//д�����
				while (1) {
					SDL_LockMutex(mtx);
					if (av_audio_fifo_space(fifo) >= samples)
					{
						av_audio_fifo_write(fifo, (void**)&data, samples);
						SDL_UnlockMutex(mtx);
						break;
					}
					SDL_UnlockMutex(mtx);
					//���п��ÿռ䲻������ʱ�ȴ�,�ȴ����������ݱ�����
					SDL_Delay((dataSize) * 1000.0 / (spec.freq * av_get_bytes_per_sample(forceFormat) * spec.channels) - 1);
				}
			}
		}
		av_packet_unref(&packet);
		if (!gotPacket)
		{
			//ѭ������ʱflush������֡����Ҫ���ô˷����������½��롣
			avcodec_flush_buffers(pCodecCtx);
			break;
		}
	}
	if (!exitFlag)
	{
		if (isLoop)
		{
			//��λ�����
			if (avformat_seek_file(pFormatCtx, -1, 0, 0, 0, AVSEEK_FLAG_FRAME) >= 0)
			{
				goto start;
			}
		}
	}
end:
	//������Դ
	if (fifo)
	{
		av_audio_fifo_free(fifo);
	}
	if (audioId >= 2)
	{
		SDL_PauseAudioDevice(audioId, 1);
		SDL_CloseAudioDevice(audioId);
	}
	if (mtx)
	{
		SDL_DestroyMutex(mtx);
	}
	if (pFrame)
	{
		if (pFrame->format != -1)
		{
			av_frame_unref(pFrame);

		}
		av_frame_free(&pFrame);
	}
	if (packet.data)
	{
		av_packet_unref(&packet);
	}
	if (pCodecCtx)
	{
		avcodec_close(pCodecCtx);
		avcodec_free_context(&pCodecCtx);
	}
	if (pFormatCtx)
		avformat_close_input(&pFormatCtx);
	if (pFormatCtx)
		avformat_free_context(pFormatCtx);
	swr_free(&swr_ctx);
	av_dict_free(&opts);
	if (outBuffer)
		av_free(outBuffer);
	SDL_Quit();
	return 0;

}