#ifndef AudioPlayer_H
#define AudioPlayer_H

#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <fstream>
//#include <thread>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <libavdevice/avdevice.h>

#include <libavformat/avformat.h>

#include <libavcodec/avcodec.h>
#include "libavcodec/avfft.h"

#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include <libavutil/audio_fifo.h>
#include <libavutil/fifo.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include "libavutil/opt.h"
#include "libavutil/dict.h"
#include "libavutil/parseutils.h"
#include "libavutil/samplefmt.h"
#include "libavutil/avassert.h"
#include <libavutil/audio_fifo.h>
#include "libavutil/time.h"
#include "libavutil/avstring.h"
#include "libavutil/eval.h"
#include "libavutil/mathematics.h"
#include "libavutil/pixdesc.h"

#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>



#ifdef __cplusplus
}
#endif

//char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
//#define av_err2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

int AudioPlayer();
int AudioPlayer1();


#endif
