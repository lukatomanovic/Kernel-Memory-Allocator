#pragma once
#ifndef BUDDY_H_
#define BUDDY_H_

#define CACHE_L1_LINE_SIZE (64)
#define BLOCK_SIZE (4096)
#define HEADER_SIZE (32)
#define FIRST_BLOCK_INDEX 1
#define power_of_two(degree) (1<<degree)
typedef unsigned char byte;

typedef union AllocationUnit {
	union AllocationUnit* next;
	byte block[BLOCK_SIZE];
}Block;

#include<windows.h>

typedef struct BuddyMetaData {
	Block* free_space_list[HEADER_SIZE];//each position iz a pow of 2, example free_space_list[0] iz for list of free spaces with size of 1 block
	Block* first_block;//fist blok in usable memory
	int number_of_available_blocks;
	int total_number_of_blocks;//include virtual blocks
	int last_used_degree_of_two;//it is used for free_space_list as last_index=last_used_degree_of_two-1;

	HANDLE mutex;
}BuddyMetaData;


//initialize buddy allocator
BuddyMetaData* buddy_init(void* space, int block_num);

//return address of the first allocated block 
Block* buddy_alloc_unsafe(BuddyMetaData* bmd, unsigned int number_of_bloks_for_alloc, int* error_flag);

//release allocated space
void buddy_free_unsafe(BuddyMetaData* bmd, Block* start_address, int num_of_bloks_to_be_released);
void buddy_free_with_merge_unsafe(BuddyMetaData* bmd, Block* start_address, int num_of_bloks_to_be_released);

//SAFE
Block* buddy_alloc(BuddyMetaData* bmd, unsigned int number_of_bloks_for_alloc, int* error_flag);
void buddy_free_with_merge(BuddyMetaData* bmd, Block* start_address, int num_of_bloks_to_be_released);

//just for test
void printBuddy(BuddyMetaData* bmd);


#endif