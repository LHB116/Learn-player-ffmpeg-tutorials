#include <VideoPlayer.h>


#ifndef AV_WB32
#   define AV_WB32(p, val) do {                 \
        uint32_t d = (val);                     \
        ((uint8_t*)(p))[3] = (d);               \
        ((uint8_t*)(p))[2] = (d)>>8;            \
        ((uint8_t*)(p))[1] = (d)>>16;           \
        ((uint8_t*)(p))[0] = (d)>>24;           \
    } while(0)
#endif

#ifndef AV_RB16
#   define AV_RB16(x)                           \
    ((((const uint8_t*)(x))[0] << 8) |          \
      ((const uint8_t*)(x))[1])
#endif


#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define BREAK_EVENT  (SDL_USEREVENT + 2)

int thread_exit = 0;
static bool s_playing_exit = false;
static bool s_playing_pause = false;


int CameraCapture_OpenCVShow() {
    //printf("%s", avcodec_configuration());
    //ffmpeg_init
    printf("FFmpeg version %s\n\n", FFMPEG_VERSION);

    avdevice_register_all();

    AVCodecID id = AV_CODEC_ID_MJPEG;
    const AVCodec* encoder_id = avcodec_find_encoder(id);
    if (!encoder_id) {
        fprintf(stderr, "codec not found: %d\n", id);
        return -1;
    }

    AVFormatContext* format_context = avformat_alloc_context();
    format_context->video_codec_id = id;

    const AVInputFormat* input_format = av_find_input_format("dshow");
    AVDictionary* dict = nullptr;
    if (av_dict_set(&dict, "video_size", "960x540", 0) < 0) fprintf(stderr, "fail to av_dict_set: line: %d\n", __LINE__);
    int ret = avformat_open_input(&format_context, "video=Integrated Camera", input_format, &dict);
    if (ret != 0) {
        fprintf(stderr, "fail to avformat_open_input: %d\n", ret);
        return -1;
    }

    ret = avformat_find_stream_info(format_context, nullptr);
    if (ret < 0) {
        fprintf(stderr, "fail to get stream information: %d\n", ret);
        return -1;
    }

    int video_stream_index = -1;
    for (unsigned int i = 0; i < format_context->nb_streams; ++i) {
        const AVStream* stream = format_context->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            fprintf(stdout, "type of the encoded data: %d, dimensions of the video frame in pixels: width: %d, height: %d, pixel format: %d\n",
                stream->codecpar->codec_id, stream->codecpar->width, stream->codecpar->height, stream->codecpar->format);
        }
    }

    if (video_stream_index == -1) {
        fprintf(stderr, "no video stream\n");
        return -1;
    }
    fprintf(stdout, "frame rate: %f\n", av_q2d(format_context->streams[video_stream_index]->r_frame_rate));


    //���һ����Ϣ-----------------------------
    //printf("--------------- File Information ----------------\n");
    //av_dump_format(format_context, 0, filepath, 0);
    //printf("-------------------------------------------------\n");

    AVCodecParameters* codecpar = format_context->streams[video_stream_index]->codecpar;
    if (codecpar->codec_id != id) {
        fprintf(stderr, "this test code only support mjpeg encode: %d\n", codecpar->codec_id);
        return -1;
    }

    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "fail to avcodec_find_decoder\n");
        return -1;
    }

    AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
    if (!packet) {
        fprintf(stderr, "fail to alloc\n");
        return -1;
    }

    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        fprintf(stderr, "fail to avcodec_alloc_context3\n");
        return -1;
    }

    codec_context->pix_fmt = AVPixelFormat(codecpar->format);
    codec_context->height = codecpar->height;
    codec_context->width = codecpar->width;
    codec_context->thread_count = 16;
    ret = avcodec_open2(codec_context, codec, nullptr);
    if (ret != 0) {
        fprintf(stderr, "fail to avcodec_open2: %d\n", ret);
        return -1;
    }

    AVPixelFormat dst_pixel_format = AV_PIX_FMT_BGR24;
    AVFrame* frame = av_frame_alloc();
    SwsContext* sws_context = sws_getContext(codec_context->width, codec_context->height, codec_context->pix_fmt, codec_context->width, codec_context->height, dst_pixel_format, 0, nullptr, nullptr, nullptr);
    if (!frame || !sws_context) {
        fprintf(stderr, "fail to alloc\n");
        return -1;
    }

    uint8_t* bgr_data[4];
    int bgr_linesize[4];
    av_image_alloc(bgr_data, bgr_linesize, codec_context->width, codec_context->height, dst_pixel_format, 1);
    cv::Mat mat(codecpar->height, codecpar->width, CV_8UC3);
    const char* winname = "dshow mjpeg video";
    cv::namedWindow(winname);

    while (1) {
        ret = av_read_frame(format_context, packet);
        if (ret >= 0 && packet->stream_index == video_stream_index && packet->size > 0) {
            ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                fprintf(stderr, "##### fail to avcodec_send_packet: %d\n", ret);
                av_packet_unref(packet);
                continue;
            }

            ret = avcodec_receive_frame(codec_context, frame);
            if (ret < 0) {
                fprintf(stderr, "##### fail to avcodec_receive_frame: %d\n", ret);
                av_packet_unref(packet);
                continue;
            }

            sws_scale(sws_context, frame->data, frame->linesize, 0, codec_context->height, bgr_data, bgr_linesize);
            mat.data = bgr_data[0];
            cv::imshow(winname, mat);

            printf("show frame %3d\n", codec_context->frame_number);
            fflush(stdout);
        }
        else if (ret < 0 || packet->size <= 0) {
            fprintf(stderr, "fail to av_read_frame: %d, packet size: %d\n", ret, packet->size);
            continue;
        }

        av_packet_unref(packet);

        int key = cv::waitKey(30);
        if (key == 27) break;  // Esc
    }

    cv::destroyWindow(winname);
    av_freep(packet);
    avformat_close_input(&format_context);
    av_dict_free(&dict);

    fprintf(stdout, "test finish\n");
}


int DemuxDecode_OpenCVShow(int argc, char** argv) {

    const char* url = "./file/Titanic.ts";

    avdevice_register_all();

    //AVInputFormat* input_fmt = av_find_input_format(input_format_name);
    AVFormatContext* format_ctx = avformat_alloc_context();
    int ret = avformat_open_input(&format_ctx, url, nullptr, nullptr);
    //int ret = avformat_open_input(&format_ctx, url, input_fmt, nullptr);
    if (ret != 0) {
        fprintf(stderr, "fail to open url: %s, return value: %d\n", url, ret);
        return -1;
    }

    ret = avformat_find_stream_info(format_ctx, nullptr);
    if (ret < 0) {
        fprintf(stderr, "fail to get stream information: %d\n", ret);
        return -1;
    }

    //std::cout << format_ctx->streams[0]->codecpar->codec_type << std::endl;
    //std::cout << format_ctx->streams[1]->codecpar->codec_type << std::endl;
    //exit(0);

    int video_stream_index = -1;
    // format_ctx->nb_streamsһ����2,��һ��������Ƶ�ģ��ڶ���������Ƶ��
    for (unsigned int i = 0; i < format_ctx->nb_streams; ++i) {
        const AVStream* stream = format_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            fprintf(stdout, "type of the encoded data: %d, dimensions of the video frame in pixels: width: %d, height: %d, pixel format: %d\n",
                stream->codecpar->codec_id, stream->codecpar->width, stream->codecpar->height, stream->codecpar->format);
        }
    }

    if (video_stream_index == -1) {
        fprintf(stderr, "no video stream\n");
        return -1;
    }


    AVCodecParameters* codecpar = format_ctx->streams[video_stream_index]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "fail to avcodec_find_decoder\n");
        return -1;
    }

    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "fail to avcodec_alloc_context3\n");
        return -1;
    }

    codec_ctx->pix_fmt = AVPixelFormat(codecpar->format);
    codec_ctx->height = codecpar->height;
    codec_ctx->width = codecpar->width;
    //codec_ctx->thread_count = 16;
    ret = avcodec_open2(codec_ctx, codec, nullptr);
    if (ret != 0) {
        fprintf(stderr, "fail to avcodec_open2: %d\n", ret);
        return -1;
    }

    AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));
    SwsContext* sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt, codec_ctx->width, codec_ctx->height, AV_PIX_FMT_BGR24, 0, nullptr, nullptr, nullptr);
    if (!packet || !sws_ctx) {
        fprintf(stderr, "fail to alloc\n");
        return -1;
    }

    uint8_t* bgr_data[4];
    int bgr_linesize[4];
    av_image_alloc(bgr_data, bgr_linesize, codec_ctx->width, codec_ctx->height, AV_PIX_FMT_BGR24, 1);
    cv::Mat mat(codec_ctx->height, codec_ctx->width, CV_8UC3);

    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "fail to av_frame_alloc\n");
        return -1;
    }

    int key_frame = 0;

    while (1) {
        ret = av_read_frame(format_ctx, packet);
        if (ret >= 0 && packet->stream_index == video_stream_index && packet->size > 0) {
            ret = avcodec_send_packet(codec_ctx, packet);
            if (ret < 0) {
                fprintf(stderr, "##### fail to avcodec_send_packet: %d\n", ret);
                av_packet_unref(packet);
                continue;
            }

            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret < 0) {
                fprintf(stderr, "##### fail to avcodec_receive_frame: %d\n", ret);
                av_packet_unref(packet);
                continue;
            }

            if (packet->flags && AV_PKT_FLAG_KEY) key_frame++;

            sws_scale(sws_ctx, frame->data, frame->linesize, 0, codec_ctx->height, bgr_data, bgr_linesize);
            mat.data = bgr_data[0];
            cv::imshow(url, mat);

            printf("show frame %3d\n", codec_ctx->frame_number);
            fflush(stdout);
        }
        else if (ret < 0 || packet->size <= 0) {
            fprintf(stderr, "fail to av_read_frame: %d, packet size: %d\n", ret, packet->size);
            continue;
        }

        av_packet_unref(packet);

        int key = cv::waitKey(10);
        if (key == 27) break;  // Esc
    }

    printf("Key frame number: %d\n", key_frame);

    cv::destroyWindow(url);
    sws_freeContext(sws_ctx);
    avformat_close_input(&format_ctx);
    av_freep(packet);
    av_freep(&bgr_data[0]);

    fprintf(stdout, "test finish\n");
}


int refresh_video(void* opaque) {
    thread_exit = 0;
    while (thread_exit == 0) {
        SDL_Event event;
        event.type = REFRESH_EVENT;
        SDL_PushEvent(&event);
        SDL_Delay(40);
    }
    thread_exit = 0;
    //Break
    SDL_Event event;
    event.type = BREAK_EVENT;
    SDL_PushEvent(&event);
    return 0;
}


// ����opaque����Ĳ���֡�ʲ��������̶����ʱ�䷢��ˢ���¼�
int sdl_thread_handle_refreshing(void* opaque)
{
    SDL_Event sdl_event;

    int frame_rate = *((int*)opaque);
    int interval = (frame_rate > 0) ? 1000 / frame_rate : 40;

    printf("frame rate %d FPS, refresh interval %d ms\n", frame_rate, interval);

    while (!s_playing_exit)
    {
        if (!s_playing_pause)
        {
            sdl_event.type = REFRESH_EVENT;
            SDL_PushEvent(&sdl_event);
        }
        SDL_Delay(interval);
    }

    return 0;
}


int SDLShowYUV() {
    const int bpp = 12;

    int screen_w = 640, screen_h = 360;
    const int pixel_w = 640, pixel_h = 360;

    unsigned char buffer[pixel_w * pixel_h * bpp / 8];


    if (SDL_Init(SDL_INIT_VIDEO)) {
        printf("Could not initialize SDL - %s\n", SDL_GetError());
    }
    else {
        printf("Success init SDL");
    }

    SDL_Window* screen;
    //SDL 2.0 Support for multiple windows
    screen = SDL_CreateWindow("Simplest Video Play SDL2", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        screen_w, screen_h, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!screen) {
        printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
        return -1;
    }
    SDL_Renderer* sdlRenderer = SDL_CreateRenderer(screen, -1, 0);

    Uint32 pixformat = 0;
    //IYUV: Y + U + V  (3 planes)
    //YV12: Y + V + U  (3 planes)
    pixformat = SDL_PIXELFORMAT_IYUV;

    SDL_Texture* sdlTexture = SDL_CreateTexture(sdlRenderer, pixformat, SDL_TEXTUREACCESS_STREAMING, pixel_w, pixel_h);

    FILE* fp = NULL;
    fp = fopen("./file/sintel_640_360.yuv", "rb+");

    if (fp == NULL) {
        printf("cannot open this file\n");
        return -1;
    }

    SDL_Rect sdlRect;

    SDL_Thread* refresh_thread = SDL_CreateThread(refresh_video, NULL, NULL);
    SDL_Event event;

    while (1) {
        //Wait
        SDL_WaitEvent(&event);
        if (event.type == REFRESH_EVENT) {
            if (fread(buffer, 1, pixel_w * pixel_h * bpp / 8, fp) != pixel_w * pixel_h * bpp / 8) {
                // Loop
                fseek(fp, 0, SEEK_SET);
                fread(buffer, 1, pixel_w * pixel_h * bpp / 8, fp);
            }

            SDL_UpdateTexture(sdlTexture, NULL, buffer, pixel_w);

            //FIX: If window is resize
            sdlRect.x = 0;
            sdlRect.y = 0;
            sdlRect.w = screen_w;
            sdlRect.h = screen_h;

            SDL_RenderClear(sdlRenderer);
            SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, &sdlRect);
            SDL_RenderPresent(sdlRenderer);
            //SDL_Delay(40);

        }
        else if (event.type == SDL_WINDOWEVENT) {
            //If Resize
            SDL_GetWindowSize(screen, &screen_w, &screen_h);
        }
        else if (event.type == SDL_QUIT) {
            thread_exit = 1;
        }
        else if (event.type == BREAK_EVENT) {
            break;
        }
    }
    SDL_Quit();
}


int DemuxDecode_SDLShow() {
    // const char* url = argv[1]
    // Titanic.ts  cuc_ieschool.flv
    // bigbuckbunny_480x272.h265
    //const char* url = "./file/bigbuckbunny_480x272.h265";
    const char* url = "./file/IronMan.mp4";

    AVFormatContext* p_fmt_ctx = NULL;
    AVCodecContext* p_codec_ctx = NULL;
    AVCodecParameters* p_codec_par = NULL;
    const AVCodec* p_codec = NULL;
    AVFrame* p_frm_raw = NULL;        // ֡���ɰ�����õ�ԭʼ֡
    AVFrame* p_frm_yuv = NULL;        // ֡����ԭʼ֡ɫ��ת���õ�
    AVPacket* p_packet = NULL;        // ���������ж�����һ������
    struct SwsContext* sws_ctx = NULL;  // ͼ��ת��������
    int                 buf_size;
    uint8_t* buffer = NULL;
    int                 i;
    int                 v_idx;
    int                 ret;
    int                 res;
    int                 frame_rate;
    SDL_Window* screen;
    SDL_Renderer* sdl_renderer;
    SDL_Texture* sdl_texture;
    SDL_Rect      sdl_rect;
    SDL_Thread* sdl_thread;
    SDL_Event     sdl_event;

    res = 0;

    // ��ʼ��libavformat(���и�ʽ)��ע�����и�����/�⸴����
    // av_register_all();   // �ѱ�����Ϊ��ʱ�ģ�ֱ�Ӳ���ʹ�ü���

    // A1. ����Ƶ�ļ�����ȡ�ļ�ͷ�����ļ���ʽ��Ϣ�洢��"fmt context"��
    ret = avformat_open_input(&p_fmt_ctx, url, NULL, NULL);
    if (ret != 0)
    {
        printf("avformat_open_input() failed %d\n", ret);
        res = -1;
        goto exit0;
    }

    // A2. ��������Ϣ����ȡһ����Ƶ�ļ����ݣ����Խ��룬��ȡ��������Ϣ����p_fmt_ctx->streams
    // p_fmt_ctx->streams��һ��ָ�����飬�����С��p_fmt_ctx->nb_streams
    ret = avformat_find_stream_info(p_fmt_ctx, NULL);
    if (ret < 0)
    {
        printf("avformat_find_stream_info() failed %d\n", ret);
        res = -1;
        goto exit1;
    }

    // ���ļ������Ϣ��ӡ�ڱ�׼����豸��
    av_dump_format(p_fmt_ctx, 0, url, 0);

    // A3. ���ҵ�һ����Ƶ��
    v_idx = -1;
    for (i = 0; i < p_fmt_ctx->nb_streams; i++)
    {
        if (p_fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            v_idx = i;
            printf("Find a video stream, index %d\n", v_idx);
            frame_rate = p_fmt_ctx->streams[i]->avg_frame_rate.num /
                p_fmt_ctx->streams[i]->avg_frame_rate.den;
            break;
        }
    }
    if (v_idx == -1)
    {
        printf("Cann't find a video stream\n");
        res = -1;
        goto exit1;
    }

    // A5. Ϊ��Ƶ������������AVCodecContext

    // A5.1 ��ȡ����������AVCodecParameters
    p_codec_par = p_fmt_ctx->streams[v_idx]->codecpar;

    // A5.2 ��ȡ������
    p_codec = avcodec_find_decoder(p_codec_par->codec_id);
    if (p_codec == NULL)
    {
        printf("Cann't find codec!\n");
        res = -1;
        goto exit1;
    }

    // A5.3 ����������AVCodecContext
    // A5.3.1 p_codec_ctx��ʼ��������ṹ�壬ʹ��p_codec��ʼ����Ӧ��ԱΪĬ��ֵ
    p_codec_ctx = avcodec_alloc_context3(p_codec);
    if (p_codec_ctx == NULL)
    {
        printf("avcodec_alloc_context3() failed %d\n", ret);
        res = -1;
        goto exit1;
    }
    // A5.3.2 p_codec_ctx��ʼ����p_codec_par ==> p_codec_ctx����ʼ����Ӧ��Ա
    ret = avcodec_parameters_to_context(p_codec_ctx, p_codec_par);
    if (ret < 0)
    {
        printf("avcodec_parameters_to_context() failed %d\n", ret);
        res = -1;
        goto exit2;
    }
    // A5.3.3 p_codec_ctx��ʼ����ʹ��p_codec��ʼ��p_codec_ctx����ʼ�����
    ret = avcodec_open2(p_codec_ctx, p_codec, NULL);
    if (ret < 0)
    {
        printf("avcodec_open2() failed %d\n", ret);
        res = -1;
        goto exit2;
    }

    // A6. ����AVFrame
    // A6.1 ����AVFrame�ṹ��ע�Ⲣ������data buffer(��AVFrame.*data[])
    p_frm_raw = av_frame_alloc();
    if (p_frm_raw == NULL)
    {
        printf("av_frame_alloc() for p_frm_raw failed\n");
        res = -1;
        goto exit2;
    }
    p_frm_yuv = av_frame_alloc();
    if (p_frm_yuv == NULL)
    {
        printf("av_frame_alloc() for p_frm_yuv failed\n");
        res = -1;
        goto exit3;
    }

    // A6.2 ΪAVFrame.*data[]�ֹ����仺���������ڴ洢sws_scale()��Ŀ��֡��Ƶ����
    //     p_frm_raw��data_buffer��av_read_frame()���䣬��˲����ֹ�����
    //     p_frm_yuv��data_buffer�޴����䣬����ڴ˴��ֹ�����
    buf_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
        p_codec_ctx->width,
        p_codec_ctx->height,
        1
    );
    // buffer����Ϊp_frm_yuv����Ƶ���ݻ�����
    buffer = (uint8_t*)av_malloc(buf_size);
    if (buffer == NULL)
    {
        printf("av_malloc() for buffer failed\n");
        res = -1;
        goto exit4;
    }
    // ʹ�ø��������趨p_frm_yuv->data��p_frm_yuv->linesize
    ret = av_image_fill_arrays(p_frm_yuv->data,     // dst data[]
        p_frm_yuv->linesize, // dst linesize[]
        buffer,              // src buffer
        AV_PIX_FMT_YUV420P,  // pixel format
        p_codec_ctx->width,  // width
        p_codec_ctx->height, // height
        1                    // align
    );

    if (ret < 0)
    {
        printf("av_image_fill_arrays() failed %d\n", ret);
        res = -1;
        goto exit5;
    }

    // A7. ��ʼ��SWS context�����ں���ͼ��ת��
    //     �˴���6������ʹ�õ���FFmpeg�е����ظ�ʽ���ԱȲο�ע��B4
    //     FFmpeg�е����ظ�ʽAV_PIX_FMT_YUV420P��ӦSDL�е����ظ�ʽSDL_PIXELFORMAT_IYUV
    //     ��������õ�ͼ��Ĳ���SDL֧�֣�������ͼ��ת���Ļ���SDL���޷�������ʾͼ���
    //     ��������õ�ͼ����ܱ�SDL֧�֣��򲻱ؽ���ͼ��ת��
    //     ����Ϊ�˱����㣬ͳһת��ΪSDL֧�ֵĸ�ʽAV_PIX_FMT_YUV420P==>SDL_PIXELFORMAT_IYUV
    sws_ctx = sws_getContext(p_codec_ctx->width,    // src width
        p_codec_ctx->height,   // src height
        p_codec_ctx->pix_fmt,  // src format
        p_codec_ctx->width,    // dst width
        p_codec_ctx->height,   // dst height
        AV_PIX_FMT_YUV420P,    // dst format
        SWS_BICUBIC,           // flags
        NULL,                  // src filter
        NULL,                  // dst filter
        NULL                   // param
    );
    if (sws_ctx == NULL)
    {
        printf("sws_getContext() failed\n");
        res = -1;
        goto exit6;
    }

    // B1. ��ʼ��SDL��ϵͳ��ȱʡ(�¼������ļ�IO���߳�)����Ƶ����Ƶ����ʱ��
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER))
    {
        printf("SDL_Init() failed: %s\n", SDL_GetError());
        res = -1;
        goto exit6;
    }

    // B2. ����SDL���ڣ�SDL 2.0֧�ֶര��
    //     SDL_Window�����г���󵯳�����Ƶ���ڣ�ͬSDL 1.x�е�SDL_Surface
    //screen = SDL_CreateWindow("simple ffplayer",
    //    SDL_WINDOWPOS_UNDEFINED,// �����Ĵ���X����
    //    SDL_WINDOWPOS_UNDEFINED,// �����Ĵ���Y����
    //    p_codec_ctx->width,
    //    p_codec_ctx->height,
    //    SDL_WINDOW_OPENGL
    //);
    screen = SDL_CreateWindow("simple ffplayer",
        SDL_WINDOWPOS_UNDEFINED,// �����Ĵ���X����
        SDL_WINDOWPOS_UNDEFINED,// �����Ĵ���Y����
        1280,
        720,
        SDL_WINDOW_OPENGL
    );

    if (screen == NULL)
    {
        printf("SDL_CreateWindow() failed: %s\n", SDL_GetError());
        res = -1;
        goto exit7;
    }

    // B3. ����SDL_Renderer
    //     SDL_Renderer����Ⱦ��
    sdl_renderer = SDL_CreateRenderer(screen, -1, 0);
    if (sdl_renderer == NULL)
    {
        printf("SDL_CreateRenderer() failed: %s\n", SDL_GetError());
        res = -1;
        goto exit7;
    }

    // B4. ����SDL_Texture
    //     һ��SDL_Texture��Ӧһ֡YUV���ݣ�ͬSDL 1.x�е�SDL_Overlay
    //     �˴���2������ʹ�õ���SDL�е����ظ�ʽ���ԱȲο�ע��A7
    //     FFmpeg�е����ظ�ʽAV_PIX_FMT_YUV420P��ӦSDL�е����ظ�ʽSDL_PIXELFORMAT_IYUV
    sdl_texture = SDL_CreateTexture(sdl_renderer,
        SDL_PIXELFORMAT_IYUV,
        SDL_TEXTUREACCESS_STREAMING,
        p_codec_ctx->width,
        p_codec_ctx->height
    );
    if (sdl_texture == NULL)
    {
        printf("SDL_CreateTexture() failed: %s\n", SDL_GetError());
        res = -1;
        goto exit7;
    }

    sdl_rect.x = 0;
    sdl_rect.y = 0;
    sdl_rect.w = p_codec_ctx->width;
    sdl_rect.h = p_codec_ctx->height;

    p_packet = (AVPacket*)av_malloc(sizeof(AVPacket));
    if (p_packet == NULL)
    {
        printf("SDL_CreateThread() failed: %s\n", SDL_GetError());
        res = -1;
        goto exit7;
    }

    // B5. ������ʱˢ���¼��̣߳�����Ԥ��֡�ʲ���ˢ���¼�
    sdl_thread = SDL_CreateThread(sdl_thread_handle_refreshing, NULL, (void*)&frame_rate);
    if (sdl_thread == NULL)
    {
        printf("SDL_CreateThread() failed: %s\n", SDL_GetError());
        res = -1;
        goto exit8;
    }

    while (1)
    {
        // B6. �ȴ�ˢ���¼�
        SDL_WaitEvent(&sdl_event);

        if (sdl_event.type == REFRESH_EVENT)
        {
            // A8. ����Ƶ�ļ��ж�ȡһ��packet
            //     packet��������Ƶ֡����Ƶ֡���������ݣ�������ֻ�������Ƶ֡����Ƶ֡��������Ƶ���ݲ����ᱻ
            //     �ӵ����Ӷ�����������ṩ�����ܶ����Ϣ
            //     ������Ƶ��˵��һ��packetֻ����һ��frame
            //     ������Ƶ��˵������֡���̶��ĸ�ʽ��һ��packet�ɰ���������frame��
            //                   ����֡���ɱ�ĸ�ʽ��һ��packetֻ����һ��frame
            while (av_read_frame(p_fmt_ctx, p_packet) == 0)
            {
                if (p_packet->stream_index == v_idx)  // ȡ��һ֡��Ƶ֡�����˳�
                {
                    break;
                }
            }

            // A9. ��Ƶ���룺packet ==> frame
            // A9.1 �������ι���ݣ�һ��packet������һ����Ƶ֡������Ƶ֡���˴���Ƶ֡�ѱ���һ���˵�
            ret = avcodec_send_packet(p_codec_ctx, p_packet);
            if (ret != 0)
            {
                printf("avcodec_send_packet() failed %d\n", ret);
                res = -1;
                goto exit8;
            }
            // A9.2 ���ս�������������ݣ��˴�ֻ������Ƶ֡��ÿ�ν���һ��packet����֮����õ�һ��frame
            ret = avcodec_receive_frame(p_codec_ctx, p_frm_raw);
            if (ret != 0)
            {
                if (ret == AVERROR_EOF)
                {
                    printf("avcodec_receive_frame(): the decoder has been fully flushed\n");
                }
                else if (ret == AVERROR(EAGAIN))
                {
                    printf("avcodec_receive_frame(): output is not available in this state - "
                        "user must try to send new input\n");
                    continue;
                }
                else if (ret == AVERROR(EINVAL))
                {
                    printf("avcodec_receive_frame(): codec not opened, or it is an encoder\n");
                }
                else
                {
                    printf("avcodec_receive_frame(): legitimate decoding errors\n");
                }
                res = -1;
                goto exit8;
            }

            // A10. ͼ��ת����p_frm_raw->data ==> p_frm_yuv->data
            // ��Դͼ����һƬ���������򾭹��������µ�Ŀ��ͼ���Ӧ���򣬴����ͼ�����������������
            // plane: ��YUV��Y��U��V����plane��RGB��R��G��B����plane
            // slice: ͼ����һƬ�������У������������ģ�˳���ɶ������ײ����ɵײ�������
            // stride/pitch: һ��ͼ����ռ���ֽ�����Stride=BytesPerPixel*Width+Padding��ע�����
            // AVFrame.*data[]: ÿ������Ԫ��ָ���Ӧplane
            // AVFrame.linesize[]: ÿ������Ԫ�ر�ʾ��Ӧplane��һ��ͼ����ռ���ֽ���

            printf("## Frame Number %d\n", p_codec_ctx->frame_number);

            sws_scale(sws_ctx,                                  // sws context
                (const uint8_t* const*)p_frm_raw->data,  // src slice
                p_frm_raw->linesize,                      // src stride
                0,                                        // src slice y
                p_codec_ctx->height,                      // src slice height
                p_frm_yuv->data,                          // dst planes
                p_frm_yuv->linesize                       // dst strides
            );

            printf("## raw Linesize %d, %d, %d\n", p_frm_raw->linesize[0], p_frm_raw->linesize[1], p_frm_raw->linesize[2]);
            printf("## converted Linesize %d, %d, %d\n", p_frm_yuv->linesize[0], p_frm_yuv->linesize[1], p_frm_yuv->linesize[2]);

            //if (p_codec_ctx->frame_number == 60) {
            //    for (int i = 1000; i < 1400; ++i) {
            //        printf("%d ", p_frm_yuv->data[2][i]);
            //    }
            //    printf("\n");
            //    exit(0);
            //}

            // B7. ʹ���µ�YUV�������ݸ���SDL_Rect
            SDL_UpdateYUVTexture(sdl_texture,                   // sdl texture
                &sdl_rect,                     // sdl rect
                p_frm_yuv->data[0],            // y plane
                p_frm_yuv->linesize[0],        // y pitch
                p_frm_yuv->data[1],            // u plane
                p_frm_yuv->linesize[1],        // u pitch
                p_frm_yuv->data[2],            // v plane
                p_frm_yuv->linesize[2]         // v pitch
            );

            // B8. ʹ���ض���ɫ��յ�ǰ��ȾĿ��
            SDL_RenderClear(sdl_renderer);
            // B9. ʹ�ò���ͼ������(texture)���µ�ǰ��ȾĿ��
            SDL_RenderCopy(sdl_renderer,                        // sdl renderer
                sdl_texture,                         // sdl texture
                NULL,                                // src rect, if NULL copy texture
                &sdl_rect                            // dst rect
            );

            // B10. ִ����Ⱦ��������Ļ��ʾ
            SDL_RenderPresent(sdl_renderer);

            av_packet_unref(p_packet);
        }
        else if (sdl_event.type == SDL_KEYDOWN)
        {
            if (sdl_event.key.keysym.sym == SDLK_SPACE)
            {
                // �û����ո������ͣ/����״̬�л�
                s_playing_pause = !s_playing_pause;
                printf("player %s\n", s_playing_pause ? "pause" : "continue");
            }
        }
        else if (sdl_event.type == SDL_QUIT)
        {
            // �û����¹رմ��ڰ�ť x
            printf("SDL event QUIT\n");
            s_playing_exit = true;
            break;
        }
        else
        {
            // printf("Ignore SDL event 0x%04X\n", sdl_event.type);
        }
    }

exit8:
    SDL_Quit();
exit7:
    av_packet_unref(p_packet);
exit6:
    sws_freeContext(sws_ctx);
exit5:
    av_free(buffer);
exit4:
    av_frame_free(&p_frm_yuv);
exit3:
    av_frame_free(&p_frm_raw);
exit2:
    avcodec_free_context(&p_codec_ctx);
exit1:
    avformat_close_input(&p_fmt_ctx);
exit0:
    return res;

}


// MP4�б���h264  https://blog.csdn.net/weixin_63572056/article/details/125870973
// MP4�б���aac   https://blog.csdn.net/weixin_63572056/article/details/125870054
static int alloc_and_copy(AVPacket* out,
    const uint8_t* sps_pps, uint32_t sps_pps_size,
    const uint8_t* in, uint32_t in_size)///������ֵ
{
    uint32_t offset = out->size;
    uint8_t nal_header_size = offset ? 3 : 4;//sps/pps������00 00 00 01���ֽ�  ������������00 00 01
    int err;

    err = av_grow_packet(out, sps_pps_size + in_size + nal_header_size);//����
    if (err < 0)
        return err;

    if (sps_pps)
        memcpy(out->data + offset, sps_pps, sps_pps_size);
    memcpy(out->data + sps_pps_size + nal_header_size + offset, in, in_size);
    if (!offset) {
        AV_WB32(out->data + sps_pps_size, 1);
    }
    else {
        (out->data + offset + sps_pps_size)[0] =
            (out->data + offset + sps_pps_size)[1] = 0;
        (out->data + offset + sps_pps_size)[2] = 1;
    }

    return 0;
}


int h264_add(const uint8_t* codec_extradata, const int codec_extradata_size, AVPacket* out_extradata, int padding)   //���sps/ppsͷ
{

    uint16_t unit_size;
    uint64_t total_size = 0;
    uint8_t* out = NULL, unit_nb, sps_done = 0,
        sps_seen = 0, pps_seen = 0, sps_offset = 0, pps_offset = 0;
    const uint8_t* extradata = codec_extradata + 4;
    static const uint8_t nalu_header[4] = { 0, 0, 0, 1 };
    int length_size = (*extradata++ & 0x3) + 1; // retrieve length coded size, ����ָʾ��ʾ�������ݳ��������ֽ���

    sps_offset = pps_offset = -1;

    /* retrieve sps and pps unit(s) */
    unit_nb = *extradata++ & 0x1f; /* number of sps unit(s) */
    if (!unit_nb) {
        goto pps;
    }
    else {
        sps_offset = 0;
        sps_seen = 1;
    }

    while (unit_nb--) {
        int err;

        unit_size = AV_RB16(extradata);
        total_size += unit_size + 4;
        if (total_size > INT_MAX - padding) {
            av_log(NULL, AV_LOG_ERROR,
                "Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR(EINVAL);
        }
        if (extradata + 2 + unit_size > codec_extradata + codec_extradata_size) {
            av_log(NULL, AV_LOG_ERROR, "Packet header is not contained in global extradata, "
                "corrupted stream or invalid MP4/AVCC bitstream\n");
            av_free(out);
            return AVERROR(EINVAL);
        }
        if ((err = av_reallocp(&out, total_size + padding)) < 0)
            return err;
        memcpy(out + total_size - unit_size - 4, nalu_header, 4);
        memcpy(out + total_size - unit_size, extradata + 2, unit_size);
        extradata += 2 + unit_size;
    pps:
        if (!unit_nb && !sps_done++) {
            unit_nb = *extradata++; /* number of pps unit(s) */
            if (unit_nb) {
                pps_offset = total_size;
                pps_seen = 1;
            }
        }
    }

    if (out)
        memset(out + total_size, 0, padding);

    if (!sps_seen)
        av_log(NULL, AV_LOG_WARNING,
            "Warning: SPS NALU missing or invalid. "
            "The resulting stream may not play.\n");

    if (!pps_seen)
        av_log(NULL, AV_LOG_WARNING,
            "Warning: PPS NALU missing or invalid. "
            "The resulting stream may not play.\n");

    out_extradata->data = out;
    out_extradata->size = total_size;

    return length_size;
}


int h264(AVFormatContext* fmt_ctx, AVPacket* in, FILE* dst_fd)
{

    AVPacket* out = NULL;
    AVPacket spspps_pkt;

    int len;
    uint8_t unit_type;
    int32_t nal_size;
    uint32_t cumul_size = 0;
    const uint8_t* buf;
    const uint8_t* buf_end;
    int            buf_size;
    int ret = 0, i;

    out = av_packet_alloc();

    buf = in->data;
    buf_size = in->size;
    buf_end = in->data + in->size;

    do
    {
        ret = AVERROR(EINVAL);
        if (buf + 4 /*s->length_size*/ > buf_end)
            goto fail;

        for (nal_size = 0, i = 0; i < 4/*s->length_size*/; i++)
            nal_size = (nal_size << 8) | buf[i];   //packetǰ�ĸ��ֽ���h264һ֡��С 

        buf += 4; /*s->length_size;*/
        unit_type = *buf & 0x1f;  //֡���һ�ֽڵĺ���λΪ��֡�����ͣ�ptsΪ8���ؼ�֡Ϊ5���ǹؼ�֡Ϊ1

        if (nal_size > buf_end - buf || nal_size < 0)
            goto fail;


        if (unit_type == 5) {
            //�ؼ�֡��sps / pps

                h264_add(fmt_ctx->streams[in->stream_index]->codecpar->extradata,
                    fmt_ctx->streams[in->stream_index]->codecpar->extradata_size,
                    &spspps_pkt,
                    AV_INPUT_BUFFER_PADDING_SIZE);

            if ((ret = alloc_and_copy(out,
                spspps_pkt.data, spspps_pkt.size,
                buf, nal_size)) < 0)
                goto fail;
        }
        else {
            if ((ret = alloc_and_copy(out, NULL, 0, buf, nal_size)) < 0)
                goto fail;

        }


        len = fwrite(out->data, 1, out->size, dst_fd);
        if (len != out->size) {
            av_log(NULL, AV_LOG_DEBUG, "warning, length of writed data isn't equal pkt.size(%d, %d)\n",
                len,
                out->size);
        }
        fflush(dst_fd);

    next_nal:
        buf += nal_size;
        cumul_size += nal_size + 4;//s->length_size;
    } while (cumul_size < buf_size);


fail:
    av_packet_free(&out);
    //cout << ret << endl;
    return ret;
}

int save_h274_yuv() {
    const char* file = "./file/ba.mp4";
    AVPacket pkt1, * pkt = &pkt1;
    int ret = 0;

    struct SwsContext* video_sws_ctx = NULL;
    struct SwrContext* audio_swr_ctx = NULL;
    AVInputFormat* audio_iformat = NULL;
    const char* audio_filename = NULL;
    AVFormatContext* audio_ic = NULL;
    AVStream* audio_stream = NULL, * video_stream = NULL;
    AVCodecContext* audio_avctx = NULL, * video_avctx = NULL;
    static const AVCodec* audio_codec = NULL, * video_codec = NULL;
    int audio_index = -1, video_index = -1;

    int audio_hardware_fmt = -1;
    int audio_hardware_sample_rate = 0;
    int audio_hardware_channels = 0;
    int64_t audio_hardware_layout = 0;
    int audio_volume = 100;//��Χ0-128 SDL_MIX_MAXVOLUME

    // ����Ƶͬ��ʱӦ���������¼��Ƶ��pts����Ƶ��Ⱦʱ���������Ƶ��pts����ͬ������
    static float audio_clock;
    static float video_clock;

    // ������ͣ  ����
    static bool g_exit = false;
    static bool g_pause = false;

    int buf_size;
    uint8_t* buffer = NULL;
    AVFrame* p_frm_yuv = NULL;

    static uint8_t* audio_buf_data = NULL;//�����ĵ�����Ƶԭʼ����
    static int audio_buf_size = 0;//�����ĵ�����Ƶԭʼ���ݴ�С
    static int audio_buf_index = 0;//һ���α�ָ��,������¼�Ƿ���Ҫ��ȡ��������

    SDL_AudioSpec wanted_spec, actual_spec;

    audio_filename = av_strdup(file);
    audio_iformat = NULL;

    AVFrame* audio_frame = av_frame_alloc();
    AVFrame* video_frame = av_frame_alloc();

    FILE* fp_264 = fopen("ba.h264", "wb");
    FILE* fp_yuv = fopen("ba.yuv", "wb");
    FILE* pFile = fopen("ba.pcm", "wb");
    FILE* ptxt = fopen("ba.txt", "wb+");
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
    SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &actual_spec, 0);
    wanted_spec.channels = audio_avctx->channels;//ͨ����
    wanted_spec.freq = audio_avctx->sample_rate;//������		
    wanted_spec.format = AUDIO_S16SYS; //SDL֧�ֵ��з���,16bit
    wanted_spec.silence = 0;//����ֵ
    wanted_spec.samples = audio_avctx->frame_size;//�ص������С,��λΪһ��ͨ����������
    //wanted_spec.callback = audio_refresh;//�������
    //wanted_spec.userdata = NULL;//�ص���������ָ��

    //if (actual_spec.channels != wanted_spec.channels ||
    //    actual_spec.freq != wanted_spec.freq ||
    //    actual_spec.format != wanted_spec.format)
    //{
    //    printf("SDL��֧�ֵ���Ƶ����!\n");
    //    goto fail;
    //}

    audio_hardware_fmt = AV_SAMPLE_FMT_S16;
    audio_hardware_sample_rate = actual_spec.freq;
    audio_hardware_channels = actual_spec.channels;
    audio_hardware_layout = av_get_default_channel_layout(actual_spec.channels);

    printf("ת��������:%s\n", av_get_sample_fmt_name((enum AVSampleFormat)audio_hardware_fmt));
    printf("ת�������:%d\n", audio_hardware_sample_rate);
    printf("ת�����ͨ��:%d\n", audio_hardware_channels);
    printf("ת��layout:%d\n", audio_hardware_layout);
    printf("-------------------------------------\n");

    while (1)
    {
        ret = av_read_frame(audio_ic, pkt);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF || avio_feof(audio_ic->pb))
            {
                break;
            }
            else
            {
                break;
            }
        }

        //��Ƶ�������,����������
        if (pkt->stream_index == audio_index)
        {
            printf("## Audio Frame Number %d\n", audio_avctx->frame_number);
            fprintf(ptxt, "## Audio Frame Number %d\n", audio_avctx->frame_number);
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
            if (avcodec_send_packet(audio_avctx, pkt) < 0)  // ���װ
            {
                printf("audio Decode error.\n");
                av_packet_unref(pkt);
                goto fail;
            }
            while (avcodec_receive_frame(audio_avctx, audio_frame) == 0) {
            
            }
            av_packet_unref(pkt);
        }
        else if (pkt->stream_index == video_index) {
            //fwrite(pkt->data, 1, pkt->size, fp_264);
            h264(audio_ic, pkt, fp_264);
            //�������ι���ݣ�һ��packet������һ����Ƶ֡������Ƶ֡��
            if (avcodec_send_packet(video_avctx, pkt) < 0)  // ���װ
            {
                printf("video Decode error.\n");
                av_packet_unref(pkt);
                goto fail;
            }
            // A9.2 ���ս�������������ݣ��˴�ֻ������Ƶ֡��ÿ�ν���һ��packet����֮����õ�һ��frame
            while (avcodec_receive_frame(video_avctx, video_frame) == 0) {
                printf("## Video Frame Number %d\n", video_avctx->frame_number);
                fprintf(ptxt, "## Video Frame Number %d\n", video_avctx->frame_number);

                sws_scale(video_sws_ctx,                                  // sws context
                    (const uint8_t* const*)video_frame->data,  // src slice
                    video_frame->linesize,                      // src stride
                    0,                                        // src slice y
                    video_avctx->height,                      // src slice height
                    p_frm_yuv->data,                          // dst planes
                    p_frm_yuv->linesize                       // dst strides
                );

                int y_size = video_avctx->width * video_avctx->height;
                fwrite(p_frm_yuv->data[0], 1, y_size, fp_yuv);    //Y 
                fwrite(p_frm_yuv->data[1], 1, y_size / 4, fp_yuv);  //U
                fwrite(p_frm_yuv->data[2], 1, y_size / 4, fp_yuv);  //V

            }
            av_packet_unref(pkt);
        }
        else
        {
            av_packet_unref(pkt);
        }

    }

fail:
    avformat_free_context(audio_ic);
    avcodec_free_context(&audio_avctx);
    avcodec_free_context(&video_avctx);
    fclose(fp_264);
    fclose(fp_yuv);
    fclose(pFile);
    fclose(ptxt);

    return 0;
}