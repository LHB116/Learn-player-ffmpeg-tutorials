#include <Tutorial.h>

#define MAX_FRAME_QUEUE_SIZE 32
#define WINDOW_W 1280  // SDL���ڿ��
#define WINDOW_H 720
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
int packet_queue_put(PacketQueue* q, AVPacket* pkt)
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
int packet_queue_put_nullpacket(PacketQueue* q, AVPacket* pkt, int stream_index)
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
int packet_queue_get(PacketQueue* q, AVPacket* pkt)
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
int packet_queue_num(PacketQueue* q)
{
	return q->nb_packets;
}

//packet����
void packet_queue_destroy(PacketQueue* q)
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
int frame_queue_init(FrameQueue* f, int max_size)
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
Frame* frame_queue_peek_writable(FrameQueue* f)
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
void frame_queue_push(FrameQueue* f)
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
Frame* frame_queue_peek_readable(FrameQueue* f)
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
void frame_queue_next(FrameQueue* f)
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
void frame_queue_put_nullframe(Frame* f)
{
	f->frame_quit = 1;
}

//frame����
void frame_queue_destory(FrameQueue* f)
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

struct SwsContext* video_sws_ctx = NULL;
struct SwrContext* audio_swr_ctx = NULL;
AVInputFormat* audio_iformat = NULL;
const char* audio_filename = NULL;
AVFormatContext* audio_ic = NULL;
AVStream* audio_stream = NULL, * video_stream = NULL;
AVCodecContext* audio_avctx = NULL, * video_avctx = NULL;
static const AVCodec* audio_codec = NULL, * video_codec = NULL;
FrameQueue f_audio_q, f_video_q;
PacketQueue p_audio_q, p_video_q;
int audio_index = -1, video_index = -1;

SDL_mutex* end_mutex;
SDL_cond* end_cond;

int audio_hardware_fmt = -1;
int audio_hardware_sample_rate = 0;
int audio_hardware_channels = 0;
int64_t audio_hardware_layout = 0;
SDL_AudioDeviceID audio_dev = 0;
int audio_volume = 100;//��Χ0-128 SDL_MIX_MAXVOLUME

// ����Ƶͬ��ʱӦ���������¼��Ƶ��pts����Ƶ��Ⱦʱ���������Ƶ��pts����ͬ������
static float audio_clock;
static float video_clock;

// ������ͣ  ����
static bool g_exit = false;
static bool g_pause = false;

//static FILE* fp_264 = fopen("ba.h264", "wb");
static FILE* fp_yuv = fopen("ba.yuv", "wb");
//static FILE* pFile = fopen("ba.pcm", "wb");

int buf_size;
uint8_t* buffer = NULL;
AVFrame* p_frm_yuv = NULL;

static uint8_t* audio_buf_data = NULL;//�����ĵ�����Ƶԭʼ����
static int audio_buf_size = 0;//�����ĵ�����Ƶԭʼ���ݴ�С
static int audio_buf_index = 0;//һ���α�ָ��,������¼�Ƿ���Ҫ��ȡ��������

SDL_Window* window;
SDL_Renderer* renderer;
SDL_Texture* texture;
SDL_Thread* thread1, *thread2, *thread3;
//--------------------------------------------------------------------

static int video_show(void* arg)
{
	Frame* vf;
	int ret = 0;

	while (1)
	{
		if (g_pause) continue;
		if (g_exit) break;

		vf = frame_queue_peek_readable(&f_video_q);
		if (vf->frame_quit)
		{
			printf("��Ƶ�����߳̽���!\n");
			return -1;
		}

		sws_scale(video_sws_ctx,                                  // sws context
			(const uint8_t* const*)vf->frame->data,  // src slice
			vf->frame->linesize,                      // src stride
			0,                                        // src slice y
			video_avctx->height,                      // src slice height
			p_frm_yuv->data,                          // dst planes
			p_frm_yuv->linesize                       // dst strides
		);

		int y_size = video_avctx->width * video_avctx->height;
		fwrite(p_frm_yuv->data[0], 1, y_size, fp_yuv);    //Y 
		fwrite(p_frm_yuv->data[1], 1, y_size / 4, fp_yuv);  //U
		fwrite(p_frm_yuv->data[2], 1, y_size / 4, fp_yuv);  //V

		video_clock = av_q2d(video_avctx->time_base) * video_avctx->ticks_per_frame * 1000 * video_avctx->frame_number;
		//printf("��Ƶ֡pts: %f ms\n", video_clock);
		float duration = av_q2d(video_avctx->time_base) * video_avctx->ticks_per_frame * 1000;

		//for (int i = 0; i < vf->frame->linesize[0]; ++i) {
		//	std::cout << (int)vf->frame->data[0][i] << std::endl;
		//}

		//if (video_avctx->frame_number == 60) {
		//	for (int i = 1000; i < 1400; ++i) {
		//		printf("%d ", vf->frame->data[2][i]);
		//	}
		//	printf("\n");
		//	exit(0);
		//}

		//printf("%d, %d, %d\n", video_avctx->frame_number,
		//	video_avctx->height, video_avctx->width);

		//printf("%d, %d, %d, %d\n", video_avctx->frame_number, vf->frame->linesize[0],
		//	vf->frame->linesize[1], vf->frame->linesize[2]);
		//printf("%d, %d, %d\n", p_frm_yuv->linesize[0],
		//	p_frm_yuv->linesize[1], p_frm_yuv->linesize[2]);

		//SDL_UpdateYUVTexture(texture, nullptr,
		//	vf->frame->data[0], vf->frame->linesize[0],
		//	vf->frame->data[1], vf->frame->linesize[1],
		//	vf->frame->data[2], vf->frame->linesize[2]);

		SDL_UpdateYUVTexture(texture, nullptr,
			p_frm_yuv->data[0], p_frm_yuv->linesize[0],
			p_frm_yuv->data[1], p_frm_yuv->linesize[1],
			p_frm_yuv->data[2], p_frm_yuv->linesize[2]);

		SDL_RenderClear(renderer);  //������Ⱦ��������
		SDL_RenderCopy(renderer, texture, NULL, NULL); //����������������Ⱦƽ����
		SDL_RenderPresent(renderer); //��ת��������ǰ̨��ʾ

		//��ʱ����
		float delay = duration;
		float diff = video_clock - audio_clock;
		if (fabs(diff) <= duration) //ʱ�����һ֡��Χ�ڱ�ʾ��������ʱ����ʱ��
			delay = duration;
		else if (diff > duration) //��Ƶʱ�ӱ���Ƶʱ�ӿ죬�Ҵ���һ֡��ʱ�䣬��ʱ2��
			delay *= 2;
		else if (diff < -duration) //��Ƶʱ�ӱ���Ƶʱ�������ҳ���һ֡ʱ�䣬�������ŵ�ǰ֡
			delay = 1;

		printf("֡����%d ��ʱ: %f\n", video_avctx->frame_number, delay);

		SDL_Delay(delay);

		frame_queue_next(&f_video_q);
	}

	return 0;
}

static void audio_refresh(void* opaque, Uint8* stream, int len)
{
	if (g_exit || g_pause) {
		SDL_PauseAudioDevice(audio_dev, 1);
		return;
	}

	Frame* af;
	static uint8_t* outs = NULL;
	int time_len = len;

	//����������û������
	while (len > 0)
	{
		if (audio_buf_index >= audio_buf_size)
		{
			af = frame_queue_peek_readable(&f_audio_q);  //ȡһ֡frame
			if (af->frame_quit)
			{
				//������������ҲҪ����
				printf("��Ƶ�����߳̽���!\n");
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
		//memcpy(stream, (uint8_t *)audio_buf_data + audio_buf_index, len1);
		memset(stream, 0, len1);
		SDL_MixAudioFormat(stream, (uint8_t*)audio_buf_data + audio_buf_index, AUDIO_S16SYS, len1, audio_volume);

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

static int video_decode(void* arg) {
	int ret = 0;
	AVFrame* frame = av_frame_alloc();
	AVPacket pkt;
	Frame* af;

	while (1)
	{
		if (g_pause) continue;
		if (g_exit) break;

		ret = avcodec_receive_frame(video_avctx, frame);

		int aa = 0;
		if (ret == 0)
		{
			if (1) {
				frame->pts = frame->best_effort_timestamp;
				af = frame_queue_peek_writable(&f_video_q);
				av_frame_move_ref(af->frame, frame);
				frame_queue_push(&f_video_q);

				//if (0 && video_avctx->frame_number == 60) {
				//	for (int i = 1000; i < 1400; ++i) {
				//		printf("%d ", p_frm_yuv->data[2][i]);
				//	}
				//	printf("\n");
				//	exit(0);
				//}
			}
			else {
				video_clock = av_q2d(video_avctx->time_base) * video_avctx->ticks_per_frame * 1000 * video_avctx->frame_number;
				////printf("��Ƶ֡pts: %f ms\n", video_clock);
				float duration = av_q2d(video_avctx->time_base) * video_avctx->ticks_per_frame * 1000;

				sws_scale(video_sws_ctx,                                  // sws context
					(const uint8_t* const*)frame->data,  // src slice
					frame->linesize,                      // src stride
					0,                                        // src slice y
					video_avctx->height,                      // src slice height
					p_frm_yuv->data,                          // dst planes
					p_frm_yuv->linesize                       // dst strides
				);

				SDL_UpdateYUVTexture(texture, nullptr,
					p_frm_yuv->data[0], p_frm_yuv->linesize[0],
					p_frm_yuv->data[1], p_frm_yuv->linesize[1],
					p_frm_yuv->data[2], p_frm_yuv->linesize[2]);

				SDL_RenderClear(renderer);
				SDL_RenderCopy(renderer, texture, NULL, nullptr);
				SDL_RenderPresent(renderer);

				//��ʱ����
				float delay = duration;
				float diff = video_clock - audio_clock;
				if (fabs(diff) <= duration) //ʱ�����һ֡��Χ�ڱ�ʾ��������ʱ����ʱ��
					delay = duration;
				else if (diff > duration) //��Ƶʱ�ӱ���Ƶʱ�ӿ죬�Ҵ���һ֡��ʱ�䣬��ʱ2��
					delay *= 2;
				else if (diff < -duration) //��Ƶʱ�ӱ���Ƶʱ�������ҳ���һ֡ʱ�䣬�������ŵ�ǰ֡
					delay = 1;

				printf("֡����%d ��ʱ: %f\n", video_avctx->frame_number, delay);

				SDL_Delay(delay);
			}
		}
		else if (ret == AVERROR(EAGAIN))
		{
			packet_queue_get(&p_video_q, &pkt);
			avcodec_send_packet(video_avctx, &pkt);
			av_packet_unref(&pkt);
		}
		else if (ret == AVERROR_EOF)
		{
			af = frame_queue_peek_writable(&f_video_q);
			frame_queue_put_nullframe(af);
			av_frame_move_ref(af->frame, frame);
			frame_queue_push(&f_video_q);
			printf("�����߳������˳�!\n");
			break;
		}
		else
		{
			af = frame_queue_peek_writable(&f_video_q);
			frame_queue_put_nullframe(af);
			av_frame_move_ref(af->frame, frame);
			frame_queue_push(&f_video_q);
			printf("�����߳��쳣�˳�!\n");
			break;
		}
	}

	av_frame_free(&frame);

	return 0;
}

static int audio_decode(void* arg)
{
	int ret = 0;
	AVFrame* frame = av_frame_alloc();
	AVPacket pkt;
	Frame* af;

	while (1)
	{
		if (g_pause) continue;
		if (g_exit) break;

		ret = avcodec_receive_frame(audio_avctx, frame);

		if (ret == 0)
		{
			audio_clock = frame->pts * av_q2d(audio_avctx->time_base) * 1000;
			//printf("��Ƶ֡pts: %f ms\n", audio_clock);

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

int SDLPlayer2()
{
	const char* file = "./file/ba.mp4";

	AVPacket pkt1, * pkt = &pkt1;
	int ret = 0;

	// audio_filename = av_strdup("audio=��˷� (USB2.0 MIC)");
	// audio_iformat = av_find_input_format("dshow");
	audio_filename = av_strdup(file);
	audio_iformat = NULL;

	//���г�ʼ��
	if (frame_queue_init(&f_audio_q, MAX_FRAME_QUEUE_SIZE) < 0) {
		printf("f_audio_q frame_queue_init\n");
		goto fail;
	}
	if (packet_queue_init(&p_audio_q) < 0) {
		printf("p_audio_q frame_queue_init\n");
		goto fail;
	}
	if (frame_queue_init(&f_video_q, MAX_FRAME_QUEUE_SIZE) < 0) {
		printf("f_video_q frame_queue_init\n");
		goto fail;
	}
	if (packet_queue_init(&p_video_q) < 0) {
		printf("p_p_video_qaudio_q frame_queue_init\n");
		goto fail;
	}

	//���ļ�
	ret = avformat_open_input(&audio_ic, audio_filename, audio_iformat, NULL);
	if (ret < 0)
	{
		printf("avformat_open_input\n");
		goto fail;
	}

	//������
	ret = avformat_find_stream_info(audio_ic, NULL);
	if (ret < 0)
	{
		printf("avformat_find_stream_info\n");
		goto fail;
	}

	av_dump_format(audio_ic, 0, audio_filename, 0);
	printf("-------------------------------------\n");

	//������index
	audio_index = av_find_best_stream(audio_ic, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
	video_index = av_find_best_stream(audio_ic, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	printf("audio_index = %d, video_index = %d\n", audio_index, video_index);
	if (audio_index == -1)
	{
		printf("no audio stream!\n");
		goto fail;
	}
	if (audio_index == -1)
	{
		printf("no audio stream!\n");
		goto fail;
	}

	//����ֵ
	audio_stream = audio_ic->streams[audio_index];
	video_stream = audio_ic->streams[video_index];

	//���������ctx
	audio_avctx = avcodec_alloc_context3(NULL);
	if (!audio_avctx)
	{
		printf("avcodec_alloc_context3\n");
		goto fail;
	}
	video_avctx = avcodec_alloc_context3(NULL);
	if (!video_avctx)
	{
		printf("avcodec_alloc_context3\n");
		goto fail;
	}

	//������������--->������ctx
	avcodec_parameters_to_context(audio_avctx, audio_stream->codecpar);
	avcodec_parameters_to_context(video_avctx, video_stream->codecpar);

	//���ҽ�����
	audio_codec = avcodec_find_decoder(audio_avctx->codec_id);
	if (!audio_codec)
	{
		printf("avcodec_find_decoder\n");
		goto fail;
	}
	video_codec = avcodec_find_decoder(video_avctx->codec_id);
	if (!video_codec)
	{
		printf("avcodec_find_decoder\n");
		goto fail;
	}

	//�򿪽�����
	ret = avcodec_open2(audio_avctx, audio_codec, NULL);
	if (ret < 0)
	{
		printf("avformat_open_input\n");
		goto fail;
	}
	ret = avcodec_open2(video_avctx, video_codec, NULL);
	if (ret < 0)
	{
		printf("avformat_open_input\n");
		goto fail;
	}

	//1.AV_CODEC_CAP_VARIABLE_FRAME_SIZE,�ɱ�֡��־
	//2.ͨ��layout����
	//3.����time_base����
	audio_avctx->channel_layout = av_get_default_channel_layout(audio_avctx->channels);
	audio_avctx->pkt_timebase = audio_avctx->time_base;

	p_frm_yuv = av_frame_alloc();
	if (p_frm_yuv == NULL)
	{
		printf("av_frame_alloc() for p_frm_yuv failed\n");
		return 0;
	}

	printf("%d, %d\n", video_avctx->width, video_avctx->height);
	//exit(0);

	buf_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
		video_avctx->width,
		video_avctx->height,
		1
	);
	// buffer����Ϊp_frm_yuv����Ƶ���ݻ�����
	buffer = (uint8_t*)av_malloc(buf_size);
	if (buffer == NULL)
	{
		printf("av_malloc() for buffer failed\n");
		return 0;
	}
	// ʹ�ø��������趨p_frm_yuv->data��p_frm_yuv->linesize
	ret = av_image_fill_arrays(
		p_frm_yuv->data,     // dst data[]
		p_frm_yuv->linesize, // dst linesize[]
		buffer,              // src buffer
		AV_PIX_FMT_YUV420P,  // pixel format
		video_avctx->width,  // width
		video_avctx->height, // height
		1                    // align
	);
	if (ret < 0)
	{
		printf("av_image_fill_arrays() failed %d\n", ret);
		return 0;
	}

	//printf("%d, %d, %d\n", p_frm_yuv->linesize[0],
	//p_frm_yuv->linesize[1], p_frm_yuv->linesize[2]);
	//exit(9);

	video_sws_ctx = sws_getContext(video_avctx->width,    // src width
		video_avctx->height,   // src height
		video_avctx->pix_fmt,  // src format
		video_avctx->width,    // dst width
		video_avctx->height,   // dst height
		AV_PIX_FMT_YUV420P,    // dst format
		SWS_BICUBIC,           // flags
		NULL,                  // src filter
		NULL,                  // dst filter
		NULL                   // param
	);

	if (video_sws_ctx == NULL)
	{
		printf("sws_getContext() failed\n");
		return 0;
	}

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

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
	{
		printf("init sdl error:%s\n", SDL_GetError());
		return 0;
	}

	window = SDL_CreateWindow("player", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_W, WINDOW_H, SDL_WINDOW_OPENGL);
	if (!window)
	{
		printf("Failed to create window: %s", SDL_GetError());
		return 0;
	}
	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
	//renderer = SDL_CreateRenderer(window, -1, 0);
	if (!renderer)
	{
		printf("Failed to initialize a hardware accelerated renderer!\n");
		return 0;
	}
	//�����ݺ��  SDL_PIXELFORMAT_YV12  SDL_PIXELFORMAT_IYUV
	//SDL_RenderSetLogicalSize(renderer, 200, 200); 
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, video_avctx->width, video_avctx->height);
	if (!texture)
	{
		printf("Failed to create texture: %s", SDL_GetError());
		return 0;
	}
	//SDL_SetRenderDrawColor(renderer, 0xff, 0xc0, 0xcb, 0xff);
	SDL_RenderClear(renderer);

	SDL_AudioSpec wanted_spec, actual_spec;
	wanted_spec.channels = audio_avctx->channels;//ͨ����
	wanted_spec.freq = audio_avctx->sample_rate;//������		
	wanted_spec.format = AUDIO_S16SYS; //SDL֧�ֵ��з���,16bit
	wanted_spec.silence = 0;//����ֵ
	wanted_spec.samples = audio_avctx->frame_size;//�ص������С,��λΪһ��ͨ����������
	wanted_spec.callback = audio_refresh;//�������
	wanted_spec.userdata = NULL;//�ص���������ָ��

	audio_dev = SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &actual_spec, 0);
	if (audio_dev == 0)
	{
		printf("SDL_OpenAudioDevice\n");
		goto fail;
	}

	if (actual_spec.channels != wanted_spec.channels ||
		actual_spec.freq != wanted_spec.freq ||
		actual_spec.format != wanted_spec.format)
	{
		printf("SDL��֧�ֵ���Ƶ����!\n");
		goto fail;
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

	thread1 = SDL_CreateThread(audio_decode, "audio_decode", NULL);
	if (!thread1)
	{
		printf("SDL_CreateThread thread1 fail\n");
		goto fail;
	}

	thread2 = SDL_CreateThread(video_decode, "vide_decode", NULL);
	if (!thread2)
	{
		printf("SDL_CreateThread thread2 fail\n");
		goto fail;
	}

	thread3 = SDL_CreateThread(video_show, "audio_video_show", NULL);
	if (!thread3)
	{
		printf("SDL_CreateThread thread3 fail\n");
		goto fail;
	}

	SDL_PauseAudioDevice(audio_dev, 0);

	//���������ź���,�����̲߳�����֪ͨ���߳��˳�
	end_mutex = SDL_CreateMutex();
	end_cond = SDL_CreateCond();

	//���߳�
	SDL_Event event;
	while (!g_exit)
	{
		if (packet_queue_num(&p_audio_q) > 500 || packet_queue_num(&p_video_q) > 500)
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
				packet_queue_put_nullpacket(&p_video_q, pkt, video_index);
				SDL_LockMutex(end_mutex);
				SDL_CondWait(end_cond, end_mutex);
				SDL_UnlockMutex(end_mutex);
				break;
			}
			else
			{
				packet_queue_put_nullpacket(&p_audio_q, pkt, audio_index);
				packet_queue_put_nullpacket(&p_video_q, pkt, video_index);
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
		} else if (pkt->stream_index == video_index) {
			av_packet_rescale_ts(pkt, video_stream->time_base, video_avctx->time_base);
			packet_queue_put(&p_video_q, pkt);
		} else
		{
			av_packet_unref(pkt);
		}

		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
			case SDL_KEYDOWN:
				if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)
				{
					g_exit = true;
					SDL_Log("quit\n");
					return 0;
				}
				else if (event.key.keysym.scancode == SDL_SCANCODE_SPACE)
				{
					g_pause = !g_pause;
					SDL_Log(g_pause == true ? "pause" : "continue");
				}
				break;

			case SDL_QUIT:
				SDL_Log("quit\n");
				g_exit = true;
				return 0;

			default:
				break;
			}
		}
	}

fail:
	packet_queue_destroy(&p_audio_q);
	frame_queue_destory(&f_audio_q);
	avformat_free_context(audio_ic);
	avcodec_free_context(&audio_avctx);

	packet_queue_destroy(&p_video_q);
	frame_queue_destory(&f_video_q);;
	avcodec_free_context(&video_avctx);

	if (audio_dev != 0)
		SDL_CloseAudioDevice(audio_dev);

	return 0;
}
