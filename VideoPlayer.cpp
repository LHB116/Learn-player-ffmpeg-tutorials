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


    //输出一下信息-----------------------------
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
    // format_ctx->nb_streams一般是2,第一段流是视频的，第二段流是音频的
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


// 按照opaque传入的播放帧率参数，按固定间隔时间发送刷新事件
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
    AVFrame* p_frm_raw = NULL;        // 帧，由包解码得到原始帧
    AVFrame* p_frm_yuv = NULL;        // 帧，由原始帧色彩转换得到
    AVPacket* p_packet = NULL;        // 包，从流中读出的一段数据
    struct SwsContext* sws_ctx = NULL;  // 图像转换上下文
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

    // 初始化libavformat(所有格式)，注册所有复用器/解复用器
    // av_register_all();   // 已被申明为过时的，直接不再使用即可

    // A1. 打开视频文件：读取文件头，将文件格式信息存储在"fmt context"中
    ret = avformat_open_input(&p_fmt_ctx, url, NULL, NULL);
    if (ret != 0)
    {
        printf("avformat_open_input() failed %d\n", ret);
        res = -1;
        goto exit0;
    }

    // A2. 搜索流信息：读取一段视频文件数据，尝试解码，将取到的流信息填入p_fmt_ctx->streams
    // p_fmt_ctx->streams是一个指针数组，数组大小是p_fmt_ctx->nb_streams
    ret = avformat_find_stream_info(p_fmt_ctx, NULL);
    if (ret < 0)
    {
        printf("avformat_find_stream_info() failed %d\n", ret);
        res = -1;
        goto exit1;
    }

    // 将文件相关信息打印在标准输出设备上
    av_dump_format(p_fmt_ctx, 0, url, 0);

    // A3. 查找第一个视频流
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

    // A5. 为视频流构建解码器AVCodecContext

    // A5.1 获取解码器参数AVCodecParameters
    p_codec_par = p_fmt_ctx->streams[v_idx]->codecpar;

    // A5.2 获取解码器
    p_codec = avcodec_find_decoder(p_codec_par->codec_id);
    if (p_codec == NULL)
    {
        printf("Cann't find codec!\n");
        res = -1;
        goto exit1;
    }

    // A5.3 构建解码器AVCodecContext
    // A5.3.1 p_codec_ctx初始化：分配结构体，使用p_codec初始化相应成员为默认值
    p_codec_ctx = avcodec_alloc_context3(p_codec);
    if (p_codec_ctx == NULL)
    {
        printf("avcodec_alloc_context3() failed %d\n", ret);
        res = -1;
        goto exit1;
    }
    // A5.3.2 p_codec_ctx初始化：p_codec_par ==> p_codec_ctx，初始化相应成员
    ret = avcodec_parameters_to_context(p_codec_ctx, p_codec_par);
    if (ret < 0)
    {
        printf("avcodec_parameters_to_context() failed %d\n", ret);
        res = -1;
        goto exit2;
    }
    // A5.3.3 p_codec_ctx初始化：使用p_codec初始化p_codec_ctx，初始化完成
    ret = avcodec_open2(p_codec_ctx, p_codec, NULL);
    if (ret < 0)
    {
        printf("avcodec_open2() failed %d\n", ret);
        res = -1;
        goto exit2;
    }

    // A6. 分配AVFrame
    // A6.1 分配AVFrame结构，注意并不分配data buffer(即AVFrame.*data[])
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

    // A6.2 为AVFrame.*data[]手工分配缓冲区，用于存储sws_scale()中目的帧视频数据
    //     p_frm_raw的data_buffer由av_read_frame()分配，因此不需手工分配
    //     p_frm_yuv的data_buffer无处分配，因此在此处手工分配
    buf_size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P,
        p_codec_ctx->width,
        p_codec_ctx->height,
        1
    );
    // buffer将作为p_frm_yuv的视频数据缓冲区
    buffer = (uint8_t*)av_malloc(buf_size);
    if (buffer == NULL)
    {
        printf("av_malloc() for buffer failed\n");
        res = -1;
        goto exit4;
    }
    // 使用给定参数设定p_frm_yuv->data和p_frm_yuv->linesize
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

    // A7. 初始化SWS context，用于后续图像转换
    //     此处第6个参数使用的是FFmpeg中的像素格式，对比参考注释B4
    //     FFmpeg中的像素格式AV_PIX_FMT_YUV420P对应SDL中的像素格式SDL_PIXELFORMAT_IYUV
    //     如果解码后得到图像的不被SDL支持，不进行图像转换的话，SDL是无法正常显示图像的
    //     如果解码后得到图像的能被SDL支持，则不必进行图像转换
    //     这里为了编码简便，统一转换为SDL支持的格式AV_PIX_FMT_YUV420P==>SDL_PIXELFORMAT_IYUV
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

    // B1. 初始化SDL子系统：缺省(事件处理、文件IO、线程)、视频、音频、定时器
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER))
    {
        printf("SDL_Init() failed: %s\n", SDL_GetError());
        res = -1;
        goto exit6;
    }

    // B2. 创建SDL窗口，SDL 2.0支持多窗口
    //     SDL_Window即运行程序后弹出的视频窗口，同SDL 1.x中的SDL_Surface
    //screen = SDL_CreateWindow("simple ffplayer",
    //    SDL_WINDOWPOS_UNDEFINED,// 不关心窗口X坐标
    //    SDL_WINDOWPOS_UNDEFINED,// 不关心窗口Y坐标
    //    p_codec_ctx->width,
    //    p_codec_ctx->height,
    //    SDL_WINDOW_OPENGL
    //);
    screen = SDL_CreateWindow("simple ffplayer",
        SDL_WINDOWPOS_UNDEFINED,// 不关心窗口X坐标
        SDL_WINDOWPOS_UNDEFINED,// 不关心窗口Y坐标
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

    // B3. 创建SDL_Renderer
    //     SDL_Renderer：渲染器
    sdl_renderer = SDL_CreateRenderer(screen, -1, 0);
    if (sdl_renderer == NULL)
    {
        printf("SDL_CreateRenderer() failed: %s\n", SDL_GetError());
        res = -1;
        goto exit7;
    }

    // B4. 创建SDL_Texture
    //     一个SDL_Texture对应一帧YUV数据，同SDL 1.x中的SDL_Overlay
    //     此处第2个参数使用的是SDL中的像素格式，对比参考注释A7
    //     FFmpeg中的像素格式AV_PIX_FMT_YUV420P对应SDL中的像素格式SDL_PIXELFORMAT_IYUV
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

    // B5. 创建定时刷新事件线程，按照预设帧率产生刷新事件
    sdl_thread = SDL_CreateThread(sdl_thread_handle_refreshing, NULL, (void*)&frame_rate);
    if (sdl_thread == NULL)
    {
        printf("SDL_CreateThread() failed: %s\n", SDL_GetError());
        res = -1;
        goto exit8;
    }

    while (1)
    {
        // B6. 等待刷新事件
        SDL_WaitEvent(&sdl_event);

        if (sdl_event.type == REFRESH_EVENT)
        {
            // A8. 从视频文件中读取一个packet
            //     packet可能是视频帧、音频帧或其他数据，解码器只会解码视频帧或音频帧，非音视频数据并不会被
            //     扔掉、从而能向解码器提供尽可能多的信息
            //     对于视频来说，一个packet只包含一个frame
            //     对于音频来说，若是帧长固定的格式则一个packet可包含整数个frame，
            //                   若是帧长可变的格式则一个packet只包含一个frame
            while (av_read_frame(p_fmt_ctx, p_packet) == 0)
            {
                if (p_packet->stream_index == v_idx)  // 取到一帧视频帧，则退出
                {
                    break;
                }
            }

            // A9. 视频解码：packet ==> frame
            // A9.1 向解码器喂数据，一个packet可能是一个视频帧或多个音频帧，此处音频帧已被上一句滤掉
            ret = avcodec_send_packet(p_codec_ctx, p_packet);
            if (ret != 0)
            {
                printf("avcodec_send_packet() failed %d\n", ret);
                res = -1;
                goto exit8;
            }
            // A9.2 接收解码器输出的数据，此处只处理视频帧，每次接收一个packet，将之解码得到一个frame
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

            // A10. 图像转换：p_frm_raw->data ==> p_frm_yuv->data
            // 将源图像中一片连续的区域经过处理后更新到目标图像对应区域，处理的图像区域必须逐行连续
            // plane: 如YUV有Y、U、V三个plane，RGB有R、G、B三个plane
            // slice: 图像中一片连续的行，必须是连续的，顺序由顶部到底部或由底部到顶部
            // stride/pitch: 一行图像所占的字节数，Stride=BytesPerPixel*Width+Padding，注意对齐
            // AVFrame.*data[]: 每个数组元素指向对应plane
            // AVFrame.linesize[]: 每个数组元素表示对应plane中一行图像所占的字节数

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

            // B7. 使用新的YUV像素数据更新SDL_Rect
            SDL_UpdateYUVTexture(sdl_texture,                   // sdl texture
                &sdl_rect,                     // sdl rect
                p_frm_yuv->data[0],            // y plane
                p_frm_yuv->linesize[0],        // y pitch
                p_frm_yuv->data[1],            // u plane
                p_frm_yuv->linesize[1],        // u pitch
                p_frm_yuv->data[2],            // v plane
                p_frm_yuv->linesize[2]         // v pitch
            );

            // B8. 使用特定颜色清空当前渲染目标
            SDL_RenderClear(sdl_renderer);
            // B9. 使用部分图像数据(texture)更新当前渲染目标
            SDL_RenderCopy(sdl_renderer,                        // sdl renderer
                sdl_texture,                         // sdl texture
                NULL,                                // src rect, if NULL copy texture
                &sdl_rect                            // dst rect
            );

            // B10. 执行渲染，更新屏幕显示
            SDL_RenderPresent(sdl_renderer);

            av_packet_unref(p_packet);
        }
        else if (sdl_event.type == SDL_KEYDOWN)
        {
            if (sdl_event.key.keysym.sym == SDLK_SPACE)
            {
                // 用户按空格键，暂停/继续状态切换
                s_playing_pause = !s_playing_pause;
                printf("player %s\n", s_playing_pause ? "pause" : "continue");
            }
        }
        else if (sdl_event.type == SDL_QUIT)
        {
            // 用户按下关闭窗口按钮 x
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


// MP4中保存h264  https://blog.csdn.net/weixin_63572056/article/details/125870973
// MP4中保存aac   https://blog.csdn.net/weixin_63572056/article/details/125870054
static int alloc_and_copy(AVPacket* out,
    const uint8_t* sps_pps, uint32_t sps_pps_size,
    const uint8_t* in, uint32_t in_size)///加特征值
{
    uint32_t offset = out->size;
    uint8_t nal_header_size = offset ? 3 : 4;//sps/pps特征码00 00 00 01四字节  其他的特征码00 00 01
    int err;

    err = av_grow_packet(out, sps_pps_size + in_size + nal_header_size);//扩容
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


int h264_add(const uint8_t* codec_extradata, const int codec_extradata_size, AVPacket* out_extradata, int padding)   //添加sps/pps头
{

    uint16_t unit_size;
    uint64_t total_size = 0;
    uint8_t* out = NULL, unit_nb, sps_done = 0,
        sps_seen = 0, pps_seen = 0, sps_offset = 0, pps_offset = 0;
    const uint8_t* extradata = codec_extradata + 4;
    static const uint8_t nalu_header[4] = { 0, 0, 0, 1 };
    int length_size = (*extradata++ & 0x3) + 1; // retrieve length coded size, 用于指示表示编码数据长度所需字节数

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
            nal_size = (nal_size << 8) | buf[i];   //packet前四个字节是h264一帧大小 

        buf += 4; /*s->length_size;*/
        unit_type = *buf & 0x1f;  //帧里第一字节的后五位为该帧的类型，pts为8，关键帧为5，非关键帧为1

        if (nal_size > buf_end - buf || nal_size < 0)
            goto fail;


        if (unit_type == 5) {
            //关键帧加sps / pps

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
    int audio_volume = 100;//范围0-128 SDL_MIX_MAXVOLUME

    // 音视频同步时应该在哪里记录音频的pts，视频渲染时如何利用音频的pts进行同步控制
    static float audio_clock;
    static float video_clock;

    // 控制暂停  播放
    static bool g_exit = false;
    static bool g_pause = false;

    int buf_size;
    uint8_t* buffer = NULL;
    AVFrame* p_frm_yuv = NULL;

    static uint8_t* audio_buf_data = NULL;//解码后的到的音频原始数据
    static int audio_buf_size = 0;//解码后的到的音频原始数据大小
    static int audio_buf_index = 0;//一个游标指针,用来记录是否需要获取更多数据

    SDL_AudioSpec wanted_spec, actual_spec;

    audio_filename = av_strdup(file);
    audio_iformat = NULL;

    AVFrame* audio_frame = av_frame_alloc();
    AVFrame* video_frame = av_frame_alloc();

    FILE* fp_264 = fopen("ba.h264", "wb");
    FILE* fp_yuv = fopen("ba.yuv", "wb");
    FILE* pFile = fopen("ba.pcm", "wb");
    FILE* ptxt = fopen("ba.txt", "wb+");
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
    SDL_OpenAudioDevice(NULL, 0, &wanted_spec, &actual_spec, 0);
    wanted_spec.channels = audio_avctx->channels;//通道数
    wanted_spec.freq = audio_avctx->sample_rate;//采样率		
    wanted_spec.format = AUDIO_S16SYS; //SDL支持的有符号,16bit
    wanted_spec.silence = 0;//静音值
    wanted_spec.samples = audio_avctx->frame_size;//回调缓冲大小,单位为一个通道的样本数
    //wanted_spec.callback = audio_refresh;//按需调用
    //wanted_spec.userdata = NULL;//回调函数参数指针

    //if (actual_spec.channels != wanted_spec.channels ||
    //    actual_spec.freq != wanted_spec.freq ||
    //    actual_spec.format != wanted_spec.format)
    //{
    //    printf("SDL不支持的音频参数!\n");
    //    goto fail;
    //}

    audio_hardware_fmt = AV_SAMPLE_FMT_S16;
    audio_hardware_sample_rate = actual_spec.freq;
    audio_hardware_channels = actual_spec.channels;
    audio_hardware_layout = av_get_default_channel_layout(actual_spec.channels);

    printf("转码采样深度:%s\n", av_get_sample_fmt_name((enum AVSampleFormat)audio_hardware_fmt));
    printf("转码采样率:%d\n", audio_hardware_sample_rate);
    printf("转码采样通道:%d\n", audio_hardware_channels);
    printf("转码layout:%d\n", audio_hardware_layout);
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

        //视频包入队列,其他包丢弃
        if (pkt->stream_index == audio_index)
        {
            printf("## Audio Frame Number %d\n", audio_avctx->frame_number);
            fprintf(ptxt, "## Audio Frame Number %d\n", audio_avctx->frame_number);
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
            if (avcodec_send_packet(audio_avctx, pkt) < 0)  // 解封装
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
            //向解码器喂数据，一个packet可能是一个视频帧或多个音频帧，
            if (avcodec_send_packet(video_avctx, pkt) < 0)  // 解封装
            {
                printf("video Decode error.\n");
                av_packet_unref(pkt);
                goto fail;
            }
            // A9.2 接收解码器输出的数据，此处只处理视频帧，每次接收一个packet，将之解码得到一个frame
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