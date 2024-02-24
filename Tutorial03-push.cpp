#include <Tutorial.h>

// 解码有个细节是需要注意的，即当av_read_frame到文件尾结束后，
// 需要再次调用avcodec_send_packet传入NULL或者空包flush出里面的缓存帧
// 
// 当遇到音频格式或采样率、声道数与输出目标不一致时，需要进行重采样
// 
// https://blog.csdn.net/yinshipin007/article/details/131216846  push
// https://blog.csdn.net/qq21497936/article/details/108596396
// https://blog.csdn.net/yunxiaobaobei/article/details/130531382  介绍push和pull
// 标准CD音乐的质量就是16bit、44.1KHz采样
// SDL播放音频数据的两种方式 
// 1.回调方式pull SDL_Init->SDL_OpenAudio->SDL_PauseAudio,有数据就回调函数
// 2.主动将音频数加到数据队列中  push， SDL_Init->SDL_OpenAudioDevice->SDL_PauseAudioDevice->SDL_QueueAudio


// ffmpeg -i test.mp4 -c copy test.h264
int Tutorial31_push() {
	//const char* input = "./test.mp4";
	const char* input = "./ba.mp4";
	//const char* input = "test_music.wav";

	//pid参数
	double error_p = 0;		//kp误差
	double error_i = 0;		//ki误差
	double error_d = 0;		//kd误差
	double error_dp = 0;	//上一次的kp误差、用于计算kd误差

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
	SDL_AudioSpec wanted_spec, spec;
	int audioId = 0;
	memset(&packet, 0, sizeof(AVPacket));
	//初始化SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	//打开输入流
	if (avformat_open_input(&pFormatCtx, input, NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		goto end;
	}
	//查找输入流信息
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		printf("Couldn't find stream information.\n");
		goto end;
	}
	//获取音频流
	for (unsigned i = 0; i < pFormatCtx->nb_streams; i++)
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioindex = i;
			break;
		}
	if (audioindex == -1) {
		printf("Didn't find a audio stream.\n");
		goto end;
	}
	//创建解码上下文
	pCodecCtx = avcodec_alloc_context3(NULL);
	if (pCodecCtx == NULL)
	{
		printf("Could not allocate AVCodecContext\n");
		goto end;
	}
	//获取解码器
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
	//使用多线程解码
	if (!av_dict_get(opts, "threads", NULL, 0))
		av_dict_set(&opts, "threads", "auto", 0);
	//打开解码器
	if (avcodec_open2(pCodecCtx, pCodec, &opts) < 0) {
		printf("Could not open codec.\n");
		goto end;
	}

	if (pCodecCtx->sample_fmt == AV_SAMPLE_FMT_NONE)
	{
		printf("Unknown sample foramt.\n");
		goto end;
	}

	if (pCodecCtx->sample_rate <= 0 || av_get_channel_layout_nb_channels(pFormatCtx->streams[audioindex]->codecpar->channels) <= 0)
	{
		printf("Invalid sample rate or channel count!\n");
		goto end;
	}
	//打开设备
	wanted_spec.channels = pFormatCtx->streams[audioindex]->codecpar->channels;
	wanted_spec.freq = pCodecCtx->sample_rate;
	wanted_spec.format = AUDIO_F32SYS;
	wanted_spec.silence = 0;
	wanted_spec.samples = FFMAX(512, 2 << av_log2(wanted_spec.freq / 30));
	wanted_spec.callback = NULL;
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
	SDL_PauseAudioDevice(audioId, 0);

start:
	while (!exitFlag)
	{
		//读取包  若未获取到packet需要将解码器的缓存flush还需要进一次解码流程。
		int gotPacket = av_read_frame(pFormatCtx, &packet) == 0;
		if (!gotPacket || packet.stream_index == audioindex)
		{
			if (avcodec_send_packet(pCodecCtx, &packet) < 0)  // 解封装
			{
				printf("Decode error.\n");
				av_packet_unref(&packet);
				goto end;
			}
			while (avcodec_receive_frame(pCodecCtx, pFrame) == 0) {
				uint8_t* data;
				size_t dataSize;
				// 重采样
				if (forceFormat != pCodecCtx->sample_fmt || spec.freq != pFrame->sample_rate || spec.channels != pFrame->channels)
				{
					//printf("%d, %d, %d\n", forceFormat != pCodecCtx->sample_fmt, spec.freq != pFrame->sample_rate, spec.channels != pFrame->channels);
					//printf("%d, %d\n", forceFormat, pCodecCtx->sample_fmt);
					//计算输入采样数
					int out_count = (int64_t)pFrame->nb_samples * spec.freq / pFrame->sample_rate + 256;
					//计算输出数据大小
					int out_size = av_samples_get_buffer_size(NULL, spec.channels, out_count, forceFormat, 0);
					//输入数据指针
					const uint8_t** in = (const uint8_t**)pFrame->extended_data;
					//输出缓冲区指针
					uint8_t** out = &outBuffer;
					int len2 = 0;
					if (out_size < 0) {
						av_log(NULL, AV_LOG_ERROR, "av_samples_get_buffer_size() failed\n");
						goto end;
					}
					if (!swr_ctx)
						//初始化重采样对象
					{
						swr_ctx = swr_alloc_set_opts(NULL, av_get_default_channel_layout(spec.channels), forceFormat, spec.freq, av_get_default_channel_layout(pFormatCtx->streams[audioindex]->codecpar->channels), pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, NULL);
						if (!swr_ctx || swr_init(swr_ctx) < 0) {
							av_log(NULL, AV_LOG_ERROR, "swr_alloc_set_opts() failed\n");
							goto end;
						}
					}
					if (!outBuffer)
						//申请输出缓冲区
					{
						outBuffer = (uint8_t*)av_mallocz(out_size);
					}
					//执行重采样
					len2 = swr_convert(swr_ctx, out, out_count, in, pFrame->nb_samples);
					if (len2 < 0) {
						av_log(NULL, AV_LOG_ERROR, "swr_convert() failed\n");
						goto end;
					}
					//取得输出数据
					data = outBuffer;
					//输出数据长度
					dataSize = av_samples_get_buffer_size(0, spec.channels, len2, forceFormat, 1);
				}
				else
				{
					data = pFrame->data[0];
					dataSize = av_samples_get_buffer_size(pFrame->linesize, pFrame->channels, pFrame->nb_samples, forceFormat, 0);
				}

				//写入音频设备队列
				//使用pid控制队列长度,在一定范围内,防止不受限的增长，
				// 或数据太少声音卡顿。设置targetSize队列目标长度，输入队列当前长度size，输出延时delay。
				double targetSize = spec.size * 3;
				int size = SDL_GetQueuedAudioSize(audioId);
				printf("queue size:%dbytes\n", size);
				error_p = targetSize - size;
				error_i += error_p;
				error_d = error_p - error_dp;
				error_dp = error_p;
				size = (0.8 * error_p + 0.2 * error_i + 0.05 * error_d);
				//将数据大小转换成时长
				double  delay = (targetSize - size) * 1000.0 / (spec.freq * av_get_bytes_per_sample(forceFormat) * spec.channels);
				//使用pid控制队列长度--end
				
				SDL_QueueAudio(audioId, data, dataSize);
				if (delay > 0)
					SDL_Delay(delay);
				
				////写入数据
				//SDL_QueueAudio(audioId, data, dataSize);
				////延时，按照数据长度，-1是防止写入过慢卡顿
				//SDL_Delay((dataSize) * 1000.0 / (spec.freq * av_get_bytes_per_sample(forceFormat) * spec.channels) - 1);
			}
		}
		av_packet_unref(&packet);
		if (!gotPacket)
		{
			//循环播放时flush出缓存帧后需要调用此方法才能重新解码。
			avcodec_flush_buffers(pCodecCtx);
			break;
		}
	}
	if (!exitFlag)
	{
		if (isLoop)
		{
			//定位到起点
			if (avformat_seek_file(pFormatCtx, -1, 0, 0, 0, AVSEEK_FLAG_FRAME) >= 0)
			{
				goto start;
			}
		}
	}
end:
	//销毁资源
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
	if (audioId >= 2)
		SDL_CloseAudioDevice(audioId);
	SDL_Quit();
	return 0;
}