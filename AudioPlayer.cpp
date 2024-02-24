#include <AudioPlayer.h>

#define FILE_NAME               "./file/IronMan.mp4"
//#define FILE_NAME               "./file/ba.mp4"

#define SDL_AUDIO_BUFFER_SIZE   1024    //
#define MAX_AUDIO_FRAME_SIZE    192000  //1 second of 48khz 32bit audio
#define ERR_STREAM              stderr
#define OUT_SAMPLE_RATE         44100

static  Uint8* audio_chunk;
static  Uint32 audio_len;
static  Uint8* audio_pos;

void fill_audio(void* udata, Uint8* stream, int len) {
	//SDL 2.0
	SDL_memset(stream, 0, len);
	if (audio_len == 0)
		return;

	len = len > audio_len ? audio_len : len;	/*  Mix  as  much  data  as  possible  */

	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	audio_pos += len;
	audio_len -= len;
}

int AudioPlayer() {

	int ret = 0;
	int audioStream = -1;

	AVFormatContext* avFormatContext = avformat_alloc_context();
	//打开文件
	if (avformat_open_input(&avFormatContext, FILE_NAME, NULL, NULL) != 0)
	{
		fprintf(ERR_STREAM, "Couldn't open input file\n");
		exit(-1);
	}

	if (avformat_find_stream_info(avFormatContext, NULL) < 0)
	{
		fprintf(ERR_STREAM, "Not Found Stream Info\n");
		exit(-1);
	}

	//显示文件信息，十分好用的一个函数
	av_dump_format(avFormatContext, 0, FILE_NAME, 0);


	// Find the first audio stream
	for (int i = 0; i < avFormatContext->nb_streams; i++)
		if (avFormatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioStream = i;
			break;
		}
	if (audioStream == -1)
	{
		fprintf(ERR_STREAM, "Couldn't find stream index\n");
		exit(-1);
	}
	printf("audio_stream = %d\n", audioStream);


	AVCodecContext* avCodecContext = avcodec_alloc_context3(NULL);
	ret = avcodec_parameters_to_context(avCodecContext, avFormatContext->streams[audioStream]->codecpar);
	if (ret < 0)
	{
		fprintf(ERR_STREAM, "avcodec_parameters_to_context error\n");
		exit(-1);
	}
	avCodecContext->pkt_timebase = avFormatContext->streams[audioStream]->time_base;
	const AVCodec* codec = avcodec_find_decoder(avCodecContext->codec_id);
	avCodecContext->codec_id = codec->id;
	if (!codec)
	{
		fprintf(ERR_STREAM, "Couldn't find decoder\n");
		exit(-1);
	}

	ret = avcodec_open2(avCodecContext, codec, nullptr);
	if (ret < 0)
	{
		fprintf(ERR_STREAM, "avcodec_open2 error\n");
		exit(-1);
	}

	AVFrame* pFrame = nullptr;

	pFrame = av_frame_alloc();

	const uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;
	int out_nb_samples = avCodecContext->frame_size;
	AVSampleFormat	out_sample_fmt = AV_SAMPLE_FMT_S16;
	int out_sample_rate = 44100;
	int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
	int out_buffer_size = av_samples_get_buffer_size(nullptr, out_channels, out_nb_samples, out_sample_fmt, 1);

	uint8_t* out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);

	SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER);
	SDL_AudioSpec audioSpec = {};
	audioSpec.freq = out_sample_rate;
	audioSpec.format = AUDIO_S16SYS;
	audioSpec.channels = out_channels;
	audioSpec.silence = 0;
	audioSpec.samples = out_nb_samples;
	audioSpec.callback = fill_audio;
	audioSpec.userdata = avCodecContext;
	if (SDL_OpenAudio(&audioSpec, nullptr) < 0) {
		fprintf(ERR_STREAM, "SDL_OpenAudio error\n");
		return 0;
	}

	//FIX:Some Codec's Context Information is missing
	int64_t in_channel_layout = av_get_default_channel_layout(avCodecContext->channels);

	SwrContext* au_convert_ctx = swr_alloc();
	au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
		in_channel_layout, avCodecContext->sample_fmt, avCodecContext->sample_rate, 0, NULL);
	swr_init(au_convert_ctx);

	//Play
	SDL_PauseAudio(0);

	AVPacket packet;
	bool bReadEof = false;
	while (true)
	{
		int readResult = -1;
		if (!bReadEof)
		{
			readResult = av_read_frame(avFormatContext, &packet);
			if (readResult < 0) {
				fprintf(ERR_STREAM, "av_read_frame error\n");
				//::MessageBoxA(0, 0, GetFFmpegErorString(readResult).c_str(), 0);
				bReadEof = true;
			}
			else if (readResult == 0) {
				static int iCnt = 0;
				if (packet.stream_index == audioStream) {
					++iCnt;
				}
				printf("iCnt = %d\n", iCnt);
			}
		}
		if (bReadEof)
		{
			avcodec_send_packet(avCodecContext, NULL);
		}
		else
		{
			// Is this a packet from the video stream?
			if (packet.stream_index == audioStream) {
				// Decode video frame
				avcodec_send_packet(avCodecContext, &packet);
			}

		}

		int receiveResult = avcodec_receive_frame(avCodecContext, pFrame);
		// Did we get a audio frame?
		if (receiveResult == 0) {
			swr_convert(au_convert_ctx, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t**)pFrame->data, pFrame->nb_samples);
			while (audio_len > 0)//Wait until finish
			{
				// 延迟
				SDL_Delay(1);
			}

			//Set audio buffer (PCM data)
			audio_chunk = (Uint8*)out_buffer;
			//Audio buffer length
			audio_len = out_buffer_size;
			audio_pos = audio_chunk;
		}
		else if (receiveResult == AVERROR_EOF)
		{
			fprintf(ERR_STREAM, "AVERROR_EOF\n");
			//::MessageBoxA(0, 0, "read eof", 0);
			break;
		}
		else if (receiveResult == AVERROR(EAGAIN)) {
			if (bReadEof) {
				break;
			}
			else {

			}
		}
		else {
			fprintf(ERR_STREAM, "AVERROR1111F\n");
			//msgBoxFFmpegError(receiveResult);
		}

		// Free the packet that was allocated by av_read_frame
		if (readResult == 0)
			av_packet_unref(&packet);
	}

	SDL_CloseAudio();//Close SDL
	SDL_Quit();

	av_frame_free(&pFrame);
	avcodec_close(avCodecContext);
	avformat_close_input(&avFormatContext);

	return 0;
}