/**
*
*   File:   tutorial02.c
*           So our current plan is to replace the SaveFrame() function from
*           Tutorial 1, and instead output our frame to the screen. But first
*           we have to start by seeing how to use the SDL Library.
*           Uncommented lines of code refer to previous tutorials.
*
*           Compiled using
*               gcc -o tutorial02 tutorial02.c -lavutil -lavformat -lavcodec -lswscale -lz -lm  `sdl2-config --cflags --libs`
*           on Arch Linux.
*           sdl-config just prints out the proper flags for gcc to include the
*           SDL libraries properly. You may need to do something different to
*           get it to compile on your system; please check the SDL documentation
*           for your system. Once it compiles, go ahead and run it.
*
*           What happens when you run this program? The video is going crazy!
*           In fact, we're just displaying all the video frames as fast as we
*           can extract them from the movie file. We don't have any code right
*           now for figuring out when we need to display video. Eventually (in
*           Tutorial 5), we'll get around to syncing the video. But first we're
*           missing something even more important: sound!
*
*   Author: Rambod Rahmani <rambodrahmani@autistici.org>
*           Created on 8/10/18.
*
**/

#include <Tutorial.h>

#define REFRESH_EVENT  (SDL_USEREVENT + 1)
#define BREAK_EVENT  (SDL_USEREVENT + 2)

int thread_exit1 = 0;
static bool s_playing_exit1 = false;
static bool s_playing_pause1 = false;

int sdl_thread_handle_refreshing1(void* opaque)
{
    SDL_Event sdl_event;

    int frame_rate = *((int*)opaque);
    int interval = (frame_rate > 0) ? 1000 / frame_rate : 40;

    printf("frame rate %d FPS, refresh interval %d ms\n", frame_rate, interval);

    while (!s_playing_exit1)
    {
        if (!s_playing_pause1)
        {
            sdl_event.type = REFRESH_EVENT;
            SDL_PushEvent(&sdl_event);
        }
        SDL_Delay(interval);
    }

    return 0;
}

int Tutorial2()
{
    int ret = -1;
    const char* filename = "./file/IronMan.mp4";
    ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);   // [1]
    if (ret != 0)
    {
        // error while initializing SDL
        printf("Could not initialize SDL - %s\n.", SDL_GetError());

        // exit with error
        return -1;
    }

    AVFormatContext * pFormatCtx = NULL;
    ret = avformat_open_input(&pFormatCtx, filename, NULL, NULL);
    if (ret < 0)
    {
        printf("Could not open file %s\n", filename);
        return -1;
    }

    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0)
    {
        printf("Could not find stream information %s\n", filename);
        return -1;
    }

    av_dump_format(pFormatCtx, 0, filename, 0);

    int i;
    AVCodecContext * pCodecCtx = NULL;

    int videoStream = -1;
    for (i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = i;
            break;
        }
    }

    if (videoStream == -1)
    {
        printf("Could not find video stream.");
        return -1;
    }

    const AVCodec * pCodec = NULL;
    pCodec = avcodec_find_decoder(pFormatCtx->streams[videoStream]->codecpar->codec_id);
    if (pCodec == NULL)
    {
        printf("Unsupported codec!\n");
        return -1;
    }

    pCodecCtx = avcodec_alloc_context3(pCodec);
    ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[videoStream]->codecpar);
    if (ret != 0)
    {
        printf("Could not copy codec context.\n");
        return -1;
    }

    ret = avcodec_open2(pCodecCtx, pCodec, NULL);
    if (ret < 0)
    {
        printf("Could not open codec.\n");
        return -1;
    }

    AVFrame * pFrame = NULL;
    pFrame = av_frame_alloc();
    if (pFrame == NULL)
    {
        printf("Could not allocate frame.\n");
        return -1;
    }

    // Create a window with the specified position, dimensions, and flags.
    SDL_Window * screen = SDL_CreateWindow( // [2]
                            "SDL Video Player",
                            SDL_WINDOWPOS_UNDEFINED,
                            SDL_WINDOWPOS_UNDEFINED,
                            pCodecCtx->width/2,
                            pCodecCtx->height/2,
                            SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!screen)
    {
        // could not set video mode
        printf("SDL: could not set video mode - exiting.\n");

        // exit with Error
        return -1;
    }

    //
    SDL_GL_SetSwapInterval(1);

    // A structure that contains a rendering state.
    SDL_Renderer * renderer = NULL;

    // Use this function to create a 2D rendering context for a window.
    renderer = SDL_CreateRenderer(screen, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);   // [3]

    // A structure that contains an efficient, driver-specific representation
    // of pixel data.
    SDL_Texture * texture = NULL;

    // Use this function to create a texture for a rendering context.
    texture = SDL_CreateTexture(  // [4]
                renderer,
                SDL_PIXELFORMAT_YV12,
                SDL_TEXTUREACCESS_STREAMING,
                pCodecCtx->width,
                pCodecCtx->height
            );

    struct SwsContext * sws_ctx = NULL;
    AVPacket * pPacket = av_packet_alloc();
    if (pPacket == NULL)
    {
        printf("Could not alloc packet,\n");
        return -1;
    }

    // set up our SWSContext to convert the image data to YUV420:
    //printf("%d\n", pCodecCtx->pix_fmt);
    //exit(0);
    sws_ctx = sws_getContext(
        pCodecCtx->width,
        pCodecCtx->height,
        pCodecCtx->pix_fmt,
        pCodecCtx->width,
        pCodecCtx->height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        NULL,
        NULL,
        NULL
    );

    /**
     * As we said before, we are using YV12 to display the image, and getting
     * YUV420 data from ffmpeg.
     */

    int numBytes;
    uint8_t * buffer = NULL;

    numBytes = av_image_get_buffer_size(
                AV_PIX_FMT_YUV420P,
                pCodecCtx->width,
                pCodecCtx->height,
                32
            );
    buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));

    // used later to handle quit event
    SDL_Event event;
    SDL_Thread* sdl_thread;
    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = pCodecCtx->width;
    rect.h = pCodecCtx->height;
    int                 frame_rate;
    sdl_thread = SDL_CreateThread(sdl_thread_handle_refreshing1, NULL, (void*)&frame_rate);
    if (sdl_thread == NULL)
    {
        printf("SDL_CreateThread() failed: %s\n", SDL_GetError());
        //res = -1;
        //goto exit8;
    }

    AVFrame * pict = av_frame_alloc();

    av_image_fill_arrays(
        pict->data,
        pict->linesize,
        buffer,
        AV_PIX_FMT_YUV420P,
        pCodecCtx->width,
        pCodecCtx->height,
        32
    );

    int maxFramesToDecode = 500;
    //sscanf(argv[2], "%d", &maxFramesToDecode);

    i = 0;
    while (1) {
        SDL_WaitEvent(&event);
        if (event.type == REFRESH_EVENT)
        {
            while (av_read_frame(pFormatCtx, pPacket) >= 0)
            {
                if (pPacket->stream_index == videoStream)
                {
                    break;
                }
            }

            ret = avcodec_send_packet(pCodecCtx, pPacket);
            if (ret != 0)
            {
                printf("avcodec_send_packet() failed %d\n", ret);
            }
 
            ret = avcodec_receive_frame(pCodecCtx, pFrame);
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
            }

            printf("## Frame Number %d\n", pCodecCtx->frame_number);

            // 转换颜色空间或图像缩放
            // 如果格式一样就是将pFrame中数据拷贝到pict中
            sws_scale(
                sws_ctx,
                (uint8_t const* const*)pFrame->data,
                pFrame->linesize,
                0,
                pCodecCtx->height,
                pict->data,
                pict->linesize
            );

            printf("## raw Linesize %d, %d, %d\n", pFrame->linesize[0], pFrame->linesize[1], pFrame->linesize[2]);
            printf("## converted Linesize %d, %d, %d\n", pict->linesize[0], pict->linesize[1], pict->linesize[2]);

            printf(
                "Frame %c (%d) pts %d dts %d key_frame %d [coded_picture_number %d, display_picture_number %d, %dx%d]\n",
                av_get_picture_type_char(pFrame->pict_type),
                pCodecCtx->frame_number,
                pFrame->pts,
                pFrame->pkt_dts,
                pFrame->key_frame,
                pFrame->coded_picture_number,
                pFrame->display_picture_number,
                pCodecCtx->width,
                pCodecCtx->height
            );

            // Use this function to update a rectangle within a planar
            // YV12 or IYUV texture with new pixel data.
            SDL_UpdateYUVTexture(
                texture,            // the texture to update
                &rect,              // a pointer to the rectangle of pixels to update, or NULL to update the entire texture
                pict->data[0],      // the raw pixel data for the Y plane
                pict->linesize[0],  // the number of bytes between rows of pixel data for the Y plane
                pict->data[1],      // the raw pixel data for the U plane
                pict->linesize[1],  // the number of bytes between rows of pixel data for the U plane
                pict->data[2],      // the raw pixel data for the V plane
                pict->linesize[2]   // the number of bytes between rows of pixel data for the V plane
            );

            // clear the current rendering target with the drawing color
            SDL_RenderClear(renderer);

            // copy a portion of the texture to the current rendering target
            SDL_RenderCopy(
                renderer,   // the rendering context
                texture,    // the source texture
                NULL,       // the source SDL_Rect structure or NULL for the entire texture
                NULL        // the destination SDL_Rect structure or NULL for the entire rendering
                            // target; the texture will be stretched to fill the given rectangle
            );

            // update the screen with any rendering performed since the previous call
            SDL_RenderPresent(renderer);

            av_packet_unref(pPacket);
        }
        else if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_SPACE)
            {
                // 用户按空格键，暂停/继续状态切换
                s_playing_pause1 = !s_playing_pause1;
                printf("player %s\n", s_playing_pause1 ? "pause" : "continue");
            }
        }
        else if (event.type == SDL_QUIT)
        {
            // 用户按下关闭窗口按钮 x
            printf("SDL event QUIT\n");
            s_playing_exit1 = true;
            break;
        }
        else
        {
            // printf("Ignore SDL event 0x%04X\n", sdl_event.type);
        }
    }

    while (0 && av_read_frame(pFormatCtx, pPacket) >= 0)
    {
        if (pPacket->stream_index == videoStream)
        {
            ret = avcodec_send_packet(pCodecCtx, pPacket);
            if (ret < 0)
            {
                printf("Error sending packet for decoding.\n");
                return -1;
            }

            while (ret >= 0)
            {
                ret = avcodec_receive_frame(pCodecCtx, pFrame);

                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
                    printf("Error while decoding.\n");
                    return -1;
                }

                // Convert the image into YUV format that SDL uses:
                // We change the conversion format to PIX_FMT_YUV420P, and we
                // use sws_scale just like before.
                sws_scale(
                    sws_ctx,
                    (uint8_t const * const *)pFrame->data,
                    pFrame->linesize,
                    0,
                    pCodecCtx->height,
                    pict->data,
                    pict->linesize
                );

                if (++i <= maxFramesToDecode)
                {
                    // get clip fps
                    //printf();
                    //exit(0);
                    double fps = av_q2d(pFormatCtx->streams[videoStream]->r_frame_rate);

                    // get clip sleep time
                    double sleep_time = 1.0/(double)fps;

                    // sleep: usleep won't work when using SDL_CreateWindow
                    // usleep(sleep_time);
                    // Use SDL_Delay in milliseconds to allow for cpu scheduling
                    SDL_Delay((1000 * sleep_time) - 10);    // [5]

                    // The simplest struct in SDL. It contains only four shorts. x, y which
                    // holds the position and w, h which holds width and height.It's important
                    // to note that 0, 0 is the upper-left corner in SDL. So a higher y-value
                    // means lower, and the bottom-right corner will have the coordinate x + w,
                    // y + h.
                    SDL_Rect rect;
                    rect.x = 0;
                    rect.y = 0;
                    rect.w = pCodecCtx->width;
                    rect.h = pCodecCtx->height;

                    printf(
                        "Frame %c (%d) pts %d dts %d key_frame %d [coded_picture_number %d, display_picture_number %d, %dx%d]\n",
                        av_get_picture_type_char(pFrame->pict_type),
                        pCodecCtx->frame_number,
                        pFrame->pts,
                        pFrame->pkt_dts,
                        pFrame->key_frame,
                        pFrame->coded_picture_number,
                        pFrame->display_picture_number,
                        pCodecCtx->width,
                        pCodecCtx->height
                    );

                    // Use this function to update a rectangle within a planar
                    // YV12 or IYUV texture with new pixel data.
                    SDL_UpdateYUVTexture(
                        texture,            // the texture to update
                        &rect,              // a pointer to the rectangle of pixels to update, or NULL to update the entire texture
                        pict->data[0],      // the raw pixel data for the Y plane
                        pict->linesize[0],  // the number of bytes between rows of pixel data for the Y plane
                        pict->data[1],      // the raw pixel data for the U plane
                        pict->linesize[1],  // the number of bytes between rows of pixel data for the U plane
                        pict->data[2],      // the raw pixel data for the V plane
                        pict->linesize[2]   // the number of bytes between rows of pixel data for the V plane
                    );

                    // clear the current rendering target with the drawing color
                    SDL_RenderClear(renderer);

                    // copy a portion of the texture to the current rendering target
                    SDL_RenderCopy(
                        renderer,   // the rendering context
                        texture,    // the source texture
                        NULL,       // the source SDL_Rect structure or NULL for the entire texture
                        NULL        // the destination SDL_Rect structure or NULL for the entire rendering
                                    // target; the texture will be stretched to fill the given rectangle
                    );

                    // update the screen with any rendering performed since the previous call
                    SDL_RenderPresent(renderer);
                }
                else
                {
                    break;
                }
            }

            if (i > maxFramesToDecode)
            {
                break;
            }
        }

        av_packet_unref(pPacket);

        // handle Ctrl + C event
        SDL_PollEvent(&event);
        switch(event.type)
        {
            case SDL_QUIT:
            {
                SDL_Quit();
                exit(0);
            }
            break;

            default:
            {
                // nothing to do
            }
            break;
        }
    }

    /**
     * Cleanup.
     */
    av_frame_free(&pFrame);
    av_free(pFrame);
    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);

    SDL_DestroyRenderer(renderer);
    SDL_Quit();

    // exit
    return 0;
}

// [1]
/**
 * SDL_Init() essentially tells the library what features we're going to use.
 * SDL_GetError(), of course, is a handy debugging function.
 *
 * Use this function to initialize the SDL library. This must be called before
 * using most other SDL functions.
 *
 * Returns 0 on success or a negative error code on failure; call
 * SDL_GetError() for more information.
 *
 * https://wiki.libsdl.org/SDL_Init
 */

// [2]
/**
 * Use this function to create a window with the specified position, dimensions,
 * and flags.
 *
 * Returns the window that was created or NULL on failure; call SDL_GetError()
 * for more information.
 *
 * On Apple's OS X you must set the NSHighResolutionCapable Info.plist property
 * to YES, otherwise you will not receive a High DPI OpenGL canvas.
 *
 * SDL_WINDOW_FULLSCREEN: If the window is set fullscreen, the width and height
 * parameters w and h will not be used.
 */

// [3]
/**
 * Use this function to create a 2D rendering context for a window.
 *
 * Returns a valid rendering context or NULL if there was an error; call
 * SDL_GetError() for more information.
 */

// [4]
/**
 * Use this function to create a texture for a rendering context.
 *
 * Returns a pointer to the created texture or NULL if no rendering context was
 * active, the format was unsupported, or the width or height were out of range;
 * call SDL_GetError() for more information.
 */

// [5]
/**
 * This function waits a specified number of milliseconds before returning. It
 * waits at least the specified time, but possible longer due to OS scheduling.
 * The delay granularity is at least 10 ms. Some platforms have shorter clock
 * ticks but this is the most common.
 */
