#ifndef VideoPlayer_H
#define VideoPlayer_H

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
#include <libavutil/imgutils.h>
#include <libavutil/ffversion.h>
#include <libavutil/fifo.h>
#include <libswresample/swresample.h>

#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>

#ifdef __cplusplus
}
#endif

#include<opencv2/opencv.hpp>

int CameraCapture_OpenCVShow();
int DemuxDecode_OpenCVShow(int argc, char** argv);
int SDLShowYUV();
int DemuxDecode_SDLShow();
int save_h274_yuv();

#endif
