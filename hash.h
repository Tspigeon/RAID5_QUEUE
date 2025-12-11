#ifndef __HASH_H__
#define __HASH_H__

#include "initialize.h"

void alloc_assert(void *p,char *s);



typedef struct buffer_info_Hash
{
	unsigned long read_hit;                      /*�����hit����ʾsector�����д�������û���еĴ���*/
	unsigned long read_miss_hit;  
	unsigned long write_hit;   
	unsigned long write_miss_hit;
	unsigned long write_free;   
	unsigned long eject;

	struct buffer_group *buffer_head;            /*as LRU head which is most recently used*/
	struct buffer_group *buffer_tail;            /*as LRU tail which is least recently used*/
	HASH_NODE	**nodeArray;     				 

	unsigned int max_buffer_sector;
	unsigned int buffer_sector_count;
    unsigned int additional_capacity;

	unsigned int	count;		                 /*AVL����Ľڵ�����*/
	int 			(*keyCompare)(HASH_NODE * , HASH_NODE *);
	int			(*free)(HASH_NODE *);

    struct sub_request *temp_buffer_head;
    struct sub_request *temp_buffer_tail;
    unsigned int backup_capacity;
    unsigned int backup_sector_count;
    unsigned int extra_buffer;
    ///////////add_part///////////////
    unsigned int blocked_trigger_count;
    unsigned int Max_Blocked_Count;
	unsigned int Last_blocked_sub_count;
	unsigned int Blocked_Count;
	unsigned int total_blocked_count;
	unsigned long long total_time_service_sub;
	unsigned int total_sub_count;
	unsigned int total_read_count;
	unsigned int per_round_blocked_count;

	unsigned long parity_write_hit;   
	unsigned long parity_write_miss_hit;

    //创建一个数组，记录不同缓存大小下的hit
    unsigned long range_read_hit[16];
    unsigned long range_write_hit[16];

    //record window dram miss count
    unsigned long window_write_miss;

    // 记录不同dram大小下的命中率
    double dram_hit[16];
    //接收python参数
    double k;
    double c;
    double mae;
    //每个ssd的请求到达速率
    double lambda;
    int64_t max_t;
    int64_t min_t;
    unsigned long num_req;
} tHash;


tHash *hash_create(int *freeFunc);
int hash_add(tHash *pHash ,  HASH_NODE *pInsertNode);
HASH_NODE *hash_find(struct ssd_info *ssd,tHash *pHash, HASH_NODE *pKeyNode,int raidID,long *loc);
int hash_del(struct ssd_info *ssd,tHash *pHash ,HASH_NODE *pDelNode,int raidID);
void hash_node_free(tHash *pHash, HASH_NODE *pNode);
int hash_destroy(tHash *pHash);




















#endif