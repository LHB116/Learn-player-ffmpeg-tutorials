1、FFmpeg学习
<br /> https://mp.weixin.qq.com/s/MYecqlqJdmesWZD42kjBcA
<br /> https://ffmpeg.xianwaizhiyin.net/ffplay/jump.html

<br /> 2、生产者消费者模型
<br />https://blog.csdn.net/GG_Bruse/article/details/129035055

<br /> 3、VS2019常用快捷键  https://zhuanlan.zhihu.com/p/560897683?utm_id=0
<br />快速折叠所有：Ctrl+m+o
<br />快速展开所有：Ctrl+m+l
<br />剪切：Ctrl+x

<br /> 4、Tutorials
Tutorial01:
FFmpeg解封装解码视频流，保存图片

Tutorial02:
FFmpeg解封装解码视频流，SDL显示

Tutorial03-pull/Tutorial03-push:
FFmpeg解封装解码音频流，SDL播放

Tutorial03/Tutorial031:
FFmpeg解码视频流与音频流，SDL显示+播放

Tutorial04:
FFmpeg解码视频流与音频流，SDL显示+播放  多线程方式  代码风格类似ffplay

Tutorial05:
基于Tutorial04添加了  将视频同步到音频上 
1.如果视频超前音频，视频不刷新了，延时等待，就相对于重复帧
2.如果视频滞后音频，视频立即刷新，直到重新同步

Tutorial06: (无法修改显示窗口大小？？)
基于Tutorial05添加了  将音频同步到视频上 

Tutorial07:
基于Tutorial05添加了  seeking

<br /> FFplay源码分析
<br />https://mp.weixin.qq.com/s/MYecqlqJdmesWZD42kjBcA
<br />https://blog.csdn.net/m0_60565784/article/details/131740449
<br />https://blog.csdn.net/m0_60565784?type=blog

<br /> ffmpeg实现单线程异步的视频播放器
<br />https://blog.csdn.net/u013113678/article/details/128293698


<br /> 5、 SDL_CondSignal的作用是什么，如何发挥作用？  
<br />https://avmedia.0voice.com/?id=47057
<br />SDL_CondSignal用于唤醒一个等待在条件变量上的线程,被唤醒的线程将会从 SDL_CondWait() 或者类似函数处继续执行
<br />5.1、如在packet_queue_put函数与packet_queue_get函数中，
packet_queue_get函数中如果block标志为1，进入SDL_CondWait(q->cond, q->mutex)，等待有新的数据放入队列中，
而这个新数据进入队列就由packet_queue_put中SDL_CondSignal函数来进行通知
<br />5.2、还有一种情况就是队列中数据太多了，需要等待被消费，也可以用SDL_CondWait/SDL_CondWaitTimeout
SDL_CondWaitTimeout(is->continue_read_thread, wait_mutex, 10);
如果没有唤醒则超时10ms退出


<br />6、 一秒钟音频字节数=采样率 * 声道数 * 采样格式大小（bit） / 8


<br />7、 Packet按照dts解封装，Frame按照pts解码
av_rescale_q用于计算Packet的PTS
https://blog.csdn.net/Dontla/article/details/134901321

<br />8、 生产者消费者
https://blog.csdn.net/GG_Bruse/article/details/129035055


<br />9、 ffmpeg中av_seek_frame只能跳转到关键帧位置，这种seek就会出现不跟手的问题
seek到非关键帧的思路：
https://blog.csdn.net/ice_ly000/article/details/84977374
http://blog.yundiantech.com/?log=blog&id=13
SDLPlayer-ffplystyle无法正常退出  seek到末尾也有问题

<br />10、 暂停的时候是否要进行静音  http://blog.yundiantech.com/?log=blog&id=13

<br />11、视频同步到音频的基本方法是：如果视频超前音频，则不进行播放，以等待音频；
如果视频落后音频，则丢弃当前帧直接播放下一帧，以追赶音频。

<br />12、为什么FFplay要有两个数据队列 packet_queue 和 frame_queue
其中packet_queue中存放的是解封装后的packet，
frame_queue中存放的是解码后的数据，
为了快速解码，如果是RA或LDB有包含B帧，那么pts与dts不同，
packet是按照dts解码的，frame在ffmpeg的api中按照pts输出的，
那么有可能无法快速解码渲染图像，如果packet_queue与frame_queue分开，并用不同的线程处理就可以解决上述问题

