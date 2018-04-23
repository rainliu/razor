#include "sim_internal.h"
#include <assert.h>

/*�������ķ��͵�������*/
#define MAX_SEND_COUNT		10

/*��������razor�����ʵ���ͨ��*/
static void sim_bitrate_change(void* trigger, uint32_t bitrate, uint8_t fraction_loss, uint32_t rtt)
{
	sim_session_t* s = (sim_session_t*)trigger;

	/*todo: ������Ƶ�����ô�������*/
}

static void sim_send_packet(void* handler, uint32_t packet_id, int retrans, size_t size)
{
	sim_header_t header;
	sim_segment_t* seg;
	skiplist_iter_t* it;
	skiplist_item_t key;
	int64_t now_ts;

	sim_sender_t* sender = (sim_sender_t*)handler;
	sim_session_t* s = sender->s;

	key.u32 = packet_id;
	it = skiplist_search(sender->cache, key);
	if (it == NULL)
		return;

	now_ts = GET_SYS_MS();

	seg = it->val.ptr;
	/*ÿ����һ�Σ��ͽ��д������+1*/
	seg->transport_seq = sender->transport_seq_seed++;
	/*send_ts����Ե�ǰ֡������ʱ��֮����ڽ��ն˼��㷢��ʱ����*/
	seg->send_ts = (uint16_t)(now_ts - sender->first_ts - seg->timestamp);
	/*�����ͼ�¼����ӵ�������н���bwe�������ӳٹ���*/
	sender->cc->on_send(sender->cc, seg->transport_seq, seg->data_size + SIM_SEGMENT_HEADER_SIZE);


	INIT_SIM_HEADER(header, SIM_SEG, s->uid);
	sim_encode_msg(&s->sstrm, &header, seg);

	sim_session_network_send(s, &s->sstrm);
}

sim_sender_t* sim_sender_create(sim_session_t* s)
{
	sim_sender_t* sender = calloc(1, sizeof(sim_sender_t));
	sender->first_ts = -1;

	sender->cache = skiplist_create(idu32_compare, free_video_seg, s);
	sender->cc = razor_sender_create(s, sim_bitrate_change, sender, sim_send_packet, 300);
	sender->s = s;

	return sender;
}

void sim_sender_destroy(sim_session_t* s, sim_sender_t* sender)
{
	if (sender == NULL)
		return;

	if (sender->cache != NULL){
		skiplist_destroy(sender->cache);
		sender->cache = NULL;
	}

	if (sender->cc != NULL){
		razor_sender_destroy(sender->cc);
		sender->cc = NULL;
	}

	free(sender);
}

void sim_sender_reset(sim_session_t* s, sim_sender_t* sender)
{
	sender->actived = 0;
	sender->base_packet_id = 0;
	sender->packet_id_seed = 0;
	sender->frame_id_seed = 0;

	sender->first_ts = -1;
	sender->transport_seq_seed = 0;

	skiplist_clear(sender->cache);

	/*����ӵ������*/
	if (sender->cc != NULL){
		razor_sender_destroy(sender->cc);
		sender->cc = NULL;
	}
	sender->cc = razor_sender_create(s, sim_bitrate_change, sender, sim_send_packet, 300);
}

int sim_sender_active(sim_session_t* s, sim_sender_t* sender)
{
	if (sender->actived == 1)
		return -1;

	sender->actived = 1;
	return 0;
}

/*��Ƶ��Ƭ*/
#define SPLIT_NUMBER	1024
static uint16_t sim_split_frame(sim_session_t* s, uint16_t splits[], size_t size)
{
	uint16_t ret, i;
	uint16_t remain_size;

	if (size <= SIM_VIDEO_SIZE){
		ret = 1;
		splits[0] = size;
	}
	else{
		ret = size / SIM_VIDEO_SIZE;
		for (i = 0; i < ret; i++)
			splits[i] = SIM_VIDEO_SIZE;

		remain_size = size % SIM_VIDEO_SIZE;
		if (remain_size > 0){
			splits[ret] = remain_size;
			ret++;
		}
	}

	return ret;
}

int sim_sender_put(sim_session_t* s, sim_sender_t* sender, uint8_t ftype, const uint8_t* data, size_t size)
{
	sim_segment_t* seg;
	int64_t now_ts;
	uint16_t splits[SPLIT_NUMBER], total, i;
	uint8_t* pos;
	skiplist_item_t key, val;
	uint32_t timestamp;

	assert((size / SIM_VIDEO_SIZE) < SPLIT_NUMBER);

	now_ts = GET_SYS_MS();
	/*֡�ְ�*/
	total = sim_split_frame(s, splits, size);

	/*����ʱ���*/
	if (sender->first_ts == -1){
		timestamp = 0;
		sender->first_ts = now_ts;
	}
	else
		timestamp = (uint32_t)(now_ts - sender->first_ts);

	pos = (uint8_t*)data;
	++sender->frame_id_seed;
	for (i = 0; i < total; ++i){
		seg = malloc(sizeof(sim_segment_t));

		seg->packet_id = ++sender->packet_id_seed;
		seg->fid = sender->frame_id_seed;
		seg->timestamp = timestamp;
		seg->ftype = ftype;
		seg->index = i;
		seg->total = total;

		seg->remb = 1;
		seg->send_ts = 0;
		seg->transport_seq = 0;

		seg->data_size = splits[i];
		memcpy(seg->data, pos, seg->data_size);
		pos += splits[i];

		/*�����ļ��뵽���ͻ�����е���*/
		key.u32 = seg->packet_id;
		val.ptr = seg;
		skiplist_insert(sender->cache, key, val);

		/*�����ļ��뵽cc��pacer��*/
		sender->cc->add_packet(sender->cc, seg->packet_id, 0, seg->data_size + SIM_SEGMENT_HEADER_SIZE);
	}

	return 0;
}

static inline void sim_sender_update_base(sim_session_t* s, sim_sender_t* sender, uint32_t base_packet_id)
{
	uint32_t i;
	skiplist_item_t key;

	for (i = sender->base_packet_id; i < base_packet_id; ++i){
		key.u32 = i;
		skiplist_remove(sender->cache, key);
	}

	if (base_packet_id > sender->base_packet_id)
		sender->base_packet_id = base_packet_id;
}

/*����nack��Ϣ*/
int sim_sender_ack(sim_session_t* s, sim_sender_t* sender, sim_segment_ack_t* ack)
{
	int i;
	sim_segment_t* seg;
	skiplist_iter_t* iter;
	skiplist_item_t key;

	int64_t now_ts;

	/*�ƽ�����*/
	sim_sender_update_base(s, sender, ack->acked_packet_id);

	for (i = 0; i < ack->nack_num; ++i){
		key.u32 = ack->base_packet_id + ack->nack[i];
		iter = skiplist_search(sender->cache, key);
		if (iter != NULL){
			seg = (sim_segment_t*)iter->val.ptr;
			/*�����ļ��뵽cc��pacer�н����ط�*/
			sender->cc->add_packet(sender->cc, seg->packet_id, 0, seg->data_size + SIM_SEGMENT_HEADER_SIZE);
		}
	}

	/*����RTT*/
	key.u32 = ack->acked_packet_id;
	iter = skiplist_search(sender->cache, key);
	if (iter != NULL){
		seg = (sim_segment_t*)iter->val.ptr;

		now_ts = GET_SYS_MS();
		if (now_ts > seg->timestamp + seg->send_ts + sender->first_ts)
			sim_session_calculate_rtt(s, (uint16_t)(now_ts - seg->timestamp - seg->send_ts - sender->first_ts));
	}

	return 0;
}

/*�������ն�����feedback��Ϣ*/
void sim_sender_feedback(sim_session_t* s, sim_sender_t* sender, sim_feedback_t* feedback)
{
	sim_sender_update_base(s, s->sender, feedback->base_packet_id);

	if (sender->cc != NULL)
		sender->cc->on_feedback(sender->cc, feedback->feedback, feedback->feedback_size);
}

void sim_sender_update_rtt(sim_session_t* s, sim_sender_t* sender)
{
	if (sender->cc != NULL){
		sender->cc->update_rtt(sender->cc, s->rtt + s->rtt_var);
	}
}

void sim_sender_set_bitrates(sim_session_t* s, sim_sender_t* sender, uint32_t min_bitrate, uint32_t start_bitrate, uint32_t max_bitrate)
{
	if (sender->cc != NULL){
		sender->cc->set_bitrates(sender->cc, min_bitrate, start_bitrate, max_bitrate);
	}
}

void sim_sender_timer(sim_session_t* s, sim_sender_t* sender, uint64_t cur_ts)
{
	if (sender->cc != NULL)
		sender->cc->heartbeat(sender->cc);
}
