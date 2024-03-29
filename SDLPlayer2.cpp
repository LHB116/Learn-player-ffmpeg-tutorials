#include <Tutorial.h>

#define MAX_FRAME_QUEUE_SIZE 32
#define WINDOW_W 1280  // SDL窗口宽高
#define WINDOW_H 720
//--------------------------------------------------------------------

//packet缓冲队列节点
typedef struct MyAVPacketList
{
	AVPacket pkt;
	struct MyAVPacketList* next;
} MyAVPacketList;

//packet队列结构体里面只保存了首节点和尾节点,中间节点
//通过MyAVPacketList->next连接起来,packet入队列时就插
//入到队列尾部,packet出队列时就从首部取packet
typedef struct PacketQueue
{
	MyAVPacketList* first_pkt;	//首节点
	MyAVPacketList* last_pkt;	//尾节点
	int abort_request;			//中断请求
	int nb_packets;				//队列总共packet个数
	int size;					//所有packet的size大小
	SDL_mutex* mutex;			//锁
	SDL_cond* cond;				//条件变量
} PacketQueue;

//frame节点
typedef struct Frame
{
	AVFrame* frame;	//frame
	int frame_quit;	//标记该frame是否是最后一个frame
} Frame;

typedef struct FrameQueue
{
	Frame queue[MAX_FRAME_QUEUE_SIZE];//用数组来表示节点
	int rindex;			//标记当前可读的Frame的index
	int windex;			//标记当前可写的Frame的index
	int size;			//标记当前队列已经写了多少个
	int max_size;		//队列最大节点数
	SDL_mutex* mutex;	//锁
	SDL_cond* cond;		//条件变量
} FrameQueue;

//packet队列初始化
static int packet_queue_init(PacketQueue* q)
{
	memset(q, 0, sizeof(PacketQueue));

	//因为packet队列会被多个线程访问
	//所以这里需要创建锁来保护资源
	q->mutex = SDL_CreateMutex();
	if (!q->mutex)
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateMutex(): %s\n", SDL_GetError());
		return -1;
	}
	//条件变量
	q->cond = SDL_CreateCond();
	if (!q->mutex)
	{
		av_log(NULL, AV_LOG_FATAL, "SDL_CreateCond(): %s\n", SDL_GetError());
		return -1;
	}

	return 0;
}

//packet入队列
int packet_queue_put(PacketQueue* q, AVPacket* pkt)
{
	SDL_LockMutex(q->mutex);

	//1.先构造一个节点,该节点数据为新加入0的数据,这个节点需要插入到队列尾部
	MyAVPacketList* pkt1;
	pkt1 = (MyAVPacketList*)av_malloc(sizeof(MyAVPacketList));
	if (!pkt1)
	{
		av_packet_unref(pkt);
		return -1;
	}
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

	//2.这里的逻辑需要处理的就是把新的节点挂载到当前
	//尾节点的next上去,起到连接作用.然后步骤3会把
	//PacketQueue的尾节点更新为本次构造的节点
	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;

	//3.更新PacketQueue的尾节点为刚刚构造的节点
	q->last_pkt = pkt1;

	//4.packet个数加一
	q->nb_packets++;

	//5.packet大小增加(可以用作内存占用统计)
	q->size += pkt1->pkt.size + sizeof(*pkt1);

	//6.有一个packet入队列了,这里通知那些等待
	//packet的线程可以从队列取packet了
	SDL_CondSignal(q->cond);

	SDL_UnlockMutex(q->mutex);

	return 0;
}

//packet空包入队列
int packet_queue_put_nullpacket(PacketQueue* q, AVPacket* pkt, int stream_index)
{
	//构造空包是为了文件结束时,往解码器发送空包,用来冲洗解码器
	//AVPacket pkt1, * pkt = &pkt1;
	//av_init_packet(pkt);
	//pkt->data = NULL;
	//pkt->size = 0;
	pkt->stream_index = stream_index;
	return packet_queue_put(q, pkt);
}

//packet出队列
int packet_queue_get(PacketQueue* q, AVPacket* pkt)
{
	MyAVPacketList* pkt1;
	int ret;

	SDL_LockMutex(q->mutex);
	for (;;)
	{
		//1.取首节点
		pkt1 = q->first_pkt;

		//队列有packet
		if (pkt1)
		{
			//2.把队列首节点更新一下
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
				q->last_pkt = NULL;

			//3.包个数减一
			q->nb_packets--;
			//4.packet大小减掉
			q->size -= pkt1->pkt.size + sizeof(*pkt1);
			//5.取packet
			*pkt = pkt1->pkt;
			//6.释放资源
			av_free(pkt1);
			ret = 1;
			break;
		}
		else
		{
			//如果队列没有包,这里会一直阻塞
			SDL_CondWait(q->cond, q->mutex);
		}
	}
	SDL_UnlockMutex(q->mutex);
	return ret;
}

//packet个数,可以用来控制队列长度
//防止无限缓冲把内存耗尽
int packet_queue_num(PacketQueue* q)
{
	return q->nb_packets;
}

//packet销毁
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

//frame初始化
int frame_queue_init(FrameQueue* f, int max_size)
{
	int i;
	//初始化成员
	memset(f, 0, sizeof(FrameQueue));

	//分配锁
	f->mutex = SDL_CreateMutex();
	if (!f->mutex)
	{
		return -1;
	}
	//条件变量
	f->cond = SDL_CreateCond();
	if (!f->cond)
	{
		return -1;
	}
	//最大节点数
	f->max_size = max_size;

	//分配好所有的节点
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

//写操作前应该调用此函数来查找一个可以写的节点
Frame* frame_queue_peek_writable(FrameQueue* f)
{
	SDL_LockMutex(f->mutex);

	//如果都当前已经写入队列的个数等于最大节点数
	//说明当前队列已经满了,没有位置写了,这种情况
	//下就等待,直到队列空出位置来
	while (f->size >= f->max_size)
	{
		SDL_CondWait(f->cond, f->mutex);
	}
	SDL_UnlockMutex(f->mutex);

	//这里只返回一个可写的frame节点,并没有写数据
	return &f->queue[f->windex];
}

//写操作后应该调用此函数把节点windex向后偏移一下
void frame_queue_push(FrameQueue* f)
{
	//到最大节点后开始循环了
	if (++f->windex == f->max_size)
		f->windex = 0;

	SDL_LockMutex(f->mutex);
	//size加一,队列新增一个节点
	f->size++;
	//队列有可读的节点了
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}

//读操作前应该调用此函数来查找一个可以读的节点
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

//读操作后应该调用此函数把节点rindex向后偏移一下
void frame_queue_next(FrameQueue* f)
{
	av_frame_unref(f->queue[f->rindex].frame);

	if (++f->rindex == f->max_size)
		f->rindex = 0;

	SDL_LockMutex(f->mutex);
	//size减一,队列减少一个节点
	f->size--;
	//队列有可写的节点了
	SDL_CondSignal(f->cond);
	SDL_UnlockMutex(f->mutex);
}

//空frame只是做了一个标记
void frame_queue_put_nullframe(Frame* f)
{
	f->frame_quit = 1;
}

//frame销毁
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
int audio_volume = 100;//范围0-128 SDL_MIX_MAXVOLUME

// 音视频同步时应该在哪里记录音频的pts，视频渲染时如何利用音频的pts进行同步控制
static float audio_clock;
static float video_clock;

// 控制暂停  播放
static bool g_exit = false;
static bool g_pause = false;

//static FILE* fp_264 = fopen("ba.h264", "wb");
static FILE* fp_yuv = fopen("ba.yuv", "wb");
//static FILE* pFile = fopen("ba.pcm", "wb");

int buf_size;
uint8_t* buffer = NULL;
AVFrame* p_frm_yuv = NULL;

static uint8_t* audio_buf_data = NULL;//解码后的到的音频原始数据
static int audio_buf_size = 0;//解码后的到的音频原始数据大小
static int audio_buf_index = 0;//一个游标指针,用来记录是否需要获取更多数据

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
			printf("视频播放线程结束!\n");
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
		//printf("视频帧pts: %f ms\n", video_clock);
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

		SDL_RenderClear(renderer);  //清理渲染器缓冲区
		SDL_RenderCopy(renderer, texture, NULL, NULL); //将纹理拷贝到窗口渲染平面上
		SDL_RenderPresent(renderer); //翻转缓冲区，前台显示

		//延时处理
		float delay = duration;
		float diff = video_clock - audio_clock;
		if (fabs(diff) <= duration) //时间差在一帧范围内表示正常，延时正常时间
			delay = duration;
		else if (diff > duration) //视频时钟比音频时钟快，且大于一帧的时间，延时2倍
			delay *= 2;
		else if (diff < -duration) //视频时钟比音频时钟慢，且超出一帧时间，立即播放当前帧
			delay = 1;

		printf("帧数：%d 延时: %f\n", video_avctx->frame_number, delay);

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

	//缓冲区数据没有填满
	while (len > 0)
	{
		if (audio_buf_index >= audio_buf_size)
		{
			af = frame_queue_peek_readable(&f_audio_q);  //取一帧frame
			if (af->frame_quit)
			{
				//最后残留的数据也要播放
				printf("音频播放线程结束!\n");
				goto fail;
			}

			static int run_once = 1;
			if ((af->frame->format != audio_hardware_fmt ||
				af->frame->channel_layout != audio_hardware_layout ||
				af->frame->sample_rate != audio_hardware_sample_rate ||
				af->frame->channels != audio_hardware_channels) && run_once)
			{
				//初始化重采样结构体
				audio_swr_ctx = swr_alloc_set_opts(NULL, audio_hardware_layout, (enum AVSampleFormat)audio_hardware_fmt, audio_hardware_sample_rate,
					af->frame->channel_layout, (enum AVSampleFormat)af->frame->format, af->frame->sample_rate, 0, NULL);

				if (!audio_swr_ctx || swr_init(audio_swr_ctx) < 0)
				{
					swr_free(&audio_swr_ctx);
					printf("重采样结构体分配失败!\n");
					goto fail;
				}
				run_once = 0;
			}

			if (audio_swr_ctx)
			{
				//考虑到音频变帧问题,这里每次都会计算重采样样本数,重采样每通道理论的样本数
				int out_count = (int64_t)af->frame->nb_samples * audio_hardware_sample_rate / af->frame->sample_rate + 256;
				//重采样所有通道缓存大小
				int out_size = av_samples_get_buffer_size(NULL, audio_hardware_channels, out_count, (enum AVSampleFormat)audio_hardware_fmt, 1);

				//分配重采样空间
				outs = (uint8_t*)realloc(outs, out_size);

				int len2 = swr_convert(audio_swr_ctx, &outs, out_count, (const uint8_t**)af->frame->extended_data, af->frame->nb_samples);
				audio_buf_size = len2 * audio_hardware_channels * av_get_bytes_per_sample((enum AVSampleFormat)audio_hardware_fmt);
				audio_buf_data = outs;
			}
			else
			{
				//不重采样
				audio_buf_size = av_samples_get_buffer_size(NULL, af->frame->channels, af->frame->nb_samples, (enum AVSampleFormat)af->frame->format, 1);
				audio_buf_data = af->frame->data[0];
			}

			frame_queue_next(&f_audio_q);

			audio_buf_index = 0;
		}

		//计算当前解码出来有多少字节的数据需要送到硬件缓冲区
		int len1 = audio_buf_size - audio_buf_index;
		if (len1 > len)
			len1 = len;

		//copy数据到音频缓冲区,并刷新音量
		//memcpy(stream, (uint8_t *)audio_buf_data + audio_buf_index, len1);
		memset(stream, 0, len1);
		SDL_MixAudioFormat(stream, (uint8_t*)audio_buf_data + audio_buf_index, AUDIO_S16SYS, len1, audio_volume);

		len -= len1;
		stream += len1;
		audio_buf_index += len1;
	}

	//数据拷贝出去分析
	//memcpy(audio_data, stream, time_len);

	//计算缓冲区数据播放完需要多少时间
	static double time = 0.0;
	time += (double)time_len / av_get_bytes_per_sample((enum AVSampleFormat)audio_hardware_fmt) / (double)audio_hardware_channels * (1.0 / audio_hardware_sample_rate);
	printf("已经播放时间:%.3lfs\r", time);

	return;

fail:
	if (audio_swr_ctx)
		swr_free(&audio_swr_ctx);

	if (outs)
		free(outs);

	SDL_LockMutex(end_mutex);
	SDL_CondSignal(end_cond);
	SDL_UnlockMutex(end_mutex);

	//设备在主线程打开,也在主线程关闭
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
				////printf("视频帧pts: %f ms\n", video_clock);
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

				//延时处理
				float delay = duration;
				float diff = video_clock - audio_clock;
				if (fabs(diff) <= duration) //时间差在一帧范围内表示正常，延时正常时间
					delay = duration;
				else if (diff > duration) //视频时钟比音频时钟快，且大于一帧的时间，延时2倍
					delay *= 2;
				else if (diff < -duration) //视频时钟比音频时钟慢，且超出一帧时间，立即播放当前帧
					delay = 1;

				printf("帧数：%d 延时: %f\n", video_avctx->frame_number, delay);

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
			printf("解码线程正常退出!\n");
			break;
		}
		else
		{
			af = frame_queue_peek_writable(&f_video_q);
			frame_queue_put_nullframe(af);
			av_frame_move_ref(af->frame, frame);
			frame_queue_push(&f_video_q);
			printf("解码线程异常退出!\n");
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
			//printf("音频帧pts: %f ms\n", audio_clock);

			static int debug_run_once = 1;
			if (debug_run_once)
			{
				printf("帧(视频)宽:%d\n", frame->width);
				printf("帧(视频)高:%d\n", frame->height);
				printf("帧(音频)每通道样本数:%d\n", frame->nb_samples);
				printf("帧数据格式:%s\n", av_get_sample_fmt_name((enum AVSampleFormat)frame->format));
				printf("帧(视频)是否是关键帧:%d\n", frame->key_frame);
				printf("帧(视频)类型IPB:%d\n", frame->pict_type);
				printf("帧(视频)采样纵横比:%d|%d\n", frame->sample_aspect_ratio.num, frame->sample_aspect_ratio.den);
				printf("帧pts:%d\n", frame->pts);
				printf("帧pkt_dts:%d\n", frame->pkt_dts);
				printf("帧(音频)采样率:%d\n", frame->sample_rate);
				printf("帧(音频)channel_layout:%d\n", frame->channel_layout);
				printf("帧best_effort_timestamp:%d\n", frame->best_effort_timestamp);
				printf("帧pkt_pos:%d\n", frame->pkt_pos);
				printf("帧pkt_duration:%d\n", frame->pkt_duration);
				printf("帧(音频)通道数:%d\n", frame->channels);
				printf("帧pkt_size:%d\n", frame->pkt_size);
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
			printf("解码线程正常退出!\n");
			break;
		}
		else
		{
			af = frame_queue_peek_writable(&f_audio_q);
			frame_queue_put_nullframe(af);
			av_frame_move_ref(af->frame, frame);
			frame_queue_push(&f_audio_q);
			printf("解码线程异常退出!\n");
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

	// audio_filename = av_strdup("audio=麦克风 (USB2.0 MIC)");
	// audio_iformat = av_find_input_format("dshow");
	audio_filename = av_strdup(file);
	audio_iformat = NULL;

	//队列初始化
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

	//打开文件
	ret = avformat_open_input(&audio_ic, audio_filename, audio_iformat, NULL);
	if (ret < 0)
	{
		printf("avformat_open_input\n");
		goto fail;
	}

	//查找流
	ret = avformat_find_stream_info(audio_ic, NULL);
	if (ret < 0)
	{
		printf("avformat_find_stream_info\n");
		goto fail;
	}

	av_dump_format(audio_ic, 0, audio_filename, 0);
	printf("-------------------------------------\n");

	//查找流index
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

	//流赋值
	audio_stream = audio_ic->streams[audio_index];
	video_stream = audio_ic->streams[video_index];

	//分配解码器ctx
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

	//流解码器参数--->解码器ctx
	avcodec_parameters_to_context(audio_avctx, audio_stream->codecpar);
	avcodec_parameters_to_context(video_avctx, video_stream->codecpar);

	//查找解码器
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

	//打开解码器
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

	//1.AV_CODEC_CAP_VARIABLE_FRAME_SIZE,可变帧标志
	//2.通道layout设置
	//3.两个time_base设置
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
	// buffer将作为p_frm_yuv的视频数据缓冲区
	buffer = (uint8_t*)av_malloc(buf_size);
	if (buffer == NULL)
	{
		printf("av_malloc() for buffer failed\n");
		return 0;
	}
	// 使用给定参数设定p_frm_yuv->data和p_frm_yuv->linesize
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

	//打印一些关键参数
#if 1
	printf("全局time_base:%d\n", AV_TIME_BASE);
	printf("全局nb_streams:%d\n", audio_ic->nb_streams);
	printf("全局start_time:%d\n", audio_ic->start_time);
	printf("全局duration:%d\n", audio_ic->duration);
	printf("全局bit_rate:%dbits/s\n", audio_ic->bit_rate);
	printf("-------------------------------------\n");
	printf("流index:%d\n", audio_index);
	printf("流time_base:%d|%d\n", audio_stream->time_base.num, audio_stream->time_base.den);
	printf("流start_time:%d\n", audio_stream->start_time);
	printf("流duration:%d\n", audio_stream->duration);
	printf("-------------------------------------\n");
	printf("解码器解码流类型(视频|音频|字幕):%d\n", audio_avctx->codec_type);
	printf("解码器类型:%s\n", avcodec_get_name(audio_avctx->codec_id));
	printf("解码器比特率:%dbits/s\n", audio_avctx->bit_rate);
	printf("解码器time_base:%d|%d\n", audio_avctx->time_base.num, audio_avctx->time_base.den);
	printf("解码器编解码延时:%d\n", audio_avctx->delay);
	printf("解码器(视频)width:%d\n", audio_avctx->width);
	printf("解码器(视频)height:%d\n", audio_avctx->height);
	printf("解码器(视频)coded_width:%d\n", audio_avctx->coded_width);
	printf("解码器(视频)coded_height:%d\n", audio_avctx->coded_height);
	printf("解码器(视频)GOP帧个数:%d\n", audio_avctx->gop_size);
	printf("解码器(视频)原始数据格式:%s\n", av_get_pix_fmt_name(audio_avctx->pix_fmt));
	printf("解码器(视频)最大B帧数:%d\n", audio_avctx->max_b_frames);
	printf("解码器(视频)是否有B帧:%d\n", audio_avctx->has_b_frames);
	printf("解码器(音频)采样率:%d\n", audio_avctx->sample_rate);
	printf("解码器(音频)通道数:%d\n", audio_avctx->channels);
	printf("解码器(音频)采样深度:%s\n", av_get_sample_fmt_name(audio_avctx->sample_fmt));
	printf("解码器(音频)每通道样本数:%d\n", audio_avctx->frame_size);
	printf("解码器(音频)channel_layout:%d\n", audio_avctx->channel_layout);
	printf("解码器(视频)帧率:%d|%d\n", audio_avctx->framerate.num, audio_avctx->framerate.den);
	printf("解码器pkt_timebase:%d|%d\n", audio_avctx->pkt_timebase.num, audio_avctx->pkt_timebase.den);
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
	//保持纵横比  SDL_PIXELFORMAT_YV12  SDL_PIXELFORMAT_IYUV
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
	wanted_spec.channels = audio_avctx->channels;//通道数
	wanted_spec.freq = audio_avctx->sample_rate;//采样率		
	wanted_spec.format = AUDIO_S16SYS; //SDL支持的有符号,16bit
	wanted_spec.silence = 0;//静音值
	wanted_spec.samples = audio_avctx->frame_size;//回调缓冲大小,单位为一个通道的样本数
	wanted_spec.callback = audio_refresh;//按需调用
	wanted_spec.userdata = NULL;//回调函数参数指针

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
		printf("SDL不支持的音频参数!\n");
		goto fail;
	}

	audio_hardware_fmt = AV_SAMPLE_FMT_S16;
	audio_hardware_sample_rate = actual_spec.freq;
	audio_hardware_channels = actual_spec.channels;
	audio_hardware_layout = av_get_default_channel_layout(actual_spec.channels);

	printf("设备ID:%d\n", audio_dev);
	printf("转码采样深度:%s\n", av_get_sample_fmt_name((enum AVSampleFormat)audio_hardware_fmt));
	printf("转码采样率:%d\n", audio_hardware_sample_rate);
	printf("转码采样通道:%d\n", audio_hardware_channels);
	printf("转码layout:%d\n", audio_hardware_layout);
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

	//创建锁和信号量,用子线程播放完通知主线程退出
	end_mutex = SDL_CreateMutex();
	end_cond = SDL_CreateCond();

	//主线程
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

		//视频包入队列,其他包丢弃
		if (pkt->stream_index == audio_index)
		{
			static int debug_run_once = 1;
			if (debug_run_once)
			{
				printf("压缩包PTS:%d\n", pkt->pts);
				printf("压缩包DTS:%d\n", pkt->dts);
				printf("压缩包size:%d\n", pkt->size);
				printf("压缩包stream_index:%d\n", pkt->stream_index);
				printf("压缩包duration:%d\n", pkt->duration);
				printf("压缩包pos:%d\n", pkt->pos);
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
