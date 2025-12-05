/*****************************************************************************************************************************
This project was supported by the National Basic Research 973 Program of China under Grant No.2011CB302301
Huazhong University of Science and Technology (HUST)   Wuhan National Laboratory for Optoelectronics

FileName�� flash.c
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

#include <stdbool.h>
#include "flash.h"
#include "ssd.h"
#include "hash.h"
#include <time.h>
#include <math.h>
//#include "layer_weight.h"
extern int index1; 
extern int index2;
extern int RRcount;
extern char aveber;

/**********************
*只作用于创建写子请求的使用
***********************/
Status allocate_location(struct ssd_info * ssd ,struct sub_request *sub_req)
{
	struct sub_request * update=NULL;
	unsigned int channel_num=0,chip_num=0,die_num=0,plane_num=0;
	struct local *location=NULL;

	channel_num=ssd->parameter->channel_number;
	chip_num=ssd->parameter->chip_channel[0];
	die_num=ssd->parameter->die_chip;
	plane_num=ssd->parameter->plane_die;
	unsigned int full_page;
    if(ssd->parameter->subpage_page == 32){
		full_page = 0xffffffff;
	}
	else{
		full_page=~(0xffffffff<<(ssd->parameter->subpage_page));
	}
	
	if (ssd->parameter->allocation_scheme==0)          /*动态分配*/
	{
		/******************************************************************
		*在动态分配中，需要产生一个读请求，并且只有这个读请求完成之后才能进行这个页的写操作
		*******************************************************************/
		//数据为的lpn，并且映射表中存放了state
		if (sub_req->lpn != ssd->stripe.checkLpn && ssd->dram->map->map_entry[sub_req->lpn].state!=0)    
		{
            //这个sub的state和这个sub上次存放在映射表中的state不一致
			if((sub_req->state&ssd->dram->map->map_entry[sub_req->lpn].state)!=ssd->dram->map->map_entry[sub_req->lpn].state)
			{
				ssd->read_count++;
				ssd->read1++;
				ssd->update_read_count++;
                //更新写的话，需要先读原位置上的旧数据后写新位置上的新数据
				update=(struct sub_request *)malloc(sizeof(struct sub_request));
				alloc_assert(update,"update");
				memset(update,0, sizeof(struct sub_request));

				if(update==NULL)
				{
					return ERROR;
				}
				update->location=NULL;
				update->next_node=NULL;
				update->next_subs=NULL;
				update->update=NULL;						
				location = find_location(ssd,ssd->dram->map->map_entry[sub_req->lpn].pn);
				update->location=location;
				update->begin_time = ssd->current_time;
				update->current_state = SR_WAIT;
				update->current_time=MAX_INT64;
				update->next_state = SR_R_C_A_TRANSFER;
				update->next_state_predict_time=MAX_INT64;
				update->lpn = sub_req->lpn;
				update->state=((ssd->dram->map->map_entry[sub_req->lpn].state^sub_req->state)&0x7fffffff);
				update->size=size(update->state);
				update->ppn = ssd->dram->map->map_entry[sub_req->lpn].pn;
				update->operation = READ;
                //把update读子请求插入到channel的sub_r的队尾
				if(1){
					if (ssd->channel_head[location->channel].subs_r_tail!=NULL)
					{
						ssd->channel_head[location->channel].subs_r_tail->next_node=update;
						ssd->channel_head[location->channel].subs_r_tail=update;
					} 
					else
					{
						ssd->channel_head[location->channel].subs_r_tail=update;
						ssd->channel_head[location->channel].subs_r_head=update;
					}
                    ssd->channel_head[location->channel].read_queue_length++;
				}
			}
		}
		/***************************************
		*动态分配，case2的情况
		****************************************/
		switch(ssd->parameter->dynamic_allocation)
		{
			case 0:
			{
				sub_req->location->channel=-1;
				sub_req->location->chip=-1;
				sub_req->location->die=-1;
				sub_req->location->plane=-1;
				sub_req->location->block=-1;
				sub_req->location->page=-1;

				if (ssd->subs_w_tail!=NULL)
				{
					ssd->subs_w_tail->next_node=sub_req;
					ssd->subs_w_tail=sub_req;
				} 
				else
				{
					ssd->subs_w_tail=sub_req;
					ssd->subs_w_head=sub_req;
				}


				if (update!=NULL)
				{
					sub_req->update=update;
				}

				break;
			}
			case 1:
			{
				sub_req->location->channel=sub_req->lpn%ssd->parameter->channel_number;
				sub_req->location->chip=-1;
				sub_req->location->die=-1;
				sub_req->location->plane=-1;
				sub_req->location->block=-1;
				sub_req->location->page=-1;

				if (update!=NULL)
				{
					sub_req->update=update;
				}

				break;
			}
			case 2:
			{
				/* sub_req的location是动态分配且分配策略为2的时候，分配得到的*/
				sub_req->location->channel = ssd->channelToken;
				sub_req->location->chip = ssd->chipToken;
				
				sub_req->location->die=-1;
				sub_req->location->plane=-1;
				sub_req->location->block=-1;
				sub_req->location->page=-1;
				if (update!=NULL)
				{
                    //可是update读子请求读旧数据的话，旧数据是有ppn的啊，对应的有location为什么还要这样呢？
					sub_req->update=update;
					sub_req->location->channel = update->location->channel;
					sub_req->location->chip = update->location->chip;
				}
                else
                {
					if(sub_req->lpn == ssd->stripe.checkLpn){
						if(ssd->trip2Page[sub_req->raidNUM].PChannel == -1){
							if(++ssd->channelToken >= ssd->parameter->channel_number){
								ssd->channelToken = 0;
								if(++ssd->chipToken >= ssd->parameter->chip_channel[0])
									ssd->chipToken = 0;
							}
							ssd->trip2Page[sub_req->raidNUM].PChannel = sub_req->location->channel;
						}else{
							sub_req->location->channel = ssd->trip2Page[sub_req->raidNUM].PChannel;
						}
					}else if(ssd->dram->map->map_entry[sub_req->lpn].state!=0) {
						location = find_location(ssd,ssd->dram->map->map_entry[sub_req->lpn].pn);
						sub_req->location->channel = location->channel;
						sub_req->location->chip = location->chip;
						free(location);
					}else{
						if(++ssd->channelToken >= ssd->parameter->channel_number){
							ssd->channelToken = 0;
							if(++ssd->chipToken >= ssd->parameter->chip_channel[0])
								ssd->chipToken = 0;
						}
					}
				}
				break;
			}
			case 3:
			{
				break;
			}
		}

	}
	else                                                                          
	{   /***************************************************************************
		静态分配
		****************************************************************************/
		switch (ssd->parameter->static_allocation)
		{
			case 0:         //no striping static allocation
			{
				sub_req->location->channel=(sub_req->lpn/(plane_num*die_num*chip_num))%channel_num;
				sub_req->location->chip=sub_req->lpn%chip_num;
				sub_req->location->die=(sub_req->lpn/chip_num)%die_num;
				sub_req->location->plane=(sub_req->lpn/(die_num*chip_num))%plane_num;
				break;
			}
			case 1:
			{
				sub_req->location->channel=sub_req->lpn%channel_num;
				sub_req->location->chip=(sub_req->lpn/channel_num)%chip_num;
				sub_req->location->die=(sub_req->lpn/(chip_num*channel_num))%die_num;
				sub_req->location->plane=(sub_req->lpn/(die_num*chip_num*channel_num))%plane_num;
							
				break;
			}
			case 2:
			{
				sub_req->location->channel=sub_req->lpn%channel_num;
				sub_req->location->chip=(sub_req->lpn/(plane_num*channel_num))%chip_num;
				sub_req->location->die=(sub_req->lpn/(plane_num*chip_num*channel_num))%die_num;
				sub_req->location->plane=(sub_req->lpn/channel_num)%plane_num;
				break;
			}
			case 3:
			{
				sub_req->location->channel=sub_req->lpn%channel_num;
				sub_req->location->chip=(sub_req->lpn/(die_num*channel_num))%chip_num;
				sub_req->location->die=(sub_req->lpn/channel_num)%die_num;
				sub_req->location->plane=(sub_req->lpn/(die_num*chip_num*channel_num))%plane_num;
				break;
			}
			case 4:  
			{
				sub_req->location->channel=sub_req->lpn%channel_num;
				sub_req->location->chip=(sub_req->lpn/(plane_num*die_num*channel_num))%chip_num;
				sub_req->location->die=(sub_req->lpn/(plane_num*channel_num))%die_num;
				sub_req->location->plane=(sub_req->lpn/channel_num)%plane_num;
							
				break;
			}
			case 5:   
			{
				sub_req->location->channel=sub_req->lpn%channel_num;
				sub_req->location->chip=(sub_req->lpn/(plane_num*die_num*channel_num))%chip_num;
				sub_req->location->die=(sub_req->lpn/channel_num)%die_num;
				sub_req->location->plane=(sub_req->lpn/(die_num*channel_num))%plane_num;
							
				break;
			}
			
			default : return ERROR;
		
		}
		if (ssd->dram->map->map_entry[sub_req->lpn].state!=0)
		{                                                                              /*���д�ص���������߼�ҳ�����Ը���֮ǰ��д�ص����� ��Ҫ����������*/ 
			if ((sub_req->state&ssd->dram->map->map_entry[sub_req->lpn].state)!=ssd->dram->map->map_entry[sub_req->lpn].state)  
			{
				ssd->read_count++;
				ssd->update_read_count++;
				update=(struct sub_request *)malloc(sizeof(struct sub_request));
				alloc_assert(update,"update");
				memset(update,0, sizeof(struct sub_request));
				
				if(update==NULL)
				{
					return ERROR;
				}
				update->location=NULL;
				update->next_node=NULL;
				update->next_subs=NULL;
				update->update=NULL;						
				location = find_location(ssd,ssd->dram->map->map_entry[sub_req->lpn].pn);
				update->location=location;
				update->begin_time = ssd->current_time;
				update->current_state = SR_WAIT;
				update->current_time=MAX_INT64;
				update->next_state = SR_R_C_A_TRANSFER;
				update->next_state_predict_time=MAX_INT64;
				update->lpn = sub_req->lpn;
				update->state=((ssd->dram->map->map_entry[sub_req->lpn].state^sub_req->state)&0x7fffffff);
				update->size=size(update->state);
				update->ppn = ssd->dram->map->map_entry[sub_req->lpn].pn;
				update->operation = READ;
				
				sub_req->location->channel = update->location->channel;
				sub_req->location->chip = update->location->chip;
				
				if (ssd->channel_head[location->channel].subs_r_tail!=NULL)
				{
					ssd->channel_head[location->channel].subs_r_tail->next_node=update;
					ssd->channel_head[location->channel].subs_r_tail=update;
				} 
				else
				{
					ssd->channel_head[location->channel].subs_r_tail=update;
					ssd->channel_head[location->channel].subs_r_head=update;
				}
                ssd->channel_head[location->channel].read_queue_length++;
			}

			if (update!=NULL)
			{
				sub_req->update=update;

				sub_req->state=(sub_req->state|update->state);
				sub_req->size=size(sub_req->state);
			}

 		}
	}
	if ((ssd->parameter->allocation_scheme!=0)||(ssd->parameter->dynamic_allocation!=0))
	{
        //将sub插入到channel下的sub_w链表的尾部
		if (ssd->channel_head[sub_req->location->channel].subs_w_tail!=NULL)
		{
			ssd->channel_head[sub_req->location->channel].subs_w_tail->next_node=sub_req;
			ssd->channel_head[sub_req->location->channel].subs_w_tail=sub_req;
		} 
		else
		{
			ssd->channel_head[sub_req->location->channel].subs_w_tail=sub_req;
			ssd->channel_head[sub_req->location->channel].subs_w_head=sub_req;
		}
        ssd->channel_head[sub_req->location->channel].write_queue_length++;
	}
	return SUCCESS;					
}

Status allocate_buffer_sub_location(struct ssd_info * ssd ,struct sub_request *sub_req)
{
    struct sub_request * update=NULL;
    unsigned int channel_num=0,chip_num=0,die_num=0,plane_num=0;
    struct local *location=NULL;

    channel_num=ssd->parameter->channel_number;
    chip_num=ssd->parameter->chip_channel[0];
    die_num=ssd->parameter->die_chip;
    plane_num=ssd->parameter->plane_die;
    unsigned int full_page;
    if(ssd->parameter->subpage_page == 32){
        full_page = 0xffffffff;
    }
    else{
        full_page=~(0xffffffff<<(ssd->parameter->subpage_page));
    }

    if (ssd->parameter->allocation_scheme==0)          /*动态分配*/
    {
        /******************************************************************
        *在动态分配中，需要产生一个读请求，并且只有这个读请求完成之后才能进行这个页的写操作
        *******************************************************************/
        //数据为的lpn，并且映射表中存放了state
        if (sub_req->lpn != ssd->stripe.checkLpn && ssd->dram->map->map_entry[sub_req->lpn].state!=0)
        {
            //这个sub的state和这个sub上次存放在映射表中的state不一致
            if((sub_req->state&ssd->dram->map->map_entry[sub_req->lpn].state)!=ssd->dram->map->map_entry[sub_req->lpn].state)
            {
                ssd->read_count++;
                ssd->read1++;
                ssd->update_read_count++;
                //更新写的话，需要先读原位置上的旧数据后写新位置上的新数据
                update=(struct sub_request *)malloc(sizeof(struct sub_request));
                alloc_assert(update,"update");
                memset(update,0, sizeof(struct sub_request));

                if(update==NULL)
                {
                    return ERROR;
                }
                update->location=NULL;
                update->next_node=NULL;
                update->next_subs=NULL;
                update->update=NULL;
                location = find_location(ssd,ssd->dram->map->map_entry[sub_req->lpn].pn);
                update->location=location;
                update->begin_time = ssd->current_time;
                update->current_state = SR_WAIT;
                update->current_time=MAX_INT64;
                update->next_state = SR_R_C_A_TRANSFER;
                update->next_state_predict_time=MAX_INT64;
                update->lpn = sub_req->lpn;
                update->state=((ssd->dram->map->map_entry[sub_req->lpn].state^sub_req->state)&0x7fffffff);
                update->size=size(update->state);
                update->ppn = ssd->dram->map->map_entry[sub_req->lpn].pn;
                update->operation = READ;
                //把update读子请求插入到channel的sub_r的队尾
                if(1){
                    if (ssd->channel_head[location->channel].subs_r_tail!=NULL)
                    {
                        ssd->channel_head[location->channel].subs_r_tail->next_node=update;
                        ssd->channel_head[location->channel].subs_r_tail=update;
                    }
                    else
                    {
                        ssd->channel_head[location->channel].subs_r_tail=update;
                        ssd->channel_head[location->channel].subs_r_head=update;
                    }
                    ssd->channel_head[location->channel].read_queue_length++;
                }
            }
        }
        /***************************************
        *动态分配，case2的情况
        ****************************************/
        switch(ssd->parameter->dynamic_allocation)
        {
            case 0:
            {
                sub_req->location->channel=-1;
                sub_req->location->chip=-1;
                sub_req->location->die=-1;
                sub_req->location->plane=-1;
                sub_req->location->block=-1;
                sub_req->location->page=-1;

                if (ssd->subs_w_tail!=NULL)
                {
                    ssd->subs_w_tail->next_node=sub_req;
                    ssd->subs_w_tail=sub_req;
                }
                else
                {
                    ssd->subs_w_tail=sub_req;
                    ssd->subs_w_head=sub_req;
                }

                if (update!=NULL)
                {
                    sub_req->update=update;
                }

                break;
            }
            case 1:
            {

                sub_req->location->channel=sub_req->lpn%ssd->parameter->channel_number;
                sub_req->location->chip=-1;
                sub_req->location->die=-1;
                sub_req->location->plane=-1;
                sub_req->location->block=-1;
                sub_req->location->page=-1;

                if (update!=NULL)
                {
                    sub_req->update=update;
                }

                break;
            }
            case 2:
            {
                /* sub_req的location是动态分配且分配策略为2的时候，分配得到的*/
                sub_req->location->channel = ssd->channelToken;
                sub_req->location->chip = ssd->chipToken;

                sub_req->location->die=-1;
                sub_req->location->plane=-1;
                sub_req->location->block=-1;
                sub_req->location->page=-1;
                if (update!=NULL)
                {
                    //可是update读子请求读旧数据的话，旧数据是有ppn的啊，对应的有location为什么还要这样呢？
                    sub_req->update=update;
                    sub_req->location->channel = update->location->channel;
                    sub_req->location->chip = update->location->chip;
                }
                else
                {
                    if(sub_req->lpn == ssd->stripe.checkLpn){
                        if(ssd->trip2Page[sub_req->raidNUM].PChannel == -1){
                            if(++ssd->channelToken >= ssd->parameter->channel_number){
                                ssd->channelToken = 0;
                                if(++ssd->chipToken >= ssd->parameter->chip_channel[0])
                                    ssd->chipToken = 0;
                            }
                            ssd->trip2Page[sub_req->raidNUM].PChannel = sub_req->location->channel;
                        }else{
                            sub_req->location->channel = ssd->trip2Page[sub_req->raidNUM].PChannel;
                        }
                    }
                    else if(ssd->dram->map->map_entry[sub_req->lpn].state!=0) {
                        location = find_location(ssd,ssd->dram->map->map_entry[sub_req->lpn].pn);
                        sub_req->location->channel = location->channel;
                        sub_req->location->chip = location->chip;
                        free(location);
                    }else{
                        if(++ssd->channelToken >= ssd->parameter->channel_number){
                            ssd->channelToken = 0;
                            if(++ssd->chipToken >= ssd->parameter->chip_channel[0])
                                ssd->chipToken = 0;
                        }
                    }
                }
                break;
            }
            case 3:
            {
                break;
            }
        }

    }
    else
    {   /***************************************************************************
		静态分配
		****************************************************************************/
        switch (ssd->parameter->static_allocation)
        {
            case 0:         //no striping static allocation
            {
                sub_req->location->channel=(sub_req->lpn/(plane_num*die_num*chip_num))%channel_num;
                sub_req->location->chip=sub_req->lpn%chip_num;
                sub_req->location->die=(sub_req->lpn/chip_num)%die_num;
                sub_req->location->plane=(sub_req->lpn/(die_num*chip_num))%plane_num;
                break;
            }
            case 1:
            {
                sub_req->location->channel=sub_req->lpn%channel_num;
                sub_req->location->chip=(sub_req->lpn/channel_num)%chip_num;
                sub_req->location->die=(sub_req->lpn/(chip_num*channel_num))%die_num;
                sub_req->location->plane=(sub_req->lpn/(die_num*chip_num*channel_num))%plane_num;

                break;
            }
            case 2:
            {
                sub_req->location->channel=sub_req->lpn%channel_num;
                sub_req->location->chip=(sub_req->lpn/(plane_num*channel_num))%chip_num;
                sub_req->location->die=(sub_req->lpn/(plane_num*chip_num*channel_num))%die_num;
                sub_req->location->plane=(sub_req->lpn/channel_num)%plane_num;
                break;
            }
            case 3:
            {
                sub_req->location->channel=sub_req->lpn%channel_num;
                sub_req->location->chip=(sub_req->lpn/(die_num*channel_num))%chip_num;
                sub_req->location->die=(sub_req->lpn/channel_num)%die_num;
                sub_req->location->plane=(sub_req->lpn/(die_num*chip_num*channel_num))%plane_num;
                break;
            }
            case 4:
            {
                sub_req->location->channel=sub_req->lpn%channel_num;
                sub_req->location->chip=(sub_req->lpn/(plane_num*die_num*channel_num))%chip_num;
                sub_req->location->die=(sub_req->lpn/(plane_num*channel_num))%die_num;
                sub_req->location->plane=(sub_req->lpn/channel_num)%plane_num;

                break;
            }
            case 5:
            {
                sub_req->location->channel=sub_req->lpn%channel_num;
                sub_req->location->chip=(sub_req->lpn/(plane_num*die_num*channel_num))%chip_num;
                sub_req->location->die=(sub_req->lpn/channel_num)%die_num;
                sub_req->location->plane=(sub_req->lpn/(die_num*channel_num))%plane_num;

                break;
            }

            default : return ERROR;

        }
        if (ssd->dram->map->map_entry[sub_req->lpn].state!=0)
        {                                                                              /*���д�ص���������߼�ҳ�����Ը���֮ǰ��д�ص����� ��Ҫ����������*/
            if ((sub_req->state&ssd->dram->map->map_entry[sub_req->lpn].state)!=ssd->dram->map->map_entry[sub_req->lpn].state)
            {
                ssd->read_count++;
                ssd->update_read_count++;
                update=(struct sub_request *)malloc(sizeof(struct sub_request));
                alloc_assert(update,"update");
                memset(update,0, sizeof(struct sub_request));

                if(update==NULL)
                {
                    return ERROR;
                }
                update->location=NULL;
                update->next_node=NULL;
                update->next_subs=NULL;
                update->update=NULL;
                location = find_location(ssd,ssd->dram->map->map_entry[sub_req->lpn].pn);
                update->location=location;
                update->begin_time = ssd->current_time;
                update->current_state = SR_WAIT;
                update->current_time=MAX_INT64;
                update->next_state = SR_R_C_A_TRANSFER;
                update->next_state_predict_time=MAX_INT64;
                update->lpn = sub_req->lpn;
                update->state=((ssd->dram->map->map_entry[sub_req->lpn].state^sub_req->state)&0x7fffffff);
                update->size=size(update->state);
                update->ppn = ssd->dram->map->map_entry[sub_req->lpn].pn;
                update->operation = READ;

                sub_req->location->channel = update->location->channel;
                sub_req->location->chip = update->location->chip;

                if (ssd->channel_head[location->channel].subs_r_tail!=NULL)
                {
                    ssd->channel_head[location->channel].subs_r_tail->next_node=update;
                    ssd->channel_head[location->channel].subs_r_tail=update;
                }
                else
                {
                    ssd->channel_head[location->channel].subs_r_tail=update;
                    ssd->channel_head[location->channel].subs_r_head=update;
                }
                ssd->channel_head[location->channel].read_queue_length++;
            }

            if (update!=NULL)
            {
                sub_req->update=update;

                sub_req->state=(sub_req->state|update->state);
                sub_req->size=size(sub_req->state);
            }

        }
    }
//    if ((ssd->parameter->allocation_scheme!=0)||(ssd->parameter->dynamic_allocation!=0))
//    {
//        //将sub插入到channel下的sub_w链表的尾部
//        if (ssd->channel_head[sub_req->location->channel].subs_w_tail!=NULL)
//        {
//            ssd->channel_head[sub_req->location->channel].subs_w_tail->next_node=sub_req;
//            ssd->channel_head[sub_req->location->channel].subs_w_tail=sub_req;
//        }
//        else
//        {
//            ssd->channel_head[sub_req->location->channel].subs_w_tail=sub_req;
//            ssd->channel_head[sub_req->location->channel].subs_w_head=sub_req;
//        }
//    }
    return SUCCESS;
}


Status init_allocate_location(struct ssd_info * ssd ,struct sub_request *sub_req)
{
    struct sub_request * update=NULL;
    unsigned int channel_num=0,chip_num=0,die_num=0,plane_num=0;
    struct local *location=NULL;

    channel_num=ssd->parameter->channel_number;
    chip_num=ssd->parameter->chip_channel[0];
    die_num=ssd->parameter->die_chip;
    plane_num=ssd->parameter->plane_die;

    if (ssd->parameter->allocation_scheme==0)          /*动态分配*/
    {
        /******************************************************************
        *在动态分配中，需要产生一个读请求，并且只有这个读请求完成之后才能进行这个页的写操作
        *******************************************************************/

        /***************************************
        *动态分配，case2的情况
        ****************************************/
        switch(ssd->parameter->dynamic_allocation)
        {
            case 2:
            {
                /* sub_req的location是动态分配且分配策略为2的时候，分配得到的*/
                sub_req->location->channel = ssd->channelToken;
                sub_req->location->chip = ssd->chipToken;

                sub_req->location->die=-1;
                sub_req->location->plane=-1;
                sub_req->location->block=-1;
                sub_req->location->page=-1;
                if (update!=NULL)
                {
                    //可是update读子请求读旧数据的话，旧数据是有ppn的啊，对应的有location为什么还要这样呢？
                    sub_req->update=update;
                    sub_req->location->channel = update->location->channel;
                    sub_req->location->chip = update->location->chip;
                }
                else
                {
                    if(sub_req->lpn == ssd->stripe.checkLpn){
                        if(ssd->trip2Page[sub_req->raidNUM].PChannel == -1){
                            if(++ssd->channelToken >= ssd->parameter->channel_number){
                                ssd->channelToken = 0;
                                if(++ssd->chipToken >= ssd->parameter->chip_channel[0])
                                    ssd->chipToken = 0;
                            }
                            ssd->trip2Page[sub_req->raidNUM].PChannel = sub_req->location->channel;
                        }else{
                            sub_req->location->channel = ssd->trip2Page[sub_req->raidNUM].PChannel;
                        }
                    }else if(ssd->dram->map->map_entry[sub_req->lpn].state!=0) {
                        location = find_location(ssd,ssd->dram->map->map_entry[sub_req->lpn].pn);
                        sub_req->location->channel = location->channel;
                        sub_req->location->chip = location->chip;
                        free(location);
                    }else{
                        if(++ssd->channelToken >= ssd->parameter->channel_number){
                            ssd->channelToken = 0;
                            if(++ssd->chipToken >= ssd->parameter->chip_channel[0])
                                ssd->chipToken = 0;
                        }
                    }
                }
                break;
            }
        }

    }
    return SUCCESS;
}

Status add_sub2channel(struct ssd_info *ssd,struct sub_request * sub,struct request * req)
{
    //将 sub 插入到 req 的子请求链表（队列）的开头
//    sub->req = req;
//    sub->next_subs = req->subs;
//    req->subs = sub;
//    req->now++;

//    if(ssd->channel_head[sub->location->channel].write_queue_length >= 1024){
//        abort();
//    }
    if ((ssd->parameter->allocation_scheme!=0)||(ssd->parameter->dynamic_allocation!=0))
    {
        //将sub插入到channel下的sub_w链表的尾部
        if (ssd->channel_head[sub->location->channel].subs_w_tail!=NULL)
        {
            ssd->channel_head[sub->location->channel].subs_w_tail->next_node=sub;
            ssd->channel_head[sub->location->channel].subs_w_tail=sub;
        }
        else
        {
            ssd->channel_head[sub->location->channel].subs_w_tail=sub;
            ssd->channel_head[sub->location->channel].subs_w_head=sub;
        }
        ssd->channel_head[sub->location->channel].write_queue_length++;
        sub->channel_queue_pos = ssd->channel_head[sub->location->channel].write_queue_length;
        int a1 = ssd->channel_head[0].write_queue_length;
        int a2 = ssd->channel_head[1].write_queue_length;
        int a3 = ssd->channel_head[2].write_queue_length;
        int a4 = ssd->channel_head[3].write_queue_length;
    }

    return SUCCESS;
}

Status active_pop_add_sub2channel(struct ssd_info *ssd,struct sub_request * sub,struct request * req,unsigned int lpn,unsigned int state,int sub_size)
{
    if ((ssd->parameter->allocation_scheme!=0)||(ssd->parameter->dynamic_allocation!=0))
    {
        //将sub插入到channel下的sub_w链表的尾部
        if (ssd->channel_head[sub->location->channel].subs_w_tail!=NULL)
        {
            ssd->channel_head[sub->location->channel].subs_w_tail->next_node=sub;
            ssd->channel_head[sub->location->channel].subs_w_tail=sub;
        }
        else
        {
            ssd->channel_head[sub->location->channel].subs_w_tail=sub;
            ssd->channel_head[sub->location->channel].subs_w_head=sub;
        }
        ssd->channel_head[sub->location->channel].write_queue_length++;
    }

    return SUCCESS;
}


Status add_sub2channel_for_raid(struct ssd_info *ssd,struct sub_request * init_sub,struct request * req,unsigned int lpn,unsigned int state,int sub_size)
{
    struct sub_request * update=NULL;
    struct local *location=NULL;

    unsigned int i, j;
    struct request *nowreq = ssd->request_queue;
    struct sub_request *sub, *psub;
    int channel = -1;
    int raidid = 0;
    while(nowreq && channel == -1){
        if(nowreq->operation == WRITE){
            sub	= nowreq->subs;
            while(sub && channel == -1){
                if(sub->lpn == lpn && sub->current_state==SR_WAIT){
                    sub->state |= state;
                    sub->size = size(sub->state);
                    req->all--;
                    req->MergeFlag = 1;
                    channel = sub->location->channel;
                    raidid = sub->raidNUM;

                }
                sub = sub->next_subs;
            }
        }
        nowreq = nowreq->next_node;
    }
    sub	= ssd->raidReq->subs;
    while(sub && channel == -1){
        if(sub->lpn == lpn && sub->current_state==SR_WAIT){
            sub->state |= state;
            sub->size = size(sub->state);
            req->all--;
            req->MergeFlag = 1;
            channel = sub->location->channel;
            raidid = sub->raidNUM;
        }
        sub = sub->next_subs;
    }

    if(channel != -1){
        sub = create_recovery_sub_write(ssd, channel, 0 ,0 ,0 ,0 ,0);
        ssd->raidReq->subs = ssd->raidReq->subs->next_subs;
        sub->next_subs = req->subs;
        req->subs = sub;
        sub->raidNUM = raidid;
        return -1;
    }

    sub->next_node=NULL;
    sub->next_subs=NULL;
    sub->update=NULL;
    sub->readXorFlag = 0;
    sub->target_page_type = 0;
    sub->readRaidLpn = 0;
    sub->req = req;
    //将当前的读子请求 sub 添加到读请求 req 的子请求队列中
    if(req!=NULL && req->operation == READ)
    {
        sub->next_subs = req->subs;
        req->subs = sub;
    }

    //将 sub 插入到 req 的子请求链表（队列）的开头
    sub->next_subs = req->subs;
    req->subs = sub;
    req->now++;
    sub->ppn=0;
    sub->operation = WRITE;
    sub->current_state=SR_WAIT;
    sub->current_time=ssd->current_time;
    sub->begin_time=ssd->current_time;
    sub->write_freqently = 0;

    if (ssd->parameter->allocation_scheme==0)          /*动态分配*/
    {
        //数据为的lpn，并且映射表中存放了state
        if (sub->lpn != ssd->stripe.checkLpn && ssd->dram->map->map_entry[sub->lpn].state!=0) {
            //这个sub的state和这个sub上次存放在映射表中的state不一致
            if ((sub->state & ssd->dram->map->map_entry[sub->lpn].state) != ssd->dram->map->map_entry[sub->lpn].state) {
                ssd->read_count++;
                ssd->read1++;
                ssd->update_read_count++;
                //更新写的话，需要先读原位置上的旧数据后写新位置上的新数据
                update = (struct sub_request *) malloc(sizeof(struct sub_request));
                alloc_assert(update, "update");
                memset(update, 0, sizeof(struct sub_request));

                if (update == NULL) {
                    return ERROR;
                }
                update->location = NULL;
                update->next_node = NULL;
                update->next_subs = NULL;
                update->update = NULL;
                location = find_location(ssd, ssd->dram->map->map_entry[sub->lpn].pn);
                update->location = location;
                update->begin_time = ssd->current_time;
                update->current_state = SR_WAIT;
                update->current_time = MAX_INT64;
                update->next_state = SR_R_C_A_TRANSFER;
                update->next_state_predict_time = MAX_INT64;
                update->lpn = sub->lpn;
                update->state = ((ssd->dram->map->map_entry[sub->lpn].state ^ sub->state) & 0x7fffffff);
                update->size = size(update->state);
                update->ppn = ssd->dram->map->map_entry[sub->lpn].pn;
                update->operation = READ;
                //把update读子请求插入到channel的sub_r的队尾
                if (1) {
                    if (ssd->channel_head[location->channel].subs_r_tail != NULL) {
                        ssd->channel_head[location->channel].subs_r_tail->next_node = update;
                        ssd->channel_head[location->channel].subs_r_tail = update;
                    } else {
                        ssd->channel_head[location->channel].subs_r_tail = update;
                        ssd->channel_head[location->channel].subs_r_head = update;
                    }
                    ssd->channel_head[location->channel].read_queue_length++;
                }
            }
        }
    }
    if ((ssd->parameter->allocation_scheme!=0)||(ssd->parameter->dynamic_allocation!=0))
    {
        //将sub插入到channel下的sub_w链表的尾部
        if (ssd->channel_head[sub->location->channel].subs_w_tail!=NULL)
        {
            ssd->channel_head[sub->location->channel].subs_w_tail->next_node=sub;
            ssd->channel_head[sub->location->channel].subs_w_tail=sub;
        }
        else
        {
            ssd->channel_head[sub->location->channel].subs_w_tail=sub;
            ssd->channel_head[sub->location->channel].subs_w_head=sub;
        }
        ssd->channel_head[sub->location->channel].write_queue_length++;
    }
    return SUCCESS;
}


/*******************************************************************************
*insert2buffer���������ר��Ϊд�������������������buffer_management�б����á�
********************************************************************************/
struct ssd_info * insert2buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write)
{
	int write_back_count,flag=0;                                                             /*flag��ʾΪд���������ڿռ��Ƿ���ɣ�0��ʾ��Ҫ��һ���ڣ�1��ʾ�Ѿ��ڿ�*/
	unsigned int i,lsn,hit_flag,add_flag,sector_count,active_region_flag=0,free_sector=0;
	struct buffer_group *buffer_node=NULL,*pt,*new_node=NULL,key;
	struct sub_request *sub_req=NULL,*update=NULL,*pop_sub=NULL;

	unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0,sub_req_channel=0,sub_req_first=0;

	#ifdef DEBUG
	printf("enter insert2buffer,  current time:%lld, lpn:%d, state:%d,\n",ssd->current_time,lpn,state);
	#endif
	state = ~(0xffffffff<<(ssd->parameter->subpage_page));
	sector_count=size(state);                                                                /*��Ҫд��buffer��sector����*/
	key.group=lpn;
	//buffer_node= (struct buffer_group*)avlTreeFind(ssd->dram->buffer, (TREE_NODE *)&key);    /*��ƽ���������Ѱ��buffer node*/
    long cache_loc = 0;
    long *loc = &cache_loc;
	buffer_node= (struct buffer_group*)hash_find(ssd,ssd->dram->buffer, (HASH_NODE *)&key,buffer_sub->raidNUM, loc);
	/************************************************************************************************
	*û�����С�
	*��һ���������lpn�ж�����ҳ��Ҫд��buffer��ȥ����д�ص�lsn��Ϊ��lpn�ڳ�λ�ã�
	*���ȼ�Ҫ�����free sector����ʾ���ж��ٿ���ֱ��д��buffer�ڵ㣩��
	*���free_sector>=sector_count�����ж���Ŀռ乻lpn������д������Ҫ����д������
	*����û�ж���Ŀռ乩lpn������д����ʱ��Ҫ�ͷ�һ���ֿռ䣬����д�����󡣾�Ҫcreat_sub_request()
	*************************************************************************************************/
	if(buffer_node==NULL || buffer_node->group == ssd->stripe.checkLpn)
	{
		free_sector=ssd->dram->buffer->max_buffer_sector-ssd->dram->buffer->buffer_sector_count;  
		
		if(free_sector>=sector_count)
		{
			//ssd->dram->buffer->write_hit=ssd->dram->buffer->write_hit + size(state);
			ssd->dram->buffer->write_free++;
			flag=1;    
		}
		if(flag==0)     
		{
			write_back_count=sector_count-free_sector;
			//ssd->dram->buffer->write_miss_hit=ssd->dram->buffer->write_miss_hit + size(state);
			ssd->dram->buffer->write_miss_hit++;
			
			while(write_back_count>0)
			{
                sub_req=NULL;
                pop_sub = ssd->dram->buffer->buffer_tail->sub;
				sub_req_state=ssd->dram->buffer->buffer_tail->stored;
				sub_req_size=size(ssd->dram->buffer->buffer_tail->stored);
				sub_req_lpn=ssd->dram->buffer->buffer_tail->group;
//                sub_req_channel = ssd->dram->buffer->buffer_tail->channel;
                sub_req_first = ssd->dram->buffer->buffer_tail->first_write;
				req->all++;
//				if(ssd->dram->map->map_entry[sub_req_lpn].state == 0 && ssd->parameter->allocation_scheme==0 && ssd->parameter->dynamic_allocation == 2)
				if(sub_req_first)
                {
                    if(++ssd->complete_sub_count == STRIPENUM-1)
                    {
                        XOR_process(ssd, 16);
                        ssd->complete_sub_count = 0;
                    }
//                    if(pop_sub->check_sub)
//                    {
//                        req = ssd->raidReq;
//                    }
//                      add_sub2channel(ssd,pop_sub,req,sub_req_lpn,sub_req_state,sub_req_size);
//                    creat_buffer_sub_request_for_raid(ssd,sub_req_lpn, sub_req_state, req,pop_sub);
//                    creat_sub_write_request_for_raid(ssd,sub_req_lpn, sub_req_state, req, ~(0xffffffff<<(ssd->parameter->subpage_page)));
//                    first_sub=creat_buffer_sub_request(ssd,sub_req_lpn,sub_req_size,sub_req_state,req,WRITE,0, ssd->page2Trip[sub_req_lpn],sub_req_channel);
				}else {
                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;
                    if(ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1){
                        ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                    }else{
                        ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                    }
//                    sub_req=distribute_sub_request(ssd,WRITE,req,pop_sub);
//                    sub_req=creat_sub_request(ssd,sub_req_lpn,sub_req_size,sub_req_state,req,WRITE,0, ssd->page2Trip[sub_req_lpn]);
//                    add_sub2channel(ssd,pop_sub,req,sub_req_lpn,sub_req_state,sub_req_size);
				}
				/**********************************************************************************
				*req��Ϊ�գ���ʾ���insert2buffer��������buffer_management�е��ã�������request����
				*reqΪ�գ���ʾ�����������process�����д���һ�Զ�ӳ���ϵ�Ķ���ʱ����Ҫ���������
				*�����ݼӵ�buffer�У�����ܲ���ʵʱ��д�ز�������Ҫ�����ʵʱ��д�ز��������������
				*������������������
				***********************************************************************************/
				if(req!=NULL)                                             
				{
				}
				else    
				{
					sub_req->next_subs=sub->next_subs;
					sub->next_subs=sub_req;
				}
                
				/*********************************************************************
				*д������뵽��ƽ�����������ʱ��Ҫ�޸�dram��buffer_sector_count��
				*ά��ƽ�����������avlTreeDel()��AVL_TREENODE_FREE()������ά��LRU�㷨��
				**********************************************************************/
				ssd->dram->buffer->buffer_sector_count=ssd->dram->buffer->buffer_sector_count-size(sub_req_state);
				pt = ssd->dram->buffer->buffer_tail;
				//avlTreeDel(ssd->dram->buffer, (TREE_NODE *) pt);
				hash_del(ssd,ssd->dram->buffer, (HASH_NODE *) pt,pop_sub->raidNUM);
				if(ssd->dram->buffer->buffer_head->LRU_link_next == NULL){
					ssd->dram->buffer->buffer_head = NULL;
					ssd->dram->buffer->buffer_tail = NULL;
				}else{
					ssd->dram->buffer->buffer_tail=ssd->dram->buffer->buffer_tail->LRU_link_pre;
					ssd->dram->buffer->buffer_tail->LRU_link_next=NULL;
				}
				pt->LRU_link_next=NULL;
				pt->LRU_link_pre=NULL;
				//AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *) pt);
				hash_node_free(ssd->dram->buffer, (HASH_NODE *) pt);
				pt = NULL;
				
				write_back_count=write_back_count-size(sub_req_state);                            /*��Ϊ������ʵʱд�ز�������Ҫ������д�ز�����������*/
			}
		}
		
		/******************************************************************************
		*����һ��buffer node���������ҳ������ֱ�ֵ��������Ա�����ӵ����׺Ͷ�������
		*******************************************************************************/
		new_node=NULL;
		new_node=(struct buffer_group *)malloc(sizeof(struct buffer_group));
		alloc_assert(new_node,"buffer_group_node");
		memset(new_node,0, sizeof(struct buffer_group));

        new_node->first_write = first_write;
        new_node->sub = buffer_sub;
		new_node->read_count = 0;
		new_node->group=lpn;
		new_node->stored=state;
		new_node->dirty_clean=state;
		new_node->LRU_link_pre = NULL;
		new_node->LRU_link_next=ssd->dram->buffer->buffer_head;
		if(ssd->dram->buffer->buffer_head != NULL){
			ssd->dram->buffer->buffer_head->LRU_link_pre=new_node;
		}else{
			ssd->dram->buffer->buffer_tail = new_node;
		}
		ssd->dram->buffer->buffer_head=new_node;
		new_node->LRU_link_pre=NULL;
		//avlTreeAdd(ssd->dram->buffer, (TREE_NODE *) new_node);
		hash_add(ssd->dram->buffer, (HASH_NODE *) new_node);
		ssd->dram->buffer->buffer_sector_count += sector_count;
		
	}
	/****************************************************************************************
	*��buffer�����е����
	*��Ȼ�����ˣ��������е�ֻ��lpn���п���������д����ֻ����Ҫдlpn��һpage��ĳ����sub_page
	*��ʱ����Ҫ��һ�����ж�
	*****************************************************************************************/
	else
	{
		ssd->dram->buffer->write_hit++;
		unsigned int unHitBit = 0;
		buffer_node->read_count = 0;
		for(i=0;i<ssd->parameter->subpage_page;i++)
		{
			/*************************************************************
			*�ж�state��iλ�ǲ���1
			*�����жϵ�i��sector�Ƿ����buffer�У�1��ʾ���ڣ�0��ʾ�����ڡ�
			**************************************************************/
			if((state>>i)%2!=0)
			{
				lsn=lpn*ssd->parameter->subpage_page+i;
				hit_flag=0;
				hit_flag=(buffer_node->stored)&(0x00000001<<i);
				
				if(hit_flag!=0)				                                          /*�����ˣ���Ҫ���ýڵ��Ƶ�buffer�Ķ��ף����ҽ����е�lsn���б��*/
				{	
					active_region_flag=1;                                             /*������¼�����buffer node�е�lsn�Ƿ����У����ں������ֵ���ж�*/

					if(req!=NULL)
					{
						if(ssd->dram->buffer->buffer_head != buffer_node)     
						{		
							buf_node *preNode  = buffer_node->LRU_link_pre;
							if(ssd->dram->buffer->buffer_tail==buffer_node)
							{				
								ssd->dram->buffer->buffer_tail=buffer_node->LRU_link_pre;
								buffer_node->LRU_link_pre->LRU_link_next=NULL;					
							}				
							else if(buffer_node != ssd->dram->buffer->buffer_head)
							{					
								buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;				
								buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
							}
							if(1){
							//if(size(buffer_node->stored) >= (ssd->parameter->subpage_page - 1)){
								buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;	
								ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
								buffer_node->LRU_link_pre=NULL;				
								ssd->dram->buffer->buffer_head=buffer_node;
							}else{
								if(preNode == ssd->dram->buffer->buffer_head){
									buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;	
									ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
									buffer_node->LRU_link_pre=NULL;				
									ssd->dram->buffer->buffer_head=buffer_node;
								}else{
									buffer_node->LRU_link_pre = preNode->LRU_link_pre;
									buffer_node->LRU_link_next = preNode;

									buffer_node->LRU_link_pre->LRU_link_next = buffer_node;
									buffer_node->LRU_link_next->LRU_link_pre = buffer_node;
								}
							}
												
						}					
						//ssd->dram->buffer->write_hit++;
						req->complete_lsn_count++;                                        /*�ؼ� ����buffer������ʱ ����req->complete_lsn_count++��ʾ��buffer��д�����ݡ�*/					
					}
					else
					{
					}				
				}			
				else                 			
				{
					/************************************************************************************************************
					*��lsnû�����У����ǽڵ���buffer�У���Ҫ�����lsn�ӵ�buffer�Ķ�Ӧ�ڵ���
					*��buffer��ĩ����һ���ڵ㣬��һ���Ѿ�д�ص�lsn�ӽڵ���ɾ��(����ҵ��Ļ�)����������ڵ��״̬��ͬʱ������µ�
					*lsn�ӵ���Ӧ��buffer�ڵ��У��ýڵ������bufferͷ�����ڵĻ��������Ƶ�ͷ�������û���ҵ��Ѿ�д�ص�lsn����buffer
					*�ڵ���һ��group����д�أ�����������������������ϡ�������ǰ����һ��channel�ϡ�
					*��һ��:��buffer��β���Ѿ�д�صĽڵ�ɾ��һ����Ϊ�µ�lsn�ڳ��ռ䣬������Ҫ�޸Ķ�βĳ�ڵ��stored״̬���ﻹ��Ҫ
					*       ���ӣ���û�п���֮��ɾ����lsnʱ����Ҫ�����µ�д������д��LRU���Ľڵ㡣
					*�ڶ���:���µ�lsn�ӵ�������buffer�ڵ��С�
					*************************************************************************************************************/	
					unHitBit |= (0x00000001 << i);
					//ssd->dram->buffer->write_miss_hit++;
					
					if(ssd->dram->buffer->buffer_sector_count>=ssd->dram->buffer->max_buffer_sector)
					{
						if (buffer_node==ssd->dram->buffer->buffer_tail)                  /*������еĽڵ���buffer�����һ���ڵ㣬������������ڵ�*/
						{
							pt = ssd->dram->buffer->buffer_tail->LRU_link_pre;
							ssd->dram->buffer->buffer_tail->LRU_link_pre=pt->LRU_link_pre;
							ssd->dram->buffer->buffer_tail->LRU_link_pre->LRU_link_next=ssd->dram->buffer->buffer_tail;
							ssd->dram->buffer->buffer_tail->LRU_link_next=pt;
							pt->LRU_link_next=NULL;
							pt->LRU_link_pre=ssd->dram->buffer->buffer_tail;
							ssd->dram->buffer->buffer_tail=pt;
							
						}
						sub_req=NULL;
                        struct sub_request *sub = ssd->dram->buffer->buffer_tail->sub;
						sub_req_state=ssd->dram->buffer->buffer_tail->stored; 
						sub_req_size=size(ssd->dram->buffer->buffer_tail->stored);
						sub_req_lpn=ssd->dram->buffer->buffer_tail->group;
                        sub_req_channel=ssd->dram->buffer->buffer_tail->channel;
						req->all++;
						if(ssd->dram->map->map_entry[sub_req_lpn].state == 0 && ssd->parameter->allocation_scheme==0 && ssd->parameter->dynamic_allocation == 2){
							creat_sub_write_request_for_raid(ssd,sub_req_lpn, sub_req_state, req, ~(0xffffffff<<(ssd->parameter->subpage_page)));
						}else {
							
							ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;
							
							if(ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1){
								ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
							}else{
								ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
							}
							
							sub_req=creat_sub_request(ssd,sub_req_lpn,sub_req_size,sub_req_state,req,WRITE,0, ssd->page2Trip[sub_req_lpn]);
						}
						if(req!=NULL)           
						{
							
						}
						else if(req==NULL)   
						{
							sub_req->next_subs=sub->next_subs;
							sub->next_subs=sub_req;
						}

						ssd->dram->buffer->buffer_sector_count=ssd->dram->buffer->buffer_sector_count-size(sub_req_state);
						pt = ssd->dram->buffer->buffer_tail;	
						//avlTreeDel(ssd->dram->buffer, (TREE_NODE *) pt);
						hash_del(ssd,ssd->dram->buffer, (HASH_NODE *) pt,sub->raidNUM);
							
						/************************************************************************/
						/* ��:  ������������buffer�Ľڵ㲻Ӧ����ɾ����						*/
						/*			��ȵ�д����֮�����ɾ��									*/
						/************************************************************************/
						if(ssd->dram->buffer->buffer_head->LRU_link_next == NULL)
						{
							ssd->dram->buffer->buffer_head = NULL;
							ssd->dram->buffer->buffer_tail = NULL;
						}else{
							ssd->dram->buffer->buffer_tail=ssd->dram->buffer->buffer_tail->LRU_link_pre;
							ssd->dram->buffer->buffer_tail->LRU_link_next=NULL;
						}
						pt->LRU_link_next=NULL;
						pt->LRU_link_pre=NULL;
						//AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *) pt);
						hash_node_free(ssd->dram->buffer, (HASH_NODE *) pt);
						ssd->dram->buffer->write_miss_hit++;
						pt = NULL;	
					}

					                                                                     /*�ڶ���:���µ�lsn�ӵ�������buffer�ڵ���*/	
					add_flag=0x00000001<<(lsn%ssd->parameter->subpage_page);
					
					if(ssd->dram->buffer->buffer_head!=buffer_node)                      /*�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ����*/
					{				
						buf_node *preNode  = buffer_node->LRU_link_pre;
						if(ssd->dram->buffer->buffer_tail==buffer_node)
						{				
							ssd->dram->buffer->buffer_tail=buffer_node->LRU_link_pre;
							buffer_node->LRU_link_pre->LRU_link_next=NULL;					
						}				
						else if(buffer_node != ssd->dram->buffer->buffer_head)
						{					
							buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;				
							buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
						}
						if(1){
						//if(size(buffer_node->stored) >= (ssd->parameter->subpage_page - 1)){
							buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;	
							ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
							buffer_node->LRU_link_pre=NULL;				
							ssd->dram->buffer->buffer_head=buffer_node;
						}else{
							if(preNode == ssd->dram->buffer->buffer_head){
								buffer_node->LRU_link_next=ssd->dram->buffer->buffer_head;	
								ssd->dram->buffer->buffer_head->LRU_link_pre=buffer_node;
								buffer_node->LRU_link_pre=NULL;				
								ssd->dram->buffer->buffer_head=buffer_node;
							}else{
								buffer_node->LRU_link_pre = preNode->LRU_link_pre;
								buffer_node->LRU_link_next = preNode;

								buffer_node->LRU_link_pre->LRU_link_next = buffer_node;
								buffer_node->LRU_link_next->LRU_link_pre = buffer_node;
							}
						}						
					}					
					buffer_node->stored=buffer_node->stored|add_flag;		
					buffer_node->dirty_clean=buffer_node->dirty_clean|add_flag;	
					ssd->dram->buffer->buffer_sector_count++;
				}			

			}
		}
		
		
	}

	return ssd;
}

void active_pop(struct ssd_info *ssd,int ssd_buffer_num,struct request *req,int count_flag){
    unsigned free_sector = 0;
    int flag = 0,write_back_count = 0;
    struct buffer_group *pt,*pop_node;
    struct sub_request *sub_req=NULL,*update=NULL,*pop_sub=NULL;
    unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0,sub_req_channel=0,sub_req_first=0;
    double proportion;


    switch(ssd_buffer_num){
        case 0:
//            proportion = (double)ssd->dram->SSD1_buffer->backup_capacity / ssd->dram->SSD1_buffer->current_max_sector;
//            if(proportion <= 0.1) {
                free_sector = ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
                if (ssd->dram->SSD1_buffer->extra_buffer > 0) {
                    for(int i = 0;i < 1;i++) {
                        if (ssd->dram->SSD1_buffer->buffer_tail != NULL) {
                            sub_req = NULL;
                            pop_sub = ssd->dram->SSD1_buffer->buffer_tail->sub;
                            sub_req_state = ssd->dram->SSD1_buffer->buffer_tail->stored;
                            sub_req_size = size(ssd->dram->SSD1_buffer->buffer_tail->stored);
                            sub_req_lpn = ssd->dram->SSD1_buffer->buffer_tail->group;
                            sub_req_first = ssd->dram->SSD1_buffer->buffer_tail->first_write;
                            pop_sub->pop_patch = -2;
                            pop_sub->req = NULL;
                            pop_sub->next_subs = NULL;
                            if (sub_req_first) {
                                active_pop_add_sub2channel(ssd, pop_sub, req, sub_req_lpn, sub_req_state, sub_req_size);
                            } else {
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;
                                if (ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1) {
                                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                                } else {
                                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                                }
                                active_pop_add_sub2channel(ssd, pop_sub, req, sub_req_lpn, sub_req_state, sub_req_size);
                            }

                            if(ssd->dram->SSD1_buffer->extra_buffer > 0){
                                if(size(sub_req_state) <= ssd->dram->SSD1_buffer->extra_buffer){
                                    ssd->dram->SSD1_buffer->backup_sector_count -= size(sub_req_state);
                                    ssd->dram->SSD1_buffer->extra_buffer -= size(sub_req_state);
                                }else{
                                    unsigned int pop_sector = 0;
                                    ssd->dram->SSD1_buffer->backup_sector_count -= ssd->dram->SSD1_buffer->extra_buffer;
                                    pop_sector = size(sub_req_state) - ssd->dram->SSD1_buffer->extra_buffer;
                                    ssd->dram->SSD1_buffer->buffer_sector_count =
                                            ssd->dram->SSD1_buffer->buffer_sector_count - pop_sector;
                                    ssd->dram->SSD1_buffer->extra_buffer = 0;
                                }
                            }else{
                                ssd->dram->SSD1_buffer->buffer_sector_count =
                                        ssd->dram->SSD1_buffer->buffer_sector_count - size(sub_req_state);
                            }
//                            ssd->dram->SSD1_buffer->buffer_sector_count =
//                                    ssd->dram->SSD1_buffer->buffer_sector_count - size(sub_req_state);
//                        ssd->dram->SSD1_buffer->current_max_sector -= size(sub_req_state);
//                        ssd->dram->SSD1_buffer->backup_capacity += size(sub_req_state);

                            pt = ssd->dram->SSD1_buffer->buffer_tail;

                            hash_del(ssd, ssd->dram->SSD1_buffer, (HASH_NODE *) pt, pop_sub->raidNUM);
                            if (ssd->dram->SSD1_buffer->buffer_head->LRU_link_next == NULL) {
                                ssd->dram->SSD1_buffer->buffer_head = NULL;
                                ssd->dram->SSD1_buffer->buffer_tail = NULL;
                            } else {
                                ssd->dram->SSD1_buffer->buffer_tail = ssd->dram->SSD1_buffer->buffer_tail->LRU_link_pre;
                                ssd->dram->SSD1_buffer->buffer_tail->LRU_link_next = NULL;
                            }
                            pt->LRU_link_next = NULL;
                            pt->LRU_link_pre = NULL;

                            hash_node_free(ssd->dram->SSD1_buffer, (HASH_NODE *) pt);
                            pt = NULL;

//                        ssd->dram->buffer_free_sectors[0].free_sector =
//                                ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
                        } else {
                            break;
                        }
                    }
                }
//            }
            break;

        case 1:
//            proportion = (double)ssd->dram->SSD2_buffer->backup_capacity / ssd->dram->SSD2_buffer->current_max_sector;
//            if(proportion <= 0.1) {
                free_sector = ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
                if (ssd->dram->SSD2_buffer->extra_buffer > 0) {
                    for (int i = 0; i < 1; i++) {
                        if (ssd->dram->SSD2_buffer->buffer_tail != NULL) {
                            sub_req = NULL;
                            pop_sub = ssd->dram->SSD2_buffer->buffer_tail->sub;
                            sub_req_state = ssd->dram->SSD2_buffer->buffer_tail->stored;
                            sub_req_size = size(ssd->dram->SSD2_buffer->buffer_tail->stored);
                            sub_req_lpn = ssd->dram->SSD2_buffer->buffer_tail->group;
                            sub_req_first = ssd->dram->SSD2_buffer->buffer_tail->first_write;
                            pop_sub->pop_patch = -2;
                            pop_sub->req = NULL;
                            pop_sub->next_subs = NULL;
                            if (sub_req_first) {
                                active_pop_add_sub2channel(ssd, pop_sub, req, sub_req_lpn, sub_req_state, sub_req_size);
                            } else {
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;
                                if (ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1) {
                                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                                } else {
                                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                                }
                                active_pop_add_sub2channel(ssd, pop_sub, req, sub_req_lpn, sub_req_state, sub_req_size);
                            }

                            if(ssd->dram->SSD2_buffer->extra_buffer > 0){
                                if(size(sub_req_state) <= ssd->dram->SSD2_buffer->extra_buffer){
                                    ssd->dram->SSD2_buffer->backup_sector_count -= size(sub_req_state);
                                    ssd->dram->SSD2_buffer->extra_buffer -= size(sub_req_state);
                                }else{
                                    unsigned int pop_sector = 0;
                                    ssd->dram->SSD2_buffer->backup_sector_count -= ssd->dram->SSD2_buffer->extra_buffer;
                                    pop_sector = size(sub_req_state) - ssd->dram->SSD2_buffer->extra_buffer;
                                    ssd->dram->SSD2_buffer->buffer_sector_count =
                                            ssd->dram->SSD2_buffer->buffer_sector_count - pop_sector;
                                    ssd->dram->SSD2_buffer->extra_buffer = 0;
                                }
                            }else{
                                ssd->dram->SSD2_buffer->buffer_sector_count =
                                        ssd->dram->SSD2_buffer->buffer_sector_count - size(sub_req_state);
                            }
//                            ssd->dram->SSD2_buffer->buffer_sector_count =
//                                    ssd->dram->SSD2_buffer->buffer_sector_count - size(sub_req_state);
//                            ssd->dram->SSD2_buffer->current_max_sector -= size(sub_req_state);
//                            ssd->dram->SSD2_buffer->backup_capacity += size(sub_req_state);
                            pt = ssd->dram->SSD2_buffer->buffer_tail;

                            hash_del(ssd, ssd->dram->SSD2_buffer, (HASH_NODE *) pt, pop_sub->raidNUM);
                            if (ssd->dram->SSD2_buffer->buffer_head->LRU_link_next == NULL) {
                                ssd->dram->SSD2_buffer->buffer_head = NULL;
                                ssd->dram->SSD2_buffer->buffer_tail = NULL;
                            } else {
                                ssd->dram->SSD2_buffer->buffer_tail = ssd->dram->SSD2_buffer->buffer_tail->LRU_link_pre;
                                ssd->dram->SSD2_buffer->buffer_tail->LRU_link_next = NULL;
                            }
                            pt->LRU_link_next = NULL;
                            pt->LRU_link_pre = NULL;

                            hash_node_free(ssd->dram->SSD2_buffer, (HASH_NODE *) pt);
                            pt = NULL;

//                        ssd->dram->buffer_free_sectors[1].free_sector =
//                                ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
                        } else {
                            break;
                        }
                    }
                }
//            }
            break;

        case 2:
//            proportion = (double)ssd->dram->SSD3_buffer->backup_capacity / ssd->dram->SSD3_buffer->current_max_sector;
//            if(proportion <= 0.1) {
                free_sector = ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
                if (ssd->dram->SSD3_buffer->extra_buffer > 0) {
                    for(int i = 0;i < 1;i++) {
                        if (ssd->dram->SSD3_buffer->buffer_tail != NULL) {
                            sub_req = NULL;
                            pop_sub = ssd->dram->SSD3_buffer->buffer_tail->sub;
                            sub_req_state = ssd->dram->SSD3_buffer->buffer_tail->stored;
                            sub_req_size = size(ssd->dram->SSD3_buffer->buffer_tail->stored);
                            sub_req_lpn = ssd->dram->SSD3_buffer->buffer_tail->group;
                            sub_req_first = ssd->dram->SSD3_buffer->buffer_tail->first_write;
                            pop_sub->pop_patch = -2;
                            pop_sub->req = NULL;
                            pop_sub->next_subs = NULL;
                            if (sub_req_first) {
                                active_pop_add_sub2channel(ssd, pop_sub, req, sub_req_lpn, sub_req_state, sub_req_size);
                            } else {
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;
                                if (ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1) {
                                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                                } else {
                                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                                }
                                active_pop_add_sub2channel(ssd, pop_sub, req, sub_req_lpn, sub_req_state, sub_req_size);
                            }

                            if(ssd->dram->SSD3_buffer->extra_buffer > 0){
                                if(size(sub_req_state) <= ssd->dram->SSD3_buffer->extra_buffer){
                                    ssd->dram->SSD3_buffer->backup_sector_count -= size(sub_req_state);
                                    ssd->dram->SSD3_buffer->extra_buffer -= size(sub_req_state);
                                }else{
                                    unsigned int pop_sector = 0;
                                    ssd->dram->SSD3_buffer->backup_sector_count -= ssd->dram->SSD3_buffer->extra_buffer;
                                    pop_sector = size(sub_req_state) - ssd->dram->SSD3_buffer->extra_buffer;
                                    ssd->dram->SSD3_buffer->buffer_sector_count =
                                            ssd->dram->SSD3_buffer->buffer_sector_count - pop_sector;
                                    ssd->dram->SSD3_buffer->extra_buffer = 0;
                                }
                            }else{
                                ssd->dram->SSD3_buffer->buffer_sector_count =
                                        ssd->dram->SSD3_buffer->buffer_sector_count - size(sub_req_state);
                            }
//                            ssd->dram->SSD3_buffer->buffer_sector_count =
//                                    ssd->dram->SSD3_buffer->buffer_sector_count - size(sub_req_state);
//                        ssd->dram->SSD3_buffer->current_max_sector -= size(sub_req_state);
//                        ssd->dram->SSD3_buffer->backup_capacity += size(sub_req_state);
                            pt = ssd->dram->SSD3_buffer->buffer_tail;

                            hash_del(ssd, ssd->dram->SSD3_buffer, (HASH_NODE *) pt, pop_sub->raidNUM);
                            if (ssd->dram->SSD3_buffer->buffer_head->LRU_link_next == NULL) {
                                ssd->dram->SSD3_buffer->buffer_head = NULL;
                                ssd->dram->SSD3_buffer->buffer_tail = NULL;
                            } else {
                                ssd->dram->SSD3_buffer->buffer_tail = ssd->dram->SSD3_buffer->buffer_tail->LRU_link_pre;
                                ssd->dram->SSD3_buffer->buffer_tail->LRU_link_next = NULL;
                            }

                            pt->LRU_link_next = NULL;
                            pt->LRU_link_pre = NULL;

                            hash_node_free(ssd->dram->SSD3_buffer, (HASH_NODE *) pt);
                            pt = NULL;

//                        ssd->dram->buffer_free_sectors[2].free_sector =
//                                ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
                        } else {
                            break;
                        }
                    }
                }
//            }
            break;

        case 3:
//            proportion = (double)ssd->dram->SSD4_buffer->backup_capacity / ssd->dram->SSD4_buffer->current_max_sector;
//            if(proportion <= 0.1) {
                free_sector = ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;
                if (ssd->dram->SSD4_buffer->extra_buffer > 0) {
                    for(int i = 0;i < 1;i++) {
                        if (ssd->dram->SSD4_buffer->buffer_tail != NULL) {
                            sub_req = NULL;
                            pop_sub = ssd->dram->SSD4_buffer->buffer_tail->sub;
                            sub_req_state = ssd->dram->SSD4_buffer->buffer_tail->stored;
                            sub_req_size = size(ssd->dram->SSD4_buffer->buffer_tail->stored);
                            sub_req_lpn = ssd->dram->SSD4_buffer->buffer_tail->group;
                            sub_req_first = ssd->dram->SSD4_buffer->buffer_tail->first_write;
                            pop_sub->pop_patch = -2;
                            pop_sub->req = NULL;
                            pop_sub->next_subs = NULL;
                            if (sub_req_first) {
                                active_pop_add_sub2channel(ssd, pop_sub, req, sub_req_lpn, sub_req_state, sub_req_size);
                            } else {
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;
                                if (ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1) {
                                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                                } else {
                                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                                }

                                active_pop_add_sub2channel(ssd, pop_sub, req, sub_req_lpn, sub_req_state, sub_req_size);
                            }

                            if(ssd->dram->SSD4_buffer->extra_buffer > 0){
                                if(size(sub_req_state) <= ssd->dram->SSD4_buffer->extra_buffer){
                                    ssd->dram->SSD4_buffer->backup_sector_count -= size(sub_req_state);
                                    ssd->dram->SSD4_buffer->extra_buffer -= size(sub_req_state);
                                }else{
                                    unsigned int pop_sector = 0;
                                    ssd->dram->SSD4_buffer->backup_sector_count -= ssd->dram->SSD4_buffer->extra_buffer;
                                    pop_sector = size(sub_req_state) - ssd->dram->SSD4_buffer->extra_buffer;
                                    ssd->dram->SSD4_buffer->buffer_sector_count =
                                            ssd->dram->SSD4_buffer->buffer_sector_count - pop_sector;
                                    ssd->dram->SSD4_buffer->extra_buffer = 0;
                                }
                            }else{
                                ssd->dram->SSD4_buffer->buffer_sector_count =
                                        ssd->dram->SSD4_buffer->buffer_sector_count - size(sub_req_state);
                            }
//                            ssd->dram->SSD4_buffer->buffer_sector_count =
//                                    ssd->dram->SSD4_buffer->buffer_sector_count - size(sub_req_state);
//                        ssd->dram->SSD4_buffer->current_max_sector -= size(sub_req_state);
//                        ssd->dram->SSD4_buffer->backup_capacity += size(sub_req_state);
                            pt = ssd->dram->SSD4_buffer->buffer_tail;

                            hash_del(ssd, ssd->dram->SSD4_buffer, (HASH_NODE *) pt, pop_sub->raidNUM);
                            if (ssd->dram->SSD4_buffer->buffer_head->LRU_link_next == NULL) {
                                ssd->dram->SSD4_buffer->buffer_head = NULL;
                                ssd->dram->SSD4_buffer->buffer_tail = NULL;
                            } else {
                                ssd->dram->SSD4_buffer->buffer_tail = ssd->dram->SSD4_buffer->buffer_tail->LRU_link_pre;
                                ssd->dram->SSD4_buffer->buffer_tail->LRU_link_next = NULL;
                            }
                            pt->LRU_link_next = NULL;
                            pt->LRU_link_pre = NULL;

                            hash_node_free(ssd->dram->SSD4_buffer, (HASH_NODE *) pt);
                            pt = NULL;

//                        ssd->dram->buffer_free_sectors[3].free_sector =
//                                ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;
                        } else {
                            break;
                        }
                    }
                }
//            }
            break;
        default:
            printf("active pop failure!\n");
            break;
    }
}

void copy_last_sub_to_channel(struct ssd_info *ssd,struct sub_request *copy_sub,int channel_num){
    if(ssd->channel_head[channel_num].copy_queue_head == NULL){
        ssd->channel_head[channel_num].copy_queue_head = copy_sub;
        ssd->channel_head[channel_num].copy_queue_tail = copy_sub;
    }else{
        ssd->channel_head[channel_num].copy_queue_tail->next_subs = copy_sub;
        ssd->channel_head[channel_num].copy_queue_tail = copy_sub;
    }
    ssd->channel_head[channel_num].write_queue_length++;
}

void del_copy_sub_from_channel(struct ssd_info *ssd,struct sub_request *last_sub,int channel_num){
    int find_flag = 0;
    int sub_ID = last_sub->sub_id;
    struct sub_request *del_sub,*pre;
    del_sub = ssd->channel_head[channel_num].copy_queue_head;
    if(del_sub->sub_id == last_sub->sub_id){
        ssd->channel_head[channel_num].copy_queue_head = ssd->channel_head[channel_num].copy_queue_head->next_subs;
        if(ssd->channel_head[channel_num].copy_queue_head == NULL){
            ssd->channel_head[channel_num].copy_queue_tail = NULL;
        }
    }else{
        pre = ssd->channel_head[channel_num].copy_queue_head;
        del_sub = ssd->channel_head[channel_num].copy_queue_head->next_subs;
        while(del_sub != NULL){
            if(del_sub->sub_id == last_sub->sub_id){
                pre->next_subs = del_sub->next_subs;
                if(pre->next_subs == NULL){
                    ssd->channel_head[channel_num].copy_queue_tail = pre;
                }
                find_flag = 1;
            }
            pre = del_sub;
            del_sub = del_sub->next_subs;
        }
        if(find_flag == 0){
            printf("not find last_sub_copy in copy_channel_queue\n");
            abort();
        }
    }
    ssd->channel_head[channel_num].write_queue_length--;
}

unsigned int compute_free_sector(struct ssd_info *ssd,int ssd_num){
    unsigned int free_sector;
    unsigned int sector1 = 0;
    unsigned int sector2 = 0;
    switch (ssd_num) {
        case 0:
            sector1 = ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
            sector2 = ssd->dram->SSD1_buffer->backup_capacity - ssd->dram->SSD1_buffer->additional_capacity;
            if(sector1 > sector2){
                return sector2;
            }else{
                return sector1;
            }
        case 1:
            sector1 = ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
            sector2 = ssd->dram->SSD2_buffer->backup_capacity - ssd->dram->SSD2_buffer->additional_capacity;
            if(sector1 > sector2){
                return sector2;
            }else{
                return sector1;
            }
        case 2:
            sector1 = ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
            sector2 = ssd->dram->SSD3_buffer->backup_capacity - ssd->dram->SSD3_buffer->additional_capacity;
            if(sector1 > sector2){
                return sector2;
            }else{
                return sector1;
            }
        case 3:
            sector1 = ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;
            sector2 = ssd->dram->SSD4_buffer->backup_capacity - ssd->dram->SSD4_buffer->additional_capacity;
            if(sector1 > sector2){
                return sector2;
            }else{
                return sector1;
            }
        default:
            abort();
    }
}

double findMin(double arr[], int size) {
    if (size <= 0) {
        return 0; // 或者可以返回一个特殊值表示错误
    }

    double min = arr[0];
    for (int i = 1; i < size; i++) {
        if (arr[i] < min) {
            min = arr[i];
        }
    }
    return min;
}

int getTenthsDigit(double num) {
    // 将小数部分移到整数部分
    double shifted = num * 10;
    // 取绝对值以处理负数
    int integerPart = (int)(shifted >= 0 ? shifted : -shifted);
    // 取个位数字
    return integerPart % 10;
}

double set_threshold(struct ssd_info *ssd){
    int total_count = 0;
    double free_sector_ratio[4] = {0};
    double min_ratio = 0;
    int TenthsDigit = 0;
    double threshold = 0;
    double ssd1_ratio = (double)ssd->dram->SSD1_buffer->backup_sector_count/ssd->dram->SSD1_buffer->backup_capacity;
    double ssd2_ratio = (double)ssd->dram->SSD2_buffer->backup_sector_count/ssd->dram->SSD2_buffer->backup_capacity;
    double ssd3_ratio = (double)ssd->dram->SSD3_buffer->backup_sector_count/ssd->dram->SSD3_buffer->backup_capacity;
    double ssd4_ratio = (double)ssd->dram->SSD4_buffer->backup_sector_count/ssd->dram->SSD4_buffer->backup_capacity;

    free_sector_ratio[0] = 1-ssd1_ratio;
    free_sector_ratio[1] = 1-ssd2_ratio;
    free_sector_ratio[2] = 1-ssd3_ratio;
    free_sector_ratio[3] = 1-ssd4_ratio;

    min_ratio = findMin(free_sector_ratio,STRIPENUM);

    TenthsDigit = getTenthsDigit(min_ratio);
    if(TenthsDigit > 8  || TenthsDigit == 0){
        threshold = (TenthsDigit) * 0.1;
        return threshold;
    }else{
        return 1;
    }
//    if(ssd1_ratio >= 0.7){
//        total_count++;
//    }
//    if(ssd2_ratio >= 0.7){
//        total_count++;
//    }
//    if(ssd3_ratio >= 0.7){
//        total_count++;
//    }
//    if(ssd4_ratio >= 0.7){
//        total_count++;
//    }
//    return total_count;

}

void add_last_sub_to_buffer(struct ssd_info *ssd,int ssd_buffer_num,struct sub_request *last_sub,int count_flag){
    int sector_count = size(last_sub->state);
    unsigned free_sector = 0;
    int flag = 0,write_back_count = 0;
    struct buffer_group *pt;
    struct sub_request *sub_req=NULL,*update=NULL,*pop_sub=NULL;
    unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0,sub_req_channel=0,sub_req_first=0;

    switch(ssd_buffer_num){
        case 0:
            if(last_sub!=NULL) {
//                free_sector = ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
                free_sector = ssd->dram->SSD1_buffer->backup_capacity - ssd->dram->SSD1_buffer->backup_sector_count;
                if (free_sector >= sector_count) {
                    ssd->dram->buffer->write_free++;
                    flag = 1;
                    last_sub->additional_flag = 0;
                }else{
//                    free_sector = ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
                    free_sector = compute_free_sector(ssd,0);
                    if(free_sector >= sector_count){
                        last_sub->additional_flag = 1;
                    }else{
                        abort();
                    }
                }
            }
            if(ssd->dram->SSD1_buffer->temp_buffer_head == NULL)
            {
                ssd->dram->SSD1_buffer->temp_buffer_head = last_sub;
                ssd->dram->SSD1_buffer->temp_buffer_tail = last_sub;
            }else{
                ssd->dram->SSD1_buffer->temp_buffer_tail->next_subs = last_sub;
                ssd->dram->SSD1_buffer->temp_buffer_tail = last_sub;
            }
//            ssd->dram->SSD1_buffer->buffer_sector_count += sector_count;
//            ssd->dram->SSD1_buffer->backup_capacity += sector_count;
//            ssd->dram->SSD1_buffer->backup_sector_count += sector_count;
            if(last_sub->additional_flag == 1){
                ssd->dram->SSD1_buffer->buffer_sector_count += sector_count;
                ssd->dram->SSD1_buffer->additional_capacity += sector_count;
            }else{
                ssd->dram->SSD1_buffer->backup_sector_count += sector_count;
            }
            if(ssd->dram->SSD1_buffer->backup_sector_count > ssd->max_ssd1){
                ssd->max_ssd1 = ssd->dram->SSD1_buffer->backup_sector_count;
            }
//            if(ssd->dram->SSD1_buffer->backup_sector_count >= ssd->max_ssd1){
//                ssd->max_ssd1 = ssd->dram->SSD1_buffer->backup_sector_count;
//            }
            break;

        case 1:
            if(last_sub!=NULL) {
//                free_sector = ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
                free_sector = ssd->dram->SSD2_buffer->backup_capacity - ssd->dram->SSD2_buffer->backup_sector_count;
                if (free_sector >= sector_count) {
                    ssd->dram->buffer->write_free++;
                    flag = 1;
                    last_sub->additional_flag = 0;
                }else{
//                    free_sector = ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
                    free_sector = compute_free_sector(ssd,1);
                    if(free_sector >= sector_count){
                        last_sub->additional_flag = 1;
                    }else{
                        abort();
                    }
                }
            }
            if(ssd->dram->SSD2_buffer->temp_buffer_head == NULL)
            {
                ssd->dram->SSD2_buffer->temp_buffer_head = last_sub;
                ssd->dram->SSD2_buffer->temp_buffer_tail = last_sub;
            }else{
                ssd->dram->SSD2_buffer->temp_buffer_tail->next_subs = last_sub;
                ssd->dram->SSD2_buffer->temp_buffer_tail = last_sub;
            }
//            ssd->dram->SSD2_buffer->buffer_sector_count += sector_count;
//            ssd->dram->SSD2_buffer->backup_capacity += sector_count;
//            ssd->dram->SSD2_buffer->backup_sector_count += sector_count;
            if(last_sub->additional_flag == 1){
                ssd->dram->SSD2_buffer->buffer_sector_count += sector_count;
                ssd->dram->SSD2_buffer->additional_capacity += sector_count;
            }else{
                ssd->dram->SSD2_buffer->backup_sector_count += sector_count;
            }
            if(ssd->dram->SSD2_buffer->backup_sector_count > ssd->max_ssd2){
                ssd->max_ssd2 = ssd->dram->SSD2_buffer->backup_sector_count;
            }
//            if(ssd->dram->SSD2_buffer->backup_sector_count >= ssd->max_ssd2){
//                ssd->max_ssd2 = ssd->dram->SSD2_buffer->backup_sector_count;
//            }
            break;

        case 2:
            if(last_sub!=NULL) {
//                free_sector = ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
                free_sector = ssd->dram->SSD3_buffer->backup_capacity - ssd->dram->SSD3_buffer->backup_sector_count;
                if (free_sector >= sector_count) {
                    ssd->dram->buffer->write_free++;
                    flag = 1;
                    last_sub->additional_flag = 0;
                }else{
//                    free_sector = ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
                    free_sector = compute_free_sector(ssd,2);
                    if(free_sector >= sector_count){
                        last_sub->additional_flag = 1;
                    }else{
                        abort();
                    }
                }
            }
            if(ssd->dram->SSD3_buffer->temp_buffer_head == NULL)
            {
                ssd->dram->SSD3_buffer->temp_buffer_head = last_sub;
                ssd->dram->SSD3_buffer->temp_buffer_tail = last_sub;
            }else{
                ssd->dram->SSD3_buffer->temp_buffer_tail->next_subs = last_sub;
                ssd->dram->SSD3_buffer->temp_buffer_tail = last_sub;
            }
//            ssd->dram->SSD3_buffer->buffer_sector_count += sector_count;
//            ssd->dram->SSD3_buffer->backup_capacity += sector_count;
//            ssd->dram->SSD3_buffer->backup_sector_count += sector_count;
            if(last_sub->additional_flag == 1){
                ssd->dram->SSD3_buffer->buffer_sector_count += sector_count;
                ssd->dram->SSD3_buffer->additional_capacity += sector_count;
            }else{
                ssd->dram->SSD3_buffer->backup_sector_count += sector_count;
            }
            if(ssd->dram->SSD3_buffer->backup_sector_count > ssd->max_ssd3){
                ssd->max_ssd3 = ssd->dram->SSD3_buffer->backup_sector_count;
            }
//            if(ssd->dram->SSD3_buffer->backup_sector_count >= ssd->max_ssd3){
//                ssd->max_ssd3 = ssd->dram->SSD3_buffer->backup_sector_count;
//            }
            break;

        case 3:
            if(last_sub!=NULL) {
//                free_sector = ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;
                free_sector = ssd->dram->SSD4_buffer->backup_capacity - ssd->dram->SSD4_buffer->backup_sector_count;
                if (free_sector >= sector_count) {
                    ssd->dram->buffer->write_free++;
                    flag = 1;
                    last_sub->additional_flag = 0;
                }else{
//                    free_sector = ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;
                    free_sector = compute_free_sector(ssd,3);
                    if(free_sector >= sector_count){
                        last_sub->additional_flag = 1;
                    }else{
                        abort();
                    }
                }
            }
            if(ssd->dram->SSD4_buffer->temp_buffer_head == NULL)
            {
                ssd->dram->SSD4_buffer->temp_buffer_head = last_sub;
                ssd->dram->SSD4_buffer->temp_buffer_tail = last_sub;
            }else{
                ssd->dram->SSD4_buffer->temp_buffer_tail->next_subs = last_sub;
                ssd->dram->SSD4_buffer->temp_buffer_tail = last_sub;
            }
//            ssd->dram->SSD4_buffer->buffer_sector_count += sector_count;
//            ssd->dram->SSD4_buffer->backup_capacity += sector_count;
//            ssd->dram->SSD4_buffer->backup_sector_count += sector_count;
            if(last_sub->additional_flag == 1){
                ssd->dram->SSD4_buffer->buffer_sector_count += sector_count;
                ssd->dram->SSD4_buffer->additional_capacity += sector_count;
            }else{
                ssd->dram->SSD4_buffer->backup_sector_count += sector_count;
            }
            if(ssd->dram->SSD4_buffer->backup_sector_count > ssd->max_ssd4){
                ssd->max_ssd4 = ssd->dram->SSD4_buffer->backup_sector_count;
            }
//            if(ssd->dram->SSD4_buffer->backup_sector_count >= ssd->max_ssd4){
//                ssd->max_ssd4 = ssd->dram->SSD4_buffer->backup_sector_count;
//            }
            break;
        default:
            printf("ssd_buffer do not match!");
            abort();
            break;
    }
}

void del_last_sub_from_buffer(struct ssd_info *ssd,int ssd_buffer_num,unsigned int last_sub_id,struct sub_request *last_sub){
    struct buffer_group *pre_node=NULL;
    struct sub_request *pop_sub =NULL;
    struct sub_request *pre = NULL;
    int sector_count = 0;

    switch(ssd_buffer_num){
        case 0:
            pop_sub = ssd->dram->SSD1_buffer->temp_buffer_head;
//            if(pop_sub->lpn == last_sub->lpn && pop_sub->raidNUM == last_sub->raidNUM && pop_sub->patch == last_sub->patch)
            if(pop_sub->sub_id == last_sub_id)
            {
                ssd->dram->SSD1_buffer->temp_buffer_head = ssd->dram->SSD1_buffer->temp_buffer_head->next_subs;
                if(ssd->dram->SSD1_buffer->temp_buffer_head == NULL){
                    ssd->dram->SSD1_buffer->temp_buffer_tail = NULL;
                }

                sector_count = size(pop_sub->state);
                last_sub->backup_sub_state = sector_count;
//                ssd->dram->SSD1_buffer->buffer_sector_count -= sector_count;
//                ssd->dram->SSD1_buffer->backup_capacity -= sector_count;
//                ssd->dram->SSD1_buffer->backup_sector_count -= sector_count;
                if(pop_sub->additional_flag == 1){
                    ssd->dram->SSD1_buffer->buffer_sector_count -= sector_count;
                    ssd->dram->SSD1_buffer->additional_capacity -= sector_count;
                }else{
                    ssd->dram->SSD1_buffer->backup_sector_count -= sector_count;
                }
//                ssd->max_ssd1--;
                free(pop_sub);
            } else{
                pre = ssd->dram->SSD1_buffer->temp_buffer_head;
                pop_sub = ssd->dram->SSD1_buffer->temp_buffer_head->next_subs;
                while(pop_sub != NULL){
//                    if(pop_sub->lpn == last_sub->lpn && pop_sub->raidNUM == last_sub->raidNUM && pop_sub->patch == last_sub->patch)
                    if(pop_sub->sub_id == last_sub_id)
                    {
                        pre->next_subs = pop_sub->next_subs;
                        if(pre->next_subs == NULL){
                            ssd->dram->SSD1_buffer->temp_buffer_tail = pre;
                        }
                        sector_count = size(pop_sub->state);
                        last_sub->backup_sub_state = sector_count;
//                        ssd->dram->SSD1_buffer->buffer_sector_count -= sector_count;
//                        ssd->dram->SSD1_buffer->backup_capacity -= sector_count;
//                        ssd->dram->SSD1_buffer->backup_sector_count -= sector_count;
                        if(pop_sub->additional_flag == 1){
                            ssd->dram->SSD1_buffer->buffer_sector_count -= sector_count;
                            ssd->dram->SSD1_buffer->additional_capacity -= sector_count;
                        }else{
                            ssd->dram->SSD1_buffer->backup_sector_count -= sector_count;
                        }
//                        ssd->max_ssd1--;
                        free(pop_sub);
                        break;
                    }
                    pre = pop_sub;
                    pop_sub = pop_sub->next_subs;
                }
                if(pop_sub == NULL){
                    printf("not find last_sub in buffer1\n");
                    abort();
                }
            }
            break;

        case 1:
            pop_sub = ssd->dram->SSD2_buffer->temp_buffer_head;
//            if(pop_sub->lpn == last_sub->lpn && pop_sub->raidNUM == last_sub->raidNUM && pop_sub->patch == last_sub->patch)
            if(pop_sub->sub_id == last_sub_id)
            {
                ssd->dram->SSD2_buffer->temp_buffer_head = ssd->dram->SSD2_buffer->temp_buffer_head->next_subs;
                if(ssd->dram->SSD2_buffer->temp_buffer_head == NULL){
                    ssd->dram->SSD2_buffer->temp_buffer_tail = NULL;
                }
                sector_count = size(pop_sub->state);
                last_sub->backup_sub_state = sector_count;
//                ssd->dram->SSD2_buffer->buffer_sector_count -= sector_count;
//                ssd->dram->SSD2_buffer->backup_capacity -= sector_count;
//                ssd->dram->SSD2_buffer->backup_sector_count -= sector_count;
                if(pop_sub->additional_flag == 1){
                    ssd->dram->SSD2_buffer->buffer_sector_count -= sector_count;
                    ssd->dram->SSD2_buffer->additional_capacity -= sector_count;
                }else{
                    ssd->dram->SSD2_buffer->backup_sector_count -= sector_count;
                }
//                ssd->max_ssd2--;
                free(pop_sub);
            } else{
                pre = ssd->dram->SSD2_buffer->temp_buffer_head;
                pop_sub = ssd->dram->SSD2_buffer->temp_buffer_head->next_subs;
                while(pop_sub != NULL){
//                    if(pop_sub->lpn == last_sub->lpn && pop_sub->raidNUM == last_sub->raidNUM && pop_sub->patch == last_sub->patch)
                    if(pop_sub->sub_id == last_sub_id)
                    {
                        pre->next_subs = pop_sub->next_subs;
                        if(pre->next_subs == NULL){
                            ssd->dram->SSD2_buffer->temp_buffer_tail = pre;
                        }
                        sector_count = size(pop_sub->state);
                        last_sub->backup_sub_state = sector_count;
//                        ssd->dram->SSD2_buffer->buffer_sector_count -= sector_count;
//                        ssd->dram->SSD2_buffer->backup_capacity -= sector_count;
//                        ssd->dram->SSD2_buffer->backup_sector_count -= sector_count;
                        if(pop_sub->additional_flag == 1){
                            ssd->dram->SSD2_buffer->buffer_sector_count -= sector_count;
                            ssd->dram->SSD2_buffer->additional_capacity -= sector_count;
                        }else{
                            ssd->dram->SSD2_buffer->backup_sector_count -= sector_count;
                        }
//                        ssd->max_ssd2--;
                        free(pop_sub);
                        break;
                    }
                    pre = pop_sub;
                    pop_sub = pop_sub->next_subs;
                }
                if(pop_sub == NULL){
                    printf("not find last_sub in buffer2\n");
                    abort();
                }
            }
            break;

        case 2:
            pop_sub = ssd->dram->SSD3_buffer->temp_buffer_head;
//            if(pop_sub->lpn == last_sub->lpn && pop_sub->raidNUM == last_sub->raidNUM && pop_sub->patch == last_sub->patch)
            if(pop_sub->sub_id == last_sub_id)
            {
                ssd->dram->SSD3_buffer->temp_buffer_head = ssd->dram->SSD3_buffer->temp_buffer_head->next_subs;
                if(ssd->dram->SSD3_buffer->temp_buffer_head == NULL){
                    ssd->dram->SSD3_buffer->temp_buffer_tail = NULL;
                }
                sector_count = size(pop_sub->state);
                last_sub->backup_sub_state = sector_count;
//                ssd->dram->SSD3_buffer->buffer_sector_count -= sector_count;
//                ssd->dram->SSD3_buffer->backup_capacity -= sector_count;
//                ssd->dram->SSD3_buffer->backup_sector_count -= sector_count;
                if(pop_sub->additional_flag == 1){
                    ssd->dram->SSD3_buffer->buffer_sector_count -= sector_count;
                    ssd->dram->SSD3_buffer->additional_capacity -= sector_count;
                }else{
                    ssd->dram->SSD3_buffer->backup_sector_count -= sector_count;
                }
//                ssd->max_ssd3--;
                free(pop_sub);
            }else{
                pre = ssd->dram->SSD3_buffer->temp_buffer_head;
                pop_sub = ssd->dram->SSD3_buffer->temp_buffer_head->next_subs;
                while(pop_sub != NULL){
//                    if(pop_sub->lpn == last_sub->lpn && pop_sub->raidNUM == last_sub->raidNUM && pop_sub->patch == last_sub->patch)
                    if(pop_sub->sub_id == last_sub_id)
                    {
                        pre->next_subs = pop_sub->next_subs;
                        if(pre->next_subs == NULL){
                            ssd->dram->SSD3_buffer->temp_buffer_tail = pre;
                        }
                        sector_count = size(pop_sub->state);
                        last_sub->backup_sub_state = sector_count;
//                        ssd->dram->SSD3_buffer->buffer_sector_count -= sector_count;
//                        ssd->dram->SSD3_buffer->backup_capacity -= sector_count;
//                        ssd->dram->SSD3_buffer->backup_sector_count -= sector_count;
                        if(pop_sub->additional_flag == 1){
                            ssd->dram->SSD3_buffer->buffer_sector_count -= sector_count;
                            ssd->dram->SSD3_buffer->additional_capacity -= sector_count;
                        }else{
                            ssd->dram->SSD3_buffer->backup_sector_count -= sector_count;
                        }
//                        ssd->max_ssd3--;
                        free(pop_sub);
                        break;
                    }
                    pre = pop_sub;
                    pop_sub = pop_sub->next_subs;
                }
                if(pop_sub == NULL){
                    printf("not find last_sub in buffer3\n");
                    abort();
                }
            }
            break;

        case 3:
            pop_sub = ssd->dram->SSD4_buffer->temp_buffer_head;
//            if(pop_sub->lpn == last_sub->lpn && pop_sub->raidNUM == last_sub->raidNUM && pop_sub->patch == last_sub->patch)
            if(pop_sub->sub_id == last_sub_id)
            {
                ssd->dram->SSD4_buffer->temp_buffer_head = ssd->dram->SSD4_buffer->temp_buffer_head->next_subs;
                if(ssd->dram->SSD4_buffer->temp_buffer_head == NULL){
                    ssd->dram->SSD4_buffer->temp_buffer_tail = NULL;
                }
                sector_count = size(pop_sub->state);
                last_sub->backup_sub_state = sector_count;
//                ssd->dram->SSD4_buffer->buffer_sector_count -= sector_count;
//                ssd->dram->SSD4_buffer->backup_capacity -= sector_count;
//                ssd->dram->SSD4_buffer->backup_sector_count -= sector_count;
                if(pop_sub->additional_flag == 1){
                    ssd->dram->SSD4_buffer->buffer_sector_count -= sector_count;
                    ssd->dram->SSD4_buffer->additional_capacity -= sector_count;
                }else{
                    ssd->dram->SSD4_buffer->backup_sector_count -= sector_count;
                }
//                ssd->max_ssd4--;
                free(pop_sub);
            } else{
                pre = ssd->dram->SSD4_buffer->temp_buffer_head;
                pop_sub = ssd->dram->SSD4_buffer->temp_buffer_head->next_subs;
                while(pop_sub != NULL){
//                    if(pop_sub->lpn == last_sub->lpn && pop_sub->raidNUM == last_sub->raidNUM && pop_sub->patch == last_sub->patch)
                    if(pop_sub->sub_id == last_sub_id)
                    {
                        pre->next_subs = pop_sub->next_subs;
                        if(pre->next_subs == NULL){
                            ssd->dram->SSD4_buffer->temp_buffer_tail = pre;
                        }
                        sector_count = size(pop_sub->state);
                        last_sub->backup_sub_state = sector_count;
//                        ssd->dram->SSD4_buffer->buffer_sector_count -= sector_count;
//                        ssd->dram->SSD4_buffer->backup_capacity -= sector_count;
//                        ssd->dram->SSD4_buffer->backup_sector_count -= sector_count;
                        if(pop_sub->additional_flag == 1){
                            ssd->dram->SSD4_buffer->buffer_sector_count -= sector_count;
                            ssd->dram->SSD4_buffer->additional_capacity -= sector_count;
                        }else{
                            ssd->dram->SSD4_buffer->backup_sector_count -= sector_count;
                        }
//                        ssd->max_ssd4--;
                        free(pop_sub);
                        break;
                    }
                    pre = pop_sub;
                    pop_sub = pop_sub->next_subs;
                }
                if(pop_sub == NULL){
                    printf("not find last_sub in buffer1\n");
                    abort();
                }
            }
            break;
        default:
            printf("ssd_buffer do not match!");
            abort();
            break;
    }

}

void del_temp_sub(struct ssd_info *ssd,struct sub_request *sub){
    struct sub_request *current_sub = ssd->temp_new_sub.new_write_head;
    struct sub_request *pre;
    if(current_sub != NULL){
        if(sub == current_sub){
            ssd->temp_new_sub.new_write_head = ssd->temp_new_sub.new_write_head->host_queue_next;
            if(ssd->temp_new_sub.new_write_head == NULL){
                ssd->temp_new_sub.new_write_tail = NULL;
            }
            ssd->temp_new_sub.queue_length--;
        }else{
            pre = ssd->temp_new_sub.new_write_head;
            current_sub = ssd->temp_new_sub.new_write_head->host_queue_next;
            while(current_sub) {
                if(sub == current_sub){
                    pre->host_queue_next = current_sub->host_queue_next;
                    current_sub->host_queue_next = NULL;
                    break;
                }
                pre = current_sub;
                current_sub = current_sub->host_queue_next;
            }
            ssd->temp_new_sub.queue_length--;
            if(current_sub == NULL){
                abort();
            }
            if(ssd->temp_new_sub.queue_length <0){
                int a = 0;
            }
        }

    }
}

void del_sub_from_waiting_queue(struct ssd_info *ssd,struct sub_request *sub){
    struct sub_request *pre,*current;
    int find_flag = 0;
    if(sub == ssd->waiting_sub_head){
        ssd->waiting_sub_head = ssd->waiting_sub_head->crowded_next;
        if(ssd->waiting_sub_head == NULL){
            ssd->waiting_sub_tail = NULL;
        }
//        sub->crowded_next = NULL;
    }else{
        pre = ssd->waiting_sub_head;
        current = ssd->waiting_sub_head->crowded_next;
        while(current != NULL){
            if(current == sub){
                pre->crowded_next = sub->crowded_next;
//                sub->crowded_next = NULL;
                if(pre->crowded_next == NULL){
                    ssd->waiting_sub_tail = pre;
                }
                find_flag = 1;
                break;
            }
            pre = current;
            current = current->crowded_next;
        }
        if(find_flag == 0){
            abort();
        }
        sub->crowded_next = NULL;
    }

}

void del_sub_from_request(struct ssd_info *ssd,struct request *req,struct sub_request *sub){
    struct sub_request *pre,*current;
    int find_flag = 0;
    if(sub == req->subs){
        req->subs = req->subs->next_subs;
        sub->next_subs = NULL;
    }else{
        pre = req->subs;
        current = req->subs->next_subs;
        while(current != NULL){
            if(current == sub){
                pre->next_subs = current->next_subs;
                current->next_subs = NULL;
                find_flag = 1;
                break;
            }
            pre = current;
            current = current->next_subs;
        }
        if(find_flag == 0){
            printf("not find sub in req\n");
            abort();
        }
    }
    sub->req = NULL;
    req->all--;
    req->now--;
}

bool find_repetition_backup_sub(struct ssd_info *ssd,struct request *req,struct sub_request *backup_sub){
    struct sub_request *pre = req->subs;
    int parity_flag = 0;
    if(backup_sub->lpn == ssd->stripe.checkLpn){
        parity_flag = 1;
    }
    while(pre != NULL){
        if(parity_flag == 1){
            if(pre->lpn == ssd->stripe.checkLpn && backup_sub->raidNUM == pre->raidNUM){
                return true;
            }else{
                pre = pre->next_subs;
            }
        }else{
            if(pre->lpn == backup_sub->lpn && pre->backup_flag == 1){
                return true;
            }else{
                pre = pre->next_subs;
            }
        }
    }
    return false;
}

struct sub_request * copy_sub_request(struct sub_request * sub){
    struct sub_request * copy_sub;
    if(sub != NULL){
        copy_sub = malloc(sizeof(struct sub_request));
        if(copy_sub == NULL){
            printf("copy_sub is NULL!\n");
            abort();
        }
        copy_sub->location = malloc(sizeof(struct local));
        if(copy_sub->location == NULL){
            printf("copy_sub->location is NULL!\n");
            abort();
        }
        copy_sub->location = NULL;
        copy_sub->next_subs = NULL;
        copy_sub->lpn = sub->lpn;
        copy_sub->ppn = sub->ppn;
        copy_sub->operation = sub->operation;
        copy_sub->size = sub->size;
        copy_sub->state = sub->state;
        copy_sub->raidNUM = sub->raidNUM;
        copy_sub->first_write = sub->first_write;
        copy_sub->patch = sub->patch;
        copy_sub->completed_state = sub->completed_state;
        copy_sub->sub_id = sub->sub_id;
        copy_sub->current_state = sub->current_state;
        copy_sub->complete_time = sub->complete_time;
        copy_sub->next_backup_sub = NULL;
        copy_sub->next_state = sub->next_state;
        copy_sub->update = NULL;
        copy_sub->backup_sub = NULL;
        copy_sub->prototype_sub = sub;

        return copy_sub;
    }
    return NULL;
}

void update_process_time(struct ssd_info *ssd,struct sub_request *sub){
    int ssd_num = sub->location->channel;
    switch (ssd_num) {
        case 0:
            if(sub->complete_time > ssd->ssd1_process_time_end){
                ssd->ssd1_process_time_end += sub->complete_time - ssd->current_time;
            }
            ssd->ssd1_process_count++;
            break;
        case 1:
            if(sub->complete_time > ssd->ssd2_process_time_end){
                ssd->ssd2_process_time_end += sub->complete_time - ssd->current_time;
            }
            ssd->ssd2_process_count++;
            break;
        case 2:
            if(sub->complete_time > ssd->ssd3_process_time_end){
                ssd->ssd3_process_time_end += sub->complete_time - ssd->current_time;
            }
            ssd->ssd3_process_count++;
            break;
        case 3:
            if(sub->complete_time > ssd->ssd4_process_time_end){
                ssd->ssd4_process_time_end = sub->complete_time - ssd->current_time;
            }
            ssd->ssd4_process_count++;
            break;
        default:
            abort();
    }
}

void update_backup_sub(struct ssd_info *ssd,struct sub_request *sub){
    struct sub_request *pre = sub->next_backup_sub;
    while(pre != NULL){
        pre->begin_time = sub->begin_time;
        pre->complete_time = sub->complete_time;
        pre->current_state = sub->current_state;
        pre->next_state = sub->next_state;
        pre->predict_time = sub->predict_time;
        if(pre->complete_time > pre->req->last_sub_complete_time){
            pre->req->last_sub_complete_time = pre->complete_time;
        }
//        if(pre->backup_sub != NULL){
//            pre->backup_sub->waiting_processing_sub_count--;
//            if(pre->backup_sub->waiting_processing_sub_count == 0){
//                struct sub_request *last_sub = pre->backup_sub;
//                del_last_sub_from_buffer(ssd,last_sub->first,last_sub->sub_id,last_sub);
//                del_last_sub_from_buffer(ssd,last_sub->second,last_sub->sub_id,last_sub);
//            }
//        }
        if(pre->req != NULL){
            pre->req->backup_sub_count--;
            int complete_sub_count = pre->req->backup_sub_total_count-pre->req->backup_sub_count;
            int waiting_process_sub = del_completed_crowded_sub(pre->req,complete_sub_count);
            if(waiting_process_sub != 0){
                pre->req->all -= waiting_process_sub;
                pre->req->now -= waiting_process_sub;
//                if(pre->req->all == 1){
//                    struct sub_request *current_sub = pre->req->subs;
//                    struct sub_request *last_sub = NULL,*pre_sub = NULL;
//                    int flag = 0;
//                    while(current_sub != NULL){
//                        if(current_sub->complete_time == 0){
//                            last_sub = current_sub;
//                        }
//                        current_sub = current_sub->next_subs;
//                    }
//                    if(last_sub == NULL){
//                        abort();
//                    }
//                    if(last_sub->backup_flag == 1){
//                        last_sub = last_sub->prototype_sub;
//                        flag = 1;
//                    }
//                    if(start_patch_copy(ssd,last_sub,last_sub->location->channel) == true){
//                        current_sub = pre->req->subs;
//                        if(flag == 1){
//                            while(current_sub != NULL){
//                                pre_sub = current_sub->next_subs;
//                                if(current_sub->complete_time == 0 && current_sub->backup_sub == 1){
//                                    del_sub_from_request(ssd,current_sub->req,current_sub);
//                                }
//                                current_sub = pre_sub;
//                            }
//                        }else{
//                            del_sub_from_request(ssd,last_sub->req,last_sub);
//                        }
//                        last_sub->new_write_last_sub = 1;
//                    }
//
//                }
                if(pre->req->all < 0){
                    abort();
                }
//                pre->req->all++;
//                pre->req->now++;
//                del_sub_from_request(ssd,pre->req,pre);
            }
        }
        pre = pre->next_backup_sub;
    }
}

void update_patch_info(struct ssd_info * ssd,int patchID){
    ssd->stripe_patchs[patchID].current_change++;
    if(ssd->stripe_patchs[patchID].current_change == ssd->stripe_patchs[patchID].target_change){
//        if(ssd->stripe_patchs[patchID].last_sub_id != -1){
//            if(ssd->stripe_patchs[patchID].last_sub->update_flag == 1){
//                ssd->update_sector -= size(ssd->stripe_patchs[patchID].last_sub->state);
//            }
//            del_last_sub_from_buffer(ssd,ssd->stripe_patchs[patchID].temp_store_ssd1,ssd->stripe_patchs[patchID].last_sub_id,ssd->stripe_patchs[patchID].last_sub);
//            del_last_sub_from_buffer(ssd,ssd->stripe_patchs[patchID].temp_store_ssd2,ssd->stripe_patchs[patchID].last_sub_id,ssd->stripe_patchs[patchID].last_sub);
//        }
        for(int i = 0;i < STRIPENUM;i++)
        {
            if(ssd->stripe_patchs[patchID].change_sub[i] != NULL)
            {
                ssd->stripe_patchs[patchID].change_sub[i] = NULL;
                ssd->stripe_patchs[patchID].completed_sub_flag[i] = -1;;
            }
        }
        ssd->stripe_patchs[patchID].raidID = 0;
        ssd->stripe_patchs[patchID].current_change = 0;
        ssd->stripe_patchs[patchID].target_change = 0;
        ssd->stripe_patchs[patchID].begin_time = 0;
        ssd->stripe_patchs[patchID].temp_store_ssd1 = -1;
        ssd->stripe_patchs[patchID].temp_store_ssd2 = -1;
        ssd->stripe_patchs[patchID].last_sub = NULL;
        ssd->stripe_patchs[patchID].copy_sub = NULL;
        ssd->stripe_patchs[patchID].last_sub_id = -1;
        ssd->complete_patch++;

    }
}

bool is_enough_for_last_sub_to_buffer(struct ssd_info *ssd,int ssd_buffer_num,struct sub_request *last_sub){
    unsigned int free_sector = 0;
    unsigned int sector_count = 0;
    double proportion = 0;
    switch(ssd_buffer_num){
        case 0:
//            proportion = (double)ssd->dram->SSD1_buffer->backup_capacity / ssd->dram->SSD1_buffer->max_buffer_sector;
//            if(proportion <= 0.1){
//                free_sector = ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
                free_sector = ssd->dram->SSD1_buffer->backup_capacity - ssd->dram->SSD1_buffer->backup_sector_count;
                if(ssd->dram->SSD1_buffer->backup_sector_count > ssd->max_ssd1){
                    ssd->max_ssd1 = ssd->dram->SSD1_buffer->backup_sector_count;
                }
                sector_count = size(last_sub->state);
                if(free_sector >= sector_count){
                    return true;
                }else{
//                    free_sector = ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
//                    if(free_sector >= sector_count && ssd->dram->SSD1_buffer->additional_capacity <= ssd->dram->SSD1_buffer->backup_capacity){
//                        return true;
//                    }else{
                        return false;
//                    }
                }
//            }else{
//                ssd->ssd2_count++;
//                return false;
//            }
        case 1:
//            proportion = (double)ssd->dram->SSD2_buffer->backup_capacity / ssd->dram->SSD2_buffer->max_buffer_sector;
//            if(proportion <= 0.1){
//                    free_sector = ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
                    free_sector = ssd->dram->SSD2_buffer->backup_capacity - ssd->dram->SSD2_buffer->backup_sector_count;
                    if(ssd->dram->SSD2_buffer->backup_sector_count > ssd->max_ssd2){
                        ssd->max_ssd2 = ssd->dram->SSD2_buffer->backup_sector_count;
                    }
                    sector_count = size(last_sub->state);
                    if(free_sector >= sector_count){
                        return true;
                    }else{
//                        free_sector = ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
//                        if(free_sector >= sector_count && ssd->dram->SSD2_buffer->additional_capacity <= ssd->dram->SSD2_buffer->backup_capacity){
//                            return true;
//                        }else{
                            return false;
//                        }
                    }
//            }else{
//                ssd->ssd2_count++;
//                return false;
//            }
        case 2:
//            proportion = (double)ssd->dram->SSD3_buffer->backup_capacity / ssd->dram->SSD3_buffer->max_buffer_sector;
//            if(proportion <= 0.1){
//                    free_sector = ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
                    free_sector = ssd->dram->SSD3_buffer->backup_capacity - ssd->dram->SSD3_buffer->backup_sector_count;
                    if(ssd->dram->SSD3_buffer->backup_sector_count > ssd->max_ssd3){
                        ssd->max_ssd3 = ssd->dram->SSD3_buffer->backup_sector_count;
                    }
                    sector_count = size(last_sub->state);
                    if(free_sector >= sector_count){
                        return true;
                    }else{
//                        free_sector = ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
//                        if(free_sector >= sector_count && ssd->dram->SSD3_buffer->additional_capacity <= ssd->dram->SSD3_buffer->backup_capacity){
//                            return true;
//                        }else{
                            return false;
//                        }
                    }
//            }else{
//                ssd->ssd2_count++;
//                return false;
//            }
        case 3:
//            proportion = (double)ssd->dram->SSD4_buffer->backup_capacity / ssd->dram->SSD4_buffer->max_buffer_sector;
//            if(proportion <= 0.1){
//                    free_sector = ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;
                    free_sector = ssd->dram->SSD4_buffer->backup_capacity - ssd->dram->SSD4_buffer->backup_sector_count;
                    if(ssd->dram->SSD4_buffer->backup_sector_count > ssd->max_ssd4){
                        ssd->max_ssd4 = ssd->dram->SSD4_buffer->backup_sector_count;
                    }
                    sector_count = size(last_sub->state);
                    if(free_sector >= sector_count){
                        return true;
                    }else{
//                        free_sector = ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;
//                        if(free_sector >= sector_count && ssd->dram->SSD4_buffer->additional_capacity <= ssd->dram->SSD4_buffer->backup_capacity){
//                            return true;
//                        }else{
                            return false;
//                        }
                    }
//            }else{
//                ssd->ssd2_count++;
//                return false;
//            }
        default:
            return false;
    }
}

bool is_enough_for_last_sub(struct ssd_info *ssd,int ssd_buffer_num,struct sub_request *last_sub){
    unsigned int free_sector = 0;
    unsigned int sector_count = 0;
    double proportion = 0;
    switch(ssd_buffer_num){
        case 0:
//            free_sector = ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
            free_sector = compute_free_sector(ssd,0);
            sector_count = size(last_sub->state);
            if(free_sector >= sector_count){
                return true;
            }else{
                return false;
            }
//            proportion = (double)ssd->dram->SSD1_buffer->backup_capacity / ssd->dram->SSD1_buffer->max_buffer_sector;
//            if(proportion <= 0.2){
//                free_sector = ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
//            free_sector = ssd->dram->SSD1_buffer->backup_capacity - ssd->dram->SSD1_buffer->backup_sector_count;
//            sector_count = size(last_sub->state);
//            if(free_sector >= sector_count){
//                return true;
//            }else{
//                free_sector = ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
//                if(free_sector >= sector_count && ssd->dram->SSD1_buffer->additional_capacity <= ssd->dram->SSD1_buffer->backup_capacity){
//                    return true;
//                }else{
//                    return false;
//                }
////                    return false;
//            }
//            }else{
//                ssd->ssd2_count++;
//                return false;
//            }
        case 1:
//            free_sector = ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
            free_sector = compute_free_sector(ssd,1);
            sector_count = size(last_sub->state);
            if(free_sector >= sector_count){
                return true;
            }else{
                return false;
            }
//            proportion = (double)ssd->dram->SSD2_buffer->backup_capacity / ssd->dram->SSD2_buffer->max_buffer_sector;
//            if(proportion <= 0.2){
//                    free_sector = ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
//            free_sector = ssd->dram->SSD2_buffer->backup_capacity - ssd->dram->SSD2_buffer->backup_sector_count;
//            sector_count = size(last_sub->state);
//            if(free_sector >= sector_count){
//                return true;
//            }else{
//                free_sector = ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
//                if(free_sector >= sector_count && ssd->dram->SSD2_buffer->additional_capacity <= ssd->dram->SSD2_buffer->backup_capacity){
//                    return true;
//                }else{
//                    return false;
//                }
////                        return false;
//            }
//            }else{
//                ssd->ssd2_count++;
//                return false;
//            }
        case 2:
//            free_sector = ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
            free_sector = compute_free_sector(ssd,2);
            sector_count = size(last_sub->state);
            if(free_sector >= sector_count){
                return true;
            }else{
                return false;
            }
//            proportion = (double)ssd->dram->SSD3_buffer->backup_capacity / ssd->dram->SSD3_buffer->max_buffer_sector;
//            if(proportion <= 0.2){
//                    free_sector = ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
//            free_sector = ssd->dram->SSD3_buffer->backup_capacity - ssd->dram->SSD3_buffer->backup_sector_count;
//            sector_count = size(last_sub->state);
//            if(free_sector >= sector_count){
//                return true;
//            }else{
//                free_sector = ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
//                if(free_sector >= sector_count && ssd->dram->SSD3_buffer->additional_capacity <= ssd->dram->SSD3_buffer->backup_capacity){
//                    return true;
//                }else{
//                    return false;
//                }
////                        return false;
//            }
//            }else{
//                ssd->ssd2_count++;
//                return false;
//            }
        case 3:
//            free_sector = ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;
            free_sector = compute_free_sector(ssd,3);
            sector_count = size(last_sub->state);
            if(free_sector >= sector_count){
                return true;
            }else{
                return false;
            }
//            proportion = (double)ssd->dram->SSD4_buffer->backup_capacity / ssd->dram->SSD4_buffer->max_buffer_sector;
//            if(proportion <= 0.2){
//                    free_sector = ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;
//            free_sector = ssd->dram->SSD4_buffer->backup_capacity - ssd->dram->SSD4_buffer->backup_sector_count;
//            sector_count = size(last_sub->state);
//            if(free_sector >= sector_count){
//                return true;
//            }else{
//                free_sector = ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;
//                if(free_sector >= sector_count && ssd->dram->SSD4_buffer->additional_capacity <= ssd->dram->SSD4_buffer->backup_capacity){
//                    return true;
//                }else{
//                    return false;
//                }
////                        return false;
//            }
//            }else{
//                ssd->ssd2_count++;
//                return false;
//            }
        default:
            return false;
    }
}



struct MinTwoIndices find_Two_Max_Buffer(struct ssd_info* ssd, int size) {
    struct MinTwoIndices result = {-1, -1}; // 初始化返回值为 -1

    // 计算每个 buffer 的 free_sector
    ssd->dram->buffer_free_sectors[0].free_sector =
            ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
    ssd->dram->buffer_free_sectors[1].free_sector =
            ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
    ssd->dram->buffer_free_sectors[2].free_sector =
            ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
    ssd->dram->buffer_free_sectors[3].free_sector =
            ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;

    // 输入验证
    if (size <= 1 || !ssd || !ssd->dram || !ssd->dram->buffer_free_sectors) {
        return result; // 无效输入
    }

    int max1 = -1; // 第一个最大值的下标
    int max2 = -1; // 第二个最大值的下标

    // 第一次遍历：找到第一个最大值
    for (int i = 0; i < size; i++) {
        if (max1 == -1 ||
            ssd->dram->buffer_free_sectors[i].free_sector >
            ssd->dram->buffer_free_sectors[max1].free_sector) {
            max1 = i;
        }
    }

    // 第二次遍历：找到第二个最大值
    for (int i = 0; i < size; i++) {
        if (i == max1) continue; // 跳过第一个最大值
        if (max2 == -1 ||
            ssd->dram->buffer_free_sectors[i].free_sector >
            ssd->dram->buffer_free_sectors[max2].free_sector) {
            max2 = i;
        }
    }

    // 设置返回值
    if (max1 == -1 || max2 == -1) {
        result.first = -1;
        result.second = -1;
    } else {
        result.first = max1;
        result.second = max2;
        // 按 free_sector 值从大到小排序
        if (ssd->dram->buffer_free_sectors[max1].free_sector <
            ssd->dram->buffer_free_sectors[max2].free_sector) {
            result.first = max2;
            result.second = max1;
        }
    }

    return result;
}

struct MinTwoIndices find_Free_Buffer(struct ssd_info* ssd, int size, int excludeIndex) {
    struct MinTwoIndices result = {-1, -1}; // 初始化返回值为 -1

    // 计算每个 buffer 的 free_sector
    ssd->dram->buffer_free_sectors[0].free_sector =
            ssd->dram->SSD1_buffer->max_buffer_sector - ssd->dram->SSD1_buffer->buffer_sector_count;
    ssd->dram->buffer_free_sectors[1].free_sector =
            ssd->dram->SSD2_buffer->max_buffer_sector - ssd->dram->SSD2_buffer->buffer_sector_count;
    ssd->dram->buffer_free_sectors[2].free_sector =
            ssd->dram->SSD3_buffer->max_buffer_sector - ssd->dram->SSD3_buffer->buffer_sector_count;
    ssd->dram->buffer_free_sectors[3].free_sector =
            ssd->dram->SSD4_buffer->max_buffer_sector - ssd->dram->SSD4_buffer->buffer_sector_count;

    // 输入验证
//    if (size <= 1 || excludeIndex < 0 || excludeIndex >= size || !ssd || !ssd->dram || !ssd->dram->buffer_free_sectors) {
//        return result; // 无效输入
//    }

    int max1 = -1; // 第一个最大值的下标
    int max2 = -1; // 第二个最大值的下标

    // 第一次遍历：找到第一个最大值
    for (int i = 0; i < size; i++) {
        if (i == excludeIndex) continue; // 跳过排除的下标
        if (max1 == -1 ||
            ssd->dram->buffer_free_sectors[i].free_sector >
            ssd->dram->buffer_free_sectors[max1].free_sector) {
            max1 = i;
        }
    }

    // 第二次遍历：找到第二个最大值
    for (int i = 0; i < size; i++) {
        if (i == excludeIndex || i == max1) continue; // 跳过排除的下标和第一个最大值
        if (max2 == -1 ||
            ssd->dram->buffer_free_sectors[i].free_sector >
            ssd->dram->buffer_free_sectors[max2].free_sector) {
            max2 = i;
        }
    }

    // 设置返回值
    if (max1 == -1 || max2 == -1) {
        result.first = -1;
        result.second = -1;
    } else {
        result.first = max1;
        result.second = max2;
        // 按 free_sector 值从大到小排序
        if (ssd->dram->buffer_free_sectors[max1].free_sector <
            ssd->dram->buffer_free_sectors[max2].free_sector) {
            result.first = max2;
            result.second = max1;
        }
    }

    return result;
}

void findTwoMaxIndices(double arr[], int size, int* min1Index, int* min2Index) {
    // 检查数组大小
    if (size < 2) {
        *min1Index = -1;
        *min2Index = -1;
        return;
    }

    // 初始化最小值和次小值的下标
    *min1Index = 0;
    *min2Index = 1;
    if (arr[1] > arr[0]) {
        *min1Index = 1;
        *min2Index = 0;
    }

    // 遍历数组，更新最小值和次小值的下标
    for (int i = 2; i < size; i++) {
        if (arr[i] > arr[*min1Index]) {
            *min2Index = *min1Index;
            *min1Index = i;
        } else if (arr[i] >= arr[*min2Index] && i != *min1Index) {
            *min2Index = i;
        }
    }
}

struct MinTwoIndices find_Two_Free_Buffer(struct ssd_info* ssd, int size, int excludeIndex) {
    struct MinTwoIndices result = {-1, -1}; // 初始化返回值为 -1

    // 计算每个 buffer 的 free_sector
    ssd->dram->buffer_free_sectors[0].free_sector =
            ssd->dram->SSD1_buffer->backup_capacity - ssd->dram->SSD1_buffer->backup_sector_count;
    ssd->dram->buffer_free_sectors[1].free_sector =
            ssd->dram->SSD2_buffer->backup_capacity - ssd->dram->SSD2_buffer->backup_sector_count;
    ssd->dram->buffer_free_sectors[2].free_sector =
            ssd->dram->SSD3_buffer->backup_capacity - ssd->dram->SSD3_buffer->backup_sector_count;
    ssd->dram->buffer_free_sectors[3].free_sector =
            ssd->dram->SSD4_buffer->backup_capacity - ssd->dram->SSD4_buffer->backup_sector_count;

    // 输入验证
//    if (size <= 1 || excludeIndex < 0 || excludeIndex >= size || !ssd || !ssd->dram || !ssd->dram->buffer_free_sectors) {
//        return result; // 无效输入
//    }

    int max1 = -1; // 第一个最大值的下标
    int max2 = -1; // 第二个最大值的下标

    // 第一次遍历：找到第一个最大值
    for (int i = 0; i < size; i++) {
        if (i == excludeIndex) continue; // 跳过排除的下标
        if (max1 == -1 ||
            ssd->dram->buffer_free_sectors[i].free_sector >
            ssd->dram->buffer_free_sectors[max1].free_sector) {
            max1 = i;
        }
    }

    // 第二次遍历：找到第二个最大值
    for (int i = 0; i < size; i++) {
        if (i == excludeIndex || i == max1) continue; // 跳过排除的下标和第一个最大值
        if (max2 == -1 ||
            ssd->dram->buffer_free_sectors[i].free_sector >
            ssd->dram->buffer_free_sectors[max2].free_sector) {
            max2 = i;
        }
    }

    // 设置返回值
    if (max1 == -1 || max2 == -1) {
        result.first = -1;
        result.second = -1;
    } else {
        result.first = max1;
        result.second = max2;
        // 按 free_sector 值从大到小排序
        if (ssd->dram->buffer_free_sectors[max1].free_sector <
            ssd->dram->buffer_free_sectors[max2].free_sector) {
            result.first = max2;
            result.second = max1;
        }
    }

    return result;
}


// 返回最大 host_queue_pos 对应的数组下标
int findMaxHostQueuePosIndex(struct sub_request* arr[], int size) {
    if (size <= 0) {
        return -1; // 数组为空或大小无效
    }

    int maxIndex = 0;           // 记录最大值的下标
    int maxValue = arr[0]->host_queue_pos;  // 初始最大值
    int hasDuplicate = 0;       // 标记是否出现重复最大值

    // 遍历数组，找到最大值并检查是否有重复
    for (int i = 1; i < size; i++) {
        if (arr[i]->host_queue_pos > maxValue) {
            maxValue = arr[i]->host_queue_pos;  // 更新最大值
            maxIndex = i;                      // 更新下标
            hasDuplicate = 0;                  // 重置重复标记
        } else if (arr[i]->host_queue_pos == maxValue) {
            hasDuplicate = 1;                  // 标记发现重复最大值
        }
    }

    // 如果有重复最大值，返回-1，否则返回最大值下标
    return hasDuplicate ? -1 : maxIndex;
}

struct ssd_info * insert_2_SSD1_buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write,int last_sub_pop)
{
    int write_back_count,flag=0;/*flag��ʾΪд���������ڿռ��Ƿ���ɣ�0��ʾ��Ҫ��һ���ڣ�1��ʾ�Ѿ��ڿ�*/
    int buffer_sub_patch = buffer_sub->patch;
    unsigned int i,lsn,hit_flag,add_flag,sector_count,active_region_flag=0,free_sector=0;
    struct buffer_group *buffer_node=NULL,*pt,*new_node=NULL,key;
    struct sub_request *sub_req=NULL,*update=NULL,*pop_sub=NULL;

    unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0,sub_req_channel=0,sub_req_first=0;

#ifdef DEBUG
    printf("enter insert2buffer,  current time:%lld, lpn:%d, state:%d,\n",ssd->current_time,lpn,state);
#endif
    state = ~(0xffffffff<<(ssd->parameter->subpage_page));
    sector_count=size(state);                                                              /*��Ҫд��buffer��sector����*/
    key.group=lpn;
    long cache_loc = -1;
    long *loc = &cache_loc;
    buffer_node= (struct buffer_group*)hash_find(ssd,ssd->dram->SSD1_buffer, (HASH_NODE *)&key,buffer_sub->raidNUM, loc);
    //cul hit range to get diff. C-V
    if(cache_loc >= 0 && buffer_sub->lpn != ssd->stripe.checkLpn){
        cache_loc = cache_loc/128;
        for(; cache_loc < 16; cache_loc++){
            ssd->dram->SSD1_buffer->range_write_hit[cache_loc]++;
        }
    }
    /************************************************************************************************
    *û�����С�
    *��һ���������lpn�ж�����ҳ��Ҫд��buffer��ȥ����д�ص�lsn��Ϊ��lpn�ڳ�λ�ã�
    *���ȼ�Ҫ�����free sector����ʾ���ж��ٿ���ֱ��д��buffer�ڵ㣩��
    *���free_sector>=sector_count�����ж���Ŀռ乻lpn������д������Ҫ����д������
    *����û�ж���Ŀռ乩lpn������д����ʱ��Ҫ�ͷ�һ���ֿռ䣬����д�����󡣾�Ҫcreat_sub_request()
    *************************************************************************************************/
    if(buffer_node==NULL)   //buffer node miss
    {
        free_sector=ssd->dram->SSD1_buffer->max_buffer_sector-ssd->dram->SSD1_buffer->buffer_sector_count;
        if(buffer_sub->lpn != ssd->stripe.checkLpn){
            ssd->dram->SSD1_buffer->write_miss_hit++;
            ssd->dram->SSD1_buffer->window_write_miss++;
        }
        // else{
        //     ssd->dram->SSD1_buffer->parity_write_miss_hit++;
        // }
        if(free_sector>=sector_count)
        {
            ssd->dram->SSD1_buffer->write_free++;
            flag=1;
        }
        if(flag==0)
        {
            write_back_count=sector_count-free_sector;
            // write_back_count=sector_count;
            while(write_back_count>0)
            {
                sub_req=NULL;
                pop_sub = ssd->dram->SSD1_buffer->buffer_tail->sub;
                sub_req_state=ssd->dram->SSD1_buffer->buffer_tail->stored;
                sub_req_size=size(ssd->dram->SSD1_buffer->buffer_tail->stored);
                sub_req_lpn=ssd->dram->SSD1_buffer->buffer_tail->group;
                sub_req_first = ssd->dram->SSD1_buffer->buffer_tail->first_write;
//                req->all++;

//                pop_sub->last_sub_pop_flag = last_sub_pop;
                buffer_sub->pop_sub = pop_sub;
                pop_sub->pop_sub_id = buffer_sub->sub_id;
//                pop_sub->prototype_sub = buffer_sub;
//                pop_sub->last_pop_sub = 0;
                pop_sub->pop_patch = buffer_sub_patch;
//                if(first_write == 0){
//                    pop_sub->pop_patch = buffer_sub_patch;
//                }else{
//                    pop_sub->pop_patch = -1;
//                }
                pop_sub->begin_time = buffer_sub->begin_time;
                if(sub_req_first)
                {
                    add_sub2channel(ssd,pop_sub,req);
                }else {
                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;
                    if(ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1){
                        ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                    }else{
                        ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                    }

                    add_sub2channel(ssd,pop_sub,req);
                }
                /**********************************************************************************
                *req��Ϊ�գ���ʾ���insert2buffer��������buffer_management�е��ã�������request����
                *reqΪ�գ���ʾ�����������process�����д���һ�Զ�ӳ���ϵ�Ķ���ʱ����Ҫ���������
                *�����ݼӵ�buffer�У�����ܲ���ʵʱ��д�ز�������Ҫ�����ʵʱ��д�ز��������������
                *������������������
                ***********************************************************************************/
                if(req!=NULL)
                {
                }
                else
                {
//                    sub_req->next_subs=sub->next_subs;
//                    sub->next_subs=sub_req;
                }

                /*********************************************************************
                *д������뵽��ƽ�����������ʱ��Ҫ�޸�dram��buffer_sector_count��
                *ά��ƽ�����������avlTreeDel()��AVL_TREENODE_FREE()������ά��LRU�㷨��
                **********************************************************************/
                ssd->dram->SSD1_buffer->buffer_sector_count=ssd->dram->SSD1_buffer->buffer_sector_count-size(sub_req_state);
                // ssd->dram->SSD1_buffer->buffer_sector_count=ssd->dram->SSD1_buffer->buffer_sector_count-sector_count;
                pt = ssd->dram->SSD1_buffer->buffer_tail;

                hash_del(ssd,ssd->dram->SSD1_buffer,  (HASH_NODE*)pt,pop_sub->raidNUM);
                if(ssd->dram->SSD1_buffer->buffer_head->LRU_link_next == NULL){
                    ssd->dram->SSD1_buffer->buffer_head = NULL;
                    ssd->dram->SSD1_buffer->buffer_tail = NULL;
                }else{
                    ssd->dram->SSD1_buffer->buffer_tail=ssd->dram->SSD1_buffer->buffer_tail->LRU_link_pre;
                    ssd->dram->SSD1_buffer->buffer_tail->LRU_link_next=NULL;
                }
                pt->LRU_link_next=NULL;
                pt->LRU_link_pre=NULL;

                hash_node_free(ssd->dram->SSD1_buffer, (HASH_NODE *) pt);
                pt = NULL;

                write_back_count=write_back_count-size(sub_req_state);                            /*��Ϊ������ʵʱд�ز�������Ҫ������д�ز�����������*/
            }
        }

        /******************************************************************************
        *����һ��buffer node���������ҳ������ֱ�ֵ��������Ա�����ӵ����׺Ͷ�������
        *******************************************************************************/
        new_node=NULL;
        new_node=(struct buffer_group *)malloc(sizeof(struct buffer_group));
        alloc_assert(new_node,"buffer_group_node");
        memset(new_node,0, sizeof(struct buffer_group));

        new_node->first_write = first_write;
        new_node->sub = buffer_sub;
        new_node->read_count = 0;
        new_node->group=lpn;
        new_node->stored=state;
        new_node->dirty_clean=state;
        new_node->LRU_link_pre = NULL;
        new_node->LRU_link_next=ssd->dram->SSD1_buffer->buffer_head;
        if(ssd->dram->SSD1_buffer->buffer_head != NULL){
            ssd->dram->SSD1_buffer->buffer_head->LRU_link_pre=new_node;
        }else{
            ssd->dram->SSD1_buffer->buffer_tail = new_node;
        }
        ssd->dram->SSD1_buffer->buffer_head=new_node;
        new_node->LRU_link_pre=NULL;

        hash_add(ssd->dram->SSD1_buffer, (HASH_NODE *) new_node);
        ssd->dram->SSD1_buffer->buffer_sector_count += sector_count;
//        ssd->dram->buffer_free_sectors[0].free_sector = ssd->dram->SSD1_buffer->max_buffer_sector-ssd->dram->SSD1_buffer->buffer_sector_count;
    }
        /****************************************************************************************
        *��buffer�����е����
        *��Ȼ�����ˣ��������е�ֻ��lpn���п���������д����ֻ����Ҫдlpn��һpage��ĳ����sub_page
        *��ʱ����Ҫ��һ�����ж�
        *****************************************************************************************/
    else    //buffer node hit
    {
        if(buffer_sub->lpn != ssd->stripe.checkLpn){
            ssd->dram->SSD1_buffer->write_hit++;
        }
        // else{
            // ssd->dram->SSD1_buffer->parity_write_hit++;
        // }
        unsigned int unHitBit = 0;
        buffer_node->read_count = 0;
        for(i=0;i<ssd->parameter->subpage_page;i++)     //handle each subpage
        {
            /*************************************************************
            *�ж�state��iλ�ǲ���1
            *�����жϵ�i��sector�Ƿ����buffer�У�1��ʾ���ڣ�0��ʾ�����ڡ�
            **************************************************************/
            if((state>>i)%2!=0)
            {
                lsn=lpn*ssd->parameter->subpage_page+i;
                hit_flag=0;
                hit_flag=(buffer_node->stored)&(0x00000001<<i);

                if(hit_flag!=0)				                                          /*write hit*/
                {
                    active_region_flag=1;                                             /*������¼�����buffer node�е�lsn�Ƿ����У����ں������ֵ���ж�*/

                    if(req!=NULL && lpn != ssd->stripe.checkLpn)
                    {
                        if(ssd->dram->SSD1_buffer->buffer_head != buffer_node)  // 调整LRU链表
                        {
                            buf_node *preNode  = buffer_node->LRU_link_pre;
                            if(ssd->dram->SSD1_buffer->buffer_tail==buffer_node)
                            {
                                ssd->dram->SSD1_buffer->buffer_tail=buffer_node->LRU_link_pre;
                                buffer_node->LRU_link_pre->LRU_link_next=NULL;
                            }
                            else if(buffer_node != ssd->dram->SSD1_buffer->buffer_head)
                            {
                                buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;
                                buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
                            }
                            if(1){
                                buffer_node->LRU_link_next=ssd->dram->SSD1_buffer->buffer_head;
                                ssd->dram->SSD1_buffer->buffer_head->LRU_link_pre=buffer_node;
                                buffer_node->LRU_link_pre=NULL;
                                ssd->dram->SSD1_buffer->buffer_head=buffer_node;
                            }else{
                                if(preNode == ssd->dram->SSD1_buffer->buffer_head){
                                    buffer_node->LRU_link_next=ssd->dram->SSD1_buffer->buffer_head;
                                    ssd->dram->SSD1_buffer->buffer_head->LRU_link_pre=buffer_node;
                                    buffer_node->LRU_link_pre=NULL;
                                    ssd->dram->SSD1_buffer->buffer_head=buffer_node;
                                }else{
                                    buffer_node->LRU_link_pre = preNode->LRU_link_pre;
                                    buffer_node->LRU_link_next = preNode;

                                    buffer_node->LRU_link_pre->LRU_link_next = buffer_node;
                                    buffer_node->LRU_link_next->LRU_link_pre = buffer_node;
                                }
                            }

                        }
                        //ssd->dram->buffer->write_hit++;
                        req->complete_lsn_count++;                                        /*�ؼ� ����buffer������ʱ ����req->complete_lsn_count++��ʾ��buffer��д�����ݡ�*/
                    }
                    else
                    {
                    }
                }
                else    // write miss
                {
                    /************************************************************************************************************
                    *��lsnû�����У����ǽڵ���buffer�У���Ҫ�����lsn�ӵ�buffer�Ķ�Ӧ�ڵ���
                    *��buffer��ĩ����һ���ڵ㣬��һ���Ѿ�д�ص�lsn�ӽڵ���ɾ��(����ҵ��Ļ�)����������ڵ��״̬��ͬʱ������µ�
                    *lsn�ӵ���Ӧ��buffer�ڵ��У��ýڵ������bufferͷ�����ڵĻ��������Ƶ�ͷ�������û���ҵ��Ѿ�д�ص�lsn����buffer
                    *�ڵ���һ��group����д�أ�����������������������ϡ�������ǰ����һ��channel�ϡ�
                    *��һ��:��buffer��β���Ѿ�д�صĽڵ�ɾ��һ����Ϊ�µ�lsn�ڳ��ռ䣬������Ҫ�޸Ķ�βĳ�ڵ��stored״̬���ﻹ��Ҫ
                    *       ���ӣ���û�п���֮��ɾ����lsnʱ����Ҫ�����µ�д������д��LRU���Ľڵ㡣
                    *�ڶ���:���µ�lsn�ӵ�������buffer�ڵ��С�
                    *************************************************************************************************************/
                    unHitBit |= (0x00000001 << i);  //标记miss的bit
                    //ssd->dram->buffer->write_miss_hit++;

                    if(ssd->dram->SSD1_buffer->buffer_sector_count>=ssd->dram->SSD1_buffer->max_buffer_sector)  // buffer满了
                    {
                        if (buffer_node==ssd->dram->SSD1_buffer->buffer_tail)                  /*������еĽڵ���buffer�����һ���ڵ㣬������������ڵ�*/
                        {
                            pt = ssd->dram->SSD1_buffer->buffer_tail->LRU_link_pre;
                            ssd->dram->SSD1_buffer->buffer_tail->LRU_link_pre=pt->LRU_link_pre;
                            ssd->dram->SSD1_buffer->buffer_tail->LRU_link_pre->LRU_link_next=ssd->dram->SSD1_buffer->buffer_tail;
                            ssd->dram->SSD1_buffer->buffer_tail->LRU_link_next=pt;
                            pt->LRU_link_next=NULL;
                            pt->LRU_link_pre=ssd->dram->SSD1_buffer->buffer_tail;
                            ssd->dram->SSD1_buffer->buffer_tail=pt;

                        }
                        //创建写回链表，将淘汰的数据写回存储
                        sub_req=NULL;
                        struct sub_request *sub = ssd->dram->SSD1_buffer->buffer_tail->sub;
                        sub_req_state=ssd->dram->SSD1_buffer->buffer_tail->stored;
                        sub_req_size=size(ssd->dram->SSD1_buffer->buffer_tail->stored);
                        sub_req_lpn=ssd->dram->SSD1_buffer->buffer_tail->group;
                        sub_req_channel=ssd->dram->SSD1_buffer->buffer_tail->channel;
//                        req->all++;
                        //根据不同的分配方案创建子请求
                        if(ssd->dram->map->map_entry[sub_req_lpn].state == 0 && ssd->parameter->allocation_scheme==0 && ssd->parameter->dynamic_allocation == 2){
                            creat_sub_write_request_for_raid(ssd,sub_req_lpn, sub_req_state, req, ~(0xffffffff<<(ssd->parameter->subpage_page)));
                        }else {
                            //更新条带话信息
                            ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;

                            if(ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1){
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                            }else{
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                            }

                            sub_req=creat_sub_request(ssd,sub_req_lpn,sub_req_size,sub_req_state,req,WRITE,0, ssd->page2Trip[sub_req_lpn]);
                        }
                        if(req!=NULL)
                        {

                        }
                        else if(req==NULL)
                        {
                            sub_req->next_subs=sub->next_subs;
                            sub->next_subs=sub_req;
                        }
                        // del node from buffer
                        ssd->dram->SSD1_buffer->buffer_sector_count=ssd->dram->SSD1_buffer->buffer_sector_count-size(sub_req_state);
                        pt = ssd->dram->SSD1_buffer->buffer_tail;
                        //avlTreeDel(ssd->dram->buffer, (TREE_NODE *) pt);
                        hash_del(ssd,ssd->dram->SSD1_buffer, (HASH_NODE *) pt,sub->raidNUM);

                        /************************************************************************/
                        /* ��:  ������������buffer�Ľڵ㲻Ӧ����ɾ����						*/
                        /*			��ȵ�д����֮�����ɾ��									*/
                        /************************************************************************/
                        if(ssd->dram->SSD1_buffer->buffer_head->LRU_link_next == NULL)
                        {
                            ssd->dram->SSD1_buffer->buffer_head = NULL;
                            ssd->dram->SSD1_buffer->buffer_tail = NULL;
                        }else{
                            ssd->dram->SSD1_buffer->buffer_tail=ssd->dram->SSD1_buffer->buffer_tail->LRU_link_pre;
                            ssd->dram->SSD1_buffer->buffer_tail->LRU_link_next=NULL;
                        }
                        pt->LRU_link_next=NULL;
                        pt->LRU_link_pre=NULL;
                        //AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *) pt);
                        hash_node_free(ssd->dram->SSD1_buffer, (HASH_NODE *) pt);
                        ssd->dram->buffer->write_miss_hit++;
                        pt = NULL;
                    }

                    /*新数据加入缓存*/
                    add_flag=0x00000001<<(lsn%ssd->parameter->subpage_page);
                    //当前节点移动到LRU头部
                    if(ssd->dram->SSD1_buffer->buffer_head!=buffer_node)                      /*�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ����*/
                    {

                        buf_node *preNode  = buffer_node->LRU_link_pre;
                        if(ssd->dram->SSD1_buffer->buffer_tail==buffer_node)
                        {
                            ssd->dram->SSD1_buffer->buffer_tail=buffer_node->LRU_link_pre;
                            buffer_node->LRU_link_pre->LRU_link_next=NULL;
                        }
                        else if(buffer_node != ssd->dram->SSD1_buffer->buffer_head)
                        {
                            buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;
                            buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
                        }
                        if(1){
                            buffer_node->LRU_link_next=ssd->dram->SSD1_buffer->buffer_head;
                            ssd->dram->SSD1_buffer->buffer_head->LRU_link_pre=buffer_node;
                            buffer_node->LRU_link_pre=NULL;
                            ssd->dram->SSD1_buffer->buffer_head=buffer_node;
                        }else{
                            if(preNode == ssd->dram->SSD1_buffer->buffer_head){
                                buffer_node->LRU_link_next=ssd->dram->SSD1_buffer->buffer_head;
                                ssd->dram->SSD1_buffer->buffer_head->LRU_link_pre=buffer_node;
                                buffer_node->LRU_link_pre=NULL;
                                ssd->dram->SSD1_buffer->buffer_head=buffer_node;
                            }else{
                                buffer_node->LRU_link_pre = preNode->LRU_link_pre;
                                buffer_node->LRU_link_next = preNode;

                                buffer_node->LRU_link_pre->LRU_link_next = buffer_node;
                                buffer_node->LRU_link_next->LRU_link_pre = buffer_node;
                            }
                        }
                    }
                    buffer_node->stored=buffer_node->stored|add_flag;
                    buffer_node->dirty_clean=buffer_node->dirty_clean|add_flag;
//                    ssd->dram->SSD1_buffer->buffer_sector_count++;
                }

            }
        }
    }

    return ssd;
}

struct ssd_info * insert_2_SSD2_buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write,int last_sub_pop)
{
    int write_back_count,flag=0;                                                             /*flag��ʾΪд���������ڿռ��Ƿ���ɣ�0��ʾ��Ҫ��һ���ڣ�1��ʾ�Ѿ��ڿ�*/
    int buffer_sub_patch = buffer_sub->patch;
    unsigned int i,lsn,hit_flag,add_flag,sector_count,active_region_flag=0,free_sector=0;
    struct buffer_group *buffer_node=NULL,*pt,*new_node=NULL,key;
    struct sub_request *sub_req=NULL,*update=NULL,*pop_sub=NULL;

    unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0,sub_req_channel=0,sub_req_first=0;

#ifdef DEBUG
    printf("enter insert2buffer,  current time:%lld, lpn:%d, state:%d,\n",ssd->current_time,lpn,state);
#endif
    state = ~(0xffffffff<<(ssd->parameter->subpage_page));
    sector_count=size(state);                                                             /*��Ҫд��buffer��sector����*/
    key.group=lpn;
    long cache_loc = -1;
    long *loc = &cache_loc;
    buffer_node= (struct buffer_group*)hash_find(ssd,ssd->dram->SSD2_buffer, (HASH_NODE *)&key,buffer_sub->raidNUM, loc);
    //cul hit range to get diff. C-V
    if(cache_loc >= 0 && buffer_sub->lpn != ssd->stripe.checkLpn){
        cache_loc = cache_loc/128;
        for(; cache_loc < 16; cache_loc++){
            ssd->dram->SSD2_buffer->range_write_hit[cache_loc]++;
        }
    }
    /************************************************************************************************
    *û�����С�
    *��һ���������lpn�ж�����ҳ��Ҫд��buffer��ȥ����д�ص�lsn��Ϊ��lpn�ڳ�λ�ã�
    *���ȼ�Ҫ�����free sector����ʾ���ж��ٿ���ֱ��д��buffer�ڵ㣩��
    *���free_sector>=sector_count�����ж���Ŀռ乻lpn������д������Ҫ����д������
    *����û�ж���Ŀռ乩lpn������д����ʱ��Ҫ�ͷ�һ���ֿռ䣬����д�����󡣾�Ҫcreat_sub_request()
    *************************************************************************************************/
    if(buffer_node==NULL)
    {
        free_sector=ssd->dram->SSD2_buffer->max_buffer_sector-ssd->dram->SSD2_buffer->buffer_sector_count;
        if(buffer_sub->lpn != ssd->stripe.checkLpn){
            ssd->dram->SSD2_buffer->write_miss_hit++;
            ssd->dram->SSD2_buffer->window_write_miss++;
        }
        // else{
            // ssd->dram->SSD2_buffer->parity_write_hit++;
        // }
        if(free_sector>=sector_count)
        {
            ssd->dram->SSD2_buffer->write_free++;
            flag=1;
        }
        if(flag==0)
        {
            write_back_count=sector_count-free_sector;
            // write_back_count=sector_count;
            while(write_back_count>0)
            {
                sub_req=NULL;
                pop_sub = ssd->dram->SSD2_buffer->buffer_tail->sub;
                sub_req_state=ssd->dram->SSD2_buffer->buffer_tail->stored;
                sub_req_size=size(ssd->dram->SSD2_buffer->buffer_tail->stored);
                sub_req_lpn=ssd->dram->SSD2_buffer->buffer_tail->group;
                sub_req_first = ssd->dram->SSD2_buffer->buffer_tail->first_write;
//                req->all++;
//                pop_sub->last_sub_pop_flag = last_sub_pop;
                buffer_sub->pop_sub = pop_sub;
                pop_sub->pop_sub_id = buffer_sub->sub_id;
//                pop_sub->prototype_sub = buffer_sub;
//                pop_sub->last_pop_sub = 0;
                pop_sub->pop_patch = buffer_sub_patch;
//                if(first_write == 0){
//                    pop_sub->pop_patch = buffer_sub_patch;
//                }else{
//                    pop_sub->pop_patch = -1;
//                }
                pop_sub->begin_time = buffer_sub->begin_time;
                if(sub_req_first)
                {
                    add_sub2channel(ssd,pop_sub,req);
                }else {
                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;
                    if(ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1){
                        ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                    }else{
                        ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                    }

                    add_sub2channel(ssd,pop_sub,req);
                }
                /**********************************************************************************
                *req��Ϊ�գ���ʾ���insert2buffer��������buffer_management�е��ã�������request����
                *reqΪ�գ���ʾ�����������process�����д���һ�Զ�ӳ���ϵ�Ķ���ʱ����Ҫ���������
                *�����ݼӵ�buffer�У�����ܲ���ʵʱ��д�ز�������Ҫ�����ʵʱ��д�ز��������������
                *������������������
                ***********************************************************************************/
                if(req!=NULL)
                {
                }
                else
                {
//                    sub_req->next_subs=sub->next_subs;
//                    sub->next_subs=sub_req;
                }

                /*********************************************************************
                *д������뵽��ƽ�����������ʱ��Ҫ�޸�dram��buffer_sector_count��
                *ά��ƽ�����������avlTreeDel()��AVL_TREENODE_FREE()������ά��LRU�㷨��
                **********************************************************************/
                ssd->dram->SSD2_buffer->buffer_sector_count=ssd->dram->SSD2_buffer->buffer_sector_count-size(sub_req_state);
                pt = ssd->dram->SSD2_buffer->buffer_tail;

                hash_del(ssd,ssd->dram->SSD2_buffer, (HASH_NODE*)pt,pop_sub->raidNUM);
                if(ssd->dram->SSD2_buffer->buffer_head->LRU_link_next == NULL){
                    ssd->dram->SSD2_buffer->buffer_head = NULL;
                    ssd->dram->SSD2_buffer->buffer_tail = NULL;
                }else{
                    ssd->dram->SSD2_buffer->buffer_tail=ssd->dram->SSD2_buffer->buffer_tail->LRU_link_pre;
                    ssd->dram->SSD2_buffer->buffer_tail->LRU_link_next=NULL;
                }
                pt->LRU_link_next=NULL;
                pt->LRU_link_pre=NULL;

                hash_node_free(ssd->dram->SSD2_buffer, (HASH_NODE *) pt);
                pt = NULL;

                write_back_count=write_back_count-size(sub_req_state);                            /*��Ϊ������ʵʱд�ز�������Ҫ������д�ز�����������*/
            }
        }

        /******************************************************************************
        *����һ��buffer node���������ҳ������ֱ�ֵ��������Ա�����ӵ����׺Ͷ�������
        *******************************************************************************/
        new_node=NULL;
        new_node=(struct buffer_group *)malloc(sizeof(struct buffer_group));
        alloc_assert(new_node,"buffer_group_node");
        memset(new_node,0, sizeof(struct buffer_group));

        new_node->first_write = first_write;
        new_node->sub = buffer_sub;
        new_node->read_count = 0;
        new_node->group=lpn;
        new_node->stored=state;
        new_node->dirty_clean=state;
        new_node->LRU_link_pre = NULL;
        new_node->LRU_link_next=ssd->dram->SSD2_buffer->buffer_head;
        if(ssd->dram->SSD2_buffer->buffer_head != NULL){
            ssd->dram->SSD2_buffer->buffer_head->LRU_link_pre=new_node;
        }else{
            ssd->dram->SSD2_buffer->buffer_tail = new_node;
        }
        ssd->dram->SSD2_buffer->buffer_head=new_node;
        new_node->LRU_link_pre=NULL;

        hash_add(ssd->dram->SSD2_buffer, (HASH_NODE *) new_node);
        ssd->dram->SSD2_buffer->buffer_sector_count += sector_count;
//        ssd->dram->buffer_free_sectors[1].free_sector = ssd->dram->SSD2_buffer->max_buffer_sector-ssd->dram->SSD2_buffer->buffer_sector_count;
    }
        /****************************************************************************************
        *��buffer�����е����
        *��Ȼ�����ˣ��������е�ֻ��lpn���п���������д����ֻ����Ҫдlpn��һpage��ĳ����sub_page
        *��ʱ����Ҫ��һ�����ж�
        *****************************************************************************************/
    else
    {
        if(buffer_sub->lpn != ssd->stripe.checkLpn){
            ssd->dram->SSD2_buffer->write_hit++;
        }
        // else{
            // ssd->dram->SSD2_buffer->parity_write_hit++;
        // }
        unsigned int unHitBit = 0;
        buffer_node->read_count = 0;
        for(i=0;i<ssd->parameter->subpage_page;i++)
        {
            /*************************************************************
            *�ж�state��iλ�ǲ���1
            *�����жϵ�i��sector�Ƿ����buffer�У�1��ʾ���ڣ�0��ʾ�����ڡ�
            **************************************************************/
            if((state>>i)%2!=0)
            {
                lsn=lpn*ssd->parameter->subpage_page+i;
                hit_flag=0;
                hit_flag=(buffer_node->stored)&(0x00000001<<i);

                if(hit_flag!=0)				                                          /*�����ˣ���Ҫ���ýڵ��Ƶ�buffer�Ķ��ף����ҽ����е�lsn���б��*/
                {
                    active_region_flag=1;                                             /*������¼�����buffer node�е�lsn�Ƿ����У����ں������ֵ���ж�*/
                    if(req!=NULL && lpn != ssd->stripe.checkLpn)
                    {
                        if(ssd->dram->SSD2_buffer->buffer_head != buffer_node)
                        {
                            buf_node *preNode  = buffer_node->LRU_link_pre;
                            if(ssd->dram->SSD2_buffer->buffer_tail==buffer_node)
                            {
                                ssd->dram->SSD2_buffer->buffer_tail=buffer_node->LRU_link_pre;
                                buffer_node->LRU_link_pre->LRU_link_next=NULL;
                            }
                            else if(buffer_node != ssd->dram->SSD2_buffer->buffer_head)
                            {
                                buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;
                                buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
                            }
                            if(1){
                                buffer_node->LRU_link_next=ssd->dram->SSD2_buffer->buffer_head;
                                ssd->dram->SSD2_buffer->buffer_head->LRU_link_pre=buffer_node;
                                buffer_node->LRU_link_pre=NULL;
                                ssd->dram->SSD2_buffer->buffer_head=buffer_node;
                            }else{
                                if(preNode == ssd->dram->SSD2_buffer->buffer_head){
                                    buffer_node->LRU_link_next=ssd->dram->SSD2_buffer->buffer_head;
                                    ssd->dram->SSD2_buffer->buffer_head->LRU_link_pre=buffer_node;
                                    buffer_node->LRU_link_pre=NULL;
                                    ssd->dram->SSD2_buffer->buffer_head=buffer_node;
                                }else{
                                    buffer_node->LRU_link_pre = preNode->LRU_link_pre;
                                    buffer_node->LRU_link_next = preNode;

                                    buffer_node->LRU_link_pre->LRU_link_next = buffer_node;
                                    buffer_node->LRU_link_next->LRU_link_pre = buffer_node;
                                }
                            }

                        }
                        //ssd->dram->buffer->write_hit++;
                        req->complete_lsn_count++;                                        /*�ؼ� ����buffer������ʱ ����req->complete_lsn_count++��ʾ��buffer��д�����ݡ�*/
                    }
                    else
                    {
                    }
                }
                else
                {
                    /************************************************************************************************************
                    *��lsnû�����У����ǽڵ���buffer�У���Ҫ�����lsn�ӵ�buffer�Ķ�Ӧ�ڵ���
                    *��buffer��ĩ����һ���ڵ㣬��һ���Ѿ�д�ص�lsn�ӽڵ���ɾ��(����ҵ��Ļ�)����������ڵ��״̬��ͬʱ������µ�
                    *lsn�ӵ���Ӧ��buffer�ڵ��У��ýڵ������bufferͷ�����ڵĻ��������Ƶ�ͷ�������û���ҵ��Ѿ�д�ص�lsn����buffer
                    *�ڵ���һ��group����д�أ�����������������������ϡ�������ǰ����һ��channel�ϡ�
                    *��һ��:��buffer��β���Ѿ�д�صĽڵ�ɾ��һ����Ϊ�µ�lsn�ڳ��ռ䣬������Ҫ�޸Ķ�βĳ�ڵ��stored״̬���ﻹ��Ҫ
                    *       ���ӣ���û�п���֮��ɾ����lsnʱ����Ҫ�����µ�д������д��LRU���Ľڵ㡣
                    *�ڶ���:���µ�lsn�ӵ�������buffer�ڵ��С�
                    *************************************************************************************************************/
                    unHitBit |= (0x00000001 << i);
                    //ssd->dram->buffer->write_miss_hit++;

                    if(ssd->dram->SSD2_buffer->buffer_sector_count>=ssd->dram->SSD2_buffer->max_buffer_sector)
                    {
                        if (buffer_node==ssd->dram->SSD2_buffer->buffer_tail)                  /*������еĽڵ���buffer�����һ���ڵ㣬������������ڵ�*/
                        {
                            pt = ssd->dram->SSD2_buffer->buffer_tail->LRU_link_pre;
                            ssd->dram->SSD2_buffer->buffer_tail->LRU_link_pre=pt->LRU_link_pre;
                            ssd->dram->SSD2_buffer->buffer_tail->LRU_link_pre->LRU_link_next=ssd->dram->SSD2_buffer->buffer_tail;
                            ssd->dram->SSD2_buffer->buffer_tail->LRU_link_next=pt;
                            pt->LRU_link_next=NULL;
                            pt->LRU_link_pre=ssd->dram->SSD2_buffer->buffer_tail;
                            ssd->dram->SSD2_buffer->buffer_tail=pt;

                        }
                        sub_req=NULL;
                        struct sub_request *sub = ssd->dram->SSD2_buffer->buffer_tail->sub;
                        sub_req_state=ssd->dram->SSD2_buffer->buffer_tail->stored;
                        sub_req_size=size(ssd->dram->SSD2_buffer->buffer_tail->stored);
                        sub_req_lpn=ssd->dram->SSD2_buffer->buffer_tail->group;
                        sub_req_channel=ssd->dram->SSD2_buffer->buffer_tail->channel;
//                        req->all++;
                        if(ssd->dram->map->map_entry[sub_req_lpn].state == 0 && ssd->parameter->allocation_scheme==0 && ssd->parameter->dynamic_allocation == 2){
                            creat_sub_write_request_for_raid(ssd,sub_req_lpn, sub_req_state, req, ~(0xffffffff<<(ssd->parameter->subpage_page)));
                        }else {

                            ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;

                            if(ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1){
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                            }else{
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                            }

                            sub_req=creat_sub_request(ssd,sub_req_lpn,sub_req_size,sub_req_state,req,WRITE,0, ssd->page2Trip[sub_req_lpn]);
                        }
                        if(req!=NULL)
                        {

                        }
                        else if(req==NULL)
                        {
                            sub_req->next_subs=sub->next_subs;
                            sub->next_subs=sub_req;
                        }

                        ssd->dram->SSD2_buffer->buffer_sector_count=ssd->dram->SSD2_buffer->buffer_sector_count-size(sub_req_state);
                        pt = ssd->dram->SSD2_buffer->buffer_tail;
                        //avlTreeDel(ssd->dram->buffer, (TREE_NODE *) pt);
                        hash_del(ssd,ssd->dram->SSD2_buffer, (HASH_NODE *) pt,sub->raidNUM);

                        /************************************************************************/
                        /* ��:  ������������buffer�Ľڵ㲻Ӧ����ɾ����						*/
                        /*			��ȵ�д����֮�����ɾ��									*/
                        /************************************************************************/
                        if(ssd->dram->SSD2_buffer->buffer_head->LRU_link_next == NULL)
                        {
                            ssd->dram->SSD2_buffer->buffer_head = NULL;
                            ssd->dram->SSD2_buffer->buffer_tail = NULL;
                        }else{
                            ssd->dram->SSD2_buffer->buffer_tail=ssd->dram->SSD2_buffer->buffer_tail->LRU_link_pre;
                            ssd->dram->SSD2_buffer->buffer_tail->LRU_link_next=NULL;
                        }
                        pt->LRU_link_next=NULL;
                        pt->LRU_link_pre=NULL;
                        //AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *) pt);
                        hash_node_free(ssd->dram->SSD2_buffer, (HASH_NODE *) pt);
                        ssd->dram->buffer->write_miss_hit++;
                        pt = NULL;
                    }

                    /*�ڶ���:���µ�lsn�ӵ�������buffer�ڵ���*/
                    add_flag=0x00000001<<(lsn%ssd->parameter->subpage_page);

                    if(ssd->dram->SSD2_buffer->buffer_head!=buffer_node)                      /*�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ����*/
                    {
                        buf_node *preNode  = buffer_node->LRU_link_pre;
                        if(ssd->dram->SSD2_buffer->buffer_tail==buffer_node)
                        {
                            ssd->dram->SSD2_buffer->buffer_tail=buffer_node->LRU_link_pre;
                            buffer_node->LRU_link_pre->LRU_link_next=NULL;
                        }
                        else if(buffer_node != ssd->dram->SSD2_buffer->buffer_head)
                        {
                            buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;
                            buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
                        }
                        if(1){
                            buffer_node->LRU_link_next=ssd->dram->SSD2_buffer->buffer_head;
                            ssd->dram->SSD2_buffer->buffer_head->LRU_link_pre=buffer_node;
                            buffer_node->LRU_link_pre=NULL;
                            ssd->dram->SSD2_buffer->buffer_head=buffer_node;
                        }else{
                            if(preNode == ssd->dram->SSD2_buffer->buffer_head){
                                buffer_node->LRU_link_next=ssd->dram->SSD2_buffer->buffer_head;
                                ssd->dram->SSD2_buffer->buffer_head->LRU_link_pre=buffer_node;
                                buffer_node->LRU_link_pre=NULL;
                                ssd->dram->SSD2_buffer->buffer_head=buffer_node;
                            }else{
                                buffer_node->LRU_link_pre = preNode->LRU_link_pre;
                                buffer_node->LRU_link_next = preNode;

                                buffer_node->LRU_link_pre->LRU_link_next = buffer_node;
                                buffer_node->LRU_link_next->LRU_link_pre = buffer_node;
                            }
                        }
                    }
                    buffer_node->stored=buffer_node->stored|add_flag;
                    buffer_node->dirty_clean=buffer_node->dirty_clean|add_flag;
//                    ssd->dram->SSD2_buffer->buffer_sector_count++;
                }

            }
        }
    }

    return ssd;
}

struct ssd_info * insert_2_SSD3_buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write,int last_sub_pop)
{
    int write_back_count,flag=0;                                                             /*flag��ʾΪд���������ڿռ��Ƿ���ɣ�0��ʾ��Ҫ��һ���ڣ�1��ʾ�Ѿ��ڿ�*/
    int buffer_sub_patch = buffer_sub->patch;
    unsigned int i,lsn,hit_flag,add_flag,sector_count,active_region_flag=0,free_sector=0;
    struct buffer_group *buffer_node=NULL,*pt,*new_node=NULL,key;
    struct sub_request *sub_req=NULL,*update=NULL,*pop_sub=NULL;

    unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0,sub_req_channel=0,sub_req_first=0;

#ifdef DEBUG
    printf("enter insert2buffer,  current time:%lld, lpn:%d, state:%d,\n",ssd->current_time,lpn,state);
#endif
    state = ~(0xffffffff<<(ssd->parameter->subpage_page));
    sector_count=size(state);                                                                 /*��Ҫд��buffer��sector����*/
    key.group=lpn;
    long cache_loc = -1;
    long *loc = &cache_loc;
    buffer_node= (struct buffer_group*)hash_find(ssd,ssd->dram->SSD3_buffer, (HASH_NODE *)&key,buffer_sub->raidNUM, loc);
    //cul hit range to get diff. C-V
    if(cache_loc >= 0 && buffer_sub->lpn != ssd->stripe.checkLpn){
        cache_loc = cache_loc/128;
        for(; cache_loc < 16; cache_loc++){
            ssd->dram->SSD3_buffer->range_write_hit[cache_loc]++;
        }
    }
    /************************************************************************************************
    *û�����С�
    *��һ���������lpn�ж�����ҳ��Ҫд��buffer��ȥ����д�ص�lsn��Ϊ��lpn�ڳ�λ�ã�
    *���ȼ�Ҫ�����free sector����ʾ���ж��ٿ���ֱ��д��buffer�ڵ㣩��
    *���free_sector>=sector_count�����ж���Ŀռ乻lpn������д������Ҫ����д������
    *����û�ж���Ŀռ乩lpn������д����ʱ��Ҫ�ͷ�һ���ֿռ䣬����д�����󡣾�Ҫcreat_sub_request()
    *************************************************************************************************/
    if(buffer_node==NULL)
    {
        free_sector=ssd->dram->SSD3_buffer->max_buffer_sector-ssd->dram->SSD3_buffer->buffer_sector_count;
        if(buffer_sub->lpn != ssd->stripe.checkLpn){
            ssd->dram->SSD3_buffer->write_miss_hit++;
            ssd->dram->SSD3_buffer->window_write_miss++;
        }
        // else{
            // ssd->dram->SSD3_buffer->parity_write_miss_hit++;
        // }
        if(free_sector>=sector_count)
        {
            ssd->dram->SSD3_buffer->write_free++;
            flag=1;
        }
        if(flag==0)
        {
            write_back_count=sector_count-free_sector;
            // write_back_count=sector_count;
            while(write_back_count>0)
            {
                sub_req=NULL;
                pop_sub = ssd->dram->SSD3_buffer->buffer_tail->sub;
                sub_req_state=ssd->dram->SSD3_buffer->buffer_tail->stored;
                sub_req_size=size(ssd->dram->SSD3_buffer->buffer_tail->stored);
                sub_req_lpn=ssd->dram->SSD3_buffer->buffer_tail->group;
                sub_req_first = ssd->dram->SSD3_buffer->buffer_tail->first_write;
//                req->all++;
//                pop_sub->last_sub_pop_flag = last_sub_pop;
                buffer_sub->pop_sub = pop_sub;
                pop_sub->pop_sub_id = buffer_sub->sub_id;
//                pop_sub->prototype_sub = buffer_sub;
//                pop_sub->last_pop_sub = 0;
                pop_sub->pop_patch = buffer_sub_patch;
//                if(first_write == 0){
//                    pop_sub->pop_patch = buffer_sub_patch;
//                }else{
//                    pop_sub->pop_patch = -1;
//                }
                pop_sub->begin_time = buffer_sub->begin_time;
                if(sub_req_first)
                {
                    add_sub2channel(ssd,pop_sub,req);
                }else {
                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;
                    if(ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1){
                        ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                    }else{
                        ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                    }

                    add_sub2channel(ssd,pop_sub,req);
                }
                /**********************************************************************************
                *req��Ϊ�գ���ʾ���insert2buffer��������buffer_management�е��ã�������request����
                *reqΪ�գ���ʾ�����������process�����д���һ�Զ�ӳ���ϵ�Ķ���ʱ����Ҫ���������
                *�����ݼӵ�buffer�У�����ܲ���ʵʱ��д�ز�������Ҫ�����ʵʱ��д�ز��������������
                *������������������
                ***********************************************************************************/
                if(req!=NULL)
                {
                }
                else
                {
//                    sub_req->next_subs=sub->next_subs;
//                    sub->next_subs=sub_req;
                }

                /*********************************************************************
                *д������뵽��ƽ�����������ʱ��Ҫ�޸�dram��buffer_sector_count��
                *ά��ƽ�����������avlTreeDel()��AVL_TREENODE_FREE()������ά��LRU�㷨��
                **********************************************************************/
                ssd->dram->SSD3_buffer->buffer_sector_count=ssd->dram->SSD3_buffer->buffer_sector_count-size(sub_req_state);
                pt = ssd->dram->SSD3_buffer->buffer_tail;

                hash_del(ssd,ssd->dram->SSD3_buffer,  (HASH_NODE*)pt,pop_sub->raidNUM);
                if(ssd->dram->SSD3_buffer->buffer_head->LRU_link_next == NULL){
                    ssd->dram->SSD3_buffer->buffer_head = NULL;
                    ssd->dram->SSD3_buffer->buffer_tail = NULL;
                }else{
                    ssd->dram->SSD3_buffer->buffer_tail=ssd->dram->SSD3_buffer->buffer_tail->LRU_link_pre;
                    ssd->dram->SSD3_buffer->buffer_tail->LRU_link_next=NULL;
                }
                pt->LRU_link_next=NULL;
                pt->LRU_link_pre=NULL;

                hash_node_free(ssd->dram->SSD3_buffer, (HASH_NODE *) pt);
                pt = NULL;

                write_back_count=write_back_count-size(sub_req_state);                            /*��Ϊ������ʵʱд�ز�������Ҫ������д�ز�����������*/
            }
        }

        /******************************************************************************
        *����һ��buffer node���������ҳ������ֱ�ֵ��������Ա�����ӵ����׺Ͷ�������
        *******************************************************************************/
        new_node=NULL;
        new_node=(struct buffer_group *)malloc(sizeof(struct buffer_group));
        alloc_assert(new_node,"buffer_group_node");
        memset(new_node,0, sizeof(struct buffer_group));

        new_node->first_write = first_write;
        new_node->sub = buffer_sub;
        new_node->read_count = 0;
        new_node->group=lpn;
        new_node->stored=state;
        new_node->dirty_clean=state;
        new_node->LRU_link_pre = NULL;
        new_node->LRU_link_next=ssd->dram->SSD3_buffer->buffer_head;
        if(ssd->dram->SSD3_buffer->buffer_head != NULL){
            ssd->dram->SSD3_buffer->buffer_head->LRU_link_pre=new_node;
        }else{
            ssd->dram->SSD3_buffer->buffer_tail = new_node;
        }
        ssd->dram->SSD3_buffer->buffer_head=new_node;
        new_node->LRU_link_pre=NULL;

        hash_add(ssd->dram->SSD3_buffer, (HASH_NODE *) new_node);
        ssd->dram->SSD3_buffer->buffer_sector_count += sector_count;
    }
        /****************************************************************************************
        *��buffer�����е����
        *��Ȼ�����ˣ��������е�ֻ��lpn���п���������д����ֻ����Ҫдlpn��һpage��ĳ����sub_page
        *��ʱ����Ҫ��һ�����ж�
        *****************************************************************************************/
    else
    {
        if(buffer_sub->lpn != ssd->stripe.checkLpn){
            ssd->dram->SSD3_buffer->write_hit++;
        }
        // else{
            // ssd->dram->SSD3_buffer->parity_write_hit;
        // }
        unsigned int unHitBit = 0;
        buffer_node->read_count = 0;
        for(i=0;i<ssd->parameter->subpage_page;i++)
        {
            /*************************************************************
            *�ж�state��iλ�ǲ���1
            *�����жϵ�i��sector�Ƿ����buffer�У�1��ʾ���ڣ�0��ʾ�����ڡ�
            **************************************************************/
            if((state>>i)%2!=0)
            {
                lsn=lpn*ssd->parameter->subpage_page+i;
                hit_flag=0;
                hit_flag=(buffer_node->stored)&(0x00000001<<i);

                if(hit_flag!=0)				                                          /*�����ˣ���Ҫ���ýڵ��Ƶ�buffer�Ķ��ף����ҽ����е�lsn���б��*/
                {
                    active_region_flag=1;                                             /*������¼�����buffer node�е�lsn�Ƿ����У����ں������ֵ���ж�*/
                    if(req!=NULL && lpn != ssd->stripe.checkLpn)
                    {
                        if(ssd->dram->SSD3_buffer->buffer_head != buffer_node)
                        {
                            buf_node *preNode  = buffer_node->LRU_link_pre;
                            if(ssd->dram->SSD3_buffer->buffer_tail==buffer_node)
                            {
                                ssd->dram->SSD3_buffer->buffer_tail=buffer_node->LRU_link_pre;
                                buffer_node->LRU_link_pre->LRU_link_next=NULL;
                            }
                            else if(buffer_node != ssd->dram->SSD3_buffer->buffer_head)
                            {
                                buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;
                                buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
                            }
                            if(1){
                                buffer_node->LRU_link_next=ssd->dram->SSD3_buffer->buffer_head;
                                ssd->dram->SSD3_buffer->buffer_head->LRU_link_pre=buffer_node;
                                buffer_node->LRU_link_pre=NULL;
                                ssd->dram->SSD3_buffer->buffer_head=buffer_node;
                            }else{
                                if(preNode == ssd->dram->SSD3_buffer->buffer_head){
                                    buffer_node->LRU_link_next=ssd->dram->SSD3_buffer->buffer_head;
                                    ssd->dram->SSD3_buffer->buffer_head->LRU_link_pre=buffer_node;
                                    buffer_node->LRU_link_pre=NULL;
                                    ssd->dram->SSD3_buffer->buffer_head=buffer_node;
                                }else{
                                    buffer_node->LRU_link_pre = preNode->LRU_link_pre;
                                    buffer_node->LRU_link_next = preNode;

                                    buffer_node->LRU_link_pre->LRU_link_next = buffer_node;
                                    buffer_node->LRU_link_next->LRU_link_pre = buffer_node;
                                }
                            }

                        }
                        //ssd->dram->buffer->write_hit++;
                        req->complete_lsn_count++;                                        /*�ؼ� ����buffer������ʱ ����req->complete_lsn_count++��ʾ��buffer��д�����ݡ�*/
                    }
                    else
                    {
                    }
                }
                else
                {
                    /************************************************************************************************************
                    *��lsnû�����У����ǽڵ���buffer�У���Ҫ�����lsn�ӵ�buffer�Ķ�Ӧ�ڵ���
                    *��buffer��ĩ����һ���ڵ㣬��һ���Ѿ�д�ص�lsn�ӽڵ���ɾ��(����ҵ��Ļ�)����������ڵ��״̬��ͬʱ������µ�
                    *lsn�ӵ���Ӧ��buffer�ڵ��У��ýڵ������bufferͷ�����ڵĻ��������Ƶ�ͷ�������û���ҵ��Ѿ�д�ص�lsn����buffer
                    *�ڵ���һ��group����д�أ�����������������������ϡ�������ǰ����һ��channel�ϡ�
                    *��һ��:��buffer��β���Ѿ�д�صĽڵ�ɾ��һ����Ϊ�µ�lsn�ڳ��ռ䣬������Ҫ�޸Ķ�βĳ�ڵ��stored״̬���ﻹ��Ҫ
                    *       ���ӣ���û�п���֮��ɾ����lsnʱ����Ҫ�����µ�д������д��LRU���Ľڵ㡣
                    *�ڶ���:���µ�lsn�ӵ�������buffer�ڵ��С�
                    *************************************************************************************************************/
                    unHitBit |= (0x00000001 << i);

                    if(ssd->dram->SSD3_buffer->buffer_sector_count>=ssd->dram->SSD3_buffer->max_buffer_sector)
                    {
                        if (buffer_node==ssd->dram->SSD3_buffer->buffer_tail)                  /*������еĽڵ���buffer�����һ���ڵ㣬������������ڵ�*/
                        {
                            pt = ssd->dram->SSD3_buffer->buffer_tail->LRU_link_pre;
                            ssd->dram->SSD3_buffer->buffer_tail->LRU_link_pre=pt->LRU_link_pre;
                            ssd->dram->SSD3_buffer->buffer_tail->LRU_link_pre->LRU_link_next=ssd->dram->SSD3_buffer->buffer_tail;
                            ssd->dram->SSD3_buffer->buffer_tail->LRU_link_next=pt;
                            pt->LRU_link_next=NULL;
                            pt->LRU_link_pre=ssd->dram->SSD3_buffer->buffer_tail;
                            ssd->dram->SSD3_buffer->buffer_tail=pt;

                        }
                        sub_req=NULL;
                        struct sub_request *sub = ssd->dram->SSD3_buffer->buffer_tail->sub;
                        sub_req_state=ssd->dram->SSD3_buffer->buffer_tail->stored;
                        sub_req_size=size(ssd->dram->SSD3_buffer->buffer_tail->stored);
                        sub_req_lpn=ssd->dram->SSD3_buffer->buffer_tail->group;
                        sub_req_channel=ssd->dram->SSD3_buffer->buffer_tail->channel;
//                        req->all++;
                        if(ssd->dram->map->map_entry[sub_req_lpn].state == 0 && ssd->parameter->allocation_scheme==0 && ssd->parameter->dynamic_allocation == 2){
                            creat_sub_write_request_for_raid(ssd,sub_req_lpn, sub_req_state, req, ~(0xffffffff<<(ssd->parameter->subpage_page)));
                        }else {

                            ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;

                            if(ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1){
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                            }else{
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                            }

                            sub_req=creat_sub_request(ssd,sub_req_lpn,sub_req_size,sub_req_state,req,WRITE,0, ssd->page2Trip[sub_req_lpn]);
                        }
                        if(req!=NULL)
                        {

                        }
                        else if(req==NULL)
                        {
                            sub_req->next_subs=sub->next_subs;
                            sub->next_subs=sub_req;
                        }

                        ssd->dram->SSD3_buffer->buffer_sector_count=ssd->dram->SSD3_buffer->buffer_sector_count-size(sub_req_state);
                        pt = ssd->dram->SSD3_buffer->buffer_tail;
                        //avlTreeDel(ssd->dram->buffer, (TREE_NODE *) pt);
                        hash_del(ssd,ssd->dram->SSD3_buffer, (HASH_NODE *) pt,sub->raidNUM);

                        /************************************************************************/
                        /* ��:  ������������buffer�Ľڵ㲻Ӧ����ɾ����						*/
                        /*			��ȵ�д����֮�����ɾ��									*/
                        /************************************************************************/
                        if(ssd->dram->SSD3_buffer->buffer_head->LRU_link_next == NULL)
                        {
                            ssd->dram->SSD3_buffer->buffer_head = NULL;
                            ssd->dram->SSD3_buffer->buffer_tail = NULL;
                        }else{
                            ssd->dram->SSD3_buffer->buffer_tail=ssd->dram->SSD3_buffer->buffer_tail->LRU_link_pre;
                            ssd->dram->SSD3_buffer->buffer_tail->LRU_link_next=NULL;
                        }
                        pt->LRU_link_next=NULL;
                        pt->LRU_link_pre=NULL;
                        //AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *) pt);
                        hash_node_free(ssd->dram->SSD3_buffer, (HASH_NODE *) pt);
                        ssd->dram->buffer->write_miss_hit++;
                        pt = NULL;
                    }

                    /*�ڶ���:���µ�lsn�ӵ�������buffer�ڵ���*/
                    add_flag=0x00000001<<(lsn%ssd->parameter->subpage_page);

                    if(ssd->dram->SSD3_buffer->buffer_head!=buffer_node)                      /*�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ����*/
                    {
                        buf_node *preNode  = buffer_node->LRU_link_pre;
                        if(ssd->dram->SSD3_buffer->buffer_tail==buffer_node)
                        {
                            ssd->dram->SSD3_buffer->buffer_tail=buffer_node->LRU_link_pre;
                            buffer_node->LRU_link_pre->LRU_link_next=NULL;
                        }
                        else if(buffer_node != ssd->dram->SSD3_buffer->buffer_head)
                        {
                            buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;
                            buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
                        }
                        if(1){
                            buffer_node->LRU_link_next=ssd->dram->SSD3_buffer->buffer_head;
                            ssd->dram->SSD3_buffer->buffer_head->LRU_link_pre=buffer_node;
                            buffer_node->LRU_link_pre=NULL;
                            ssd->dram->SSD3_buffer->buffer_head=buffer_node;
                        }else{
                            if(preNode == ssd->dram->SSD3_buffer->buffer_head){
                                buffer_node->LRU_link_next=ssd->dram->SSD3_buffer->buffer_head;
                                ssd->dram->SSD3_buffer->buffer_head->LRU_link_pre=buffer_node;
                                buffer_node->LRU_link_pre=NULL;
                                ssd->dram->SSD3_buffer->buffer_head=buffer_node;
                            }else{
                                buffer_node->LRU_link_pre = preNode->LRU_link_pre;
                                buffer_node->LRU_link_next = preNode;

                                buffer_node->LRU_link_pre->LRU_link_next = buffer_node;
                                buffer_node->LRU_link_next->LRU_link_pre = buffer_node;
                            }
                        }
                    }
                    buffer_node->stored=buffer_node->stored|add_flag;
                    buffer_node->dirty_clean=buffer_node->dirty_clean|add_flag;
                    ssd->dram->SSD3_buffer->buffer_sector_count++;
                }

            }
        }

    }

    return ssd;
}

struct ssd_info * insert_2_SSD4_buffer(struct ssd_info *ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write,int last_sub_pop)
{
    int write_back_count,flag=0;                                                             /*flag��ʾΪд���������ڿռ��Ƿ���ɣ�0��ʾ��Ҫ��һ���ڣ�1��ʾ�Ѿ��ڿ�*/
    int buffer_sub_patch = buffer_sub->patch;
    unsigned int i,lsn,hit_flag,add_flag,sector_count,active_region_flag=0,free_sector=0;
    struct buffer_group *buffer_node=NULL,*pt,*new_node=NULL,key;
    struct sub_request *sub_req=NULL,*update=NULL,*pop_sub=NULL;

    unsigned int sub_req_state=0, sub_req_size=0,sub_req_lpn=0,sub_req_channel=0,sub_req_first=0;

#ifdef DEBUG
    printf("enter insert2buffer,  current time:%lld, lpn:%d, state:%d,\n",ssd->current_time,lpn,state);
#endif
    state = ~(0xffffffff<<(ssd->parameter->subpage_page));
    sector_count=size(state);                                                               /*��Ҫд��buffer��sector����*/
    key.group=lpn;
    long cache_loc = -1;
    long *loc = &cache_loc;
    buffer_node= (struct buffer_group*)hash_find(ssd,ssd->dram->SSD4_buffer, (HASH_NODE *)&key,buffer_sub->raidNUM, loc);
    //cul hit range to get diff. C-V
    if(cache_loc >= 0 && buffer_sub->lpn != ssd->stripe.checkLpn){
        cache_loc = cache_loc/128;
        for(; cache_loc < 16; cache_loc++){
            ssd->dram->SSD4_buffer->range_write_hit[cache_loc]++;
        }
    }
    /************************************************************************************************
    *û�����С�
    *��һ���������lpn�ж�����ҳ��Ҫд��buffer��ȥ����д�ص�lsn��Ϊ��lpn�ڳ�λ�ã�
    *���ȼ�Ҫ�����free sector����ʾ���ж��ٿ���ֱ��д��buffer�ڵ㣩��
    *���free_sector>=sector_count�����ж���Ŀռ乻lpn������д������Ҫ����д������
    *����û�ж���Ŀռ乩lpn������д����ʱ��Ҫ�ͷ�һ���ֿռ䣬����д�����󡣾�Ҫcreat_sub_request()
    *************************************************************************************************/
    if(buffer_node==NULL)
    {
        free_sector=ssd->dram->SSD4_buffer->max_buffer_sector-ssd->dram->SSD4_buffer->buffer_sector_count;
        if(buffer_sub->lpn != ssd->stripe.checkLpn){
            ssd->dram->SSD4_buffer->write_miss_hit++;
            ssd->dram->SSD4_buffer->window_write_miss++;
        }
        // else{
            // ssd->dram->SSD4_buffer->parity_write_miss_hit++;
        // }
        if(free_sector>=sector_count)
        {
            ssd->dram->SSD4_buffer->write_free++;
            flag=1;
        }
        if(flag==0)
        {
            write_back_count=sector_count-free_sector;
            // write_back_count=sector_count;
            while(write_back_count>0)
            {
                sub_req=NULL;
                pop_sub = ssd->dram->SSD4_buffer->buffer_tail->sub;
                sub_req_state=ssd->dram->SSD4_buffer->buffer_tail->stored;
                sub_req_size=size(ssd->dram->SSD4_buffer->buffer_tail->stored);
                sub_req_lpn=ssd->dram->SSD4_buffer->buffer_tail->group;
                sub_req_first = ssd->dram->SSD4_buffer->buffer_tail->first_write;
//                req->all++;
//                pop_sub->last_sub_pop_flag = last_sub_pop;
                buffer_sub->pop_sub = pop_sub;
                pop_sub->pop_sub_id = buffer_sub->sub_id;
//                pop_sub->prototype_sub = buffer_sub;
//                pop_sub->last_pop_sub = 0;
                pop_sub->pop_patch = buffer_sub_patch;
//                if(first_write == 0){
////                    pop_sub->patch = buffer_sub_patch;
//                    pop_sub->pop_patch = buffer_sub_patch;
//                }else{
////                    pop_sub->patch = -1;
//                    pop_sub->pop_patch = -1;
//                }
                pop_sub->begin_time = buffer_sub->begin_time;
                if(sub_req_first)
                {
                    add_sub2channel(ssd,pop_sub,req);
                }else {
                    ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;
                    if(ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1){
                        ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                    }else{
                        ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                    }

                    add_sub2channel(ssd,pop_sub,req);
                }
                /**********************************************************************************
                *req��Ϊ�գ���ʾ���insert2buffer��������buffer_management�е��ã�������request����
                *reqΪ�գ���ʾ�����������process�����д���һ�Զ�ӳ���ϵ�Ķ���ʱ����Ҫ���������
                *�����ݼӵ�buffer�У�����ܲ���ʵʱ��д�ز�������Ҫ�����ʵʱ��д�ز��������������
                *������������������
                ***********************************************************************************/
                if(req!=NULL)
                {
                }
                else
                {
//                    sub_req->next_subs=sub->next_subs;
//                    sub->next_subs=sub_req;
                }

                /*********************************************************************
                *д������뵽��ƽ�����������ʱ��Ҫ�޸�dram��buffer_sector_count��
                *ά��ƽ�����������avlTreeDel()��AVL_TREENODE_FREE()������ά��LRU�㷨��
                **********************************************************************/
                ssd->dram->SSD4_buffer->buffer_sector_count=ssd->dram->SSD4_buffer->buffer_sector_count-size(sub_req_state);
                pt = ssd->dram->SSD4_buffer->buffer_tail;

                hash_del(ssd,ssd->dram->SSD4_buffer, (HASH_NODE*)pt,pop_sub->raidNUM);
                if(ssd->dram->SSD4_buffer->buffer_head->LRU_link_next == NULL){
                    ssd->dram->SSD4_buffer->buffer_head = NULL;
                    ssd->dram->SSD4_buffer->buffer_tail = NULL;
                }else{
                    ssd->dram->SSD4_buffer->buffer_tail=ssd->dram->SSD4_buffer->buffer_tail->LRU_link_pre;
                    ssd->dram->SSD4_buffer->buffer_tail->LRU_link_next=NULL;
                }
                pt->LRU_link_next=NULL;
                pt->LRU_link_pre=NULL;

                hash_node_free(ssd->dram->SSD4_buffer, (HASH_NODE *) pt);
                pt = NULL;

                write_back_count=write_back_count-size(sub_req_state);                            /*��Ϊ������ʵʱд�ز�������Ҫ������д�ز�����������*/
            }
        }

        /******************************************************************************
        *����һ��buffer node���������ҳ������ֱ�ֵ��������Ա�����ӵ����׺Ͷ�������
        *******************************************************************************/
        new_node=NULL;
        new_node=(struct buffer_group *)malloc(sizeof(struct buffer_group));
        alloc_assert(new_node,"buffer_group_node");
        memset(new_node,0, sizeof(struct buffer_group));

        new_node->first_write = first_write;
        new_node->sub = buffer_sub;
        new_node->read_count = 0;
        new_node->group=lpn;
        new_node->stored=state;
        new_node->dirty_clean=state;
        new_node->LRU_link_pre = NULL;
        new_node->LRU_link_next=ssd->dram->SSD4_buffer->buffer_head;
        if(ssd->dram->SSD4_buffer->buffer_head != NULL){
            ssd->dram->SSD4_buffer->buffer_head->LRU_link_pre=new_node;
        }else{
            ssd->dram->SSD4_buffer->buffer_tail = new_node;
        }
        ssd->dram->SSD4_buffer->buffer_head=new_node;
        new_node->LRU_link_pre=NULL;

        hash_add(ssd->dram->SSD4_buffer, (HASH_NODE *) new_node);
        ssd->dram->SSD4_buffer->buffer_sector_count += sector_count;
//        ssd->dram->buffer_free_sectors[3].free_sector = ssd->dram->SSD4_buffer->max_buffer_sector-ssd->dram->SSD4_buffer->buffer_sector_count;
    }
        /****************************************************************************************
        *��buffer�����е����
        *��Ȼ�����ˣ��������е�ֻ��lpn���п���������д����ֻ����Ҫдlpn��һpage��ĳ����sub_page
        *��ʱ����Ҫ��һ�����ж�
        *****************************************************************************************/
    else
    {
        if(buffer_sub->lpn != ssd->stripe.checkLpn){
            ssd->dram->SSD4_buffer->write_hit++;
        }
        // else{
            // ssd->dram->SSD4_buffer->parity_write_hit++;
        // }
        unsigned int unHitBit = 0;
        buffer_node->read_count = 0;
        for(i=0;i<ssd->parameter->subpage_page;i++)
        {
            /*************************************************************
            *�ж�state��iλ�ǲ���1
            *�����жϵ�i��sector�Ƿ����buffer�У�1��ʾ���ڣ�0��ʾ�����ڡ�
            **************************************************************/
            if((state>>i)%2!=0)
            {
                lsn=lpn*ssd->parameter->subpage_page+i;
                hit_flag=0;
                hit_flag=(buffer_node->stored)&(0x00000001<<i);

                if(hit_flag!=0)				                                          /*�����ˣ���Ҫ���ýڵ��Ƶ�buffer�Ķ��ף����ҽ����е�lsn���б��*/
                {
                    active_region_flag=1;                                             /*������¼�����buffer node�е�lsn�Ƿ����У����ں������ֵ���ж�*/
                    if(req!=NULL && lpn != ssd->stripe.checkLpn)
                    {
                        if(ssd->dram->SSD4_buffer->buffer_head != buffer_node)
                        {
                            buf_node *preNode  = buffer_node->LRU_link_pre;
                            if(ssd->dram->SSD4_buffer->buffer_tail==buffer_node)
                            {
                                ssd->dram->SSD4_buffer->buffer_tail=buffer_node->LRU_link_pre;
                                buffer_node->LRU_link_pre->LRU_link_next=NULL;
                            }
                            else if(buffer_node != ssd->dram->SSD4_buffer->buffer_head)
                            {
                                buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;
                                buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
                            }
                            if(1){
                                buffer_node->LRU_link_next=ssd->dram->SSD4_buffer->buffer_head;
                                ssd->dram->SSD4_buffer->buffer_head->LRU_link_pre=buffer_node;
                                buffer_node->LRU_link_pre=NULL;
                                ssd->dram->SSD4_buffer->buffer_head=buffer_node;
                            }else{
                                if(preNode == ssd->dram->SSD4_buffer->buffer_head){
                                    buffer_node->LRU_link_next=ssd->dram->SSD4_buffer->buffer_head;
                                    ssd->dram->SSD4_buffer->buffer_head->LRU_link_pre=buffer_node;
                                    buffer_node->LRU_link_pre=NULL;
                                    ssd->dram->SSD4_buffer->buffer_head=buffer_node;
                                }else{
                                    buffer_node->LRU_link_pre = preNode->LRU_link_pre;
                                    buffer_node->LRU_link_next = preNode;

                                    buffer_node->LRU_link_pre->LRU_link_next = buffer_node;
                                    buffer_node->LRU_link_next->LRU_link_pre = buffer_node;
                                }
                            }

                        }
                        //ssd->dram->buffer->write_hit++;
                        req->complete_lsn_count++;                                        /*�ؼ� ����buffer������ʱ ����req->complete_lsn_count++��ʾ��buffer��д�����ݡ�*/
                    }
                    else
                    {
                    }
                }
                else
                {
                    /************************************************************************************************************
                    *��lsnû�����У����ǽڵ���buffer�У���Ҫ�����lsn�ӵ�buffer�Ķ�Ӧ�ڵ���
                    *��buffer��ĩ����һ���ڵ㣬��һ���Ѿ�д�ص�lsn�ӽڵ���ɾ��(����ҵ��Ļ�)����������ڵ��״̬��ͬʱ������µ�
                    *lsn�ӵ���Ӧ��buffer�ڵ��У��ýڵ������bufferͷ�����ڵĻ��������Ƶ�ͷ�������û���ҵ��Ѿ�д�ص�lsn����buffer
                    *�ڵ���һ��group����д�أ�����������������������ϡ�������ǰ����һ��channel�ϡ�
                    *��һ��:��buffer��β���Ѿ�д�صĽڵ�ɾ��һ����Ϊ�µ�lsn�ڳ��ռ䣬������Ҫ�޸Ķ�βĳ�ڵ��stored״̬���ﻹ��Ҫ
                    *       ���ӣ���û�п���֮��ɾ����lsnʱ����Ҫ�����µ�д������д��LRU���Ľڵ㡣
                    *�ڶ���:���µ�lsn�ӵ�������buffer�ڵ��С�
                    *************************************************************************************************************/
                    unHitBit |= (0x00000001 << i);
                    //ssd->dram->buffer->write_miss_hit++;

                    if(ssd->dram->SSD4_buffer->buffer_sector_count>=ssd->dram->SSD4_buffer->max_buffer_sector)
                    {
                        if (buffer_node==ssd->dram->SSD4_buffer->buffer_tail)                  /*������еĽڵ���buffer�����һ���ڵ㣬������������ڵ�*/
                        {
                            pt = ssd->dram->SSD4_buffer->buffer_tail->LRU_link_pre;
                            ssd->dram->SSD4_buffer->buffer_tail->LRU_link_pre=pt->LRU_link_pre;
                            ssd->dram->SSD4_buffer->buffer_tail->LRU_link_pre->LRU_link_next=ssd->dram->SSD4_buffer->buffer_tail;
                            ssd->dram->SSD4_buffer->buffer_tail->LRU_link_next=pt;
                            pt->LRU_link_next=NULL;
                            pt->LRU_link_pre=ssd->dram->SSD4_buffer->buffer_tail;
                            ssd->dram->SSD4_buffer->buffer_tail=pt;
                        }
                        sub_req=NULL;
                        struct sub_request *sub = ssd->dram->SSD4_buffer->buffer_tail->sub;
                        sub_req_state=ssd->dram->SSD4_buffer->buffer_tail->stored;
                        sub_req_size=size(ssd->dram->SSD4_buffer->buffer_tail->stored);
                        sub_req_lpn=ssd->dram->SSD4_buffer->buffer_tail->group;
                        sub_req_channel=ssd->dram->SSD4_buffer->buffer_tail->channel;
//                        req->all++;
                        if(ssd->dram->map->map_entry[sub_req_lpn].state == 0 && ssd->parameter->allocation_scheme==0 && ssd->parameter->dynamic_allocation == 2){
                            creat_sub_write_request_for_raid(ssd,sub_req_lpn, sub_req_state, req, ~(0xffffffff<<(ssd->parameter->subpage_page)));
                        }else {
                            ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange++;

                            if(ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].allChange == 1){
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState = sub_req_state;
                            }else{
                                ssd->trip2Page[ssd->page2Trip[sub_req_lpn]].changeState |= sub_req_state;
                            }

                            sub_req=creat_sub_request(ssd,sub_req_lpn,sub_req_size,sub_req_state,req,WRITE,0, ssd->page2Trip[sub_req_lpn]);
                        }
                        if(req!=NULL)
                        {

                        }
                        else if(req==NULL)
                        {
                            sub_req->next_subs=sub->next_subs;
                            sub->next_subs=sub_req;
                        }

                        ssd->dram->SSD4_buffer->buffer_sector_count=ssd->dram->SSD4_buffer->buffer_sector_count-size(sub_req_state);
                        pt = ssd->dram->SSD4_buffer->buffer_tail;
                        //avlTreeDel(ssd->dram->buffer, (TREE_NODE *) pt);
                        hash_del(ssd,ssd->dram->SSD4_buffer, (HASH_NODE *) pt,sub->raidNUM);

                        /************************************************************************/
                        /* ��:  ������������buffer�Ľڵ㲻Ӧ����ɾ����						*/
                        /*			��ȵ�д����֮�����ɾ��									*/
                        /************************************************************************/
                        if(ssd->dram->SSD4_buffer->buffer_head->LRU_link_next == NULL)
                        {
                            ssd->dram->SSD4_buffer->buffer_head = NULL;
                            ssd->dram->SSD4_buffer->buffer_tail = NULL;
                        }else{
                            ssd->dram->SSD4_buffer->buffer_tail=ssd->dram->SSD4_buffer->buffer_tail->LRU_link_pre;
                            ssd->dram->SSD4_buffer->buffer_tail->LRU_link_next=NULL;
                        }
                        pt->LRU_link_next=NULL;
                        pt->LRU_link_pre=NULL;
                        //AVL_TREENODE_FREE(ssd->dram->buffer, (TREE_NODE *) pt);
                        hash_node_free(ssd->dram->SSD4_buffer, (HASH_NODE *) pt);
                        ssd->dram->buffer->write_miss_hit++;
                        pt = NULL;
                    }

                    /*�ڶ���:���µ�lsn�ӵ�������buffer�ڵ���*/
                    add_flag=0x00000001<<(lsn%ssd->parameter->subpage_page);

                    if(ssd->dram->SSD4_buffer->buffer_head!=buffer_node)                      /*�����buffer�ڵ㲻��buffer�Ķ��ף���Ҫ������ڵ��ᵽ����*/
                    {
                        buf_node *preNode  = buffer_node->LRU_link_pre;
                        if(ssd->dram->SSD4_buffer->buffer_tail==buffer_node)
                        {
                            ssd->dram->SSD4_buffer->buffer_tail=buffer_node->LRU_link_pre;
                            buffer_node->LRU_link_pre->LRU_link_next=NULL;
                        }
                        else if(buffer_node != ssd->dram->SSD4_buffer->buffer_head)
                        {
                            buffer_node->LRU_link_pre->LRU_link_next=buffer_node->LRU_link_next;
                            buffer_node->LRU_link_next->LRU_link_pre=buffer_node->LRU_link_pre;
                        }
                        if(1){
                            buffer_node->LRU_link_next=ssd->dram->SSD4_buffer->buffer_head;
                            ssd->dram->SSD4_buffer->buffer_head->LRU_link_pre=buffer_node;
                            buffer_node->LRU_link_pre=NULL;
                            ssd->dram->SSD4_buffer->buffer_head=buffer_node;
                        }else{
                            if(preNode == ssd->dram->SSD4_buffer->buffer_head){
                                buffer_node->LRU_link_next=ssd->dram->SSD4_buffer->buffer_head;
                                ssd->dram->SSD4_buffer->buffer_head->LRU_link_pre=buffer_node;
                                buffer_node->LRU_link_pre=NULL;
                                ssd->dram->SSD4_buffer->buffer_head=buffer_node;
                            }else{
                                buffer_node->LRU_link_pre = preNode->LRU_link_pre;
                                buffer_node->LRU_link_next = preNode;

                                buffer_node->LRU_link_pre->LRU_link_next = buffer_node;
                                buffer_node->LRU_link_next->LRU_link_pre = buffer_node;
                            }
                        }
                    }
                    buffer_node->stored=buffer_node->stored|add_flag;
                    buffer_node->dirty_clean=buffer_node->dirty_clean|add_flag;
                    ssd->dram->SSD4_buffer->buffer_sector_count++;
                }

            }
        }
    }

    return ssd;
}

void add_sub_to_host_queue(struct ssd_info *ssd,struct sub_request * sub){
    int queue_num = sub->location->channel;
    if(queue_num < 0 || queue_num >3){
        printf("queue_num Error!\n");
        abort();
    }
    if(ssd->host_queue[queue_num].host_queue_head == NULL){
        ssd->host_queue[queue_num].host_queue_head = sub;
        ssd->host_queue[queue_num].host_queue_tail = sub;
    }else{
        ssd->host_queue[queue_num].host_queue_tail->host_queue_next = sub;
        ssd->host_queue[queue_num].host_queue_tail = sub;
    }
    ssd->host_queue[queue_num].queue_length++;
    sub->host_queue_pos = ssd->host_queue[queue_num].queue_length;
    if(ssd->host_queue[queue_num].queue_length >= 2000){
        ssd->crowded_flag = 1;
    }
    switch (sub->location->channel) {
        case 0:
            if(ssd->host_queue[0].queue_length > ssd->ssd1_count){
                ssd->ssd1_count = ssd->host_queue[0].queue_length;
            }
            break;
        case 1:
            if(ssd->host_queue[1].queue_length > ssd->ssd2_count){
                ssd->ssd2_count = ssd->host_queue[1].queue_length;
            }
            break;
        case 2:
            if(ssd->host_queue[2].queue_length > ssd->ssd3_count){
                ssd->ssd3_count = ssd->host_queue[2].queue_length;
            }
            break;
        case 3:
            if(ssd->host_queue[3].queue_length > ssd->ssd4_count){
                ssd->ssd4_count = ssd->host_queue[3].queue_length;
            }
            break;
    }
}

void update_sub_complete_info(struct ssd_info* ssd,int patch_id,int sub_id){
    int pos = -1;
//    int patch_id = sub->patch;
    for(int i = 0;i < STRIPENUM;i++){
        if(ssd->stripe_patchs[patch_id].completed_sub_flag[i] == sub_id){
            pos = i;
            ssd->stripe_patchs[patch_id].completed_sub_flag[i] = -1;
            break;
        }
        struct sub_request * sub = ssd->stripe_patchs[patch_id].change_sub[i];
    }
    if(pos == -1){
        abort();
    }

}

int countMinIndex(struct ssd_info *ssd,int arr[], int size) {
    if (size <= 0) return -1; // 空数组返回-1

    int minIndex = 0;
    int minValue = arr[0];

    for (int i = 0; i < size; i++) {
        if (arr[i] < minValue) {
            minValue = arr[i];
            minIndex = i;
        }
    }
    switch (minIndex) {
        case 0:
            ssd->ssd1_patch_count++;
            break;
        case 1:
            ssd->ssd2_patch_count++;
            break;
        case 2:
            ssd->ssd3_patch_count++;
            break;
        case 3:
            ssd->ssd4_patch_count++;
            break;
        default:
            abort();
    }
    return minIndex;
}

int findMaxIndex(int arr[], int size) {
    if (size <= 0) return -1; // 空数组返回-1

    int maxIndex = 0;
    int maxValue = arr[0];

    for (int i = 0; i < size; i++) {
        if (arr[i] > maxValue) {
            maxValue = arr[i];
            maxIndex = i;
        }
    }
    return maxIndex;
}

void add_crowded_last_sub(struct ssd_info *ssd,struct sub_request *sub){
    if(ssd->waiting_sub_head == NULL){
        ssd->waiting_sub_head = sub;
        ssd->waiting_sub_tail = sub;
    }else{
        ssd->waiting_sub_tail->crowded_next = sub;
        ssd->waiting_sub_tail = sub;
    }
}

void allocate_space(struct ssd_info *ssd,int min_ssd){
    int trigger_flag = 0;
    switch (min_ssd) {
        double free_capacity;
        case 0:
            free_capacity = (double)ssd->dram->SSD1_buffer->backup_sector_count / ssd->dram->SSD1_buffer->backup_capacity;
            if(free_capacity <=0.5){
                unsigned int extra_capacity = (ssd->dram->SSD1_buffer->backup_capacity - ssd->dram->SSD1_buffer->backup_sector_count)/2;
                ssd->dram->SSD1_buffer->buffer_sector_count -= extra_capacity;
                ssd->dram->SSD1_buffer->extra_buffer += extra_capacity;
                ssd->dram->SSD1_buffer->backup_sector_count += extra_capacity;
                ssd->ssd_num = 0;
                trigger_flag = 1;
                if(ssd->dram->SSD1_buffer->backup_sector_count > ssd->max_ssd1){
                    ssd->max_ssd1 = ssd->dram->SSD1_buffer->backup_sector_count;
                }
            }
            break;
        case 1:
            free_capacity = (double)ssd->dram->SSD2_buffer->backup_sector_count / ssd->dram->SSD2_buffer->backup_capacity;
            if(free_capacity <=0.5){
                unsigned int extra_capacity = (ssd->dram->SSD2_buffer->backup_capacity - ssd->dram->SSD2_buffer->backup_sector_count)/2;
                ssd->dram->SSD2_buffer->buffer_sector_count -= extra_capacity;
                ssd->dram->SSD2_buffer->extra_buffer += extra_capacity;
                ssd->dram->SSD2_buffer->backup_sector_count += extra_capacity;
                ssd->ssd_num = 1;
                trigger_flag = 1;
                if(ssd->dram->SSD2_buffer->backup_sector_count > ssd->max_ssd2){
                    ssd->max_ssd2 = ssd->dram->SSD2_buffer->backup_sector_count;
                }
            }
            break;
        case 2:
            free_capacity = (double)ssd->dram->SSD3_buffer->backup_sector_count / ssd->dram->SSD3_buffer->backup_capacity;
            if(free_capacity <=0.5){
                unsigned int extra_capacity = (ssd->dram->SSD3_buffer->backup_capacity - ssd->dram->SSD3_buffer->backup_sector_count)/2;
                ssd->dram->SSD3_buffer->buffer_sector_count -= extra_capacity;
                ssd->dram->SSD3_buffer->extra_buffer += extra_capacity;
                ssd->dram->SSD3_buffer->backup_sector_count += extra_capacity;
                ssd->ssd_num = 2;
                trigger_flag = 1;
                if(ssd->dram->SSD3_buffer->backup_sector_count > ssd->max_ssd3){
                    ssd->max_ssd3 = ssd->dram->SSD3_buffer->backup_sector_count;
                }
            }
            break;
        case 3:
            free_capacity = (double)ssd->dram->SSD4_buffer->backup_sector_count / ssd->dram->SSD4_buffer->backup_capacity;
            if(free_capacity <=0.5){
                unsigned int extra_capacity = (ssd->dram->SSD4_buffer->backup_capacity - ssd->dram->SSD4_buffer->backup_sector_count)/2;
                ssd->dram->SSD4_buffer->buffer_sector_count -= extra_capacity;
                ssd->dram->SSD4_buffer->extra_buffer += extra_capacity;
                ssd->dram->SSD4_buffer->backup_sector_count += extra_capacity;
                ssd->ssd_num = 3;
                trigger_flag = 1;
                if(ssd->dram->SSD4_buffer->backup_sector_count > ssd->max_ssd4){
                    ssd->max_ssd4 = ssd->dram->SSD4_buffer->backup_sector_count;
                }
            }
            break;
        default:
            break;
    }
    ssd->ssd1_count = 0;
    ssd->ssd1_patch_count = 0;
    ssd->ssd2_patch_count = 0;
    ssd->ssd3_patch_count = 0;
    ssd->ssd4_patch_count = 0;
    if(trigger_flag == 1){
        ssd->trigger_count++;
    }
}

void start_backup(struct ssd_info *ssd,int first_ssd,int second_ssd,struct sub_request *sub){
    int buffer_free_capacity[4];
    struct sub_request *backup_sub1, *backup_sub2;
    // ssd->ssd1_count++;
    backup_sub1 = copy_sub_request(sub);
    backup_sub2  = copy_sub_request(sub);
    sub->backup_sub = backup_sub1;
    backup_sub1->first = first_ssd;
    backup_sub1->second = second_ssd;
    if(sub->new_write == 1){
        sub->first = first_ssd;
        sub->second = second_ssd;
        add_last_sub_to_buffer(ssd,first_ssd,backup_sub1,0);
        add_last_sub_to_buffer(ssd,second_ssd,backup_sub2,0);
    }else{
        ssd->stripe_patchs[sub->patch].temp_store_ssd1 = first_ssd;
        ssd->stripe_patchs[sub->patch].temp_store_ssd2 = second_ssd;
        add_last_sub_to_buffer(ssd,first_ssd,backup_sub1,0);
        add_last_sub_to_buffer(ssd,second_ssd,backup_sub2,0);
        ssd->stripe_patchs[sub->patch].last_sub_id = sub->sub_id;
        sub->update_flag = 1;
        ssd->stripe_patchs[sub->patch].last_sub = sub;
        ssd->update_sector += size(sub->state);
    }

//    buffer_free_capacity[0] = ssd->dram->SSD1_buffer->backup_sector_count;
//    buffer_free_capacity[1] = ssd->dram->SSD2_buffer->backup_sector_count;
//    buffer_free_capacity[2] = ssd->dram->SSD3_buffer->backup_sector_count;
//    buffer_free_capacity[3] = ssd->dram->SSD4_buffer->backup_sector_count;
//    countMinIndex(ssd,buffer_free_capacity,STRIPENUM);
//    if(ssd->ssd1_count>=500){
//        int min_count[4];
//        int min_ssd;
//        int trigger_flag = 0;
//        int min_ssd1 = -1;
//        int min_ssd2 = -1;
//        min_count[0] = ssd->ssd1_patch_count;
//        min_count[1] = ssd->ssd2_patch_count;
//        min_count[2] = ssd->ssd3_patch_count;
//        min_count[3] = ssd->ssd4_patch_count;
////        min_ssd = findMaxIndex(min_count,STRIPENUM);
//        findTwoMaxIndices(min_count,STRIPENUM,&min_ssd1,&min_ssd2);
//        allocate_space(ssd,min_ssd1);
//        allocate_space(ssd,min_ssd2);
//    }

}

int64_t predict_complete_time(struct ssd_info *ssd,struct sub_request *sub){
    int sub_channel = sub->location->channel;
    int chip_sub_count[4] = {0};
    int total_count = 0;
    int64_t predict_time = 0;
    struct sub_request *pre = ssd->channel_head[sub_channel].subs_w_head;
    while(pre != sub){
        total_count++;
        switch (pre->location->chip) {
            case 0:
                chip_sub_count[0]++;
                break;
            case 1:
                chip_sub_count[1]++;
                break;
            case 2:
                chip_sub_count[2]++;
                break;
            case 3:
                chip_sub_count[3]++;
                break;
            default:
                abort();
        }
        pre = pre->next_node;
    }
    if(pre == NULL){
        abort();
    }
    for(int i = 0; i <= sub->location->chip;i++){
        total_count += chip_sub_count[i];
    }
    sub->channel_queue_pos = total_count;

    switch (sub_channel) {
        case 0:
            predict_time = ssd->ssd1_process_speed * total_count;
            return predict_time;
        case 1:
            predict_time = ssd->ssd2_process_speed * total_count;
            return predict_time;
        case 2:
            predict_time = ssd->ssd3_process_speed * total_count;
            return predict_time;
        case 3:
            predict_time = ssd->ssd4_process_speed * total_count;
            return predict_time;
        default:
            abort();
    }
}

bool start_patch_copy(struct ssd_info *ssd,struct sub_request *sub,int current_channel){
    struct sub_request *backup_sub1, *backup_sub2;
    bool enough1 = false;
    bool enough2 = false;
    int flag = 0;
    int first_use_flag = 0;
    int second_use_flag = 0;
    int first_ssd = -1,second_ssd = -1;
    struct MinTwoIndices free_buffer = find_Two_Free_Buffer(ssd,STRIPENUM,-1);
//    struct MinTwoIndices free_buffer_temp = find_Free_Buffer(ssd,STRIPENUM,-1);
    enough1 = is_enough_for_last_sub_to_buffer(ssd,free_buffer.first,sub);
    if(enough1){
        first_ssd = free_buffer.first;
        first_use_flag = 1;
    }
//    else{
//        enough1 = is_enough_for_last_sub(ssd,free_buffer_temp.first,sub);
//        if(enough1){
//            first_ssd = free_buffer_temp.first;
//            second_use_flag = 1;
//            flag = 1;
//        }
//    }

    enough2 = is_enough_for_last_sub_to_buffer(ssd,free_buffer.second,sub);
    if(enough2){
        second_ssd = free_buffer.second;
    }
//    if(enough1){
//        if(first_ssd != free_buffer.second){
//            enough2 = is_enough_for_last_sub_to_buffer(ssd,free_buffer.second,sub);
//            if(enough2){
//                second_ssd = free_buffer.second;
//            }
//        }
//        if(enough2 == false && first_ssd != free_buffer_temp.second){
//            enough2 = is_enough_for_last_sub(ssd,free_buffer_temp.second,sub);
//            if(enough2){
//                second_ssd = free_buffer_temp.second;
//            }
//        }
//    }
//    if(enough2){
//        second_ssd = free_buffer.second;
//    }else{
//        if(flag == 0){
//            enough2 = is_enough_for_last_sub(ssd,free_buffer_temp.first,sub);
//            if(enough2){
//                second_ssd = free_buffer_temp.first;
//            }
//        }else{
//            enough2 = is_enough_for_last_sub(ssd,free_buffer_temp.second,sub);
//            if(enough2){
//                second_ssd = free_buffer_temp.second;
//            }
//        }
//    }
//    if(sub->new_write == 1){
//        if(enough1 == false || enough2 == false){
//            add_crowded_last_sub(ssd,sub);
//            sub->waiting_process_flag = 1;
//        }
//    }
    if(enough1 && enough2){
        start_backup(ssd,first_ssd,second_ssd,sub);
        return true;
    }
    return false;
}


void add_processing_backup_sub(struct ssd_info *ssd,struct sub_request *sub){
    if(ssd->backup_sub_head == NULL){
        ssd->backup_sub_head = sub;
        ssd->backup_sub_tail = sub;
    }else{
        ssd->backup_sub_tail->next_processing_back_sub = sub;
        ssd->backup_sub_tail = sub;
    }
    ssd->backup_sub_length++;
}

void del_processing_backup_sub(struct ssd_info *ssd,struct sub_request *sub){
    struct sub_request *pre,*current;
    int flag = 0;
    if(sub == ssd->backup_sub_head){
        ssd->backup_sub_head = ssd->backup_sub_head->next_processing_back_sub;
        if(ssd->backup_sub_head == NULL){
            ssd->backup_sub_tail = NULL;
        }
    }else{
        pre = ssd->backup_sub_head;
        current = ssd->backup_sub_head->next_processing_back_sub;
        while(current != NULL){
            if(current == sub){
                pre->next_processing_back_sub = current->next_processing_back_sub;
                current->next_processing_back_sub = NULL;
                if(pre->next_processing_back_sub == NULL){
                    ssd->backup_sub_tail = pre;
                }
                flag = 1;
                break;
            }
            pre = current;
            current = current->next_processing_back_sub;
        }
        if(flag == 0){
            abort();
        }
    }
    ssd->backup_sub_length--;
}

void add_backup_sub_to_pop_sub(struct sub_request *pop_sub,struct sub_request *backup_sub){
    struct sub_request *pre = pop_sub;
    while(pre->next_backup_sub != NULL){
        pre = pre->next_backup_sub;
    }
    if(pre->next_backup_sub == NULL){
        pre->next_backup_sub = backup_sub;
    }else{
        abort();
    }
}

void add_processing_sub(struct ssd_info *ssd,struct sub_request *sub){
    int flag = 0;
    for(int i = 0; i < STRIPENUM;i++){
        if(ssd->processing_sub[i] == NULL){
            ssd->processing_sub[i] = sub;
            sub->processing_flag = 1;
            flag = 1;
            break;
        }
    }
    if(flag == 0){
        abort();
    }
}

bool del_processing_sub(struct ssd_info *ssd,struct sub_request *sub){
    int flag = 0;
    int complete_flag = 0;
    for(int i = 0;i < STRIPENUM;i++){
        if(ssd->processing_sub[i] == sub){
            ssd->processing_sub[i] = NULL;
            flag = 1;
            break;
        }
    }
    if(flag == 0){
        abort();
    }
    for(int k = 0;k < STRIPENUM;k++){
        if(ssd->processing_sub[k] != NULL){
            complete_flag = 1;
            break;
        }
    }
    if(complete_flag == 0){
        return true;
    }else{
        return false;
    }
}

void count_sub_of_ssd(struct ssd_info *ssd){

    if(ssd->dram->SSD1_buffer->Blocked_Count > ssd->dram->SSD1_buffer->Max_Blocked_Count){
        ssd->dram->SSD1_buffer->Max_Blocked_Count = ssd->dram->SSD1_buffer->Blocked_Count;
    }
    if(ssd->dram->SSD2_buffer->Blocked_Count > ssd->dram->SSD2_buffer->Max_Blocked_Count){
        ssd->dram->SSD2_buffer->Max_Blocked_Count = ssd->dram->SSD2_buffer->Blocked_Count;
    }
    if(ssd->dram->SSD3_buffer->Blocked_Count > ssd->dram->SSD3_buffer->Max_Blocked_Count){
        ssd->dram->SSD3_buffer->Max_Blocked_Count = ssd->dram->SSD3_buffer->Blocked_Count;
    }
    if(ssd->dram->SSD4_buffer->Blocked_Count > ssd->dram->SSD4_buffer->Max_Blocked_Count){
        ssd->dram->SSD4_buffer->Max_Blocked_Count = ssd->dram->SSD4_buffer->Blocked_Count;
    }
    
    ssd->dram->SSD1_buffer->Blocked_Count = 0;
    ssd->dram->SSD2_buffer->Blocked_Count = 0;
    ssd->dram->SSD3_buffer->Blocked_Count = 0;
    ssd->dram->SSD4_buffer->Blocked_Count = 0;

}

void blocked_sub_of_ssd(struct ssd_info *ssd,int ssd_id){
    ssd_id = ssd_id % 4;
    switch (ssd_id) {
        case 0:
            ssd->dram->SSD1_buffer->blocked_trigger_count++;
            ssd->dram->SSD1_buffer->per_round_blocked_count++;
            break;
        case 1:
            ssd->dram->SSD2_buffer->blocked_trigger_count++;
            ssd->dram->SSD2_buffer->per_round_blocked_count++;
            break;
        case 2:
            ssd->dram->SSD3_buffer->blocked_trigger_count++;
            ssd->dram->SSD3_buffer->per_round_blocked_count++;
            break;
        case 3:
            ssd->dram->SSD4_buffer->blocked_trigger_count++;
            ssd->dram->SSD4_buffer->per_round_blocked_count++;
            break;
        default:
            printf("ssd_id ERROR!\n");
            break;
    }
}

void count_waiting_sub_in_ssd(struct ssd_info *ssd){
    struct sub_request * channel1_head = ssd->channel_head[0].subs_w_head;
    struct sub_request * channel2_head = ssd->channel_head[1].subs_w_head;
    struct sub_request * channel3_head = ssd->channel_head[2].subs_w_head;
    struct sub_request * channel4_head = ssd->channel_head[3].subs_w_head;
    // struct sub_request * channel1_read_head = ssd->channel_head[0].subs_r_head;
    // struct sub_request * channel2_read_head = ssd->channel_head[1].subs_r_head;
    // struct sub_request * channel3_read_head = ssd->channel_head[2].subs_r_head;
    // struct sub_request * channel4_read_head = ssd->channel_head[3].subs_r_head;
    int channel1_blocked_sub = 0;
    int channel2_blocked_sub = 0;
    int channel3_blocked_sub = 0;
    int channel4_blocked_sub = 0;

    while(channel1_head != NULL){
        channel1_head = channel1_head->next_node;
        channel1_blocked_sub++;
    }
    while(channel2_head != NULL){
        channel2_head = channel2_head->next_node;
        channel2_blocked_sub++;
    }
    while(channel3_head != NULL){
        channel3_head = channel3_head->next_node;
        channel3_blocked_sub++;
    }
    while(channel4_head != NULL){
        channel4_head = channel4_head->next_node;
        channel4_blocked_sub++;
    }
    ////////////////////read///////////////////
    // while(channel1_read_head != NULL){
    //     channel1_read_head = channel1_read_head->next_node;
    //     channel1_blocked_sub++;
    // }
    // while(channel2_read_head != NULL){
    //     channel2_read_head = channel2_read_head->next_node;
    //     channel2_blocked_sub++;
    // }
    // while(channel3_read_head != NULL){
    //     channel3_read_head = channel3_read_head->next_node;
    //     channel3_blocked_sub++;
    // }
    // while(channel4_read_head != NULL){
    //     channel4_read_head = channel4_read_head->next_node;
    //     channel4_blocked_sub++;
    // }

    channel1_blocked_sub++;
    channel2_blocked_sub++;
    channel3_blocked_sub++;
    channel4_blocked_sub++;
    ssd->dram->SSD1_buffer->Blocked_Count += channel1_blocked_sub;
    ssd->dram->SSD2_buffer->Blocked_Count += channel2_blocked_sub;
    ssd->dram->SSD3_buffer->Blocked_Count += channel3_blocked_sub;
    ssd->dram->SSD4_buffer->Blocked_Count += channel4_blocked_sub;
    ssd->dram->SSD1_buffer->total_blocked_count += ssd->dram->SSD1_buffer->Blocked_Count;
    ssd->dram->SSD2_buffer->total_blocked_count += ssd->dram->SSD2_buffer->Blocked_Count;
    ssd->dram->SSD3_buffer->total_blocked_count += ssd->dram->SSD3_buffer->Blocked_Count;
    ssd->dram->SSD4_buffer->total_blocked_count += ssd->dram->SSD4_buffer->Blocked_Count;
    
}

void trans_new_sub_to_buffer(struct ssd_info *ssd,int patchID) {;
    struct sub_request *sub;
    int flag = 0;

    for(int i = 0; i < STRIPENUM; i++) {
        if (ssd->stripe_patchs[patchID].change_sub[i] != NULL) {
            sub = ssd->stripe_patchs[patchID].change_sub[i];
            int ssd_id = sub->location->channel;
            insert_buffer(ssd, sub->location->channel, sub->lpn, sub->state, NULL, sub->req, sub, 1, 0);
            if (sub->pop_sub == NULL) {
                sub->new_write_last_sub = 0;
                update_patch_info(ssd, sub->patch);
                if(ssd_id == 0){
                    ssd->dram->SSD1_buffer->total_time_service_sub += 1000;
                    ssd->dram->SSD1_buffer->total_sub_count++;
                }else if(ssd_id == 1){
                    ssd->dram->SSD2_buffer->total_time_service_sub += 1000;
                    ssd->dram->SSD2_buffer->total_sub_count++;
                }else if(ssd_id == 2){
                    ssd->dram->SSD3_buffer->total_time_service_sub += 1000;
                    ssd->dram->SSD3_buffer->total_sub_count++;
                }else if(ssd_id == 3){
                    ssd->dram->SSD4_buffer->total_time_service_sub += 1000;
                    ssd->dram->SSD4_buffer->total_sub_count++;
                }
            }else{
                count_waiting_sub_in_ssd(ssd);
                count_sub_of_ssd(ssd);
                blocked_sub_of_ssd(ssd,ssd_id);
                sub->pop_sub->first_write = 1;
                if(sub->new_write_last_sub == 1){
                    sub->pop_sub->new_write_last_sub = 1;
                    sub->new_write_last_sub = 0;
                }
                add_processing_backup_sub(ssd,sub->pop_sub);
                flag = 1;
            }
        }
    }
}

void trans_sub_from_host_to_buffer(struct ssd_info *ssd,int patchID) {;
    struct sub_request *sub;
    int flag = 0;

    for(int i = 0; i < STRIPENUM; i++) {
        if (ssd->stripe_patchs[patchID].change_sub[i] != NULL) {
            sub = ssd->stripe_patchs[patchID].change_sub[i];
            int ssd_id = sub->location->channel;
            insert_buffer(ssd, sub->location->channel, sub->lpn, sub->state, NULL, sub->req, sub, 0, 0);
            sub->first_write = 0;
            if (sub->pop_sub == NULL) {
                int patch = sub->patch;
                update_patch_info(ssd, sub->patch);
                sub->req->new_write_flag = 0;
                sub->req->temp_time = ssd->current_time;
                if(sub->new_write_last_sub == 0){
                    del_sub_from_request(ssd, sub->req, sub);
                }
                if(sub->new_write_last_sub == 1){
                    sub->new_write_last_sub = 0;
                }
                if(ssd_id == 0){
                    ssd->dram->SSD1_buffer->total_time_service_sub += 1000;
                    ssd->dram->SSD1_buffer->total_sub_count++;
                }else if(ssd_id == 1){
                    ssd->dram->SSD2_buffer->total_time_service_sub += 1000;
                    ssd->dram->SSD2_buffer->total_sub_count++;
                }else if(ssd_id == 2){
                    ssd->dram->SSD3_buffer->total_time_service_sub += 1000;
                    ssd->dram->SSD3_buffer->total_sub_count++;
                }else if(ssd_id == 3){
                    ssd->dram->SSD4_buffer->total_time_service_sub += 1000;
                    ssd->dram->SSD4_buffer->total_sub_count++;
                }
            } else {
                blocked_sub_of_ssd(ssd,ssd_id);
                count_waiting_sub_in_ssd(ssd);
                count_sub_of_ssd(ssd);
                struct request *request;
                request = sub->req;
                int patchID = sub->patch;
                sub->pop_sub->next_subs = request->subs;
                request->subs = sub->pop_sub;
                sub->pop_sub->req = sub->req;
                sub->pop_sub->first_write = 0;
                sub->pop_sub->new_write = 0;
                del_sub_from_request(ssd, sub->req, sub);
                request->all++;
                request->now++;
                request->lastest_process_sub = sub;
                sub->new_write_last_sub = 0;


            }
        }
    }
}

//收集数据的函数
void record_cr(struct ssd_info *ssd, int record_size){
    //重新开始统计
    if(ssd->circle_times >= 0){
        if(ssd->window_record <= record_size * 1024){    //设置窗口大小一个窗口
            if(ssd->window_record == 0 && ssd->circle_times == 0){
                //clear the features for new window collect
                for (int iter = 0; iter < 16; iter++) {
                    ssd->dram->SSD1_buffer->dram_hit[iter] = 0;
                    ssd->dram->SSD1_buffer->window_write_miss = 0;
                    ssd->dram->SSD2_buffer->dram_hit[iter] = 0;
                    ssd->dram->SSD2_buffer->window_write_miss = 0;
                    ssd->dram->SSD3_buffer->dram_hit[iter] = 0;
                    ssd->dram->SSD3_buffer->window_write_miss = 0;
                    ssd->dram->SSD4_buffer->dram_hit[iter] = 0;
                    ssd->dram->SSD4_buffer->window_write_miss = 0;
                }
            }
            ssd->window_record++;
        }else{
            for (int iter = 0; iter < 16; iter++) {
                ssd->dram->SSD1_buffer->dram_hit[iter] = (double) ssd->dram->SSD1_buffer->range_write_hit[iter] /
                                                         (ssd->dram->SSD1_buffer->range_write_hit[15] +
                                                          ssd->dram->SSD1_buffer->window_write_miss);
                fprintf(ssd->dram_file, "%d, %d, %lf\n", 1, (iter + 1) * 2, ssd->dram->SSD1_buffer->dram_hit[iter]);
                ssd->dram->SSD2_buffer->dram_hit[iter] = (double) ssd->dram->SSD2_buffer->range_write_hit[iter] /
                                                         (ssd->dram->SSD2_buffer->range_write_hit[15] +
                                                          ssd->dram->SSD2_buffer->window_write_miss);
                fprintf(ssd->dram_file, "%d, %d, %lf\n", 2, (iter + 1) * 2, ssd->dram->SSD2_buffer->dram_hit[iter]);
                ssd->dram->SSD3_buffer->dram_hit[iter] = (double) ssd->dram->SSD3_buffer->range_write_hit[iter] /
                                                         (ssd->dram->SSD3_buffer->range_write_hit[15] +
                                                          ssd->dram->SSD3_buffer->window_write_miss);
                fprintf(ssd->dram_file, "%d, %d, %lf\n", 3, (iter + 1) * 2, ssd->dram->SSD3_buffer->dram_hit[iter]);
                ssd->dram->SSD4_buffer->dram_hit[iter] = (double) ssd->dram->SSD4_buffer->range_write_hit[iter] /
                                                         (ssd->dram->SSD4_buffer->range_write_hit[15] +
                                                          ssd->dram->SSD4_buffer->window_write_miss);
                fprintf(ssd->dram_file, "%d, %d, %lf\n", 4, (iter + 1) * 2, ssd->dram->SSD4_buffer->dram_hit[iter]);
            }
            ssd->circle_times++;
            ssd->window_record = 0;
        }
        if(ssd->circle_times == 4){
            fclose(ssd->dram_file);
            //circle time -1 stop
            ssd->circle_times = -1;
        }
    }else{
        printf("not until to calulate");
    }
}

// 解析JSON文件（直接解析简单格式）
void parse_simple_json(struct ssd_info *ssd, const char *filename) {

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("错误: 无法打开文件 %s\n", filename);
    }

    // 读取整个文件
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char *buffer = malloc(file_size + 1);
    if (!buffer) {
        fclose(fp);
    }

    fread(buffer, 1, file_size, fp);
    buffer[file_size] = '\0';
    fclose(fp);

//    printf("JSON文件内容:\n%s\n", buffer);

    // 查找SSD参数
    int ssd_count = 1;
    char *ptr = buffer;

    while (ssd_count < 5) {
        // 查找SSD编号
        char *ssd_start = strchr(ptr, '"');
        if (!ssd_start) break;

        char *ssd_end = strchr(ssd_start + 1, '"');
        if (!ssd_end) break;

        // 提取SSD编号
        int ssd_len = ssd_end - ssd_start - 1;

        // 查找k参数
        char *k_start = strstr(ssd_end, "\"k\":");
        if (!k_start) break;

        // 跳过可能的空格
        char *k_value = k_start + 4;
        while (*k_value == ' ') k_value++;

        // 解析k值
        switch (ssd_count){
            case 1:
                sscanf(k_value, "%lf", &ssd->dram->SSD1_buffer->k);
                break;
            case 2:
                sscanf(k_value, "%lf", &ssd->dram->SSD2_buffer->k);
                break;
            case 3:
                sscanf(k_value, "%lf", &ssd->dram->SSD3_buffer->k);
                break;
            case 4:
                sscanf(k_value, "%lf", &ssd->dram->SSD4_buffer->k);
                break;
            default:
                printf("ssd_buffer do not match!");
                abort();
                break;
        }



        // 查找c参数
        char *c_start = strstr(k_value, "\"c\":");
        if (!c_start) break;

        // 跳过可能的空格
        char *c_value = c_start + 4;
        while (*c_value == ' ') c_value++;

        // 解析c值
        switch (ssd_count){
            case 1:
                sscanf(c_value, "%lf", &ssd->dram->SSD1_buffer->c);
                break;
            case 2:
                sscanf(c_value, "%lf", &ssd->dram->SSD2_buffer->c);
                break;
            case 3:
                sscanf(c_value, "%lf", &ssd->dram->SSD3_buffer->c);
                break;
            case 4:
                sscanf(c_value, "%lf", &ssd->dram->SSD4_buffer->c);
                break;
            default:
                printf("ssd_buffer do not match!");
                abort();
                break;
        }

		// 查找e参数
		char *e_start = strstr(c_value, "\"e\":");
		if (!e_start) break;

		// 跳过可能的空格
		char *e_value = e_start + 4;
		while (*e_value == ' ') e_value++;

		// 解析c值
		switch (ssd_count){
			case 1:
				sscanf(e_value, "%lf", &ssd->dram->SSD1_buffer->mae);
				break;
			case 2:
				sscanf(e_value, "%lf", &ssd->dram->SSD2_buffer->mae);
				break;
			case 3:
				sscanf(e_value, "%lf", &ssd->dram->SSD3_buffer->mae);
				break;
			case 4:
				sscanf(e_value, "%lf", &ssd->dram->SSD4_buffer->mae);
				break;
			default:
				printf("ssd_buffer do not match!");
				abort();
				break;
		}

        ssd_count++;

        // 移动到下一个位置
        ptr = e_value + 1;
    }

    free(buffer);

}

// 调用Python脚本
int call_python_script(const char *python_script,
                       const char *input_csv,
                       const char *output_json) {
    char command[512];
    snprintf(command, sizeof(command),
             "python3 %s %s %s",
             python_script, input_csv, output_json);

//    printf("\n执行命令: %s\n", command);

    int ret = system(command);
    if (ret != 0) {
        fprintf(stderr, "Python脚本执行失败，返回码: %d\n", ret);
        return 0;
    }

    return 1;
}
//计算预测的准确率
double predict_hit_rate(struct ssd_info *ssd, int sub_ssd, int cache_size) {
    if(sub_ssd<0 || sub_ssd >7)
    {
        printf("Error sub_ssd!");
        abort();
    }
    sub_ssd = sub_ssd % 4;
    switch(sub_ssd){
        case 0:
            return 1.0 - exp(ssd->dram->SSD1_buffer->k * pow(cache_size, ssd->dram->SSD1_buffer->c));
            break;
        case 1:
            return 1.0 - exp(ssd->dram->SSD2_buffer->k * pow(cache_size, ssd->dram->SSD2_buffer->c));
            break;
        case 2:
            return 1.0 - exp(ssd->dram->SSD3_buffer->k * pow(cache_size, ssd->dram->SSD3_buffer->c));
            break;
        case 3:
            return 1.0 - exp(ssd->dram->SSD4_buffer->k * pow(cache_size, ssd->dram->SSD4_buffer->c));
            break;
        default:
            printf("ssd_buffer do not match!");
            abort();
            break;
    }

}

void insert_buffer(struct ssd_info *ssd,int sub_ssd,unsigned int lpn,int state,struct sub_request *sub,struct request *req,struct sub_request * buffer_sub,int first_write,int last_sub_pop)
{

    if(sub_ssd<0 || sub_ssd >7)
    {
        printf("Error sub_ssd!");
        abort();
    }
    //start input to csv
    if(ssd->circle_times >= 0) {
        record_cr(ssd, 64);
        //执行完成最后一轮
        if(ssd->circle_times < 0){
            const char *python_script = "calculate_kc.py";
            const char * input_csv = "./result/dram_ratio.csv";
            const char *output_json = "./result/ssd_params.json";
            if(call_python_script(python_script, input_csv, output_json)){
                parse_simple_json(ssd, output_json);
            }else{
                printf("执行失败");
            }
        }
    }
    double p_hit = 0;
    double diff = 0;
    if(ssd->dram->SSD1_buffer->k != 0){
        p_hit = predict_hit_rate(ssd, 0, 16);
        diff = fabs(ssd->dram->SSD1_buffer->dram_hit[7] - p_hit);
    }

    sub_ssd = sub_ssd % 4;
    switch(sub_ssd){
        case 0:
            ssd= insert_2_SSD1_buffer(ssd,lpn,state,NULL,req,buffer_sub,first_write,last_sub_pop);
            break;
        case 1:
            ssd= insert_2_SSD2_buffer(ssd,lpn,state,NULL,req,buffer_sub,first_write,last_sub_pop);
            break;
        case 2:
            ssd= insert_2_SSD3_buffer(ssd,lpn,state,NULL,req,buffer_sub,first_write,last_sub_pop);
            break;
        case 3:
            ssd= insert_2_SSD4_buffer(ssd,lpn,state,NULL,req,buffer_sub,first_write,last_sub_pop);
            break;
        default:
            printf("ssd_buffer do not match!");
            abort();
            break;
    }
}


/**************************************************************************************
*�����Ĺ�����Ѱ�һ�Ծ�죬ӦΪÿ��plane�ж�ֻ��һ����Ծ�飬ֻ�������Ծ���в��ܽ��в���
***************************************************************************************/
Status  find_active_block(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane)
{
	unsigned int active_block;
	unsigned int free_page_num=0;
	unsigned int count=0;
	unsigned int gc_processing=FALSE;


	active_block=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block;
	free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
	gc_processing=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].fast_erase;
	if(ssd->same_block_flag == 1){
		active_block=(active_block+1)%ssd->parameter->block_plane;	
		free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
		gc_processing=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].fast_erase;
	}
	//last_write_page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
//	while((free_page_num==0)&&(count<ssd->parameter->block_plane)&&(!process))
	while((free_page_num==0)&&(count<ssd->parameter->block_plane))
	{
		active_block=(active_block+1)%ssd->parameter->block_plane;	
		free_page_num=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num;
		gc_processing=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].fast_erase;
		count++;
	}
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].active_block=active_block;
	if(count<ssd->parameter->block_plane)
	{
		return SUCCESS;
	}
	else
	{
		return FAILURE;
	}
}




/*************************************************
 * 模拟一个实实在在的写操作
 * 根据传入的channel，chip，die，plane，active_block，找到last_write_page++
 * 使用一个page，就要进行相应的计数
 * 通过find_ppn确定物理位置
**************************************************/
Status write_page(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int active_block,unsigned int *ppn)
{
	int last_write_page=0;
	last_write_page=++(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page);	
	if(last_write_page>=(int)(ssd->parameter->page_block))
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_page=0;
		printf("AAA error! the last write page larger than 64!!\n");
		return ERROR;
	}
		
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_page_num--; 
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].page_head[last_write_page].written_count++;
	ssd->write_flash_count++;
	//====================================================================================
	if(last_write_page%3 == 0){
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_lsb_num--;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_lsb_num--;
		ssd->free_lsb_count--;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_lsb = last_write_page;
		ssd->write_lsb_count++;
		ssd->newest_write_lsb_count++;
		}
	else if(last_write_page%3==2){
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_msb_num--;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_msb = last_write_page;
		ssd->write_msb_count++;
		ssd->free_msb_count--;
		ssd->newest_write_msb_count++;
		}
	else{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].free_csb_num--;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[active_block].last_write_csb = last_write_page;
		ssd->write_csb_count++;
		ssd->free_csb_count--;
		ssd->newest_write_csb_count++;
		}
	//====================================================================================
	*ppn=find_ppn(ssd,channel,chip,die,plane,active_block,last_write_page);

	return SUCCESS;
}

/**********************************************
*��������Ĺ����Ǹ���lpn��size��state����������
**********************************************/

struct sub_request *find_same_sub_write(struct ssd_info * ssd, int lpn, int raidID){
	struct request *nowreq = ssd->request_queue;
	struct sub_request *sub;
	while(nowreq){
		if(nowreq->operation == WRITE){
			sub	= nowreq->subs;
			while(sub){
				if(sub->lpn == lpn && sub->raidNUM == raidID){
					return sub;
				}
				sub = sub->next_subs;
			}
		}
		nowreq = nowreq->next_node;
	}
	sub	= ssd->raidReq->subs;
	while(sub){
		if(sub->lpn == lpn && sub->raidNUM == raidID){
			return sub;
		}
		sub = sub->next_subs;
	}
	return NULL;
}

struct sub_request * creat_sub_request(struct ssd_info * ssd,unsigned int lpn,int size,unsigned int state,struct request * req,unsigned int operation, unsigned int target_page_type, unsigned int raidID)
{
    struct sub_request* sub=NULL,* sub_r=NULL;
    struct channel_info * p_ch=NULL;
    struct local * loc=NULL;
    unsigned int flag=0;

    sub = (struct sub_request*)malloc(sizeof(struct sub_request));                        /*����һ��������Ľṹ*/
    alloc_assert(sub,"sub_request");
    memset(sub,0, sizeof(struct sub_request));

    if(sub==NULL)
    {
        return NULL;
    }
    sub->location=NULL;
    sub->next_node=NULL;
    sub->next_subs=NULL;
    sub->update=NULL;
    sub->readXorFlag = 0;
    sub->target_page_type = target_page_type;
    sub->readRaidLpn = 0;
    sub->req = req;
    sub->raidNUM = raidID;
    //将当前的读子请求 sub 添加到读请求 req 的子请求队列中
    if(req!=NULL && req->operation == READ)
    {
        sub->next_subs = req->subs;
        req->subs = sub;
    }
    /*************************************************************************************
    1.读子请求的lpn是校验位lpn：sub成员变量赋值，挂载sub_r到channel上
    2.读子请求的lpn是数据位lpn:
    **************************************************************************************/
    if(operation == READ && lpn == ssd->stripe.checkLpn){
        //1.1sub成员变量赋值
        //由于校验位只是将location标记在trip2page的location中，并没有建立lpn和ppn的映射关系，所以要通过trip2page这样找！
        loc=(struct local *)malloc(sizeof(struct local));
        alloc_assert(loc,"location");
        memset(loc,0, sizeof(struct local));

        loc->channel = ssd->trip2Page[raidID].location->channel;
        loc->chip = ssd->trip2Page[raidID].location->chip;
        loc->plane = ssd->trip2Page[raidID].location->plane;
        loc->die = ssd->trip2Page[raidID].location->die;
        loc->block = ssd->trip2Page[raidID].location->block;
        loc->page = ssd->trip2Page[raidID].location->page;

        sub->location=loc;
        sub->begin_time = ssd->current_time;
        sub->current_state = SR_WAIT;
        sub->current_time=MAX_INT64;
        sub->next_state = SR_R_C_A_TRANSFER;

        sub->lpn = lpn;
        sub->size= size;
        sub->current_time=ssd->current_time;
        sub->begin_time = ssd->current_time;

        p_ch = &ssd->channel_head[loc->channel];
        sub->ppn = find_ppn(ssd, loc->channel, loc->chip, loc->die, loc->plane, loc->block, loc->page);
        sub->operation = READ;
        sub->state=(ssd->dram->map->map_entry[lpn].state&0x7fffffff);
        //1.2将sub挂载到channel下sub_r链表的尾部
        if(p_ch->subs_r_tail!=NULL){
            p_ch->subs_r_tail->next_node=sub;
            p_ch->subs_r_tail=sub;
        }else{
            p_ch->subs_r_head=sub;
            p_ch->subs_r_tail=sub;
        }
        p_ch->read_queue_length++;

    }
    else if (operation == READ){
        //数据位有lpn到ppn的映射关系，可以直接找到location
        loc = find_location(ssd,ssd->dram->map->map_entry[lpn].pn);
        ssd->read2++;
        sub->location=loc;
        sub->begin_time = ssd->current_time;
        sub->current_state = SR_WAIT;
        sub->current_time=MAX_INT64;
        sub->next_state = SR_R_C_A_TRANSFER;
        sub->next_state_predict_time=MAX_INT64;
        sub->lpn = lpn;
        sub->size=size;

        p_ch = &ssd->channel_head[loc->channel];
        sub->ppn = ssd->dram->map->map_entry[lpn].pn;
        sub->operation = READ;
        sub->state=(ssd->dram->map->map_entry[lpn].state&0x7fffffff);
        sub_r=p_ch->subs_r_head;
        flag=0;//本来这里还有sub_r.ppn=sub.ppn就置flag=1，但是这里注释掉了，所以就只能是flag=0的情况了！

        if (flag==0){	//当前这个sub和channel下sub_r链表中的sub_r子请求都不一样
            int i = 0;
            if(ssd->dram->map->map_entry[lpn].state == 0){//为什么会有这种情况呢？
                for(i = 0; i < ssd->stripe.all; ++i){
                    if(ssd->stripe.req[i].lpn == lpn && ssd->stripe.req[i].state != 0){
                        sub->current_state = SR_R_DATA_TRANSFER;
                        sub->current_time=ssd->current_time;
                        sub->next_state = SR_COMPLETE;
                        sub->next_state_predict_time=ssd->current_time+1000;
                        sub->complete_time=ssd->current_time+1000;
                        break;
                    }
                }
            }
            //将sub挂载到channel下sub_r链表的尾部
            if(sub->current_state != SR_R_DATA_TRANSFER){
                if(p_ch->subs_r_tail!=NULL)
                {
                    p_ch->subs_r_tail->next_node=sub;
                    p_ch->subs_r_tail=sub;
                }
                else
                {
                    p_ch->subs_r_head=sub;
                    p_ch->subs_r_tail=sub;
                }
                p_ch->read_queue_length++;
            }
        }
        else
        {
            sub->current_state = SR_R_DATA_TRANSFER;
            sub->current_time=ssd->current_time;
            sub->next_state = SR_COMPLETE;
            sub->next_state_predict_time=ssd->current_time+1000;
            sub->complete_time=ssd->current_time+1000;
        }
    }
        /*************************************************************************************
        写类型的操作创建
        **************************************************************************************/
    else if(operation == WRITE)
    {
        /*add by winks*/
        unsigned long offset_map_write_block;
        unsigned int pos_map_write_block;
        static unsigned int write_request_count = 0;
        /*end add by winks*/
        if(sub->raidNUM == 0)
            abort();
        //将 sub 插入到 req 的子请求链表（队列）的开头
        sub->next_subs = req->subs;
        req->subs = sub;
        req->now++;
        req->all++;
        sub->ppn=0;
        sub->operation = WRITE;
        sub->location=(struct local *)malloc(sizeof(struct local));
        alloc_assert(sub->location,"sub->location");
        memset(sub->location,0, sizeof(struct local));
        ssd->page2Trip[lpn] = sub->raidNUM;
        sub->current_state=SR_WAIT;
        sub->current_time=ssd->current_time;
        sub->lpn=lpn;
        sub->size=size;
        sub->state=state;
        sub->begin_time=ssd->current_time;
        /*add by winks*/

        sub->write_freqently = 0;
        sub->ppn = 0;

        /*end add by winks*/
        if (allocate_location(ssd ,sub)==ERROR)
        {
            free(sub->location);
            sub->location=NULL;
            free(sub);
            sub=NULL;
            return NULL;
        }
    }
    else
    {
        free(sub->location);
        sub->location=NULL;
        free(sub);
        sub=NULL;
        printf("\nERROR ! Unexpected command.%d\n", operation);
        return NULL;
    }

    return sub;
}


struct sub_request * creat_buffer_sub_request(struct ssd_info * ssd,unsigned int lpn,int size,unsigned int state,struct request * req,unsigned int operation, unsigned int target_page_type, unsigned int raidID)
{
    struct sub_request* sub=NULL,* sub_r=NULL;
    struct channel_info * p_ch=NULL;
    struct local * loc=NULL;
    unsigned int flag=0;
    int update = 0;

    sub = (struct sub_request*)malloc(sizeof(struct sub_request));                        /*����һ��������Ľṹ*/
    alloc_assert(sub,"sub_request");
    memset(sub,0, sizeof(struct sub_request));

    if(sub==NULL)
    {
        return NULL;
    }
    sub->location=NULL;
    sub->next_node=NULL;
    sub->next_subs=NULL;
    sub->update=NULL;
    sub->readXorFlag = 0;
    sub->target_page_type = target_page_type;
    sub->readRaidLpn = 0;
    sub->req = req;
    sub->raidNUM = raidID;
    //将当前的读子请求 sub 添加到读请求 req 的子请求队列中
//    if(req!=NULL && req->operation == READ)
//    {
//        sub->next_subs = req->subs;
//        req->subs = sub;
//    }
    /*************************************************************************************
    1.读子请求的lpn是校验位lpn：sub成员变量赋值，挂载sub_r到channel上
    2.读子请求的lpn是数据位lpn:
    **************************************************************************************/
    if(operation == READ && lpn == ssd->stripe.checkLpn){
        //1.1sub成员变量赋值
        //由于校验位只是将location标记在trip2page的location中，并没有建立lpn和ppn的映射关系，所以要通过trip2page这样找！
        loc=(struct local *)malloc(sizeof(struct local));
        alloc_assert(loc,"location");
        memset(loc,0, sizeof(struct local));

        loc->channel = ssd->trip2Page[raidID].location->channel;
        loc->chip = ssd->trip2Page[raidID].location->chip;
        loc->plane = ssd->trip2Page[raidID].location->plane;
        loc->die = ssd->trip2Page[raidID].location->die;
        loc->block = ssd->trip2Page[raidID].location->block;
        loc->page = ssd->trip2Page[raidID].location->page;

        sub->location=loc;
        sub->begin_time = ssd->current_time;
        sub->current_state = SR_WAIT;
        sub->current_time=MAX_INT64;
        sub->next_state = SR_R_C_A_TRANSFER;

        sub->lpn = lpn;
        sub->size= size;
        sub->current_time=ssd->current_time;
        sub->begin_time = ssd->current_time;

        p_ch = &ssd->channel_head[loc->channel];
        sub->ppn = find_ppn(ssd, loc->channel, loc->chip, loc->die, loc->plane, loc->block, loc->page);
        sub->operation = READ;
        sub->state=(ssd->dram->map->map_entry[lpn].state&0x7fffffff);
        //1.2将sub挂载到channel下sub_r链表的尾部
        if(p_ch->subs_r_tail!=NULL){
            p_ch->subs_r_tail->next_node=sub;
            p_ch->subs_r_tail=sub;
        }else{
            p_ch->subs_r_head=sub;
            p_ch->subs_r_tail=sub;
        }
        p_ch->read_queue_length++;

    }
    else if (operation == READ){
        //数据位有lpn到ppn的映射关系，可以直接找到location
        loc = find_location(ssd,ssd->dram->map->map_entry[lpn].pn);
        ssd->read2++;
        sub->location=loc;
        sub->begin_time = ssd->current_time;
        sub->current_state = SR_WAIT;
        sub->current_time=MAX_INT64;
        sub->next_state = SR_R_C_A_TRANSFER;
        sub->next_state_predict_time=MAX_INT64;
        sub->lpn = lpn;
        sub->size=size;

        p_ch = &ssd->channel_head[loc->channel];
        sub->ppn = ssd->dram->map->map_entry[lpn].pn;
        sub->operation = READ;
        sub->state=(ssd->dram->map->map_entry[lpn].state&0x7fffffff);
        sub_r=p_ch->subs_r_head;
        flag=0;//本来这里还有sub_r.ppn=sub.ppn就置flag=1，但是这里注释掉了，所以就只能是flag=0的情况了！

        if (flag==0){	//当前这个sub和channel下sub_r链表中的sub_r子请求都不一样
            int i = 0;
            if(ssd->dram->map->map_entry[lpn].state == 0){//为什么会有这种情况呢？
                for(i = 0; i < ssd->stripe.all; ++i){
                    if(ssd->stripe.req[i].lpn == lpn && ssd->stripe.req[i].state != 0){
                        sub->current_state = SR_R_DATA_TRANSFER;
                        sub->current_time=ssd->current_time;
                        sub->next_state = SR_COMPLETE;
                        sub->next_state_predict_time=ssd->current_time+1000;
                        sub->complete_time=ssd->current_time+1000;
                        break;
                    }
                }
            }
            //将sub挂载到channel下sub_r链表的尾部
            if(sub->current_state != SR_R_DATA_TRANSFER){
                if(p_ch->subs_r_tail!=NULL)
                {
                    p_ch->subs_r_tail->next_node=sub;
                    p_ch->subs_r_tail=sub;
                }
                else
                {
                    p_ch->subs_r_head=sub;
                    p_ch->subs_r_tail=sub;
                }
                p_ch->read_queue_length++;
            }
        }
        else
        {
            sub->current_state = SR_R_DATA_TRANSFER;
            sub->current_time=ssd->current_time;
            sub->next_state = SR_COMPLETE;
            sub->next_state_predict_time=ssd->current_time+1000;
            sub->complete_time=ssd->current_time+1000;
        }
    }
        /*************************************************************************************
        写类型的操作创建
        **************************************************************************************/
    else if(operation == WRITE)
    {
        /*add by winks*/
        unsigned long offset_map_write_block;
        unsigned int pos_map_write_block;
        static unsigned int write_request_count = 0;
        /*end add by winks*/
        if(sub->raidNUM == 0)
            abort();
//        将 sub 插入到 req 的子请求链表（队列）的开头
        if(req != NULL){
            sub->next_subs = req->subs;
            req->subs = sub;
            req->all++;
            req->now++;
        }
        sub->ppn=0;
        sub->operation = WRITE;
        sub->location=(struct local *)malloc(sizeof(struct local));
        alloc_assert(sub->location,"sub->location");
        memset(sub->location,0, sizeof(struct local));
        ssd->page2Trip[lpn] = sub->raidNUM;
        sub->current_state=SR_WAIT;
        sub->current_time=ssd->current_time;
        sub->lpn=lpn;
        sub->size=size;
        sub->state=state;
        sub->begin_time=ssd->current_time;
        /*add by winks*/

        sub->write_freqently = 0;
        sub->ppn = 0;

        if(lpn == ssd->stripe.checkLpn){
            if(ssd->trip2Page[sub->raidNUM].PChannel != -1){
                sub->location->channel = ssd->trip2Page[sub->raidNUM].PChannel;
                sub->location->chip = ssd->trip2Page[sub->raidNUM].PChip;
                update = 1;
            }
        }else if(ssd->dram->map->map_entry[lpn].state==0){
            int a = ssd->page2Channel[lpn];
            if(ssd->page2Channel[lpn] != -1){
                sub->location->channel = ssd->page2Channel[lpn];
                sub->location->chip = ssd->page2Chip[lpn];
                update = 1;
            }
        }
        if(update == 0){
            /*end add by winks*/
            if (allocate_buffer_sub_location(ssd ,sub)==ERROR)
            {
                free(sub->location);
                sub->location=NULL;
                free(sub);
                sub=NULL;
                return NULL;
            }
        }
    }
    else
    {
        free(sub->location);
        sub->location=NULL;
        free(sub);
        sub=NULL;
        printf("\nERROR ! Unexpected command.%d\n", operation);
        return NULL;
    }

    return sub;
}

struct sub_request * creat_write_sub_for_new_stripe(struct ssd_info * ssd,unsigned int lpn,int size,unsigned int state,struct request * req,unsigned int operation, unsigned int target_page_type)
{
    struct sub_request* sub=NULL,* sub_r=NULL;
    struct channel_info * p_ch=NULL;
    struct local * loc=NULL;
    unsigned int flag=0;
    int update = 0;

    sub = (struct sub_request*)malloc(sizeof(struct sub_request));                        /*����һ��������Ľṹ*/
    alloc_assert(sub,"sub_request");
    memset(sub,0, sizeof(struct sub_request));

    if(sub==NULL)
    {
        return NULL;
    }
    sub->location=NULL;
    sub->next_node=NULL;
    sub->next_subs=NULL;
    sub->update=NULL;
    sub->readXorFlag = 0;
    sub->target_page_type = target_page_type;
    sub->readRaidLpn = 0;
    sub->req = req;
//    sub->raidNUM = raidID;
    //将当前的读子请求 sub 添加到读请求 req 的子请求队列中
    if(req!=NULL && req->operation == READ)
    {
        sub->next_subs = req->subs;
        req->subs = sub;
    }
    /*************************************************************************************
    1.读子请求的lpn是校验位lpn：sub成员变量赋值，挂载sub_r到channel上
    2.读子请求的lpn是数据位lpn:
    **************************************************************************************/
        /*************************************************************************************
        写类型的操作创建
        **************************************************************************************/
    if(operation == WRITE)
    {
        /*add by winks*/
        unsigned long offset_map_write_block;
        unsigned int pos_map_write_block;
        static unsigned int write_request_count = 0;
        /*end add by winks*/
//        将 sub 插入到 req 的子请求链表（队列）的开头
        sub->next_subs = req->subs;
        req->subs = sub;
        req->now++;
        req->all++;
        sub->ppn=0;
        sub->operation = WRITE;
        sub->location=(struct local *)malloc(sizeof(struct local));
        alloc_assert(sub->location,"sub->location");
        memset(sub->location,0, sizeof(struct local));
//        ssd->page2Trip[lpn] = sub->raidNUM;
        sub->sub_id = ssd->sub_ID++;
        sub->current_state=SR_WAIT;
        sub->current_time=ssd->current_time;
        sub->lpn=lpn;
        sub->size=size;
        sub->state=state;
        sub->begin_time=ssd->current_time;
        /*add by winks*/

        sub->write_freqently = 0;
        sub->ppn = 0;
        sub->new_write_last_sub = 0;

    }

    return sub;
}

struct sub_request * distribute_sub_request(struct ssd_info * ssd,int operation,struct request * req,struct sub_request *sub)
{
    sub->req = req;
    //将当前的读子请求 sub 添加到读请求 req 的子请求队列中
    if(req!=NULL && req->operation == READ)
    {
        sub->next_subs = req->subs;
        req->subs = sub;
    }
    /*************************************************************************************
    1.读子请求的lpn是校验位lpn：sub成员变量赋值，挂载sub_r到channel上
    2.读子请求的lpn是数据位lpn:
    **************************************************************************************/
        /*************************************************************************************
        写类型的操作创建
        **************************************************************************************/
    if(operation == WRITE)
    {
//        将 sub 插入到 req 的子请求链表（队列）的开头
        sub->next_subs = req->subs;
        req->subs = sub;
        req->now++;
        sub->current_state=SR_WAIT;


        /*end add by winks*/
        if (allocate_buffer_sub_location(ssd ,sub)==ERROR)
        {
            free(sub->location);
            sub->location=NULL;
            free(sub);
            sub=NULL;
            return NULL;
        }
    }
    else
    {
        free(sub->location);
        sub->location=NULL;
        free(sub);
        sub=NULL;
        printf("\nERROR ! Unexpected command.%d\n", operation);
        return NULL;
    }

    return sub;
}


struct sub_request * init_sub_request(struct ssd_info * ssd,unsigned int lpn,int size,unsigned int state,struct request * req,unsigned int operation, unsigned int target_page_type, unsigned int raidID)
{
	struct sub_request* sub=NULL,* sub_r=NULL;
	struct channel_info * p_ch=NULL;
	struct local * loc=NULL;
	unsigned int flag=0;

	sub = (struct sub_request*)malloc(sizeof(struct sub_request));                        /*����һ��������Ľṹ*/
	alloc_assert(sub,"sub_request");
	memset(sub,0, sizeof(struct sub_request));

	if(sub==NULL)
	{
		return NULL;
	}
	sub->location=NULL;
	sub->raidNUM = raidID;

	if(operation == WRITE)
	{    
		/*add by winks*/
		unsigned long offset_map_write_block;
		unsigned int pos_map_write_block;
		static unsigned int write_request_count = 0;
		/*end add by winks*/
		if(sub->raidNUM == 0)
			abort();
		sub->location=(struct local *)malloc(sizeof(struct local));
		alloc_assert(sub->location,"sub->location");
		memset(sub->location,0, sizeof(struct local));
		sub->lpn=lpn;
		sub->size=size;
		sub->state=state;

        if (init_allocate_location(ssd ,sub)==ERROR)
        {
            free(sub->location);
            sub->location=NULL;
            free(sub);
            sub=NULL;
            return NULL;
        }
	}
	else
	{
		free(sub->location);
		sub->location=NULL;
		free(sub);
		sub=NULL;
		printf("\nERROR ! Unexpected command.%d\n", operation);
		return NULL;
	}
	return sub;
}

/******************************************************
*�����Ĺ������ڸ�����channel��chip��die����Ѱ�Ҷ�������
*����������ppnҪ����Ӧ��plane�ļĴ��������ppn���
*******************************************************/
struct sub_request * find_read_sub_request(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die)
{
	unsigned int plane=0;
	unsigned int address_ppn=0;
	struct sub_request *sub=NULL,* p=NULL;

	for(plane=0;plane<ssd->parameter->plane_die;plane++)
	{
		address_ppn=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].add_reg_ppn;
		if(address_ppn!=-1)
		{
			sub=ssd->channel_head[channel].subs_r_head;
			if(sub->ppn==address_ppn && sub->current_state == SR_R_READ)
			{
				if(sub->next_node==NULL)
				{
					ssd->channel_head[channel].subs_r_head=NULL;
					ssd->channel_head[channel].subs_r_tail=NULL;
				}
				ssd->channel_head[channel].subs_r_head=sub->next_node;
                ssd->channel_head[channel].read_queue_length--;
			}
			while((sub->ppn!=address_ppn || sub->current_state != SR_R_READ)&&(sub->next_node!=NULL))
			{
				if(sub->next_node->ppn==address_ppn && sub->next_node->current_state == SR_R_READ)
				{
					p=sub->next_node;
					if(p->next_node==NULL)
					{
						sub->next_node=NULL;
						ssd->channel_head[channel].subs_r_tail=sub;
					}
					else
					{
						sub->next_node=p->next_node;
					}
					sub=p;
                    ssd->channel_head[channel].read_queue_length--;
					break;
				}
				sub=sub->next_node;
			}
			if(sub->ppn==address_ppn && sub->current_state == SR_R_READ)
			{
				sub->next_node=NULL;
				return sub;
			}
			else 
			{
				abort();
				return NULL;
			}
		}
	}
	return NULL;
}

/*******************************************************************************
*�����Ĺ�����Ѱ��д������
*���������1��Ҫ������ȫ��̬�������ssd->subs_w_head��������
*2��Ҫ�ǲ�����ȫ��̬������ô����ssd->channel_head[channel].subs_w_head�����ϲ���
********************************************************************************/
struct sub_request * find_write_sub_request(struct ssd_info * ssd, unsigned int channel)
{
	struct sub_request * sub=NULL,* p=NULL;
	if ((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0))    /*����ȫ�Ķ�̬����*/
	{
		sub=ssd->subs_w_head;
		while(sub!=NULL)        							
		{
			if(sub->current_state==SR_WAIT)								
			{
				if (sub->update!=NULL)                                                      /*�������Ҫ��ǰ������ҳ*/
				{
					if ((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))   //�����µ�ҳ�Ѿ�������
					{
						break;
					}
				} 
				else
				{
					break;
				}						
			}
			p=sub;
			sub=sub->next_node;							
		}

		if (sub==NULL)                                                                      /*���û���ҵ����Է�����������������forѭ��*/
		{
			return NULL;
		}

		if (sub!=ssd->subs_w_head)
		{
			if (sub!=ssd->subs_w_tail)
			{
				p->next_node=sub->next_node;
			}
			else
			{
				ssd->subs_w_tail=p;
				ssd->subs_w_tail->next_node=NULL;
			}
		} 
		else
		{
			if (sub->next_node!=NULL)
			{
				ssd->subs_w_head=sub->next_node;
			} 
			else
			{
				ssd->subs_w_head=NULL;
				ssd->subs_w_tail=NULL;
			}
		}
		sub->next_node=NULL;
		if (ssd->channel_head[channel].subs_w_tail!=NULL)
		{
			ssd->channel_head[channel].subs_w_tail->next_node=sub;
			ssd->channel_head[channel].subs_w_tail=sub;
		} 
		else
		{
			ssd->channel_head[channel].subs_w_tail=sub;
			ssd->channel_head[channel].subs_w_head=sub;
		}
        ssd->channel_head[channel].write_queue_length++;
	}
	/**********************************************************
	*����ȫ��̬���䷽ʽ��������ʽ�������Ѿ����䵽�ض���channel��
	*��ֻ��Ҫ��channel���ҳ�׼�������������
	***********************************************************/
	else            
	{
		sub=ssd->channel_head[channel].subs_w_head;
		while(sub!=NULL)        						
		{
			if(sub->current_state==SR_WAIT)								
			{
				if (sub->update!=NULL)    
				{
					if ((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))   //�����µ�ҳ�Ѿ�������
					{
						break;
					}
				} 
				else
				{
					break;
				}						
			}
			p=sub;
			sub=sub->next_node;							
		}

		if (sub==NULL)
		{
			return NULL;
		}
	}
	
	return sub;
}

/*动态分配方式2：在channel上找到准备服务的写子请求*/
struct sub_request * find_write_sub_request_raid(struct ssd_info * ssd, unsigned int channel, unsigned int chip)
{
	struct sub_request * sub=NULL,* p=NULL, *ret = NULL;
	unsigned char emergency = 0;
    //channel的写子请求链表头结点
	sub=ssd->channel_head[channel].subs_w_head;
	while(sub!=NULL)
	{
        /* sub所在条带没有location,也就是sub没有写入过吧
         * sub不是校验位
         * 进入第一个if语句中*/
//		if(!ssd->trip2Page[sub->raidNUM].location && sub->lpn != ssd->stripe.checkLpn){
//			p=sub;
//			sub=sub->next_node;
//			continue;//跳过这个sub
//		}
//        /*sub的lpn超过了checkLpn，并且sub所在的chip不是当前chip*/
//		if(sub->lpn == ssd->stripe.checkLpn + 1 && sub->location->chip != chip){
//			p=sub;
//			sub=sub->next_node;
//			continue;//跳过这个sub
//		}
        /* 写子请求sub的当前状态是等待状态，
         * 并且这个写子请求sub伴随有更新请求，必须把这个更新读子请求读出来之后，返回sub;
         * 这个子请求sub未伴随有更新请求，就返回sub*/
        if(sub->current_state==SR_WAIT && sub->location->chip==chip)
//        if(sub->current_state==SR_WAIT)
            //add by tian:还需要sub的chip就是chipToken
		{
			if (sub->update!=NULL)
			{
				if ((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))   //�����µ�ҳ�Ѿ�������
				{
					return sub;
				}
			}
			else
			{
				return sub;
			}
		}
		p=sub;
		sub=sub->next_node;	//继续找channel上的下一个sub
	}

	if (ret==NULL)
	{
		return NULL;
	}
	return ret;
}


/******************************************************
* SR_COMPLETE
*******************************************************/
Status services_2_r_cmd_trans_and_complete(struct ssd_info * ssd)
{
	unsigned int i=0;
	struct sub_request * sub=NULL, * p=NULL;
	for(i=0;i<ssd->parameter->channel_number;i++)
	{
		sub=ssd->channel_head[i].subs_r_head;
		while(sub!=NULL)
		{
			if(sub->current_state==SR_R_C_A_TRANSFER)   //第一种状态的处理
			{
				if(sub->next_state_predict_time<=ssd->current_time)
				{
					go_one_step(ssd, sub,NULL, SR_R_READ,NORMAL);
				}
			}
            //第二种状态的处理
			else if((sub->current_state==SR_COMPLETE)||((sub->next_state==SR_COMPLETE)&&(sub->next_state_predict_time<=ssd->current_time)))					
			{
                /* if the request is completed, we delete it from read queue*/
                //
				if(sub!=ssd->channel_head[i].subs_r_head)
				{		
					p->next_node=sub->next_node;
                    //sub不是channel下sub_r链表的队头且是队尾
					if(sub == ssd->channel_head[i].subs_r_tail){
						ssd->channel_head[i].subs_r_tail = sub;
					}						
				}			 
				else					
				{	//sub处于channel下sub_r链表的队头
					if (ssd->channel_head[i].subs_r_head!=ssd->channel_head[i].subs_r_tail)
					{
						ssd->channel_head[i].subs_r_head=sub->next_node;
					} 
					else//sub处于channel下sub_r链表的队头和队尾
					{
						ssd->channel_head[i].subs_r_head=NULL;
						ssd->channel_head[i].subs_r_tail=NULL;
					}							
				}
                ssd->channel_head[i].read_queue_length--;
			}
			p=sub;
			sub=sub->next_node;
		}
	}
	
	return SUCCESS;
}

/**************************************************************************
*处理chip的当前状态是CHIP_WAIT，或者下一状态是CHIP_DATA_TRANSFER并且下一状态预计时间小于当前时间
***************************************************************************/
Status services_2_r_data_trans(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	int chip=0;
	unsigned int die=0,plane=0,address_ppn=0,die1=0;
	struct sub_request * sub=NULL, * p=NULL,*sub1=NULL;
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;
	for(chip=0;chip<ssd->channel_head[channel].chip;chip++)           			    
	{				       		      
			if((ssd->channel_head[channel].chip_head[chip].current_state==CHIP_WAIT)||((ssd->channel_head[channel].chip_head[chip].next_state==CHIP_DATA_TRANSFER)&&
				(ssd->channel_head[channel].chip_head[chip].next_state_predict_time<=ssd->current_time)))					       					
			{
				for(die=0;die<ssd->parameter->die_chip;die++)
				{
					sub=find_read_sub_request(ssd,channel,chip,die);
					if(sub!=NULL)
					{
						break;
					}
				}

				if(sub==NULL)
				{
					continue;
				}

				if(((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)==AD_TWOPLANE_READ)||((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))
				{
					if ((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)==AD_TWOPLANE_READ)     /*�п��ܲ�����two plane����������������£���ͬһ��die�ϵ�����plane���������δ���*/
					{
						sub_twoplane_one=sub;
						sub_twoplane_two=NULL;                                                      
						                                                                            /*Ϊ�˱�֤�ҵ���sub_twoplane_two��sub_twoplane_one��ͬ����add_reg_ppn=-1*/
						ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[sub->location->plane].add_reg_ppn=-1;
						sub_twoplane_two=find_read_sub_request(ssd,channel,chip,die);               /*����ͬ��channel,chip,die��Ѱ������һ����������*/
						
						/******************************************************
						*����ҵ�����ô��ִ��TWO_PLANE��״̬ת������go_one_step
						*���û�ҵ���ô��ִ����ͨ�����״̬ת������go_one_step
						******************************************************/
						if (sub_twoplane_two==NULL)
						{
							go_one_step(ssd, sub_twoplane_one,NULL, SR_R_DATA_TRANSFER,NORMAL);   
							*change_current_time_flag=0;   
							*channel_busy_flag=1;

						}
						else
						{
							go_one_step(ssd, sub_twoplane_one,sub_twoplane_two, SR_R_DATA_TRANSFER,TWO_PLANE);
							*change_current_time_flag=0;  
							*channel_busy_flag=1;

						}
					} 
					else if ((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)      /*�п��ܲ�����interleave����������������£�����ͬdie�ϵ�����plane���������δ���*/
					{
						sub_interleave_one=sub;
						sub_interleave_two=NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[sub->location->plane].add_reg_ppn=-1;
						
						for(die1=0;die1<ssd->parameter->die_chip;die1++)
						{	
							if(die1!=die)
							{
								sub_interleave_two=find_read_sub_request(ssd,channel,chip,die1);    /*����ͬ��channel��chhip��ͬ��die����������һ����������*/
								if(sub_interleave_two!=NULL)
								{
									break;
								}
							}
						}	
						if (sub_interleave_two==NULL)
						{
							go_one_step(ssd, sub_interleave_one,NULL, SR_R_DATA_TRANSFER,NORMAL);

							*change_current_time_flag=0;  
							*channel_busy_flag=1;

						}
						else
						{
							go_one_step(ssd, sub_twoplane_one,sub_interleave_two, SR_R_DATA_TRANSFER,INTERLEAVE);
												
							*change_current_time_flag=0;   
							*channel_busy_flag=1;
							
						}
					}
				}
				else   //普通命令                                                                              /*���ssd��֧�ָ߼�������ô��ִ��һ��һ����ִ�ж�������*/
				{
					go_one_step(ssd, sub,NULL, SR_R_DATA_TRANSFER,NORMAL);
					*change_current_time_flag=0;  
					*channel_busy_flag=1;
				}
				break;
			}		
			
		if(*channel_busy_flag==1)
		{
			break;
		}
	}		
	return SUCCESS;
}


/******************************************************
*处理等待状态的读子请求
*******************************************************/
int services_2_r_wait(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	unsigned int plane=0,address_ppn=0;
	struct sub_request * sub=NULL, * p=NULL;
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;
	int bet_temp;
	struct gc_operation *gc_node;
	struct local *  location=NULL;
	unsigned int block,active_block,transfer_size,free_page,page_move_count=0;//gc
	unsigned int i=0;
	int block_plane=0,block_die=0,block_chip=0,block_channel=0;
	int wl_channel,wl_chip,wl_die,wl_plane,wl_block;
	unsigned int ppn;
	unsigned int hot_page_count=0;
	unsigned int valid_page_count=0;
	int count=0;
	
	sub=ssd->channel_head[channel].subs_r_head;

	
	if ((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)==AD_TWOPLANE_READ)         /*to find whether there are two sub request can be served by two plane operation*/
	{
		sub_twoplane_one=NULL;
		sub_twoplane_two=NULL;                                                         
		                                                                                /*Ѱ����ִ��two_plane��������������*/
		find_interleave_twoplane_sub_request(ssd,channel,sub_twoplane_one,sub_twoplane_two,TWO_PLANE);

		if (sub_twoplane_two!=NULL)                                                     /*����ִ��two plane read ����*/
		{
			go_one_step(ssd, sub_twoplane_one,sub_twoplane_two, SR_R_C_A_TRANSFER,TWO_PLANE);
						
			*change_current_time_flag=0;
			*channel_busy_flag=1;                                                       /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
		} 
		else if((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)       /*û����������������page��������û��interleave read����ʱ��ֻ��ִ�е���page�Ķ�*/
		{
			while(sub!=NULL)                                                            /*if there are read requests in queue, send one of them to target die*/			
			{		
				if(sub->current_state==SR_WAIT)									
				{	                                                                    /*ע���¸�����ж�������services_2_r_data_trans���ж������Ĳ�ͬ
																						*/
					if((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state==CHIP_IDLE)&&
						(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state_predict_time<=ssd->current_time)))												
					{	
						go_one_step(ssd, sub,NULL, SR_R_C_A_TRANSFER,NORMAL);
									
						*change_current_time_flag=0;
						*channel_busy_flag=1;                                           /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
						break;										
					}	
					else
					{
						                                                                /*��Ϊdie��busy���µ�����*/
					}
				}						
				sub=sub->next_node;								
			}
		}
	} 
	if ((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)               /*to find whether there are two sub request can be served by INTERLEAVE operation*/
	{
		sub_interleave_one=NULL;
		sub_interleave_two=NULL;
		find_interleave_twoplane_sub_request(ssd,channel,sub_interleave_one,sub_interleave_two,INTERLEAVE);
		
		if (sub_interleave_two!=NULL)                                                  /*����ִ��interleave read ����*/
		{

			go_one_step(ssd, sub_interleave_one,sub_interleave_two, SR_R_C_A_TRANSFER,INTERLEAVE);
						
			*change_current_time_flag=0;
			*channel_busy_flag=1;                                                      /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
		} 
		else                                                                           /*û����������������page��ֻ��ִ�е���page�Ķ�*/
		{
			while(sub!=NULL)                                                           /*if there are read requests in queue, send one of them to target die*/			
			{		
				if(sub->current_state==SR_WAIT)									
				{	
					if((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state==CHIP_IDLE)&&
						(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state_predict_time<=ssd->current_time)))												
					{	

						go_one_step(ssd, sub,NULL, SR_R_C_A_TRANSFER,NORMAL);
									
						*change_current_time_flag=0;
						*channel_busy_flag=1;                                          /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
						break;										
					}	
					else
					{
						                                                               /*��Ϊdie��busy���µ�����*/
					}
				}						
				sub=sub->next_node;								
			}
		}
	}

	/*******************************
	*ssd����ִ��ִ�и߼�����������
	*******************************/
	if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE_READ)!=AD_TWOPLANE_READ))
	{
		while(sub!=NULL)                                                               /*if there are read requests in queue, send one of them to target chip*/			
		{		
			if(sub->current_state==SR_WAIT)									
			{	                                                                       
				if((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state==CHIP_IDLE)&&
					(ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].next_state_predict_time<=ssd->current_time)))												
				{
					ssd->channel_head[sub->location->channel].chip_head[sub->location->chip].die_head[sub->location->die].plane_head[sub->location->plane].blk_head[sub->location->block].read_count++;

					ssd->read4++;
					go_one_step(ssd, sub,NULL, SR_R_C_A_TRANSFER,NORMAL);
							
					ssd->chipWrite[sub->location->channel * ssd->parameter->chip_channel[0] + sub->location->chip]++;
					
					*change_current_time_flag=0;
					*channel_busy_flag=1;                                              /*�Ѿ�ռ����������ڵ����ߣ�����ִ��die�����ݵĻش�*/
					break;										
				}	
				else
				{
					                                                                   /*��Ϊdie��busy���µ�����*/
				}
			}						
			sub=sub->next_node;								
		}
	}

	return SUCCESS;
}

/*********************************************************************
*��һ��д�����������Ҫ�����������ɾ���������������ִ��������ܡ�
**********************************************************************/
int delete_w_sub_request(struct ssd_info * ssd, unsigned int channel, struct sub_request * sub )
{
	struct sub_request * p=NULL;
	if (sub==ssd->channel_head[channel].subs_w_head)                                   /*������������channel������ɾ��*/
	{
		if (ssd->channel_head[channel].subs_w_head!=ssd->channel_head[channel].subs_w_tail)
		{
			ssd->channel_head[channel].subs_w_head=sub->next_node;
		} 
		else
		{
			ssd->channel_head[channel].subs_w_head=NULL;
			ssd->channel_head[channel].subs_w_tail=NULL;
		}
	}
	else
	{
		p=ssd->channel_head[channel].subs_w_head;
		while(p->next_node !=sub)
		{
			p=p->next_node;
		}

		if (sub->next_node!=NULL)
		{
			p->next_node=sub->next_node;
		} 
		else
		{
			p->next_node=NULL;
			ssd->channel_head[channel].subs_w_tail=p;
		}
	}
    ssd->channel_head[channel].write_queue_length--;
	return SUCCESS;	
}

/*
*�����Ĺ��ܾ���ִ��copyback����Ĺ��ܣ�
*/
Status copy_back(struct ssd_info * ssd, unsigned int channel, unsigned int chip, unsigned int die,struct sub_request * sub)
{
	int old_ppn=-1, new_ppn=-1;
	long long time=0;
	if (ssd->parameter->greed_CB_ad==1)                                               /*����̰��ʹ��copyback�߼�����*/
	{
		old_ppn=-1;
		if (ssd->dram->map->map_entry[sub->lpn].state!=0)                             /*˵������߼�ҳ֮ǰ��д������Ҫʹ��copyback+random input�������ֱ��д��ȥ����*/
		{
			if ((sub->state&ssd->dram->map->map_entry[sub->lpn].state)==ssd->dram->map->map_entry[sub->lpn].state)       
			{
				sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;	
			} 
			else
			{
				sub->next_state_predict_time=ssd->current_time+19*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				ssd->copy_back_count++;
				ssd->read_count++;
				ssd->update_read_count++;
				old_ppn=ssd->dram->map->map_entry[sub->lpn].pn;                       /*��¼ԭ��������ҳ��������copybackʱ���ж��Ƿ�����ͬΪ���ַ����ż��ַ*/
			}															
		} 
		else
		{
			sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		}
		sub->complete_time=sub->next_state_predict_time;		
		time=sub->complete_time;

		get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);

		if (old_ppn!=-1)                                                              /*������copyback��������Ҫ�ж��Ƿ���������ż��ַ������*/
		{
			new_ppn=ssd->dram->map->map_entry[sub->lpn].pn;
			while (old_ppn%2!=new_ppn%2)                                              /*û��������ż��ַ���ƣ���Ҫ��������һҳ*/
			{
				get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);
				ssd->program_count--;
				ssd->write_flash_count--;
				ssd->waste_page_count++;
				new_ppn=ssd->dram->map->map_entry[sub->lpn].pn;
			}
		}
	} 
	else                                                                              /*����̰����ʹ��copyback�߼�����*/
	{
		if (ssd->dram->map->map_entry[sub->lpn].state!=0)
		{
			if ((sub->state&ssd->dram->map->map_entry[sub->lpn].state)==ssd->dram->map->map_entry[sub->lpn].state)        
			{
				sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);
			} 
			else
			{
				old_ppn=ssd->dram->map->map_entry[sub->lpn].pn;                       /*��¼ԭ��������ҳ��������copybackʱ���ж��Ƿ�����ͬΪ���ַ����ż��ַ*/
				get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);
				new_ppn=ssd->dram->map->map_entry[sub->lpn].pn;
				if (old_ppn%2==new_ppn%2)
				{
					ssd->copy_back_count++;
					sub->next_state_predict_time=ssd->current_time+19*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				} 
				else
				{
					sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(size(ssd->dram->map->map_entry[sub->lpn].state))*ssd->parameter->time_characteristics.tRC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				}
				ssd->read_count++;
				//ssd->read2++;
				ssd->update_read_count++;
			}
		} 
		else
		{
			sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
			get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);
		}
		sub->complete_time=sub->next_state_predict_time;		
		time=sub->complete_time;
	}
    
	/****************************************************************
	*ִ��copyback�߼�����ʱ����Ҫ�޸�channel��chip��״̬���Լ�ʱ���
	*****************************************************************/
	ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
	ssd->channel_head[channel].current_time=ssd->current_time;										
	ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
	ssd->channel_head[channel].next_state_predict_time=time;

	ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
	ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
	ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;
	//ssd->channel_head[channel].chip_head[chip].next_state_predict_time=time+ssd->parameter->time_characteristics.tPROG;
	if (new_ppn%2 == 0)
		{
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time=time+ssd->parameter->time_characteristics.tPROG_L;
			//printf("LSB programming\n");
		}
	else
		{
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time=time+ssd->parameter->time_characteristics.tPROG_M;
			//printf("MSB programming\n");
		}
	
	return SUCCESS;
}

/*****************
*��̬д������ʵ��
******************/
Status static_write(struct ssd_info * ssd, unsigned int channel,unsigned int chip, unsigned int die,struct sub_request * sub)
{
	long long time=0;
	if (ssd->dram->map->map_entry[sub->lpn].state!=0)                                    /*˵������߼�ҳ֮ǰ��д������Ҫʹ���ȶ���������д��ȥ������ֱ��д��ȥ����*/
	{
		if ((sub->state&ssd->dram->map->map_entry[sub->lpn].state)==ssd->dram->map->map_entry[sub->lpn].state)   /*���Ը���*/
		{
			sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		} 
		else
		{
			sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(size((ssd->dram->map->map_entry[sub->lpn].state^sub->state)))*ssd->parameter->time_characteristics.tRC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
			ssd->read_count++;
			ssd->update_read_count++;
		}
	} 
	else
	{
		sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
	}
	sub->complete_time=sub->next_state_predict_time;		
	time=sub->complete_time;

	get_ppn(ssd,sub->location->channel,sub->location->chip,sub->location->die,sub->location->plane,sub);

    /****************************************************************
	*ִ��copyback�߼�����ʱ����Ҫ�޸�channel��chip��״̬���Լ�ʱ���
	*****************************************************************/
	ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
	ssd->channel_head[channel].current_time=ssd->current_time;										
	ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
	ssd->channel_head[channel].next_state_predict_time=time;

	ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
	ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
	ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;
	//**************************************************************
	int new_ppn;
	new_ppn=ssd->dram->map->map_entry[sub->lpn].pn;
	//ssd->channel_head[channel].chip_head[chip].next_state_predict_time=time+ssd->parameter->time_characteristics.tPROG;
	if (new_ppn%2 == 0)
		{
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time=time+ssd->parameter->time_characteristics.tPROG_L;
			//printf("LSB programming\n");
		}
	else
		{
			ssd->channel_head[channel].chip_head[chip].next_state_predict_time=time+ssd->parameter->time_characteristics.tPROG_M;
			//printf("MSB programming\n");
		}
	//****************************************************************
	return SUCCESS;
}

/********************
* 处理写子请求，与raid有关
*********************/
Status services_2_write(struct ssd_info * ssd,unsigned int channel,unsigned int * channel_busy_flag, unsigned int * change_current_time_flag)
{
	int j=0,chip=0;
	unsigned int k=0;
	unsigned int  old_ppn=0,new_ppn=0;
	unsigned int chip_token=0,die_token=0,plane_token=0,address_ppn=0;
	unsigned int  die=0,plane=0;
	long long time=0;
	struct sub_request * sub=NULL, * p=NULL;
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;
    struct local * location=NULL;
	/************************************************************************************************************************
	*写子请求的分配方式还有静态和动态
	*************************************************************************************************************************/
	if((ssd->channel_head[channel].subs_w_head!=NULL)||(ssd->subs_w_head!=NULL))      
	{
		if (ssd->parameter->allocation_scheme==0)     //动态分配
		{
			for(j=0;j<ssd->channel_head[channel].chip;j++)					
			{
				if((ssd->channel_head[channel].subs_w_head==NULL)&&(ssd->subs_w_head==NULL)) 
				{
					break;
				}
				
				chip_token=ssd->channel_head[channel].token;//1.拿到chip_token

				if (*channel_busy_flag==0)//channel空闲
				{
					if((ssd->channel_head[channel].chip_head[chip_token].current_state==CHIP_IDLE)||((ssd->channel_head[channel].chip_head[chip_token].next_state==CHIP_IDLE)&&(ssd->channel_head[channel].chip_head[chip_token].next_state_predict_time<=ssd->current_time)))				
					{//chip空闲
							if((ssd->channel_head[channel].subs_w_head==NULL)&&(ssd->subs_w_head==NULL)) 
							{
								break;
							}
                            //2.拿到die_token
							die_token=ssd->channel_head[channel].chip_head[chip_token].token;

							if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))       //can't use advanced commands
							{
								if(ssd->parameter->dynamic_allocation == 2)
                                    //raid下使用这个函数，找到写子请求
									sub = find_write_sub_request_raid(ssd,channel, chip_token);
								else
									sub=find_write_sub_request(ssd,channel);

								if(sub==NULL)
								{
									if(ssd->parameter->dynamic_allocation == 2){
                                        //更新chip_token
										ssd->channel_head[channel].token=(ssd->channel_head[channel].token+1)%ssd->parameter->chip_channel[channel];
										continue;
									}else
										break;
								}

								if(sub->current_state==SR_WAIT)
								{
                                    //3.拿到plane_token
									plane_token=ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token;
                                    get_ppn(ssd,channel,chip_token,die_token,plane_token,sub);
                                    //更新plane_token
									ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token=(ssd->channel_head[channel].chip_head[chip_token].die_head[die_token].token+1)%ssd->parameter->plane_die;
									*change_current_time_flag=0;

									if(ssd->parameter->ad_priority2==0)
									{
										ssd->real_time_subreq--;
									}
                                    //执行普通的状态转换
                                    go_one_step(ssd,sub,NULL,SR_W_TRANSFER,NORMAL);

									delete_w_sub_request(ssd,channel,sub);
						
									*channel_busy_flag=1;
									/**************************************************************************
									更新chip_token，die_token
									***************************************************************************/
									ssd->channel_head[channel].chip_head[chip_token].token=(ssd->channel_head[channel].chip_head[chip_token].token+1)%ssd->parameter->die_chip;
									ssd->channel_head[channel].token=(ssd->channel_head[channel].token+1)%ssd->parameter->chip_channel[channel];
									break;
								}
							} 
							else        /*use advanced commands*/
							{
								if (dynamic_advanced_process(ssd,channel,chip_token)==NULL)
								{
									*channel_busy_flag=0;
								}
								else
								{
									*channel_busy_flag=1;                                 /*ִ����һ�����󣬴��������ݣ�ռ�������ߣ���Ҫ��������һ��channel*/
                                    ssd->channel_head[channel].chip_head[chip_token].token=(ssd->channel_head[channel].chip_head[chip_token].token+1)%ssd->parameter->die_chip;
                                    ssd->channel_head[channel].token=(ssd->channel_head[channel].token+1)%ssd->parameter->chip_channel[channel];
									break;
								}
							}	
						//更新die_token
						ssd->channel_head[channel].chip_head[chip_token].token=(ssd->channel_head[channel].chip_head[chip_token].token+1)%ssd->parameter->die_chip;
					}
				}
				//更新chip_token
				ssd->channel_head[channel].token=(ssd->channel_head[channel].token+1)%ssd->parameter->chip_channel[channel];
			}
		}
		else if(ssd->parameter->allocation_scheme==1)
		{
			for(chip=0;chip<ssd->channel_head[channel].chip;chip++)					
			{	
				if((ssd->channel_head[channel].chip_head[chip].current_state==CHIP_IDLE)||((ssd->channel_head[channel].chip_head[chip].next_state==CHIP_IDLE)&&(ssd->channel_head[channel].chip_head[chip].next_state_predict_time<=ssd->current_time)))				
				{		
					if(ssd->channel_head[channel].subs_w_head==NULL)
					{
						break;
					}
					if (*channel_busy_flag==0)
					{                                                            
							if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))     /*��ִ�и߼�����*/
							{
								for(die=0;die<ssd->channel_head[channel].chip_head[chip].die_num;die++)				
								{	
									if(ssd->channel_head[channel].subs_w_head==NULL)
									{
										break;
									}
									sub=ssd->channel_head[channel].subs_w_head;
									while (sub!=NULL)
									{
										if ((sub->current_state==SR_WAIT)&&(sub->location->channel==channel)&&(sub->location->chip==chip)&&(sub->location->die==die))      /*����������ǵ�ǰdie������*/
										{
											break;
										}
										sub=sub->next_node;
									}
									if (sub==NULL)
									{
										continue;
									}

									if(sub->current_state==SR_WAIT)
									{
										sub->current_time=ssd->current_time;
										sub->current_state=SR_W_TRANSFER;
										sub->next_state=SR_COMPLETE;

										if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
										{
											copy_back(ssd, channel,chip, die,sub);      /*�������ִ��copyback�߼������ô���ú���copy_back(ssd, channel,chip, die,sub)����д������*/
											*change_current_time_flag=0;
										} 
										else
										{
											static_write(ssd, channel,chip, die,sub);   /*����ִ��copyback�߼������ô����static_write(ssd, channel,chip, die,sub)����������д������*/ 
											*change_current_time_flag=0;
										}
										
										delete_w_sub_request(ssd,channel,sub);
										*channel_busy_flag=1;
										break;
									}
								}
							} 
							else                                                        /*���ܴ����߼�����*/
							{
								if (dynamic_advanced_process(ssd,channel,chip)==NULL)
								{
									*channel_busy_flag=0;
								}
								else
								{
									*channel_busy_flag=1;                               /*ִ����һ�����󣬴��������ݣ�ռ�������ߣ���Ҫ��������һ��channel*/
									break;
								}
							}	
						
					}
				}		
			}
		}			
	}
	return SUCCESS;	
}


/********************************************************
*处理读写子请求的状态变化处理
*********************************************************/
struct ssd_info *process(struct ssd_info *ssd)
{
	int old_ppn=-1,flag_die=-1; 
	unsigned int i,chan,random_num;     
	unsigned int flag=0,new_write=0,chg_cur_time_flag=1,flag2=0,flag_gc=0;       
	int64_t time, channel_time=MAX_INT64;
	struct sub_request *sub;
    int pop_flag = 0;

#ifdef DEBUG
	printf("enter process,  current time:%lld\n",ssd->current_time);
#endif
	for(i=0;i<ssd->parameter->channel_number;i++)
	{          
		if((ssd->channel_head[i].subs_r_head==NULL)&&(ssd->channel_head[i].subs_w_head==NULL)&&(ssd->subs_w_head==NULL))
		{
			flag=1;
		}
		else
		{
			flag=0;
			break;
		}
	}
	if(flag==1) // don`t have read write quest
	{
		ssd->flag=1;
		
		if (ssd->gc_request>0)                                                            /*SSD����gc����������*/
		{
			gc(ssd,0,1);   //空闲时去主动GC                                                               /*���gcҪ������channel�����������*/
		}
		
		return ssd;
	}
	else
	{
		ssd->flag=0;
	}
		
	time = ssd->current_time;
    //处理与channel和chip无关的两种状态：r_cmd_trans, complete
	services_2_r_cmd_trans_and_complete(ssd);

	random_num=ssd->program_count%ssd->parameter->channel_number;

    struct MinTwoIndices free_buffer = find_Two_Max_Buffer(ssd,STRIPENUM);
//    ssd->process_count = 0;
//    ssd->process_time = 0;
//    if(ssd->ssd1_process_count == 0){
//        ssd->ssd1_process_time_start = ssd->current_time;
//    }
//    if(ssd->ssd2_process_count == 0){
//        ssd->ssd2_process_time_start = ssd->current_time;
//    }
//    if(ssd->ssd3_process_count == 0){
//        ssd->ssd3_process_time_start = ssd->current_time;
//    }
//    if(ssd->ssd4_process_count == 0){
//        ssd->ssd4_process_time_start = ssd->current_time;
//    }

	for(chan=0;chan<ssd->parameter->channel_number;chan++)	     
	{
		i=(random_num+chan)%ssd->parameter->channel_number;
		flag=0;
		flag_gc=0;
		if((ssd->channel_head[i].current_state==CHANNEL_IDLE)||(ssd->channel_head[i].next_state==CHANNEL_IDLE&&ssd->channel_head[i].next_state_predict_time<=ssd->current_time))		
		{
			ssd->channel_head[i].current_state = CHANNEL_IDLE;
			if (ssd->gc_request>0)
			{
				if (ssd->channel_head[i].gc_command!=NULL)
				{
					flag_gc=gc(ssd,i,0);//当前channel空闲，主动GC
				}
				if (flag_gc==1)
				{
					continue;
				}
			}

//            if(i == free_buffer.first || i == free_buffer.second){
//                active_pop(ssd,i,ssd->processing_req,0);
//            }
//            active_pop(ssd,i,ssd->processing_req,0);//进行缓存主动弹出
//			sub=ssd->channel_head[i].subs_r_head;
            //处理wait状态的读子请求
			services_2_r_wait(ssd,i,&flag,&chg_cur_time_flag);
		
			if((flag==0)&&(ssd->channel_head[i].subs_r_head!=NULL))                      /*if there are no new read request and data is ready in some dies, send these data to controller and response this request*/		
			{
                //处理r_data_trans状态的读子请求
				services_2_r_data_trans(ssd,i,&flag,&chg_cur_time_flag);
			}
			if(flag==0)   /*if there are no read request to take channel, we can serve write requests*/
			{	//处理写子请求
				services_2_write(ssd,i,&flag,&chg_cur_time_flag);
			}	
			if(ssd->channel_head[i].current_state != CHANNEL_IDLE && ssd->channel_head[i].gc_req_nums != -1){
				int add = get_sub_num_channel(ssd, i) - ssd->channel_head[i].gc_req_nums + 1;
				ssd->allBlockReq += add;
				ssd->channel_head[i].gc_req_nums = -1;
			}
//            if(i == free_buffer.first || i == free_buffer.second){
//                active_pop(ssd,i,ssd->processing_req,0);
//            }
//            active_pop(ssd,i,ssd->processing_req,0);//进行缓存主动弹出
		}
	}

    if(ssd->ssd1_process_count >= 40){
        ssd->ssd1_process_speed = (ssd->ssd1_process_time_end)/ssd->ssd1_process_count;
        ssd->ssd1_process_count = 0;
        ssd->ssd1_process_time_end = 0;
    }
    if(ssd->ssd2_process_count >= 40){
        ssd->ssd2_process_speed = (ssd->ssd2_process_time_end)/ssd->ssd2_process_count;
        ssd->ssd2_process_count = 0;
        ssd->ssd2_process_time_end = 0;
    }
    if(ssd->ssd3_process_count >= 40){
        ssd->ssd3_process_speed = (ssd->ssd3_process_time_end)/ssd->ssd3_process_count;
        ssd->ssd3_process_count = 0;
        ssd->ssd3_process_time_end = 0;
    }
    if(ssd->ssd4_process_count >= 40){
        ssd->ssd4_process_speed = (ssd->ssd4_process_time_end)/ssd->ssd4_process_count;
        ssd->ssd4_process_count = 0;
        ssd->ssd4_process_time_end = 0;
    }

	return ssd;
}

/****************************************************************************************************************************
*��ssd֧�ָ߼�����ʱ��������������þ��Ǵ����߼������д������
*��������ĸ���������ѡ�����ָ߼�����������ֻ����д���󣬶������Ѿ����䵽ÿ��channel��������ִ��ʱ֮�����ѡȡ��Ӧ�����
*****************************************************************************************************************************/
struct ssd_info *dynamic_advanced_process(struct ssd_info *ssd,unsigned int channel,unsigned int chip)         
{
	unsigned int die=0,plane=0;
	unsigned int subs_count=0;
	int flag;
	unsigned int gate;                                                                    /*record the max subrequest that can be executed in the same channel. it will be used when channel-level priority order is highest and allocation scheme is full dynamic allocation*/
	unsigned int plane_place;                                                             /*record which plane has sub request in static allocation*/
	struct sub_request *sub=NULL,*p=NULL,*sub0=NULL,*sub1=NULL,*sub2=NULL,*sub3=NULL,*sub0_rw=NULL,*sub1_rw=NULL,*sub2_rw=NULL,*sub3_rw=NULL;
	struct sub_request ** subs=NULL;
	unsigned int max_sub_num=0;
	unsigned int die_token=0,plane_token=0;
	unsigned int * plane_bits=NULL;
	unsigned int interleaver_count=0;
	
	unsigned int mask=0x00000001;
	unsigned int i=0,j=0;
	
	max_sub_num=(ssd->parameter->die_chip)*(ssd->parameter->plane_die);
	gate=max_sub_num;
	subs=(struct sub_request **)malloc(max_sub_num*sizeof(struct sub_request *));
	alloc_assert(subs,"sub_request");
	
	for(i=0;i<max_sub_num;i++)
	{
		subs[i]=NULL;
	}
	
	if((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0)&&(ssd->parameter->ad_priority2==0))
	{
		gate=ssd->real_time_subreq/ssd->parameter->channel_number;

		if(gate==0)
		{
			gate=1;
		}
		else
		{
			if(ssd->real_time_subreq%ssd->parameter->channel_number!=0)
			{
				gate++;
			}
		}
	}

	if ((ssd->parameter->allocation_scheme==0))                                           /*ȫ��̬���䣬��Ҫ��ssd->subs_w_head��ѡȡ�ȴ������������*/
	{
		if(ssd->parameter->dynamic_allocation==0)
		{
			sub=ssd->subs_w_head;
		}
		else
		{
			sub=ssd->channel_head[channel].subs_w_head;
		}
		
		subs_count=0;
		
		while ((sub!=NULL)&&(subs_count<max_sub_num)&&(subs_count<gate))
		{
			if(sub->current_state==SR_WAIT)								
			{
				if ((sub->update==NULL)||((sub->update!=NULL)&&((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))))    //û����Ҫ��ǰ������ҳ
				{
					subs[subs_count]=sub;
					subs_count++;
				}						
			}
			
			p=sub;
			sub=sub->next_node;	
		}

		if (subs_count==0)                                                               /*û��������Է��񣬷���NULL*/
		{
			for(i=0;i<max_sub_num;i++)
			{
				subs[i]=NULL;
			}
			free(subs);

			subs=NULL;
			free(plane_bits);
			return NULL;
		}
		if(subs_count>=2)
		{
		    /*********************************************
			*two plane,interleave������ʹ��
			*�����channel�ϣ�ѡ��interleave_two_planeִ��
			**********************************************/
			if (((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))     
			{                                                                        
				get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,INTERLEAVE_TWO_PLANE); 
			}
			else if (((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE))
			{
				if(subs_count>ssd->parameter->plane_die)
				{	
					for(i=ssd->parameter->plane_die;i<subs_count;i++)
					{
						subs[i]=NULL;
					}
					subs_count=ssd->parameter->plane_die;
				}
				get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,TWO_PLANE);
			}
			else if (((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE)&&((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE))
			{
				
				if(subs_count>ssd->parameter->die_chip)
				{	
					for(i=ssd->parameter->die_chip;i<subs_count;i++)
					{
						subs[i]=NULL;
					}
					subs_count=ssd->parameter->die_chip;
				}
				get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,INTERLEAVE);
			}
			else
			{
				for(i=1;i<subs_count;i++)
				{
					subs[i]=NULL;
				}
				subs_count=1;
				get_ppn_for_normal_command(ssd,channel,chip,subs[0]);
			}
			
		}//if(subs_count>=2)
		else if(subs_count==1)     //only one request
		{
			get_ppn_for_normal_command(ssd,channel,chip,subs[0]);
		}
		
	}//if ((ssd->parameter->allocation_scheme==0)) 
	else                                                                                  /*��̬���䷽ʽ��ֻ�������ض���channel��ѡȡ�ȴ������������*/
	{
		                                                                                  /*�ھ�̬���䷽ʽ�У�����channel�ϵ���������ͬһ��die�ϵ���Щplane��ȷ��ʹ��ʲô����*/
		
			sub=ssd->channel_head[channel].subs_w_head;
			plane_bits=(unsigned int * )malloc((ssd->parameter->die_chip)*sizeof(unsigned int));
			alloc_assert(plane_bits,"plane_bits");
			memset(plane_bits,0, (ssd->parameter->die_chip)*sizeof(unsigned int));

			for(i=0;i<ssd->parameter->die_chip;i++)
			{
				plane_bits[i]=0x00000000;
			}
			subs_count=0;
			
			while ((sub!=NULL)&&(subs_count<max_sub_num))
			{
				if(sub->current_state==SR_WAIT)								
				{
					if ((sub->update==NULL)||((sub->update!=NULL)&&((sub->update->current_state==SR_COMPLETE)||((sub->update->next_state==SR_COMPLETE)&&(sub->update->next_state_predict_time<=ssd->current_time)))))
					{
						if (sub->location->chip==chip)
						{
							plane_place=0x00000001<<(sub->location->plane);
	
							if ((plane_bits[sub->location->die]&plane_place)!=plane_place)      //we have not add sub request to this plane
							{
								subs[sub->location->die*ssd->parameter->plane_die+sub->location->plane]=sub;
								subs_count++;
								plane_bits[sub->location->die]=(plane_bits[sub->location->die]|plane_place);
							}
						}
					}						
				}
				sub=sub->next_node;	
			}//while ((sub!=NULL)&&(subs_count<max_sub_num))

			if (subs_count==0)                                                            /*û��������Է��񣬷���NULL*/
			{
				for(i=0;i<max_sub_num;i++)
				{
					subs[i]=NULL;
				}
				free(subs);
				subs=NULL;
				free(plane_bits);
				return NULL;
			}
			
			flag=0;
			if (ssd->parameter->advanced_commands!=0)
			{
				if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)        /*ȫ���߼��������ʹ��*/
				{
					if (subs_count>1)                                                    /*��1�����Ͽ���ֱ�ӷ����д����*/
					{
						get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,COPY_BACK);
					} 
					else
					{
						for(i=0;i<max_sub_num;i++)
						{
							if(subs[i]!=NULL)
							{
								break;
							}
						}
						get_ppn_for_normal_command(ssd,channel,chip,subs[i]);
					}
				
				}// if ((ssd->parameter->advanced_commands&AD_COPYBACK)==AD_COPYBACK)
				else                                                                     /*����ִ��copyback*/
				{
					if (subs_count>1)                                                    /*��1�����Ͽ���ֱ�ӷ����д����*/
					{
						if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
						{
							get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,INTERLEAVE_TWO_PLANE);
						} 
						else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))
						{
							for(die=0;die<ssd->parameter->die_chip;die++)
							{
								if(plane_bits[die]!=0x00000000)
								{
									for(i=0;i<ssd->parameter->plane_die;i++)
									{
										plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
										ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane_token+1)%ssd->parameter->plane_die;
										mask=0x00000001<<plane_token;
										if((plane_bits[die]&mask)==mask)
										{
											plane_bits[die]=mask;
											break;
										}
									}
									for(i=i+1;i<ssd->parameter->plane_die;i++)
									{
										plane=(plane_token+1)%ssd->parameter->plane_die;
										subs[die*ssd->parameter->plane_die+plane]=NULL;
										subs_count--;
									}
									interleaver_count++;
								}//if(plane_bits[die]!=0x00000000)
							}//for(die=0;die<ssd->parameter->die_chip;die++)
							if(interleaver_count>=2)
							{
								get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,INTERLEAVE);
							}
							else
							{
								for(i=0;i<max_sub_num;i++)
								{
									if(subs[i]!=NULL)
									{
										break;
									}
								}
								get_ppn_for_normal_command(ssd,channel,chip,subs[i]);	
							}
						}//else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)==AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)!=AD_TWOPLANE))
						else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
						{
							for(i=0;i<ssd->parameter->die_chip;i++)
							{
								die_token=ssd->channel_head[channel].chip_head[chip].token;
								ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
								if(size(plane_bits[die_token])>1)
								{
									break;
								}
								
							}
							
							if(i<ssd->parameter->die_chip)
							{
								for(die=0;die<ssd->parameter->die_chip;die++)
								{
									if(die!=die_token)
									{
										for(plane=0;plane<ssd->parameter->plane_die;plane++)
										{
											if(subs[die*ssd->parameter->plane_die+plane]!=NULL)
											{
												subs[die*ssd->parameter->plane_die+plane]=NULL;
												subs_count--;
											}
										}
									}
								}
								get_ppn_for_advanced_commands(ssd,channel,chip,subs,subs_count,TWO_PLANE);
							}//if(i<ssd->parameter->die_chip)
							else
							{
								for(i=0;i<ssd->parameter->die_chip;i++)
								{
									die_token=ssd->channel_head[channel].chip_head[chip].token;
									ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
									if(plane_bits[die_token]!=0x00000000)
									{
										for(j=0;j<ssd->parameter->plane_die;j++)
										{
											plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die_token].token;
											ssd->channel_head[channel].chip_head[chip].die_head[die_token].token=(plane_token+1)%ssd->parameter->plane_die;
											if(((plane_bits[die_token])&(0x00000001<<plane_token))!=0x00000000)
											{
												sub=subs[die_token*ssd->parameter->plane_die+plane_token];
												break;
											}
										}
									}
								}//for(i=0;i<ssd->parameter->die_chip;i++)
								get_ppn_for_normal_command(ssd,channel,chip,sub);
							}//else
						}//else if (((ssd->parameter->advanced_commands&AD_INTERLEAVE)!=AD_INTERLEAVE)&&((ssd->parameter->advanced_commands&AD_TWOPLANE)==AD_TWOPLANE))
					}//if (subs_count>1)  
					else
					{
						for(i=0;i<ssd->parameter->die_chip;i++)
						{
							die_token=ssd->channel_head[channel].chip_head[chip].token;
							ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
							if(plane_bits[die_token]!=0x00000000)
							{
								for(j=0;j<ssd->parameter->plane_die;j++)
								{
									plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die_token].token;
									ssd->channel_head[channel].chip_head[chip].die_head[die_token].token=(plane_token+1)%ssd->parameter->plane_die;
									if(((plane_bits[die_token])&(0x00000001<<plane_token))!=0x00000000)
									{
										sub=subs[die_token*ssd->parameter->plane_die+plane_token];
										break;
									}
								}
								if(sub!=NULL)
								{
									break;
								}
							}
						}//for(i=0;i<ssd->parameter->die_chip;i++)
						get_ppn_for_normal_command(ssd,channel,chip,sub);
					}//else
				}
			}//if (ssd->parameter->advanced_commands!=0)
			else
			{
				for(i=0;i<ssd->parameter->die_chip;i++)
				{
					die_token=ssd->channel_head[channel].chip_head[chip].token;
					ssd->channel_head[channel].chip_head[chip].token=(die_token+1)%ssd->parameter->die_chip;
					if(plane_bits[die_token]!=0x00000000)
					{
						for(j=0;j<ssd->parameter->plane_die;j++)
						{
							plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die_token].token;
							ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane_token+1)%ssd->parameter->plane_die;
							if(((plane_bits[die_token])&(0x00000001<<plane_token))!=0x00000000)
							{
								sub=subs[die_token*ssd->parameter->plane_die+plane_token];
								break;
							}
						}
						if(sub!=NULL)
						{
							break;
						}
					}
				}//for(i=0;i<ssd->parameter->die_chip;i++)
				get_ppn_for_normal_command(ssd,channel,chip,sub);
			}//else
		
	}//else

	for(i=0;i<max_sub_num;i++)
	{
		subs[i]=NULL;
	}
	free(subs);
	subs=NULL;
	free(plane_bits);
	return ssd;
}

/****************************************
*ִ��д������ʱ��Ϊ��ͨ��д�������ȡppn
*****************************************/
Status get_ppn_for_normal_command(struct ssd_info * ssd, unsigned int channel,unsigned int chip, struct sub_request * sub)
{
	unsigned int die=0;
	unsigned int plane=0;
	if(sub==NULL)
	{
		return ERROR;
	}
	
	if (ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)
	{
		die=ssd->channel_head[channel].chip_head[chip].token;
		plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		get_ppn(ssd,channel,chip,die,plane,sub);
		ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
		ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
		
		compute_serve_time(ssd,channel,chip,die,&sub,1,NORMAL);
		return SUCCESS;
	}
	else
	{
		die=sub->location->die;
		plane=sub->location->plane;
		get_ppn(ssd,channel,chip,die,plane,sub);   
		compute_serve_time(ssd,channel,chip,die,&sub,1,NORMAL);
		return SUCCESS;
	}

}



/************************************************************************************************
*Ϊ�߼������ȡppn
*���ݲ�ͬ����������ͬһ��block��˳��д��Ҫ��ѡȡ���Խ���д������ppn��������ppnȫ����ΪʧЧ��
*��ʹ��two plane����ʱ��Ϊ��Ѱ����ͬˮƽλ�õ�ҳ��������Ҫֱ���ҵ�������ȫ�հ׵Ŀ飬���ʱ��ԭ��
*�Ŀ�û�����ֻ꣬�ܷ����⣬�ȴ��´�ʹ�ã�ͬʱ�޸Ĳ��ҿհ�page�ķ���������ǰ����Ѱ��free���Ϊ��ֻ
*Ҫinvalid block!=64���ɡ�
*except find aim page, we should modify token and decide gc operation
*************************************************************************************************/
Status get_ppn_for_advanced_commands(struct ssd_info *ssd,unsigned int channel,unsigned int chip,struct sub_request * * subs ,unsigned int subs_count,unsigned int command)      
{
	unsigned int die=0,plane=0;
	unsigned int die_token=0,plane_token=0;
	struct sub_request * sub=NULL;
	unsigned int i=0,j=0,k=0;
	unsigned int unvalid_subs_count=0;
	unsigned int valid_subs_count=0;
	unsigned int interleave_flag=FALSE;
	unsigned int multi_plane_falg=FALSE;
	unsigned int max_subs_num=0;
	struct sub_request * first_sub_in_chip=NULL;
	struct sub_request * first_sub_in_die=NULL;
	struct sub_request * second_sub_in_die=NULL;
	unsigned int state=SUCCESS;
	unsigned int multi_plane_flag=FALSE;

	max_subs_num=ssd->parameter->die_chip*ssd->parameter->plane_die;
	
	if (ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)                         /*��̬�������*/ 
	{
		if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))                      /*INTERLEAVE_TWO_PLANE�Լ�COPY_BACK�����*/
		{
			for(i=0;i<subs_count;i++)
			{
				die=ssd->channel_head[channel].chip_head[chip].token;
				if(i<ssd->parameter->die_chip)                                         /*Ϊÿ��subs[i]��ȡppn��iС��die_chip*/
				{
					plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
					get_ppn(ssd,channel,chip,die,plane,subs[i]);
					ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
				}
				else                                                                  
				{   
					/*********************************************************************************************************************************
					*����die_chip��i��ָ���subs[i]��subs[i%ssd->parameter->die_chip]��ȡ��ͬλ�õ�ppn
					*����ɹ��Ļ�ȡ������multi_plane_flag=TRUE��ִ��compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE_TWO_PLANE);
					*����ִ��compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);
					***********************************************************************************************************************************/
					state=make_level_page(ssd,subs[i%ssd->parameter->die_chip],subs[i]);
					if(state!=SUCCESS)                                                 
					{
						subs[i]=NULL;
						unvalid_subs_count++;
					}
					else
					{
						multi_plane_flag=TRUE;
					}
				}
				ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
			}
			valid_subs_count=subs_count-unvalid_subs_count;
			ssd->interleave_count++;
			if(multi_plane_flag==TRUE)
			{
				ssd->inter_mplane_count++;
				compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE_TWO_PLANE);/*����д������Ĵ���ʱ�䣬��д�������״̬ת��*/		
			}
			else
			{
				compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);
			}
			return SUCCESS;
		}//if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))
		else if(command==INTERLEAVE)
		{
			/***********************************************************************************************
			*INTERLEAVE�߼�����Ĵ��������������TWO_PLANE�߼�����Ĵ�����
			*��Ϊtwo_plane��Ҫ����ͬһ��die���治ͬplane��ͬһλ�õ�page����interleaveҪ�����ǲ�ͬdie����ġ�
			************************************************************************************************/
			for(i=0;(i<subs_count)&&(i<ssd->parameter->die_chip);i++)
			{
				die=ssd->channel_head[channel].chip_head[chip].token;
				plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
				get_ppn(ssd,channel,chip,die,plane,subs[i]);
				ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane+1)%ssd->parameter->plane_die;
				ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
				valid_subs_count++;
			}
			ssd->interleave_count++;
			compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);
			return SUCCESS;
		}//else if(command==INTERLEAVE)
		else if(command==TWO_PLANE)
		{
			if(subs_count<2)
			{
				return ERROR;
			}
			die=ssd->channel_head[channel].chip_head[chip].token;
			for(j=0;j<subs_count;j++)
			{
				if(j==1)
				{
					state=find_level_page(ssd,channel,chip,die,subs[0],subs[1]);        /*Ѱ����subs[0]��ppnλ����ͬ��subs[1]��ִ��TWO_PLANE�߼�����*/
					if(state!=SUCCESS)
					{
						get_ppn_for_normal_command(ssd,channel,chip,subs[0]);           /*û�ҵ�����ô�͵���ͨ����������*/
						return FAILURE;
					}
					else
					{
						valid_subs_count=2;
					}
				}
				else if(j>1)
				{
					state=make_level_page(ssd,subs[0],subs[j]);                         /*Ѱ����subs[0]��ppnλ����ͬ��subs[j]��ִ��TWO_PLANE�߼�����*/
					if(state!=SUCCESS)
					{
						for(k=j;k<subs_count;k++)
						{
							subs[k]=NULL;
						}
						subs_count=j;
						break;
					}
					else
					{
						valid_subs_count++;
					}
				}
			}//for(j=0;j<subs_count;j++)
			ssd->channel_head[channel].chip_head[chip].token=(die+1)%ssd->parameter->die_chip;
			ssd->m_plane_prog_count++;
			compute_serve_time(ssd,channel,chip,die,subs,valid_subs_count,TWO_PLANE);
			return SUCCESS;
		}//else if(command==TWO_PLANE)
		else 
		{
			return ERROR;
		}
	}//if (ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)
	else                                                                              /*��̬��������*/
	{
		if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))
		{
			for(die=0;die<ssd->parameter->die_chip;die++)
			{
				first_sub_in_die=NULL;
				for(plane=0;plane<ssd->parameter->plane_die;plane++)
				{
					sub=subs[die*ssd->parameter->plane_die+plane];
					if(sub!=NULL)
					{
						if(first_sub_in_die==NULL)
						{
							first_sub_in_die=sub;
							get_ppn(ssd,channel,chip,die,plane,sub);
						}
						else
						{
							state=make_level_page(ssd,first_sub_in_die,sub);
							if(state!=SUCCESS)
							{
								subs[die*ssd->parameter->plane_die+plane]=NULL;
								subs_count--;
								sub=NULL;
							}
							else
							{
								multi_plane_flag=TRUE;
							}
						}
					}
				}
			}
			if(multi_plane_flag==TRUE)
			{
				ssd->inter_mplane_count++;
				compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE_TWO_PLANE);
				return SUCCESS;
			}
			else
			{
				compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);
				return SUCCESS;
			}
		}//if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))
		else if(command==INTERLEAVE)
		{
			for(die=0;die<ssd->parameter->die_chip;die++)
			{	
				first_sub_in_die=NULL;
				for(plane=0;plane<ssd->parameter->plane_die;plane++)
				{
					sub=subs[die*ssd->parameter->plane_die+plane];
					if(sub!=NULL)
					{
						if(first_sub_in_die==NULL)
						{
							first_sub_in_die=sub;
							get_ppn(ssd,channel,chip,die,plane,sub);
							valid_subs_count++;
						}
						else
						{
							subs[die*ssd->parameter->plane_die+plane]=NULL;
							subs_count--;
							sub=NULL;
						}
					}
				}
			}
			if(valid_subs_count>1)
			{
				ssd->interleave_count++;
			}
			compute_serve_time(ssd,channel,chip,0,subs,valid_subs_count,INTERLEAVE);	
		}//else if(command==INTERLEAVE)
		else if(command==TWO_PLANE)
		{
			for(die=0;die<ssd->parameter->die_chip;die++)
			{	
				first_sub_in_die=NULL;
				second_sub_in_die=NULL;
				for(plane=0;plane<ssd->parameter->plane_die;plane++)
				{
					sub=subs[die*ssd->parameter->plane_die+plane];
					if(sub!=NULL)
					{	
						if(first_sub_in_die==NULL)
						{
							first_sub_in_die=sub;
						}
						else if(second_sub_in_die==NULL)
						{
							second_sub_in_die=sub;
							state=find_level_page(ssd,channel,chip,die,first_sub_in_die,second_sub_in_die);
							if(state!=SUCCESS)
							{
								subs[die*ssd->parameter->plane_die+plane]=NULL;
								subs_count--;
								second_sub_in_die=NULL;
								sub=NULL;
							}
							else
							{
								valid_subs_count=2;
							}
						}
						else
						{
							state=make_level_page(ssd,first_sub_in_die,sub);
							if(state!=SUCCESS)
							{
								subs[die*ssd->parameter->plane_die+plane]=NULL;
								subs_count--;
								sub=NULL;
							}
							else
							{
								valid_subs_count++;
							}
						}
					}//if(sub!=NULL)
				}//for(plane=0;plane<ssd->parameter->plane_die;plane++)
				if(second_sub_in_die!=NULL)
				{
					multi_plane_flag=TRUE;
					break;
				}
			}//for(die=0;die<ssd->parameter->die_chip;die++)
			if(multi_plane_flag==TRUE)
			{
				ssd->m_plane_prog_count++;
				compute_serve_time(ssd,channel,chip,die,subs,valid_subs_count,TWO_PLANE);
				return SUCCESS;
			}//if(multi_plane_flag==TRUE)
			else
			{
				i=0;
				sub=NULL;
				while((sub==NULL)&&(i<max_subs_num))
				{
					sub=subs[i];
					i++;
				}
				if(sub!=NULL)
				{
					get_ppn_for_normal_command(ssd,channel,chip,sub);
					return FAILURE;
				}
				else 
				{
					return ERROR;
				}
			}//else
		}//else if(command==TWO_PLANE)
		else
		{
			return ERROR;
		}
	}//elseb ��̬��������
}


/***********************************************
*��������������sub0��sub1��ppn���ڵ�pageλ����ͬ
************************************************/
Status make_level_page(struct ssd_info * ssd, struct sub_request * sub0,struct sub_request * sub1)
{
	unsigned int i=0,j=0,k=0;
	unsigned int channel=0,chip=0,die=0,plane0=0,plane1=0,block0=0,block1=0,page0=0,page1=0;
	unsigned int active_block0=0,active_block1=0;
	unsigned int old_plane_token=0;
	
	if((sub0==NULL)||(sub1==NULL)||(sub0->location==NULL))
	{
		return ERROR;
	}
	channel=sub0->location->channel;
	chip=sub0->location->chip;
	die=sub0->location->die;
	plane0=sub0->location->plane;
	block0=sub0->location->block;
	page0=sub0->location->page;
	old_plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die].token;

	/***********************************************************************************************
	*��̬����������
	*sub1��plane�Ǹ���sub0��ssd->channel_head[channel].chip_head[chip].die_head[die].token���ƻ�ȡ��
	*sub1��channel��chip��die��block��page����sub0����ͬ
	************************************************************************************************/
	if(ssd->parameter->allocation_scheme==DYNAMIC_ALLOCATION)                             
	{
		old_plane_token=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		for(i=0;i<ssd->parameter->plane_die;i++)
		{
			plane1=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
			if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].add_reg_ppn==-1)
			{
				find_active_block(ssd,channel,chip,die,plane1);                               /*��plane1���ҵ���Ծ��*/
				block1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].active_block;

				/*********************************************************************************************
				*ֻ���ҵ���block1��block0��ͬ�����ܼ�������Ѱ����ͬ��page
				*��Ѱ��pageʱ�Ƚϼ򵥣�ֱ����last_write_page����һ��д��page��+1�Ϳ����ˡ�
				*����ҵ���page����ͬ����ô���ssd����̰����ʹ�ø߼���������Ϳ�����С��page �����page��£
				*********************************************************************************************/
				if(block1==block0)
				{
					page1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].blk_head[block1].last_write_page+1;
					if(page1==page0)
					{
						break;
					}
					else if(page1<page0)
					{
						if (ssd->parameter->greed_MPW_ad==1)                                  /*����̰����ʹ�ø߼�����*/
						{                                                                   
							//make_same_level(ssd,channel,chip,die,plane1,active_block1,page0); /*С��page��ַ�����page��ַ��*/
							make_same_level(ssd,channel,chip,die,plane1,block1,page0);
							break;
						}    
					}
				}//if(block1==block0)
			}
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane1+1)%ssd->parameter->plane_die;
		}//for(i=0;i<ssd->parameter->plane_die;i++)
		if(i<ssd->parameter->plane_die)
		{
			flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page0);          /*������������þ��Ǹ���page1����Ӧ������ҳ�Լ�location����map��*/
			//flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page1);
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(plane1+1)%ssd->parameter->plane_die;
			return SUCCESS;
		}
		else
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane_token;
			return FAILURE;
		}
	}
	else                                                                                      /*��̬��������*/
	{
		if((sub1->location==NULL)||(sub1->location->channel!=channel)||(sub1->location->chip!=chip)||(sub1->location->die!=die))
		{
			return ERROR;
		}
		plane1=sub1->location->plane;
		if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].add_reg_ppn==-1)
		{
			find_active_block(ssd,channel,chip,die,plane1);
			block1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].active_block;
			if(block1==block0)
			{
				page1=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane1].blk_head[block1].last_write_page+1;
				if(page1>page0)
				{
					return FAILURE;
				}
				else if(page1<page0)
				{
					if (ssd->parameter->greed_MPW_ad==1)
					{ 
						//make_same_level(ssd,channel,chip,die,plane1,active_block1,page0);    /*С��page��ַ�����page��ַ��*/
                        make_same_level(ssd,channel,chip,die,plane1,block1,page0);
						flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page0);
						//flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page1);
						return SUCCESS;
					}
					else
					{
						return FAILURE;
					}					
				}
				else
				{
					flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page0);
					//flash_page_state_modify(ssd,sub1,channel,chip,die,plane1,block1,page1);
					return SUCCESS;
				}
				
			}
			else
			{
				return FAILURE;
			}
			
		}
		else
		{
			return ERROR;
		}
	}
	
}

/******************************************************************************************************
*�����Ĺ�����Ϊtwo plane����Ѱ�ҳ�������ͬˮƽλ�õ�ҳ�������޸�ͳ��ֵ���޸�ҳ��״̬
*ע�������������һ������make_level_page����������make_level_page�����������sub1��sub0��pageλ����ͬ
*��find_level_page�������������ڸ�����channel��chip��die��������λ����ͬ��subA��subB��
*******************************************************************************************************/
Status find_level_page(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,struct sub_request *subA,struct sub_request *subB)       
{
	unsigned int i,planeA,planeB,active_blockA,active_blockB,pageA,pageB,aim_page,old_plane;
	struct gc_operation *gc_node;

	old_plane=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
    
	/************************************************************
	*�ڶ�̬����������
	*planeA����ֵΪdie�����ƣ����planeA��ż����ôplaneB=planeA+1
	*planeA����������ôplaneA+1��Ϊż��������planeB=planeA+1
	*************************************************************/
	if (ssd->parameter->allocation_scheme==0)                                                
	{
		planeA=ssd->channel_head[channel].chip_head[chip].die_head[die].token;
		if (planeA%2==0)
		{
			planeB=planeA+1;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(ssd->channel_head[channel].chip_head[chip].die_head[die].token+2)%ssd->parameter->plane_die;
		} 
		else
		{
			planeA=(planeA+1)%ssd->parameter->plane_die;
			planeB=planeA+1;
			ssd->channel_head[channel].chip_head[chip].die_head[die].token=(ssd->channel_head[channel].chip_head[chip].die_head[die].token+3)%ssd->parameter->plane_die;
		}
	} 
	else                                                                                     /*��̬������������ֱ�Ӹ�ֵ��planeA��planeB*/
	{
		planeA=subA->location->plane;
		planeB=subB->location->plane;
	}
	find_active_block(ssd,channel,chip,die,planeA);                                          /*Ѱ��active_block*/
	find_active_block(ssd,channel,chip,die,planeB);
	active_blockA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].active_block;
	active_blockB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].active_block;

	
    
	/*****************************************************
	*���active_block��ͬ����ô������������������ͬ��page
	*����ʹ��̰���ķ����ҵ�������ͬ��page
	******************************************************/
	if (active_blockA==active_blockB)
	{
		pageA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].last_write_page+1;      
		pageB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].last_write_page+1;
		if (pageA==pageB)                                                                    /*�������õ�ҳ������ͬһ��ˮƽλ����*/
		{
			flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockA,pageA);
			flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,pageB);
		} 
		else
		{
			if (ssd->parameter->greed_MPW_ad==1)                                             /*̰����ʹ�ø߼�����*/
			{
				if (pageA<pageB)                                                            
				{
					aim_page=pageB;
					make_same_level(ssd,channel,chip,die,planeA,active_blockA,aim_page);     /*С��page��ַ�����page��ַ��*/
				}
				else
				{
					aim_page=pageA;
					make_same_level(ssd,channel,chip,die,planeB,active_blockB,aim_page);    
				}
				flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockA,aim_page);
				flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,aim_page);
			} 
			else                                                                             /*����̰����ʹ�ø߼�����*/
			{
				subA=NULL;
				subB=NULL;
				ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
				return FAILURE;
			}
		}
	}
	/*********************************
	*����ҵ�������active_block����ͬ
	**********************************/
	else
	{   
		pageA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].last_write_page+1;      
		pageB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].last_write_page+1;
		if (pageA<pageB)
		{
			if (ssd->parameter->greed_MPW_ad==1)                                             /*̰����ʹ�ø߼�����*/
			{
				/*******************************************************************************
				*��planeA�У���active_blockB��ͬλ�õĵ�block�У���pageB��ͬλ�õ�page�ǿ��õġ�
				*Ҳ����palneA�е���Ӧˮƽλ���ǿ��õģ�������Ϊ��planeB�ж�Ӧ��ҳ��
				*��ô��Ҳ��planeA��active_blockB�е�page��pageB��£
				********************************************************************************/
				if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockB].page_head[pageB].free_state==PG_SUB)    
				{
					make_same_level(ssd,channel,chip,die,planeA,active_blockB,pageB);
					flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockB,pageB);
					flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,pageB);
				}
                /********************************************************************************
				*��planeA�У���active_blockB��ͬλ�õĵ�block�У���pageB��ͬλ�õ�page�ǿ��õġ�
				*��ô��Ҫ����Ѱ��block����Ҫ������ˮƽλ����ͬ��һ��ҳ
				*********************************************************************************/
				else    
				{
					for (i=0;i<ssd->parameter->block_plane;i++)
					{
						pageA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].last_write_page+1;
						pageB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].last_write_page+1;
						if ((pageA<ssd->parameter->page_block)&&(pageB<ssd->parameter->page_block))
						{
							if (pageA<pageB)
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].page_head[pageB].free_state==PG_SUB)
								{
									aim_page=pageB;
									make_same_level(ssd,channel,chip,die,planeA,i,aim_page);
									break;
								}
							} 
							else
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].page_head[pageA].free_state==PG_SUB)
								{
									aim_page=pageA;
									make_same_level(ssd,channel,chip,die,planeB,i,aim_page);
									break;
								}
							}
						}
					}//for (i=0;i<ssd->parameter->block_plane;i++)
					if (i<ssd->parameter->block_plane)
					{
						flash_page_state_modify(ssd,subA,channel,chip,die,planeA,i,aim_page);
						flash_page_state_modify(ssd,subB,channel,chip,die,planeB,i,aim_page);
					} 
					else
					{
						subA=NULL;
						subB=NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
						return FAILURE;
					}
				}
			}//if (ssd->parameter->greed_MPW_ad==1)  
			else
			{
				subA=NULL;
				subB=NULL;
				ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
				return FAILURE;
			}
		}//if (pageA<pageB)
		else
		{
			if (ssd->parameter->greed_MPW_ad==1)     
			{
				if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockA].page_head[pageA].free_state==PG_SUB)
				{
					make_same_level(ssd,channel,chip,die,planeB,active_blockA,pageA);
					flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockA,pageA);
					flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockA,pageA);
				}
				else    
				{
					for (i=0;i<ssd->parameter->block_plane;i++)
					{
						pageA=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].last_write_page+1;
						pageB=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].last_write_page+1;
						if ((pageA<ssd->parameter->page_block)&&(pageB<ssd->parameter->page_block))
						{
							if (pageA<pageB)
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[i].page_head[pageB].free_state==PG_SUB)
								{
									aim_page=pageB;
									make_same_level(ssd,channel,chip,die,planeA,i,aim_page);
									break;
								}
							} 
							else
							{
								if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[i].page_head[pageA].free_state==PG_SUB)
								{
									aim_page=pageA;
									make_same_level(ssd,channel,chip,die,planeB,i,aim_page);
									break;
								}
							}
						}
					}//for (i=0;i<ssd->parameter->block_plane;i++)
					if (i<ssd->parameter->block_plane)
					{
						flash_page_state_modify(ssd,subA,channel,chip,die,planeA,i,aim_page);
						flash_page_state_modify(ssd,subB,channel,chip,die,planeB,i,aim_page);
					} 
					else
					{
						subA=NULL;
						subB=NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
						return FAILURE;
					}
				}
			} //if (ssd->parameter->greed_MPW_ad==1) 
			else
			{
				if ((pageA==pageB)&&(pageA==0))
				{
					/*******************************************************************************************
					*�������������
					*1��planeA��planeB�е�active_blockA��pageAλ�ö����ã���ô��ͬplane ����ͬλ�ã���blockAΪ׼
					*2��planeA��planeB�е�active_blockB��pageAλ�ö����ã���ô��ͬplane ����ͬλ�ã���blockBΪ׼
					********************************************************************************************/
					if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockA].page_head[pageA].free_state==PG_SUB)
					  &&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockA].page_head[pageA].free_state==PG_SUB))
					{
						flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockA,pageA);
						flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockA,pageA);
					}
					else if ((ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].blk_head[active_blockB].page_head[pageA].free_state==PG_SUB)
						   &&(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].blk_head[active_blockB].page_head[pageA].free_state==PG_SUB))
					{
						flash_page_state_modify(ssd,subA,channel,chip,die,planeA,active_blockB,pageA);
						flash_page_state_modify(ssd,subB,channel,chip,die,planeB,active_blockB,pageA);
					}
					else
					{
						subA=NULL;
						subB=NULL;
						ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
						return FAILURE;
					}
				}
				else
				{
					subA=NULL;
					subB=NULL;
					ssd->channel_head[channel].chip_head[chip].die_head[die].token=old_plane;
					return ERROR;
				}
			}
		}
	}

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeA].free_page<(ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->gc_hard_threshold))
	{
		gc_node=(struct gc_operation *)malloc(sizeof(struct gc_operation));
		alloc_assert(gc_node,"gc_node");
		memset(gc_node,0, sizeof(struct gc_operation));

		gc_node->next_node=NULL;
		gc_node->chip=chip;
		gc_node->die=die;
		gc_node->plane=planeA;
		gc_node->block=0xffffffff;
		gc_node->page=0;
		gc_node->state=GC_WAIT;
		gc_node->priority=GC_UNINTERRUPT;
		gc_node->next_node=ssd->channel_head[channel].gc_command;
		ssd->channel_head[channel].gc_command=gc_node;
		ssd->gc_request++;
	}
	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[planeB].free_page<(ssd->parameter->page_block*ssd->parameter->block_plane*ssd->parameter->gc_hard_threshold))
	{
		gc_node=(struct gc_operation *)malloc(sizeof(struct gc_operation));
		alloc_assert(gc_node,"gc_node");
		memset(gc_node,0, sizeof(struct gc_operation));

		gc_node->next_node=NULL;
		gc_node->chip=chip;
		gc_node->die=die;
		gc_node->plane=planeB;
		gc_node->block=0xffffffff;
		gc_node->page=0;
		gc_node->state=GC_WAIT;
		gc_node->priority=GC_UNINTERRUPT;
		gc_node->next_node=ssd->channel_head[channel].gc_command;
		ssd->channel_head[channel].gc_command=gc_node;
		ssd->gc_request++;
	}

	return SUCCESS;     
}

/*
*�����Ĺ������޸��ҵ���pageҳ��״̬�Լ���Ӧ��dram��ӳ�����ֵ
*/
struct ssd_info *flash_page_state_modify(struct ssd_info *ssd,struct sub_request *sub,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int block,unsigned int page)
{
	unsigned int ppn,full_page;
	struct local *location;
	struct direct_erase *new_direct_erase,*direct_erase_node;
	
	full_page=~(0xffffffff<<ssd->parameter->subpage_page);
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page=page;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;
	if(page%2==0){
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_lsb_num--;
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_lsb_num--;
		ssd->free_lsb_count--;
		}
	else{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_msb_num--;
		}

	if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page>=ssd->parameter->page_block)
	{
		printf("BBB error! the last write page larger than 64!!\n");
		while(1){}
	}

	if(ssd->dram->map->map_entry[sub->lpn].state==0)                                          /*this is the first logical page*/
	{
		ssd->dram->map->map_entry[sub->lpn].pn=find_ppn(ssd,channel,chip,die,plane,block,page);
		ssd->dram->map->map_entry[sub->lpn].state=sub->state;
	}
	else                                                                                      /*����߼�ҳ�����˸��£���Ҫ��ԭ����ҳ��ΪʧЧ*/
	{
		ppn=ssd->dram->map->map_entry[sub->lpn].pn;
		location=find_location(ssd,ppn);
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].valid_state=0;        //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].free_state=0;         //��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].page_head[location->page].lpn=0;
		ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num++;
		if((location->page)%2==0){
			ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_lsb_num++;
			}
		if (ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].blk_head[location->block].invalid_page_num==ssd->parameter->page_block)    //��block��ȫ��invalid��ҳ������ֱ��ɾ��
		{
			new_direct_erase=(struct direct_erase *)malloc(sizeof(struct direct_erase));
			alloc_assert(new_direct_erase,"new_direct_erase");
			memset(new_direct_erase,0, sizeof(struct direct_erase));

			new_direct_erase->block=location->block;
			new_direct_erase->next_node=NULL;
			direct_erase_node=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
			if (direct_erase_node==NULL)
			{
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node=new_direct_erase;
			} 
			else
			{
				new_direct_erase->next_node=ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node;
				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].erase_node=new_direct_erase;
			}
		}
		free(location);
		location=NULL;
		ssd->dram->map->map_entry[sub->lpn].pn=find_ppn(ssd,channel,chip,die,plane,block,page);
		ssd->dram->map->map_entry[sub->lpn].state=(ssd->dram->map->map_entry[sub->lpn].state|sub->state);
	}

	sub->ppn=ssd->dram->map->map_entry[sub->lpn].pn;
	sub->location->channel=channel;
	sub->location->chip=chip;
	sub->location->die=die;
	sub->location->plane=plane;
	sub->location->block=block;
	sub->location->page=page;
	
	ssd->program_count++;
	ssd->channel_head[channel].program_count++;
	ssd->channel_head[channel].chip_head[chip].program_count++;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
	if(page%2==0){
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_lsb_num--;
		ssd->free_lsb_count--;
		}
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].lpn=sub->lpn;	
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].valid_state=sub->state;
	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page].free_state=((~(sub->state))&full_page);
	ssd->write_flash_count++;

	return ssd;
}


/********************************************
*�����Ĺ��ܾ���������λ�ò�ͬ��pageλ����ͬ
*********************************************/
struct ssd_info *make_same_level(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,unsigned int plane,unsigned int block,unsigned int aim_page)
{
	int i=0,step,page;
	struct direct_erase *new_direct_erase,*direct_erase_node;

	page=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page+1;                  /*��Ҫ�����ĵ�ǰ��Ŀ�дҳ��*/
	step=aim_page-page;
	while (i<step)
	{
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page+i].valid_state=0;     /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page+i].free_state=0;      /*��ʾĳһҳʧЧ��ͬʱ���valid��free״̬��Ϊ0*/
		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].page_head[page+i].lpn=0;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num++;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_page_num--;

		ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_page--;
		/*
		if((page+i)%2==0){
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_lsb_num++;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_lsb_num--;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].free_lsb_num--;
			}
		else{
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].free_msb_num--;
			}
		*/
		i++;
	}

	ssd->waste_page_count+=step;

	ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page=aim_page-1;

	if (ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].invalid_page_num==ssd->parameter->page_block)    /*��block��ȫ��invalid��ҳ������ֱ��ɾ��*/
	{
		new_direct_erase=(struct direct_erase *)malloc(sizeof(struct direct_erase));
		alloc_assert(new_direct_erase,"new_direct_erase");
		memset(new_direct_erase,0, sizeof(struct direct_erase));

		direct_erase_node=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
		if (direct_erase_node==NULL)
		{
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node=new_direct_erase;
		} 
		else
		{
			new_direct_erase->next_node=ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node;
			ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].erase_node=new_direct_erase;
		}
	}

	if(ssd->channel_head[channel].chip_head[chip].die_head[die].plane_head[plane].blk_head[block].last_write_page>=ssd->parameter->page_block)
		{
		printf("CCC error! the last write page larger than 64!!\n");
		while(1){}
		}

	return ssd;
}


/****************************************************************************
*�ڴ����߼������д������ʱ����������Ĺ��ܾ��Ǽ��㴦��ʱ���Լ�������״̬ת��
*���ܻ����Ǻ����ƣ���Ҫ���ƣ��޸�ʱע��Ҫ��Ϊ��̬����Ͷ�̬�����������
*****************************************************************************/
struct ssd_info *compute_serve_time(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,struct sub_request **subs, unsigned int subs_count,unsigned int command)
{
	unsigned int i=0;
	unsigned int max_subs_num=0;
	struct sub_request *sub=NULL,*p=NULL;
	struct sub_request * last_sub=NULL;
	max_subs_num=ssd->parameter->die_chip*ssd->parameter->plane_die;

	if((command==INTERLEAVE_TWO_PLANE)||(command==COPY_BACK))
	{
		for(i=0;i<max_subs_num;i++)
		{
			if(subs[i]!=NULL)
			{
				last_sub=subs[i];
				subs[i]->current_state=SR_W_TRANSFER;
				subs[i]->current_time=ssd->current_time;
				subs[i]->next_state=SR_COMPLETE;
				subs[i]->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(subs[i]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				subs[i]->complete_time=subs[i]->next_state_predict_time;

				delete_from_channel(ssd,channel,subs[i]);
			}
		}
		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=last_sub->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;	
	}
	else if(command==TWO_PLANE)
	{
		for(i=0;i<max_subs_num;i++)
		{
			if(subs[i]!=NULL)
			{
				
				subs[i]->current_state=SR_W_TRANSFER;
				if(last_sub==NULL)
				{
					subs[i]->current_time=ssd->current_time;
				}
				else
				{
					subs[i]->current_time=last_sub->complete_time+ssd->parameter->time_characteristics.tDBSY;
				}
				
				subs[i]->next_state=SR_COMPLETE;
				subs[i]->next_state_predict_time=subs[i]->current_time+7*ssd->parameter->time_characteristics.tWC+(subs[i]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				subs[i]->complete_time=subs[i]->next_state_predict_time;
				last_sub=subs[i];

				delete_from_channel(ssd,channel,subs[i]);
			}
		}
		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=last_sub->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
	}
	else if(command==INTERLEAVE)
	{
		for(i=0;i<max_subs_num;i++)
		{
			if(subs[i]!=NULL)
			{
				
				subs[i]->current_state=SR_W_TRANSFER;
				if(last_sub==NULL)
				{
					subs[i]->current_time=ssd->current_time;
				}
				else
				{
					subs[i]->current_time=last_sub->complete_time;
				}
				subs[i]->next_state=SR_COMPLETE;
				subs[i]->next_state_predict_time=subs[i]->current_time+7*ssd->parameter->time_characteristics.tWC+(subs[i]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				subs[i]->complete_time=subs[i]->next_state_predict_time;
				last_sub=subs[i];

				delete_from_channel(ssd,channel,subs[i]);
			}
		}
		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=last_sub->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
	}
	else if(command==NORMAL)
	{
		subs[0]->current_state=SR_W_TRANSFER;
		subs[0]->current_time=ssd->current_time;
		subs[0]->next_state=SR_COMPLETE;
		subs[0]->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(subs[0]->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		subs[0]->complete_time=subs[0]->next_state_predict_time;

		delete_from_channel(ssd,channel,subs[0]);

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=subs[0]->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
		//**************************************************************
		int new_ppn;
		new_ppn=subs[0]->ppn;
		/*if (new_ppn%2 == 0)
			{
				ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG_L;
				printf("LSB programming\n");
			}
		else
			{
				ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG_M;
				printf("MSB programming\n");
			}*/
		//****************************************************************
	}
	else
	{
		return NULL;
	}
	
	return ssd;

}


/*****************************************************************************************
*�����Ĺ��ܾ��ǰ��������ssd->subs_w_head����ssd->channel_head[channel].subs_w_head��ɾ��
******************************************************************************************/
struct ssd_info *delete_from_channel(struct ssd_info *ssd,unsigned int channel,struct sub_request * sub_req)
{
	struct sub_request *sub,*p;
    
	/******************************************************************
	*��ȫ��̬�������������ssd->subs_w_head��
	*������ȫ��̬�������������ssd->channel_head[channel].subs_w_head��
	*******************************************************************/
	if ((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0))    
	{
		sub=ssd->subs_w_head;
	} 
	else
	{
		sub=ssd->channel_head[channel].subs_w_head;
	}
	p=sub;

	while (sub!=NULL)
	{
		if (sub==sub_req)
		{
			if ((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0))
			{
				if(ssd->parameter->ad_priority2==0)
				{
					ssd->real_time_subreq--;
				}
				
				if (sub==ssd->subs_w_head)                                                     /*������������sub request������ɾ��*/
				{
					if (ssd->subs_w_head!=ssd->subs_w_tail)
					{
						ssd->subs_w_head=sub->next_node;
						sub=ssd->subs_w_head;
						continue;
					} 
					else
					{
						ssd->subs_w_head=NULL;
						ssd->subs_w_tail=NULL;
						p=NULL;
						break;
					}
				}//if (sub==ssd->subs_w_head) 
				else
				{
					if (sub->next_node!=NULL)
					{
						p->next_node=sub->next_node;
						sub=p->next_node;
						continue;
					} 
					else
					{
						ssd->subs_w_tail=p;
						ssd->subs_w_tail->next_node=NULL;
						break;
					}
				}
			}//if ((ssd->parameter->allocation_scheme==0)&&(ssd->parameter->dynamic_allocation==0)) 
			else
			{
				if (sub==ssd->channel_head[channel].subs_w_head)                               /*������������channel������ɾ��*/
				{
					if (ssd->channel_head[channel].subs_w_head!=ssd->channel_head[channel].subs_w_tail)
					{
						ssd->channel_head[channel].subs_w_head=sub->next_node;
						sub=ssd->channel_head[channel].subs_w_head;
                        ssd->channel_head[channel].write_queue_length--;
						continue;;
					} 
					else
					{
						ssd->channel_head[channel].subs_w_head=NULL;
						ssd->channel_head[channel].subs_w_tail=NULL;
						p=NULL;
                        ssd->channel_head[channel].write_queue_length--;
						break;
					}
				}//if (sub==ssd->channel_head[channel].subs_w_head)
				else
				{
					if (sub->next_node!=NULL)
					{
						p->next_node=sub->next_node;
						sub=p->next_node;
                        ssd->channel_head[channel].write_queue_length--;
						continue;
					} 
					else
					{
						ssd->channel_head[channel].subs_w_tail=p;
						ssd->channel_head[channel].subs_w_tail->next_node=NULL;
                        ssd->channel_head[channel].write_queue_length--;
						break;
					}
				}//else
			}//else
		}//if (sub==sub_req)
		p=sub;
		sub=sub->next_node;
	}//while (sub!=NULL)

	return ssd;
}


struct ssd_info *un_greed_interleave_copyback(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,struct sub_request *sub1,struct sub_request *sub2)
{
	unsigned int old_ppn1,ppn1,old_ppn2,ppn2,greed_flag=0;

	old_ppn1=ssd->dram->map->map_entry[sub1->lpn].pn;
	get_ppn(ssd,channel,chip,die,sub1->location->plane,sub1);                                  /*�ҳ�����ppnһ���Ƿ���������������ͬ��plane��,����ʹ��copyback����*/
	ppn1=sub1->ppn;

	old_ppn2=ssd->dram->map->map_entry[sub2->lpn].pn;
	get_ppn(ssd,channel,chip,die,sub2->location->plane,sub2);                                  /*�ҳ�����ppnһ���Ƿ���������������ͬ��plane��,����ʹ��copyback����*/
	ppn2=sub2->ppn;

	if ((old_ppn1%2==ppn1%2)&&(old_ppn2%2==ppn2%2))
	{
		ssd->copy_back_count++;
		ssd->copy_back_count++;

		sub1->current_state=SR_W_TRANSFER;
		sub1->current_time=ssd->current_time;
		sub1->next_state=SR_COMPLETE;
		sub1->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub1->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub1->complete_time=sub1->next_state_predict_time;

		sub2->current_state=SR_W_TRANSFER;
		sub2->current_time=sub1->complete_time;
		sub2->next_state=SR_COMPLETE;
		sub2->next_state_predict_time=sub2->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub2->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub2->complete_time=sub2->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub2->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;

		delete_from_channel(ssd,channel,sub1);
		delete_from_channel(ssd,channel,sub2);
	} //if ((old_ppn1%2==ppn1%2)&&(old_ppn2%2==ppn2%2))
	else if ((old_ppn1%2==ppn1%2)&&(old_ppn2%2!=ppn2%2))
	{
		ssd->interleave_count--;
		ssd->copy_back_count++;

		sub1->current_state=SR_W_TRANSFER;
		sub1->current_time=ssd->current_time;
		sub1->next_state=SR_COMPLETE;
		sub1->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub1->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub1->complete_time=sub1->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub1->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;

		delete_from_channel(ssd,channel,sub1);
	}//else if ((old_ppn1%2==ppn1%2)&&(old_ppn2%2!=ppn2%2))
	else if ((old_ppn1%2!=ppn1%2)&&(old_ppn2%2==ppn2%2))
	{
		ssd->interleave_count--;
		ssd->copy_back_count++;

		sub2->current_state=SR_W_TRANSFER;
		sub2->current_time=ssd->current_time;
		sub2->next_state=SR_COMPLETE;
		sub2->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub2->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub2->complete_time=sub2->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub2->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;

		delete_from_channel(ssd,channel,sub2);
	}//else if ((old_ppn1%2!=ppn1%2)&&(old_ppn2%2==ppn2%2))
	else
	{
		ssd->interleave_count--;

		sub1->current_state=SR_W_TRANSFER;
		sub1->current_time=ssd->current_time;
		sub1->next_state=SR_COMPLETE;
		sub1->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+2*(ssd->parameter->subpage_page*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub1->complete_time=sub1->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub1->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;

		delete_from_channel(ssd,channel,sub1);
	}//else

	return ssd;
}


struct ssd_info *un_greed_copyback(struct ssd_info *ssd,unsigned int channel,unsigned int chip,unsigned int die,struct sub_request *sub1)
{
	unsigned int old_ppn,ppn;

	old_ppn=ssd->dram->map->map_entry[sub1->lpn].pn;
	get_ppn(ssd,channel,chip,die,0,sub1);                                                     /*�ҳ�����ppnһ���Ƿ���������������ͬ��plane��,����ʹ��copyback����*/
	ppn=sub1->ppn;
	
	if (old_ppn%2==ppn%2)
	{
		ssd->copy_back_count++;
		sub1->current_state=SR_W_TRANSFER;
		sub1->current_time=ssd->current_time;
		sub1->next_state=SR_COMPLETE;
		sub1->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+(sub1->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub1->complete_time=sub1->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub1->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
	}//if (old_ppn%2==ppn%2)
	else
	{
		sub1->current_state=SR_W_TRANSFER;
		sub1->current_time=ssd->current_time;
		sub1->next_state=SR_COMPLETE;
		sub1->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC+ssd->parameter->time_characteristics.tR+2*(ssd->parameter->subpage_page*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
		sub1->complete_time=sub1->next_state_predict_time;

		ssd->channel_head[channel].current_state=CHANNEL_TRANSFER;										
		ssd->channel_head[channel].current_time=ssd->current_time;										
		ssd->channel_head[channel].next_state=CHANNEL_IDLE;										
		ssd->channel_head[channel].next_state_predict_time=sub1->complete_time;

		ssd->channel_head[channel].chip_head[chip].current_state=CHIP_WRITE_BUSY;										
		ssd->channel_head[channel].chip_head[chip].current_time=ssd->current_time;									
		ssd->channel_head[channel].chip_head[chip].next_state=CHIP_IDLE;										
		ssd->channel_head[channel].chip_head[chip].next_state_predict_time=ssd->channel_head[channel].next_state_predict_time+ssd->parameter->time_characteristics.tPROG;
	}//else

	delete_from_channel(ssd,channel,sub1);

	return ssd;
}


/****************************************************************************************
*�����Ĺ������ڴ�����������ĸ߼�����ʱ����Ҫ����one_page��ƥ�������һ��page��two_page
*û���ҵ����Ժ�one_pageִ��two plane����interleave������ҳ,��Ҫ��one_page�����һ���ڵ�
*****************************************************************************************/
struct sub_request *find_interleave_twoplane_page(struct ssd_info *ssd, struct sub_request *one_page,unsigned int command)
{
	struct sub_request *two_page;

	if (one_page->current_state!=SR_WAIT)
	{
		return NULL;                                                            
	}
	if (((ssd->channel_head[one_page->location->channel].chip_head[one_page->location->chip].current_state==CHIP_IDLE)||((ssd->channel_head[one_page->location->channel].chip_head[one_page->location->chip].next_state==CHIP_IDLE)&&
		(ssd->channel_head[one_page->location->channel].chip_head[one_page->location->chip].next_state_predict_time<=ssd->current_time))))
	{
		two_page=one_page->next_node;
		if(command==TWO_PLANE)
		{
			while (two_page!=NULL)
		    {
				if (two_page->current_state!=SR_WAIT)
				{
					two_page=two_page->next_node;
				}
				else if ((one_page->location->chip==two_page->location->chip)&&(one_page->location->die==two_page->location->die)&&(one_page->location->block==two_page->location->block)&&(one_page->location->page==two_page->location->page))
				{
					if (one_page->location->plane!=two_page->location->plane)
					{
						return two_page;                                                       /*�ҵ�����one_page����ִ��two plane������ҳ*/
					}
					else
					{
						two_page=two_page->next_node;
					}
				}
				else
				{
					two_page=two_page->next_node;
				}
		     }//while (two_page!=NULL)
		    if (two_page==NULL)                                                               /*û���ҵ����Ժ�one_pageִ��two_plane������ҳ,��Ҫ��one_page�����һ���ڵ�*/
		    {
				return NULL;
			}
		}//if(command==TWO_PLANE)
		else if(command==INTERLEAVE)
		{
			while (two_page!=NULL)
		    {
				if (two_page->current_state!=SR_WAIT)
				{
					two_page=two_page->next_node;
				}
				else if ((one_page->location->chip==two_page->location->chip)&&(one_page->location->die!=two_page->location->die))
				{
					return two_page;                                                           /*�ҵ�����one_page����ִ��interleave������ҳ*/
				}
				else
				{
					two_page=two_page->next_node;
				}
		     }
		    if (two_page==NULL)                                                                /*û���ҵ����Ժ�one_pageִ��interleave������ҳ,��Ҫ��one_page�����һ���ڵ�*/
		    {
				return NULL;
			}//while (two_page!=NULL)
		}//else if(command==INTERLEAVE)
		
	} 
	{
		return NULL;
	}
}


/*************************************************************************
*�ڴ�����������߼�����ʱ������������ǲ��ҿ���ִ�и߼������sub_request
**************************************************************************/
int find_interleave_twoplane_sub_request(struct ssd_info * ssd, unsigned int channel,struct sub_request * sub_request_one,struct sub_request * sub_request_two,unsigned int command)
{
	sub_request_one=ssd->channel_head[channel].subs_r_head;
	while (sub_request_one!=NULL)
	{
		sub_request_two=find_interleave_twoplane_page(ssd,sub_request_one,command);                /*�ҳ�����������two_plane����interleave��read�����󣬰���λ��������ʱ������*/
		if (sub_request_two==NULL)
		{
			sub_request_one=sub_request_one->next_node;
		}
		else if (sub_request_two!=NULL)                                                            /*�ҵ�����������ִ��two plane������ҳ*/
		{
			break;
		}
	}

	if (sub_request_two!=NULL)
	{
		if (ssd->request_queue!=ssd->request_tail)      
		{                                                                                         /*ȷ��interleave read���������ǵ�һ�������������*/
			if ((ssd->request_queue->lsn-ssd->parameter->subpage_page)<(sub_request_one->lpn*ssd->parameter->subpage_page))  
			{
				if ((ssd->request_queue->lsn+ssd->request_queue->size+ssd->parameter->subpage_page)>(sub_request_one->lpn*ssd->parameter->subpage_page))
				{
				}
				else
				{
					sub_request_two=NULL;
				}
			}
			else
			{
				sub_request_two=NULL;
			}
		}//if (ssd->request_queue!=ssd->request_tail) 
	}//if (sub_request_two!=NULL)

	if(sub_request_two!=NULL)
	{
		return SUCCESS;
	}
	else
	{
		return FAILURE;
	}

}


Status go_one_step(struct ssd_info * ssd, struct sub_request * sub1,struct sub_request *sub2, unsigned int aim_state,unsigned int command)
{
	unsigned int i=0,j=0,k=0,m=0;
	long long Time=0;
	struct sub_request * sub=NULL ; 
	struct sub_request * sub_twoplane_one=NULL, * sub_twoplane_two=NULL;
	struct sub_request * sub_interleave_one=NULL, * sub_interleave_two=NULL;
	struct local * location=NULL;
	if(sub1==NULL)
	{
		return ERROR;
	}

	if(command==NORMAL)
	{
		sub=sub1;
		location=sub1->location;
		switch(aim_state)						
		{	
			case SR_R_READ://目标状态是读
			{   
				/*****************************************************************************************************
			    *SR_R_DATA_TRANSFER
			    *CHIP_READ_BUSY，CHIP_DATA_TRANSFER
			    ******************************************************************************************************/
				sub->current_time=ssd->current_time;
				sub->current_state=SR_R_READ;
				sub->next_state=SR_R_DATA_TRANSFER;
				sub->next_state_predict_time=ssd->current_time+ssd->parameter->time_characteristics.tR;//data transfer from cell to register

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_READ_BUSY;
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_DATA_TRANSFER;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=ssd->current_time+ssd->parameter->time_characteristics.tR;

				break;
			}
			case SR_R_C_A_TRANSFER:
			{
				ssd->channelWorkload[sub->location->channel]++;
				sub->current_time=ssd->current_time;	
				
				sub->current_state=SR_R_C_A_TRANSFER;									
				sub->next_state=SR_R_READ;									
				sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC;	//write cycle time
				sub->begin_time=ssd->current_time;

				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn=sub->ppn;
				ssd->read_count++;
				ssd->read3++;
				ssd->channel_head[location->channel].current_state=CHANNEL_C_A_TRANSFER;
				ssd->channel_head[location->channel].current_time=ssd->current_time;										
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;								
				ssd->channel_head[location->channel].next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_C_A_TRANSFER;
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;						
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_READ_BUSY;							
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC;
				break;
			
			}
			case SR_R_DATA_TRANSFER:
			{
				sub->current_time=ssd->current_time;					
				sub->current_state=SR_R_DATA_TRANSFER;
				sub->next_state=SR_COMPLETE;				
				sub->next_state_predict_time=ssd->current_time+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;	//read cycle time
				sub->complete_time=sub->next_state_predict_time;

				ssd->channel_head[location->channel].current_state=CHANNEL_DATA_TRANSFER;		
				ssd->channel_head[location->channel].current_time=ssd->current_time;		
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;	
				ssd->channel_head[location->channel].next_state_predict_time=sub->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_DATA_TRANSFER;				
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_IDLE;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=sub->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn=-1;

				break;
			}
			case SR_W_TRANSFER:
			{
				sub->current_time=ssd->current_time;
				sub->current_state=SR_W_TRANSFER;
				sub->next_state=SR_COMPLETE;
                //current time + 7*write cycle time + sub size*write cycle time
				sub->next_state_predict_time=ssd->current_time+7*ssd->parameter->time_characteristics.tWC+(sub->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tWC;
				Time=sub->next_state_predict_time;
				
				ssd->channel_head[location->channel].current_state=CHANNEL_TRANSFER;
				ssd->channel_head[location->channel].current_time=ssd->current_time;
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;
				ssd->channel_head[location->channel].next_state_predict_time=Time;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_WRITE_BUSY;
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_IDLE;
				//current time + 7*write cycle time + sub size*write cycle time + program time
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=Time+ssd->parameter->time_characteristics.tPROG;

				sub->next_state_predict_time=ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time;
				sub->complete_time=sub->next_state_predict_time;

                //====================patch===========================
                if(sub->pop_patch >=0){
                    if(sub->sub_id == 1){
                        int a = 0;
                    }
                    int patch_ID = sub->pop_patch;
                    update_patch_info(ssd,patch_ID);
                    if(sub->first_write == 1){
                        del_processing_backup_sub(ssd,sub);
                        update_backup_sub(ssd,sub);
                    }
                    if(ssd->round_time == 0){
                        ssd->round_time = ssd->current_time;
                    }else if(ssd->current_time - ssd->round_time > 300000000){
                        unsigned int total_blocked = ssd->dram->SSD1_buffer->per_round_blocked_count +  ssd->dram->SSD2_buffer->per_round_blocked_count +
                        ssd->dram->SSD3_buffer->per_round_blocked_count + ssd->dram->SSD4_buffer->per_round_blocked_count;
//                        fprintf(ssd->statisticfile,"SSD1_per_round_blocked:%f\n",(double)ssd->dram->SSD1_buffer->per_round_blocked_count/total_blocked);
//                        fprintf(ssd->statisticfile,"SSD2_per_round_blocked:%f\n",(double)ssd->dram->SSD2_buffer->per_round_blocked_count/total_blocked);
//                        fprintf(ssd->statisticfile,"SSD3_per_round_blocked:%f\n",(double)ssd->dram->SSD3_buffer->per_round_blocked_count/total_blocked);
//                        fprintf(ssd->statisticfile,"SSD4_per_round_blocked:%f\n",(double)ssd->dram->SSD4_buffer->per_round_blocked_count/total_blocked);
//                        fprintf(ssd->statisticfile,"////////\n////////////");
                        ssd->round_time = ssd->current_time;
                        ssd->dram->SSD1_buffer->per_round_blocked_count = 0;
                        ssd->dram->SSD2_buffer->per_round_blocked_count = 0;
                        ssd->dram->SSD3_buffer->per_round_blocked_count = 0;
                        ssd->dram->SSD4_buffer->per_round_blocked_count = 0;
                    }
                    
                    int ssd_id = sub->location->channel;
                    unsigned int service_time = sub->complete_time-sub->begin_time;
                    if(ssd_id == 0){
                        if(service_time > 0){
                            ssd->dram->SSD1_buffer->total_time_service_sub += service_time;
                            ssd->dram->SSD1_buffer->total_sub_count++;
                        }else{
                            abort();
                        }
                    }else if(ssd_id == 1){
                        if(service_time > 0){
                            ssd->dram->SSD2_buffer->total_time_service_sub += service_time;
                            ssd->dram->SSD2_buffer->total_sub_count++;
                        }else{
                            abort();
                        }
                    }else if(ssd_id == 2){
                        if(service_time > 0){
                            ssd->dram->SSD3_buffer->total_time_service_sub += service_time;
                            ssd->dram->SSD3_buffer->total_sub_count++;
                        }else{
                            abort();
                        }
                    }else if(ssd_id == 3){
                        if(service_time > 0){
                            ssd->dram->SSD4_buffer->total_time_service_sub += service_time;
                            ssd->dram->SSD4_buffer->total_sub_count++;
                        }else{
                            abort();
                        }
                    }
                    if(sub->req != NULL){
                        struct sub_request *pre = sub->req->subs;
                        struct sub_request *current = NULL;
                        struct request *last_sub_req = sub->req;
                        sub->req->all--;
                        sub->req->now--;
                        sub->req->temp_time = sub->complete_time;
                        sub->req->new_write_flag = 0;
                        if(sub->complete_time > sub->req->last_sub_complete_time){
                            sub->req->last_sub_complete_time = sub->complete_time;
                        }
                        if(sub->req->all == 1){
                            sub->req->pre_complete_time = sub->complete_time;
                        }
                        if(sub->req->all == 0){    //统计最后一个完成的子请求在哪个SSD上
                            int ssd_num = sub->location->channel;
                            int64_t blocked_time = sub->complete_time - sub->req->pre_complete_time;
                            int64_t pre_sub_time = 0;
                            int64_t last_sub_time = 0;
                            int64_t req_time = 0;
                            if(blocked_time < 0){
                                req_time = sub->req->pre_complete_time - sub->req->time;
                                blocked_time =  sub->req->pre_complete_time - sub->complete_time;
//                                fprintf(ssd->statisticfile,"blocked_time_ratio:%f\n",(double)blocked_time/req_time);
                            }else{
                                req_time = sub->complete_time - sub->req->time;
                                blocked_time =  sub->complete_time - sub->req->pre_complete_time;
//                                fprintf(ssd->statisticfile,"blocked_time_ratio:%f\n",(double)blocked_time/req_time);
                            }
                            if(ssd_num == 0){
                                ssd->dram->SSD1_buffer->Last_blocked_sub_count++;
                            }else if(ssd_num == 1){
                                ssd->dram->SSD2_buffer->Last_blocked_sub_count++;
                            }else if(ssd_num == 2){
                                ssd->dram->SSD3_buffer->Last_blocked_sub_count++;
                            }else if(ssd_num == 3){
                                ssd->dram->SSD4_buffer->Last_blocked_sub_count++;
                            }
                        }
                    }
                    else{
                        struct local* temp_local = sub->location;
                        free(temp_local);
                        free(sub);
                    }
                }
                //======================================================

			}
			default :  return ERROR;
			
		}//switch(aim_state)	
	}//if(command==NORMAL)
	else if(command==TWO_PLANE)
	{
		if((sub1==NULL)||(sub2==NULL))
		{
			return ERROR;
		}
		sub_twoplane_one=sub1;
		sub_twoplane_two=sub2;
		location=sub1->location;
		
		switch(aim_state)						
		{	
			case SR_R_C_A_TRANSFER:
			{
				sub_twoplane_one->current_time=ssd->current_time;									
				sub_twoplane_one->current_state=SR_R_C_A_TRANSFER;									
				sub_twoplane_one->next_state=SR_R_READ;									
				sub_twoplane_one->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;									
				sub_twoplane_one->begin_time=ssd->current_time;

				ssd->channel_head[sub_twoplane_one->location->channel].chip_head[sub_twoplane_one->location->chip].die_head[sub_twoplane_one->location->die].plane_head[sub_twoplane_one->location->plane].add_reg_ppn=sub_twoplane_one->ppn;
				ssd->read_count++;

				sub_twoplane_two->current_time=ssd->current_time;									
				sub_twoplane_two->current_state=SR_R_C_A_TRANSFER;									
				sub_twoplane_two->next_state=SR_R_READ;									
				sub_twoplane_two->next_state_predict_time=sub_twoplane_one->next_state_predict_time;									
				sub_twoplane_two->begin_time=ssd->current_time;

				ssd->channel_head[sub_twoplane_two->location->channel].chip_head[sub_twoplane_two->location->chip].die_head[sub_twoplane_two->location->die].plane_head[sub_twoplane_two->location->plane].add_reg_ppn=sub_twoplane_two->ppn;
				ssd->read_count++;
				ssd->m_plane_read_count++;

				ssd->channel_head[location->channel].current_state=CHANNEL_C_A_TRANSFER;									
				ssd->channel_head[location->channel].current_time=ssd->current_time;										
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;								
				ssd->channel_head[location->channel].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_C_A_TRANSFER;								
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;						
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_READ_BUSY;							
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;

				
				break;
			}
			case SR_R_DATA_TRANSFER:
			{
				sub_twoplane_one->current_time=ssd->current_time;					
				sub_twoplane_one->current_state=SR_R_DATA_TRANSFER;		
				sub_twoplane_one->next_state=SR_COMPLETE;				
				sub_twoplane_one->next_state_predict_time=ssd->current_time+(sub_twoplane_one->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;			
				sub_twoplane_one->complete_time=sub_twoplane_one->next_state_predict_time;
				
				sub_twoplane_two->current_time=sub_twoplane_one->next_state_predict_time;					
				sub_twoplane_two->current_state=SR_R_DATA_TRANSFER;		
				sub_twoplane_two->next_state=SR_COMPLETE;				
				sub_twoplane_two->next_state_predict_time=sub_twoplane_two->current_time+(sub_twoplane_two->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;			
				sub_twoplane_two->complete_time=sub_twoplane_two->next_state_predict_time;
				
				ssd->channel_head[location->channel].current_state=CHANNEL_DATA_TRANSFER;		
				ssd->channel_head[location->channel].current_time=ssd->current_time;		
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;	
				ssd->channel_head[location->channel].next_state_predict_time=sub_twoplane_one->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_DATA_TRANSFER;				
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_IDLE;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=sub_twoplane_one->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn=-1;
			
				break;
			}
			default :  return ERROR;
		}//switch(aim_state)	
	}//else if(command==TWO_PLANE)
	else if(command==INTERLEAVE)
	{
		if((sub1==NULL)||(sub2==NULL))
		{
			return ERROR;
		}
		sub_interleave_one=sub1;
		sub_interleave_two=sub2;
		location=sub1->location;
		
		switch(aim_state)						
		{	
			case SR_R_C_A_TRANSFER:
			{
				sub_interleave_one->current_time=ssd->current_time;									
				sub_interleave_one->current_state=SR_R_C_A_TRANSFER;									
				sub_interleave_one->next_state=SR_R_READ;									
				sub_interleave_one->next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;									
				sub_interleave_one->begin_time=ssd->current_time;

				ssd->channel_head[sub_interleave_one->location->channel].chip_head[sub_interleave_one->location->chip].die_head[sub_interleave_one->location->die].plane_head[sub_interleave_one->location->plane].add_reg_ppn=sub_interleave_one->ppn;
				ssd->read_count++;

				sub_interleave_two->current_time=ssd->current_time;									
				sub_interleave_two->current_state=SR_R_C_A_TRANSFER;									
				sub_interleave_two->next_state=SR_R_READ;									
				sub_interleave_two->next_state_predict_time=sub_interleave_one->next_state_predict_time;									
				sub_interleave_two->begin_time=ssd->current_time;

				ssd->channel_head[sub_interleave_two->location->channel].chip_head[sub_interleave_two->location->chip].die_head[sub_interleave_two->location->die].plane_head[sub_interleave_two->location->plane].add_reg_ppn=sub_interleave_two->ppn;
				ssd->read_count++;
				ssd->interleave_read_count++;

				ssd->channel_head[location->channel].current_state=CHANNEL_C_A_TRANSFER;									
				ssd->channel_head[location->channel].current_time=ssd->current_time;										
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;								
				ssd->channel_head[location->channel].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_C_A_TRANSFER;								
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;						
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_READ_BUSY;							
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=ssd->current_time+14*ssd->parameter->time_characteristics.tWC;
				
				break;
						
			}
			case SR_R_DATA_TRANSFER:
			{
				sub_interleave_one->current_time=ssd->current_time;					
				sub_interleave_one->current_state=SR_R_DATA_TRANSFER;		
				sub_interleave_one->next_state=SR_COMPLETE;				
				sub_interleave_one->next_state_predict_time=ssd->current_time+(sub_interleave_one->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;			
				sub_interleave_one->complete_time=sub_interleave_one->next_state_predict_time;
				
				sub_interleave_two->current_time=sub_interleave_one->next_state_predict_time;					
				sub_interleave_two->current_state=SR_R_DATA_TRANSFER;		
				sub_interleave_two->next_state=SR_COMPLETE;				
				sub_interleave_two->next_state_predict_time=sub_interleave_two->current_time+(sub_interleave_two->size*ssd->parameter->subpage_capacity)*ssd->parameter->time_characteristics.tRC;			
				sub_interleave_two->complete_time=sub_interleave_two->next_state_predict_time;

				ssd->channel_head[location->channel].current_state=CHANNEL_DATA_TRANSFER;		
				ssd->channel_head[location->channel].current_time=ssd->current_time;		
				ssd->channel_head[location->channel].next_state=CHANNEL_IDLE;	
				ssd->channel_head[location->channel].next_state_predict_time=sub_interleave_two->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].current_state=CHIP_DATA_TRANSFER;				
				ssd->channel_head[location->channel].chip_head[location->chip].current_time=ssd->current_time;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state=CHIP_IDLE;			
				ssd->channel_head[location->channel].chip_head[location->chip].next_state_predict_time=sub_interleave_two->next_state_predict_time;

				ssd->channel_head[location->channel].chip_head[location->chip].die_head[location->die].plane_head[location->plane].add_reg_ppn=-1;
				
				break;
			}
			default :  return ERROR;	
		}//switch(aim_state)				
	}//else if(command==INTERLEAVE)
	else
	{
		printf("\nERROR: Unexpected command !\n" );
		return ERROR;
	}

	return SUCCESS;
}