#ifndef Tutorial_H
#define Tutorial_H

#include <stdio.h>
#include <string.h>
#include <iostream>
#include <memory>
#include <fstream>
#include <thread>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/avstring.h>
#include <libavutil/time.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libavutil/ffversion.h>
#include <libavutil/fifo.h>
#include <libavutil/audio_fifo.h>
#include <libswresample/swresample.h>

#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#ifdef __cplusplus
}
#endif


#include<opencv2/opencv.hpp>

//char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
//#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)


int SDLPlayer();
int SDLPlayer1();
int SDLPlayer2();


int Tutorial1();  // demux->decode->save decoded frames
int Tutorial2();  // demux->decode->SDL dsiplay only video
int Tutorial3();
int Tutorial31();
int Tutorial04();
int Tutorial05();
int Tutorial06();
int Tutorial07();


#endif
