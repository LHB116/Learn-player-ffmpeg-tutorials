#include <Tutorial.h>

#define MAX_FRAME_QUEUE_SIZE 32

//--------------------------------------------------------------------

//packet������нڵ�
typedef struct MyAVPacketList
{
	AVPacket pkt;
	struct MyAVPacketList* next;
} MyAVPacketList;

//packet���нṹ������ֻ�������׽ڵ��β�ڵ�,�м�ڵ�
//ͨ��MyAVPacketList->next��������,packet�����ʱ�Ͳ�
//�뵽����β��,packet������ʱ�ʹ��ײ�ȡpacket
typedef struct PacketQueue
{
	MyAVPacketList* first_pkt;	//�׽ڵ�
	MyAVPacketList* last_pkt;	//β�ڵ�
	int abort_request;			//�ж�����
	int nb_packets;				//�����ܹ�packet����
	int size;					//����packet��size��С
	SDL_mutex* mutex;			//��
	SDL_cond* cond;				//��������
} PacketQueue;

//frame�ڵ�
typedef struct Frame
{
	AVFrame* frame;	//frame
	int frame_quit;	//��Ǹ�frame�Ƿ������һ��frame
} Frame;

typedef struct FrameQueue
{
	Frame queue[MAX_FRAME_QUEUE_SIZE];//����������ʾ�ڵ�
	int rindex;			//��ǵ�ǰ�ɶ���Frame��index
	int windex;			//��ǵ�ǰ��д��Frame��index
	int size;			//��ǵ�ǰ�����Ѿ�д�˶��ٸ�
	int max_size;		//�������ڵ���
	SDL_mutex* mutex;	//��
	SDL_cond* cond;		//��������
} FrameQueue;

//packet���г�ʼ��
static int packet_queue_init(PacketQueue* q)
{
	memset(q, 0, sizeof(PacketQueue));

	//��Ϊpacket���лᱻ����̷߳���
	//����������Ҫ��������������Դ
	q->mutex = SDL_CreateMutex();
	if (!q->mutex)
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		return -1;
	}
	//��������
	q->cond = SDL_CreateCond();
	if (!q->mutex)
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		return -1;
	}

	return 0;
}

//packet�����
static int packet_queue_put(PacketQueue* q, AVPacket* pkt)
{
	SDL_LockMutex(q->mutex);

	//1.�ȹ���һ���ڵ�,�ýڵ�����Ϊ�¼���0������,����ڵ���Ҫ���뵽����β��
	MyAVPacketList* pkt1;
	pkt1 = (MyAVPacketList*)av_malloc(sizeof(MyAVPacketList));
	if (!pkt1)
	{
		av_packet_unref(pkt);
		return -1;
	}
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	//2.������߼���Ҫ����ľ��ǰ��µĽڵ���ص���ǰ
	//β�ڵ��next��ȥ,����������.Ȼ����3���
	//PacketQueue��β�ڵ����Ϊ���ι���Ľڵ�
	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;

	//3.����PacketQueue��β�ڵ�Ϊ�ոչ���Ľڵ�
	q->last_pkt = pkt1;

	//4.packet������һ
	q->nb_packets++;

	//5.packet��С����(���������ڴ�ռ��ͳ��)
	q->size += pkt1->pkt.size + sizeof(*pkt1);

	//6.��һ��packet�������,����֪ͨ��Щ�ȴ�
	//packet���߳̿��ԴӶ���ȡpacket��
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);

	return 0;
}

//packet�հ������
static int packet_queue_put_nullpacket(PacketQueue* q, AVPacket* pkt, int stream_index)
{
	//����հ���Ϊ���ļ�����ʱ,�����������Ϳհ�,������ϴ������
	//AVPacket pkt1, * pkt = &pkt1;
	//av_init_packet(pkt);
	//pkt->data = NULL;
	//pkt->size = 0;
	pkt->stream_index = stream_index;
	return packet_queue_put(q, pkt);
}

//packet������
static int packet_queue_get(PacketQueue* q, AVPacket* pkt)
{
	MyAVPacketList* pkt1;
	int ret;

	SDL_LockMutex(q->mutex);
	for (;;)
	{
		//1.ȡ�׽ڵ�
		pkt1 = q->first_pkt;

		//������packet
		if (pkt1)
		{
			//2.�Ѷ����׽ڵ����һ��
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;

			//3.��������һ
			q->nb_packets--;
			//4.packet��С����
			q->size -= pkt1->pkt.size + sizeof(*pkt1);
			//5.ȡpacket
			*pkt = pkt1->pkt;
			//6.�ͷ���Դ
			av_free(pkt1);
			ret = 1;
			break;
		}
		else
		{
			//�������û�а�,�����һֱ����
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

//packet����,�����������ƶ��г���
//��ֹ���޻�����ڴ�ľ�
static int packet_queue_num(PacketQueue* q)
{
	return q->nb_packets;
}

//packet����
static void packet_queue_destroy(PacketQueue* q)
{
	MyAVPacketList* pkt, * pkt1;

	SDL_LockMutex(q->mutex);
	for (pkt = q->first_pkt; pkt; pkt = pkt1)
	{
		pkt1 = pkt->next;
		av_packet_unref(&pkt->pkt);
		av_freep(&pkt);
	}
	q->last_pkt = NULL;
	q->first_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	SDL_UnlockMutex(q->mutex);

	SDL_DestroyMutex(q->mutex);
	SDL_DestroyCond(q->cond);
}

//frame��ʼ��
static int frame_queue_init(FrameQueue* f, int max_size)
{
	int i;
	//��ʼ����Ա
	memset(f, 0, sizeof(FrameQueue));

	//������
	f->mutex = SDL_CreateMutex();
	if (!f->mutex)
	{
		return -1;
	}
	//��������
	f->cond = SDL_CreateCond();
	if (!f->cond)
	{
		return -1;
	}
	//���ڵ���
	f->max_size = max_size;

	//��������еĽڵ�
	for (i = 0; i < f->max_size; i++)
	{
		f->queue[i].frame = av_frame_alloc();
		if (!f->queue[i].frame)
		{
			return -1;
		}
	}

	return 0;
}

//д����ǰӦ�õ��ô˺���������һ������д�Ľڵ�
static Frame* frame_queue_peek_writable(FrameQueue* f)
{
	SDL_LockMutex(f->mutex);

	//�������ǰ�Ѿ�д����еĸ����������ڵ���
	//˵����ǰ�����Ѿ�����,û��λ��д��,�������
	//�¾͵ȴ�,ֱ�����пճ�λ����
	while (f->size >= f->max_size)
	{
		SDL_CondWait(f->cond, f->mutex);
	}
	SDL_UnlockMutex(f->mutex);

	//����ֻ����һ����д��frame�ڵ�,��û��д����
	return &f->queue[f->windex];
}

//д������Ӧ�õ��ô˺����ѽڵ�windex���ƫ��һ��
static void frame_queue_push(FrameQueue* f)
{
	//�����ڵ��ʼѭ����
	if (++f->windex == f->max_size)
		f->windex = 0;

	SDL_LockMutex(f->mutex);
	//size��һ,��������һ���ڵ�
	f->size++;
	//�����пɶ��Ľڵ���
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}

//������ǰӦ�õ��ô˺���������һ�����Զ��Ľڵ�
static Frame* frame_queue_peek_readable(FrameQueue* f)
{
	SDL_LockMutex(f->mutex);

	while (f->size <= 0)
	{
		SDL_CondWait(f->cond, f->mutex);
	}
	SDL_UnlockMutex(f->mutex);

	return &f->queue[f->rindex];
}

//��������Ӧ�õ��ô˺����ѽڵ�rindex���ƫ��һ��
static void frame_queue_next(FrameQueue* f)
{
	av_frame_unref(f->queue[f->rindex].frame);

	if (++f->rindex == f->max_size)
		f->rindex = 0;

	SDL_LockMutex(f->mutex);
	//size��һ,���м���һ���ڵ�
	f->size--;
	//�����п�д�Ľڵ���
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}

//��frameֻ������һ�����
static void frame_queue_put_nullframe(Frame* f)
{
	f->frame_quit = 1;
}

//frame����
static void frame_queue_destory(FrameQueue* f)
{
	int i;
	for (i = 0; i < f->max_size; i++)
	{
		Frame* vp = &f->queue[i];
		av_frame_unref(vp->frame);
		av_frame_free(&vp->frame);
	}
	SDL_DestroyMutex(f->mutex);
	SDL_DestroyCond(f->cond);
}

//--------------------------------------------------------------------

static struct SwrContext* audio_swr_ctx = NULL;
static AVInputFormat* audio_iformat = NULL;
static const char* audio_filename = NULL;
static AVFormatContext* audio_ic = NULL;
static AVStream* audio_stream = NULL;
static AVCodecContext* audio_avctx = NULL;
static const AVCodec* audio_codec = NULL;
static FrameQueue f_audio_q;
static PacketQueue p_audio_q;
static int audio_index = -1;

static SDL_mutex* end_mutex;
static SDL_cond* end_cond;

static int audio_hardware_fmt = -1;
static int audio_hardware_sample_rate = 0;
static int audio_hardware_channels = 0;
static int64_t audio_hardware_layout = 0;
static SDL_AudioDeviceID audio_dev = 0;
static int audio_volume = 100;//��Χ0-128 SDL_MIX_MAXVOLUME

static uint8_t* audio_buf_data = NULL;//�����ĵ�����Ƶԭʼ����
static int audio_buf_size = 0;//�����ĵ�����Ƶԭʼ���ݴ�С
static int audio_buf_index = 0;//һ���α�ָ��,������¼�Ƿ���Ҫ��ȡ��������

//--------------------------------------------------------------------

static int audio_show(void* arg)
{

	SDL_Event event;
	while (1)
	{
		SDL_PumpEvents();
		while (!SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT, SDL_LASTEVENT))
		{
			SDL_PumpEvents();
		}

		//�û��¼�����
		switch (event.type)
		{
		case SDL_KEYDOWN:
			switch (event.key.keysym.sym)
			{
			case SDLK_UP:
				audio_volume += 10;
				if (audio_volume >= 128)
					audio_volume = 128;
				break;
			case SDLK_DOWN:
				audio_volume -= 10;
				if (audio_volume <= 0)
					audio_volume = 0;
				break;
			}
			break;
		case SDL_QUIT:
			//return 0;
			break;
		default:
			break;
		}
	}
}

static void audio_refresh(void* opaque, Uint8* stream, int len)
{
	Frame* af;
	static uint8_t* outs = NULL;
	int time_len = len;

	//����������û������
	while (len > 0)
	{
		if (audio_buf_index >= audio_buf_size)
		{
			//ȡһ֡frame
			af = frame_queue_peek_readable(&f_audio_q);
			if (af->frame_quit)
			{
				//������������ҲҪ����
				printf("�����߳̽���!\n");
				goto fail;
			}

			static int run_once = 1;
			if ((af->frame->format != audio_hardware_fmt ||
				af->frame->channel_layout != audio_hardware_layout ||
				af->frame->sample_rate != audio_hardware_sample_rate ||
				af->frame->channels != audio_hardware_channels) && run_once)
			{
				//��ʼ���ز����ṹ��
				audio_swr_ctx = swr_alloc_set_opts(NULL, audio_hardware_layout, (enum AVSampleFormat)audio_hardware_fmt, audio_hardware_sample_rate,
					af->frame->channel_layout, (enum AVSampleFormat)af->frame->format, af->frame->sample_rate, 0, NULL);

				if (!audio_swr_ctx || swr_init(audio_swr_ctx) < 0)
				{
					swr_free(&audio_swr_ctx);
					printf("�ز����ṹ�����ʧ��!\n");
					goto fail;
				}
				run_once = 0;
			}

			if (audio_swr_ctx)
			{
				//���ǵ���Ƶ��֡����,����ÿ�ζ�������ز���������,�ز���ÿͨ�����۵�������
				int out_count = (int64_t)af->frame->nb_samples * audio_hardware_sample_rate / af->frame->sample_rate + 256;
				//�ز�������ͨ�������С
				int out_size = av_samples_get_buffer_size(NULL, audio_hardware_channels, out_count, (enum AVSampleFormat)audio_hardware_fmt, 1);

				//�����ز����ռ�
				outs = (uint8_t*)realloc(outs, out_size);

				int len2 = swr_convert(audio_swr_ctx, &outs, out_count, (const uint8_t**)af->frame->extended_data, af->frame->nb_samples);
				audio_buf_size = len2 * audio_hardware_channels * av_get_bytes_per_sample((enum AVSampleFormat)audio_hardware_fmt);
				audio_buf_data = outs;
			}
			else
			{
				//���ز���
				audio_buf_size = av_samples_get_buffer_size(NULL, af->frame->channels, af->frame->nb_samples, (enum AVSampleFormat)af->frame->format, 1);
				audio_buf_data = af->frame->data[0];
			}
			frame_queue_next(&f_audio_q);

			audio_buf_index = 0;
		}

		//���㵱ǰ��������ж����ֽڵ�������Ҫ�͵�Ӳ��������
		int len1 = audio_buf_size - audio_buf_index;
		if (len1 > len)
			len1 = len;

		//copy���ݵ���Ƶ������,��ˢ������
		memset(stream, 0, len1);
		//SDL_MixAudioFormat(stream, (uint8_t*)audio_buf_data + audio_buf_index, AUDIO_S16SYS, len1, audio_volume);
		//SDL_MixAudio(stream, (uint8_t*)audio_buf_data + audio_buf_index, len1, audio_volume);
		memcpy(stream, (uint8_t*)audio_buf_data + audio_buf_index, len1);

		len -= len1;
		stream += len1;
		audio_buf_index += len1;
	}

	//���ݿ�����ȥ����
	//memcpy(audio_data, stream, time_len);

	//���㻺�������ݲ�������Ҫ����ʱ��
	static double time = 0.0;
	time += (double)time_len / av_get_bytes_per_sample((enum AVSampleFormat)audio_hardware_fmt) / (double)audio_hardware_channels * (1.0 / audio_hardware_sample_rate);
	printf("�Ѿ�����ʱ��:%.3lfs\r", time);

	return;

fail:
	if (audio_swr_ctx)
		swr_free(&audio_swr_ctx);

	if (outs)
		free(outs);

	SDL_LockMutex(end_mutex);
	SDL_CondSignal(end_cond);
	SDL_UnlockMutex(end_mutex);

	//�豸�����̴߳�,Ҳ�����̹߳ر�
	//SDL_CloseAudioDevice(audio_dev);
	return;
}

static int audio_decode(void* arg)
{
	int ret = 0;
	AVFrame* frame = av_frame_alloc();
	AVPacket pkt;
	Frame* af;

	while (1)
	{
		ret = avcodec_receive_frame(audio_avctx, frame);

		if (ret == 0)
		{
			static int debug_run_once = 1;
			if (debug_run_once)
			{
				printf("֡(��Ƶ)��:%d\n", frame->width);
				printf("֡(��Ƶ)��:%d\n", frame->height);
				printf("֡(��Ƶ)ÿͨ��������:%d\n", frame->nb_samples);
				printf("֡���ݸ�ʽ:%s\n", av_get_sample_fmt_name((enum AVSampleFormat)frame->format));
				printf("֡(��Ƶ)�Ƿ��ǹؼ�֡:%d\n", frame->key_frame);
				printf("֡(��Ƶ)����IPB:%d\n", frame->pict_type);
				printf("֡(��Ƶ)�����ݺ��:%d|%d\n", frame->sample_aspect_ratio.num, frame->sample_aspect_ratio.den);
				printf("֡pts:%d\n", frame->pts);
				printf("֡pkt_dts:%d\n", frame->pkt_dts);
				printf("֡(��Ƶ)������:%d\n", frame->sample_rate);
				printf("֡(��Ƶ)channel_layout:%d\n", frame->channel_layout);
				printf("֡best_effort_timestamp:%d\n", frame->best_effort_timestamp);
				printf("֡pkt_pos:%d\n", frame->pkt_pos);
				printf("֡pkt_duration:%d\n", frame->pkt_duration);
				printf("֡(��Ƶ)ͨ����:%d\n", frame->channels);
				printf("֡pkt_size:%d\n", frame->pkt_size);
				printf("-------------------------------------\n");
				debug_run_once = 0;
			}
			frame->pts = frame->best_effort_timestamp;
			af = frame_queue_peek_writable(&f_audio_q);
			av_frame_move_ref(af->frame, frame);
			frame_queue_push(&f_audio_q);
		}
		else if (ret == AVERROR(EAGAIN))
		{
			packet_queue_get(&p_audio_q, &pkt);
			avcodec_send_packet(audio_avctx, &pkt);
			av_packet_unref(&pkt);
		}
		else if (ret == AVERROR_EOF)
		{
			af = frame_queue_peek_writable(&f_audio_q);
			frame_queue_put_nullframe(af);
			av_frame_move_ref(af->frame, frame);
			frame_queue_push(&f_audio_q);
			printf("�����߳������˳�!\n");
			break;
		}
		else
		{
			af = frame_queue_peek_writable(&f_audio_q);
			frame_queue_put_nullframe(af);
			av_frame_move_ref(af->frame, frame);
			frame_queue_push(&f_audio_q);
			printf("�����߳��쳣�˳�!\n");
			break;
		}
	}
	av_frame_free(&frame);

	return 0;
}

int AudioPlayer1()
{
	const char* file = "./file/IronMan.mp4";

	AVPacket pkt1, * pkt = &pkt1;
	int ret = 0;

	SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER);

	// audio_filename = av_strdup("audio=��˷� (USB2.0 MIC)");
	// audio_iformat = av_find_input_format("dshow");
	audio_filename = av_strdup(file);
	audio_iformat = NULL;

	//���г�ʼ��
	frame_queue_init(&f_audio_q, MAX_FRAME_QUEUE_SIZE);
	packet_queue_init(&p_audio_q);

	//���ļ�
	ret = avformat_open_input(&audio_ic, audio_filename, audio_iformat, NULL);
	if (ret < 0)
	{
		printf("avformat_open_input\n");
		//goto fail;
	}

	//������
	ret = avformat_find_stream_info(audio_ic, NULL);
	if (ret < 0)
	{
		printf("avformat_find_stream_info\n");
		//goto fail;
	}

	av_dump_format(audio_ic, 0, audio_filename, 0);
	printf("-------------------------------------\n");

	//������index
	for (int i = 0; i < audio_ic->nb_streams; i++)
	{
		if (audio_ic->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			audio_index = i;
			break;
		}
	}

	if (audio_index == -1)
	{
		printf("no audio stream!\n");
		//goto fail;
	}

	//����ֵ
	audio_stream = audio_ic->streams[audio_index];

	//���������ctx
	audio_avctx = avcodec_alloc_context3(NULL);
	if (!audio_avctx)
	{
		printf("avcodec_alloc_context3\n");
		//goto fail;
	}

	//������������--->������ctx
	avcodec_parameters_to_context(audio_avctx, audio_stream->codecpar);

	//���ҽ�����
	audio_codec = avcodec_find_decoder(audio_avctx->codec_id);
	if (!audio_codec)
	{
		printf("avcodec_find_decoder\n");
		//goto fail;
	}

	//�򿪽�����
	ret = avcodec_open2(audio_avctx, audio_codec, NULL);
	if (ret < 0)
	{
		printf("avformat_open_input\n");
		//goto fail;
	}

	//1.AV_CODEC_CAP_VARIABLE_FRAME_SIZE,�ɱ�֡��־
	//2.ͨ��layout����
	//3.����time_base����
	audio_avctx->channel_layout = av_get_default_channel_layout(audio_avctx->channels);
	audio_avctx->pkt_timebase = audio_avctx->time_base;

	//��ӡһЩ�ؼ�����
#if 1
	printf("ȫ��time_base:%d\n", AV_TIME_BASE);
	printf("ȫ��nb_streams:%d\n", audio_ic->nb_streams);
	printf("ȫ��start_time:%d\n", audio_ic->start_time);
	printf("ȫ��duration:%d\n", audio_ic->duration);
	printf("ȫ��bit_rate:%dbits/s\n", audio_ic->bit_rate);
	printf("-------------------------------------\n");
	printf("��index:%d\n", audio_index);
	printf("��time_base:%d|%d\n", audio_stream->time_base.num, audio_stream->time_base.den);
	printf("��start_time:%d\n", audio_stream->start_time);
	printf("��duration:%d\n", audio_stream->duration);
	printf("-------------------------------------\n");
	printf("����������������(��Ƶ|��Ƶ|��Ļ):%d\n", audio_avctx->codec_type);
	printf("����������:%s\n", avcodec_get_name(audio_avctx->codec_id));
	printf("������������:%dbits/s\n", audio_avctx->bit_rate);
	printf("������time_base:%d|%d\n", audio_avctx->time_base.num, audio_avctx->time_base.den);
	printf("�������������ʱ:%d\n", audio_avctx->delay);
	printf("������(��Ƶ)width:%d\n", audio_avctx->width);
	printf("������(��Ƶ)height:%d\n", audio_avctx->height);
	printf("������(��Ƶ)coded_width:%d\n", audio_avctx->coded_width);
	printf("������(��Ƶ)coded_height:%d\n", audio_avctx->coded_height);
	printf("������(��Ƶ)GOP֡����:%d\n", audio_avctx->gop_size);
	printf("������(��Ƶ)ԭʼ���ݸ�ʽ:%s\n", av_get_pix_fmt_name(audio_avctx->pix_fmt));
	printf("������(��Ƶ)���B֡��:%d\n", audio_avctx->max_b_frames);
	printf("������(��Ƶ)�Ƿ���B֡:%d\n", audio_avctx->has_b_frames);
	printf("������(��Ƶ)������:%d\n", audio_avctx->sample_rate);
	printf("������(��Ƶ)ͨ����:%d\n", audio_avctx->channels);
	printf("������(��Ƶ)�������:%s\n", av_get_sample_fmt_name(audio_avctx->sample_fmt));
	printf("������(��Ƶ)ÿͨ��������:%d\n", audio_avctx->frame_size);
	printf("������(��Ƶ)channel_layout:%d\n", audio_avctx->channel_layout);
	printf("������(��Ƶ)֡��:%d|%d\n", audio_avctx->framerate.num, audio_avctx->framerate.den);
	printf("������pkt_timebase:%d|%d\n", audio_avctx->pkt_timebase.num, audio_avctx->pkt_timebase.den);
	printf("-------------------------------------\n");
#endif	

	SDL_AudioSpec wanted_spec, actual_spec;
	wanted_spec.channels = audio_avctx->channels;//ͨ����
	wanted_spec.freq = audio_avctx->sample_rate;//������		
	wanted_spec.format = AUDIO_S16SYS; //SDL֧�ֵ��з���,16bit
	wanted_spec.silence = 0;//����ֵ
	wanted_spec.samples = 512;//�ص������С,��λΪһ��ͨ����������
	wanted_spec.callback = audio_refresh;//�������
	wanted_spec.userdata = NULL;//�ص���������ָ��

	audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &actual_spec, 0);
	if (audio_dev == 0)
	{
		printf("SDL_OpenAudioDevice\n");
		//goto fail;
	}

	if (actual_spec.channels != wanted_spec.channels ||
		actual_spec.freq != wanted_spec.freq ||
		actual_spec.format != wanted_spec.format)
	{
		printf("SDL��֧�ֵ���Ƶ����!\n");
		//goto fail;
	}

	audio_hardware_fmt = AV_SAMPLE_FMT_S16;
	audio_hardware_sample_rate = actual_spec.freq;
	audio_hardware_channels = actual_spec.channels;
	audio_hardware_layout = av_get_default_channel_layout(actual_spec.channels);

	printf("�豸ID:%d\n", audio_dev);
	printf("ת��������:%s\n", av_get_sample_fmt_name((enum AVSampleFormat)audio_hardware_fmt));
	printf("ת�������:%d\n", audio_hardware_sample_rate);
	printf("ת�����ͨ��:%d\n", audio_hardware_channels);
	printf("ת��layout:%d\n", audio_hardware_layout);
	printf("-------------------------------------\n");

	SDL_Thread* thread1 = SDL_CreateThread(audio_decode, "audio_decode", NULL);
	if (!thread1)
	{
		printf("SDL_CreateThread\n");
		//goto fail;
	}

	//��Ƶ��ͼר���߳�
	//SDL_Thread* thread2 = SDL_CreateThread(audio_show, "audio_show", NULL);
	//if (!thread2)
	//{
	//	printf("SDL_CreateThread\n");
	//	goto fail;
	//}

	SDL_PauseAudioDevice(audio_dev, 0);

	//���������ź���,�����̲߳�����֪ͨ���߳��˳�
	end_mutex = SDL_CreateMutex();
	end_cond = SDL_CreateCond();

	SDL_Event event;

	//���߳�
	while (1)
	{
		if (packet_queue_num(&p_audio_q) > 500)
		{
			av_usleep(10000);
			continue;
		}

		ret = av_read_frame(audio_ic, pkt);
		if (ret < 0)
		{
			if (ret == AVERROR_EOF || avio_feof(audio_ic->pb))
			{
				packet_queue_put_nullpacket(&p_audio_q, pkt, audio_index);
				SDL_LockMutex(end_mutex);
				SDL_CondWait(end_cond, end_mutex);
				SDL_UnlockMutex(end_mutex);
				break;
			}
			else
			{
				packet_queue_put_nullpacket(&p_audio_q, pkt, audio_index);
				SDL_LockMutex(end_mutex);
				SDL_CondWait(end_cond, end_mutex);
				SDL_UnlockMutex(end_mutex);
				break;
			}
		}

		//��Ƶ�������,����������
		if (pkt->stream_index == audio_index)
		{
			static int debug_run_once = 1;
			if (debug_run_once)
			{
				printf("ѹ����PTS:%d\n", pkt->pts);
				printf("ѹ����DTS:%d\n", pkt->dts);
				printf("ѹ����size:%d\n", pkt->size);
				printf("ѹ����stream_index:%d\n", pkt->stream_index);
				printf("ѹ����duration:%d\n", pkt->duration);
				printf("ѹ����pos:%d\n", pkt->pos);
				printf("-------------------------------------\n");
				debug_run_once = 0;
			}
			av_packet_rescale_ts(pkt, audio_stream->time_base, audio_avctx->time_base);
			packet_queue_put(&p_audio_q, pkt);
		}
		else
		{
			av_packet_unref(pkt);
		}

		//SDL_PollEvent(&event);
		////�û��¼�����
		//switch (event.type)
		//{
		//case SDL_KEYDOWN:
		//	switch (event.key.keysym.sym)
		//	{
		//	case SDLK_UP:
		//		audio_volume += 10;
		//		if (audio_volume >= 128)
		//			audio_volume = 128;
		//		break;
		//	case SDLK_DOWN:
		//		audio_volume -= 10;
		//		if (audio_volume <= 0)
		//			audio_volume = 0;
		//		break;
		//	}
		//	break;
		//case SDL_QUIT:
		//	//return 0;
		//	break;
		//default:
		//	break;
		//}
	}

fail:
	packet_queue_destroy(&p_audio_q);
	frame_queue_destory(&f_audio_q);
	avformat_free_context(audio_ic);
	avcodec_free_context(&audio_avctx);

	if (audio_dev != 0)
		SDL_CloseAudioDevice(audio_dev);

	return 0;
}
