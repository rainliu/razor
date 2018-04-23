#ifndef __razor_api__001_h_
#define __razor_api__001_h_

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/**************************************************************
˵��:razorû��ʵ���̰߳�ȫ�����̷߳�����Ҫ�ϲ��ͨ��ģ������֤������
���������ǵģ���Ϊ����ӵ������û�кܴ�ļ��㣬Ҳû�в���������ֻҪ
��ͨ�Ų����̰߳�ȫ���ɣ���������뿴�����razor����
**************************************************************/

/*����ص������Ͷ���*/
#include "razor_callback.h"

/****************************************�ⲿֱ�ӵ���API************************************/
void				razor_setup_log(razor_log_func log_cb);
/*����һ�����Ͷ˵�ӵ�����ƶ���*/
razor_sender_t*		razor_sender_create(void* trigger, bitrate_changed_func bitrate_cb, void* handler, pace_send_func send_cb, int queue_ms);
/*����һ�����Ͷ˵�ӵ�����ƶ���*/
void				razor_sender_destroy(razor_sender_t* sender);

razor_receiver_t*	razor_receiver_create(int min_bitrate, int max_bitrate, int packet_header_size, void* handler, send_feedback_func cb);
void				razor_receiver_destroy(razor_receiver_t* receiver);

#ifdef __cplusplus
}
#endif

#endif

