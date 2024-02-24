
#include <VideoPlayer.h>
#include <AudioPlayer.h>

#include <Tutorial.h>

/* Prevents SDL from overriding main() */
#undef main  // 将预处理常量定义取消定义，以便再次声明常量

int main(int argc, char* argv[])
{
	printf("Learn FFmpeg %s\n\n", FFMPEG_VERSION);
	//CameraCapture_OpenCVShow();
	//DemuxDecode_OpenCVShow(argc, argv);
	//DemuxDecode_SDLShow();

	//save_h274_yuv();

	SDLPlayer();

	//AudioPlayer1();

	//Tutorial05();

	return 0;
}

