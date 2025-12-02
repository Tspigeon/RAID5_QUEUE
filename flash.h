/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName�� flash.h
Author: Hu Yang		Version: 2.1	Date:2011/12/02
Description: 

History:
<contributor>     <time>        <version>       <desc>                   <e-mail>
Yang Hu	        2009/09/25	      1.0		    Creat SSDsim       yanghu@foxmail.com
                2010/05/01        2.x           Change 
Zhiming Zhu     2011/07/01        2.0           Change               812839842@qq.com
Shuangwu Zhang  2011/11/01        2.1           Change               820876427@qq.com
Chao Ren        2011/07/01        2.0           Change               529517386@qq.com
Hao Luo         2011/01/01        2.0           Change               luohao135680@gmail.com
*****************************************************************************************************************************/

#ifndef FLASH_H
#define FLASH_H 

#include <stdlib.h>
#include <stdbool.h>
#include "pagemap.h"
#include <time.h>

struct ssd_info *process(struct ssd_info *);
struct ssd_info * insert2buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write);

struct ssd_info *flash_page_state_modify(struct ssd_info *,struct sub_request *,unsigned int,unsigned int,unsigned int,unsigned int,unsigned int,unsigned int);
struct ssd_info *make_same_level(struct ssd_info *,unsigned int,unsigned int,unsigned int,unsigned int,unsigned int,unsigned int);
int find_level_page(struct ssd_info *,unsigned int,unsigned int,unsigned int,struct sub_request *,struct sub_request *);
int make_level_page(struct ssd_info * ssd, struct sub_request * sub0,struct sub_request * sub1);//Ϊsub1������sub0��ͬλ�õ�ҳ
struct ssd_info *compute_serve_time(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,struct sub_request **subs,unsigned int subs_count,unsigned int command);
int get_ppn_for_advanced_commands(struct ssd_info *ssd,unsigned int channel,unsigned int chip,struct sub_request * * subs ,unsigned int subs_count,unsigned int command);
int get_ppn_for_normal_command(struct ssd_info * ssd, unsigned int channel,unsigned int chip,struct sub_request * sub);
struct ssd_info *dynamic_advanced_process(struct ssd_info *ssd,unsigned int channel,unsigned int chip);
struct sub_request *find_same_sub_write(struct ssd_info * ssd, int lpn, int raidID);

struct sub_request *find_two_plane_page(struct ssd_info *, struct sub_request *);
struct sub_request *find_interleave_read_page(struct ssd_info *, struct sub_request *);
int find_twoplane_write_sub_request(struct ssd_info * ssd, unsigned int channel, struct sub_request * sub_twoplane_one,struct sub_request * sub_twoplane_two);
int find_interleave_sub_request(struct ssd_info * ssd, unsigned int channel, struct sub_request * sub_interleave_one,struct sub_request * sub_interleave_two);
struct sub_request * find_read_sub_request(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die);
struct sub_request * find_write_sub_request(struct ssd_info * ssd, unsigned int channel);
struct sub_request * creat_sub_request(struct ssd_info * ssd,unsigned int lpn,int size,unsigned int state,struct request * req,unsigned int operation, unsigned int target_page_type, unsigned int raidID);
struct sub_request * find_write_sub_request_raid(struct ssd_info * ssd, unsigned int channel, unsigned int chip);

struct sub_request *find_interleave_twoplane_page(struct ssd_info *ssd, struct sub_request *onepage,unsigned int command);
int find_interleave_twoplane_sub_request(struct ssd_info * ssd, unsigned int channel,struct sub_request * sub_request_one,struct sub_request * sub_request_two,unsigned int command);

struct ssd_info *delete_from_channel(struct ssd_info *ssd,unsigned int channel,struct sub_request * sub_req);
struct ssd_info *un_greed_interleave_copyback(struct ssd_info *,unsigned int,unsigned int,unsigned int,struct sub_request *,struct sub_request *);
struct ssd_info *un_greed_copyback(struct ssd_info *,unsigned int,unsigned int,unsigned int,struct sub_request *);
int  find_active_block(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane);
int write_page(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int active_block,unsigned int *ppn);
int allocate_location(struct ssd_info * ssd ,struct sub_request *sub_req);


int go_one_step(struct ssd_info * ssd, struct sub_request * sub1,struct sub_request *sub2, unsigned int aim_state,unsigned int command);
int services_2_r_cmd_trans_and_complete(struct ssd_info * ssd);
int services_2_r_wait(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag);

int services_2_r_data_trans(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag);
int services_2_write(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag);
int delete_w_sub_request(struct ssd_info * ssd, unsigned int channel, struct sub_request * sub );
int copy_back(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die,struct sub_request * sub);
int static_write(struct ssd_info * ssd, unsigned int channel,unsigned int chip, unsigned int die,struct sub_request * sub);

struct sub_request * init_sub_request(struct ssd_info * ssd,unsigned int lpn,int size,unsigned int state,struct request * req,unsigned int operation, unsigned int target_page_type, unsigned int raidID);
Status init_allocate_location(struct ssd_info * ssd ,struct sub_request *sub_req);
Status add_sub2channel(struct ssd_info *ssd,struct sub_request * sub,struct request * req);
Status add_sub2channel_for_raid(struct ssd_info *ssd,struct sub_request * sub,struct request * req,unsigned int lpn,unsigned int state,int sub_size);
struct sub_request* create_recovery_sub_write(struct ssd_info *ssd, int channel, int chip, int die, int plane, int block, int page);
struct sub_request * creat_buffer_sub_request(struct ssd_info * ssd,unsigned int lpn,int size,unsigned int state,struct request * req,unsigned int operation, unsigned int target_page_type, unsigned int raidID);
Status allocate_buffer_sub_location(struct ssd_info * ssd ,struct sub_request *sub_req);
void combine_raid_stripe(struct ssd_info* ssd, int lpn, unsigned int state, struct request* req, unsigned int mask);
struct sub_request * distribute_sub_request(struct ssd_info * ssd,int operation,struct request * req,struct sub_request *sub);
struct ssd_info * insert_2_SSD1_buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write,int last_sub_pop);
struct ssd_info * insert_2_SSD2_buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write,int last_sub_pop);
struct ssd_info * insert_2_SSD3_buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write,int last_sub_pop);
struct ssd_info * insert_2_SSD4_buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write,int last_sub_pop);
void create_parity(struct ssd_info *ssd, struct request *req,long long raidID,int write_lpn_count);
void insert_buffer(struct ssd_info *ssd,int sub_ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write,int last_sub_pop);
void create_sub_parity(struct ssd_info *ssd, struct request *req,long long raidID);
void del_last_sub_from_buffer(struct ssd_info *ssd,int ssd_buffer_num,unsigned int last_sub_id,struct sub_request *last_sub);
void add_last_sub_to_buffer(struct ssd_info *ssd,int ssd_buffer_num,struct sub_request *last_sub,int count_flag);
struct sub_request * copy_sub_request(struct sub_request * sub);
void add_sub_to_host_queue(struct ssd_info *ssd,struct sub_request * sub);
void trans_sub_from_host_to_buffer(struct ssd_info *ssd,int patchID);
bool is_enough_for_last_sub_to_buffer(struct ssd_info *ssd,int ssd_buffer_num,struct sub_request *last_sub);
void active_pop(struct ssd_info *ssd,int ssd_buffer_num,struct request *req,int count_flag);
void create_raid_patch(struct ssd_info *ssd,struct request* new_request,int lpn,int last_lpn,int create_flag);
void update_patch_info(struct ssd_info * ssd,int patchID);
Status active_pop_add_sub2channel(struct ssd_info *ssd,struct sub_request * sub,struct request * req,unsigned int lpn,unsigned int state,int sub_size);
struct sub_request * creat_write_sub_for_new_stripe(struct ssd_info * ssd,unsigned int lpn,int size,unsigned int state,struct request * req,unsigned int operation, unsigned int target_page_type);
int findMaxHostQueuePosIndex(struct sub_request* arr[], int size);
struct MinTwoIndices find_Two_Free_Buffer(struct ssd_info* ssd, int size, int excludeIndex);
void copy_last_sub_to_channel(struct ssd_info *ssd,struct sub_request *copy_sub,int channel_num);
void del_copy_sub_from_channel(struct ssd_info *ssd,struct sub_request *last_sub,int channel_num);
void del_sub_from_request(struct ssd_info *ssd,struct request *req,struct sub_request *sub);
void update_sub_complete_info(struct ssd_info* ssd,int patch_id,int sub_id);
bool start_patch_copy(struct ssd_info *ssd,struct sub_request *sub,int current_channel);
void process_new_write_sub(struct ssd_info *ssd);
void trans_new_sub_to_buffer(struct ssd_info *ssd,int patchID);
void del_temp_sub(struct ssd_info *ssd,struct sub_request *sub);
int update_time(struct ssd_info *ssd);
struct MinTwoIndices find_Two_Max_Buffer(struct ssd_info* ssd, int size);
struct MinTwoIndices find_Free_Buffer(struct ssd_info* ssd, int size, int excludeIndex);
bool is_enough_for_last_sub(struct ssd_info *ssd,int ssd_buffer_num,struct sub_request *last_sub);
int countMinIndex(struct ssd_info *ssd,int arr[], int size);
int findMaxnIndex(int arr[], int size);
void add_crowded_last_sub(struct ssd_info *ssd,struct sub_request *sub);
void process_host_sub_pos(struct ssd_info *ssd);
void start_backup(struct ssd_info *ssd,int first_ssd,int second_ssd,struct sub_request *sub);
int64_t predict_complete_time(struct ssd_info *ssd,struct sub_request *sub);
void del_sub_from_waiting_queue(struct ssd_info *ssd,struct sub_request *sub);
unsigned int compute_free_sector(struct ssd_info *ssd,int ssd_num);
void update_process_time(struct ssd_info *ssd,struct sub_request *sub);
void add_processing_sub(struct ssd_info *ssd,struct sub_request *sub);
bool del_processing_sub(struct ssd_info *ssd,struct sub_request *sub);
double set_threshold(struct ssd_info *ssd);
double findMin(double arr[], int size);
int getTenthsDigit(double num);
void findTwoMaxIndices(double arr[], int size, int* min1Index, int* min2Index);
void allocate_space(struct ssd_info *ssd,int min_ssd);
void update_backup_sub(struct ssd_info *ssd,struct sub_request *sub);
void add_processing_backup_sub(struct ssd_info *ssd,struct sub_request *sub);
void del_processing_backup_sub(struct ssd_info *ssd,struct sub_request *sub);
void add_backup_sub_to_pop_sub(struct sub_request *pop_sub,struct sub_request *backup_sub);
bool find_repetition_backup_sub(struct ssd_info *ssd,struct request *req,struct sub_request *backup_sub);
void update_crowded_sub_info(struct ssd_info *ssd,struct request *req);
int del_completed_crowded_sub(struct request *req,int completed_sub_count);
void count_sub_of_ssd(struct ssd_info *ssd);
void blocked_sub_of_ssd(struct ssd_info *ssd,int ssd_id);
void count_waiting_sub_in_ssd(struct ssd_info *ssd);

#endif

