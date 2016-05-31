#ifndef _BWLIST_DNSCACHE_H__
#define _BWLIST_DNSCACHE_H__

int bwlist_dnsIP_init(int max_dnsIP_cnt);
void bwlist_dnsIP_fini(void);

/* ��Ҫ�ⲿ��rcu�� */
bwlist_group_t *__bwlist_find_grp_with_dnsIP(uint32_t h_dst);

void bwlist_dnsIP_invalid_all(void);
void bwlist_dnsIP_dump(void);

int bwlist_dnsIP_node_cnt(void);

int bwlist_dnsIP_set_expire_time(unsigned long seconds);
unsigned long bwlist_dnsIP_get_expire_time(void);


#endif //#ifndef _BWLIST_DNSCACHE_H__

