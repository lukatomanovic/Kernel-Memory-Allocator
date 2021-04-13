#include "buddy.h"

/*Auxiliary methods section*/
//just for numbers bigger than 1
int getDegreeOfTwo(int number) {
	// if number if less or equal than 0, this will be status flag
	int degree = -1;
	
	while (number) {
		number = number >> 1;
		degree++;
	}

	//if degree<0 than return value is error flag
	return degree;
}

/*
Block* remove_from_free_space_list(BuddyMetaData* bmd, unsigned order) {
	Block* ret = bmd->free_space_list[order];
	bmd->free_space_list[order] = bmd->free_space_list[order]->next;
	ret->next = 0;
	return ret;
}
*/
void put_in_free_space_list(BuddyMetaData* bmd, unsigned order, Block* add_to_list_addr) {
	add_to_list_addr->next = bmd->free_space_list[order];
	bmd->free_space_list[order] = add_to_list_addr;
}

Block* remove_form_free_space_list(BuddyMetaData* bmd, unsigned order, Block* block_addr) {
	Block* prev=0;
	Block* curr = bmd->free_space_list[order];
	while (curr) {
		if (curr == block_addr) {
			if (!prev) {
				bmd->free_space_list[order] = bmd->free_space_list[order]->next;
			}
			else {
				prev->next = curr->next;
			}
			curr->next = 0;
			return curr;
		}
		prev = curr;
		curr = curr->next;
	}
	return 0;
}
/*Auxiliary methods - end of section*/

/*Interface(buddy.h) implementation section*/
BuddyMetaData* buddy_init(void* space, int block_num)
{
	if (block_num <= 0)	return 0;

	Block* curr_blok_addr=(Block*)space;

	BuddyMetaData* bmd = (BuddyMetaData*)space;

	//first block is for Buddy Meta Data, so can use all space after that
	curr_blok_addr += 1;
	block_num--;

	bmd->first_block = curr_blok_addr;


	//set degree of two for the bigest possible space
	int deg_of_two = getDegreeOfTwo(block_num);
	bmd->last_used_degree_of_two = deg_of_two;

	//set number of blocks available for allocation
	bmd->number_of_available_blocks = block_num;
	bmd->total_number_of_blocks = block_num;

	//it could be possible to have a number of blocks witch is not power of 2, so we can not throw that space
	while (deg_of_two > -1) {
		int space_size = power_of_two(deg_of_two);
		if (block_num & space_size) {
			bmd->free_space_list[deg_of_two] = curr_blok_addr;
			bmd->free_space_list[deg_of_two]->next = 0;
			curr_blok_addr += space_size;
		}
		else {
			bmd->free_space_list[deg_of_two] = 0;
		}
		deg_of_two--;
	}
	bmd->mutex = CreateMutex(NULL,FALSE,NULL);
	return bmd;
}

Block* buddy_alloc_unsafe(BuddyMetaData* bmd, unsigned int number_of_bloks_for_alloc, int* error_flag)
{
	*error_flag = 0;
	
	//check for invalid arguments
	if ((number_of_bloks_for_alloc <= 0) || (bmd == 0)) {
		*error_flag = -3;
		return 0;
	}
	//number_of_bloks_for_alloc should be power of 2
	//if it is not case then we will have internal fragmentation
	int alloc_bloks_count = 1;
	int order = 0;
	while (alloc_bloks_count < number_of_bloks_for_alloc)
	{
		alloc_bloks_count = alloc_bloks_count << 1;
		order++;
	}

	//check if buddy allocator has enough free blocks
	if (alloc_bloks_count > bmd->number_of_available_blocks) {
		*error_flag = -1;//not enough space
		return 0;
	}

	//find first block big enough
	int index = order;
	while (index <= bmd->last_used_degree_of_two) {
		if (bmd->free_space_list[index]) break;
		index++;
	}

	//if there is not enough adjacent blocks
	if ((index-1) == bmd->last_used_degree_of_two )
	{
		*error_flag = -2;
		return 0;
	}

	//now we have found a suitable piece of memory to allocate
	//let's do it

	//take free part of memory from list of free spaces
	Block* beginning_of_the_allocated_space = bmd->free_space_list[index];
	bmd->free_space_list[index] = bmd->free_space_list[index]->next;


	//check if taken memory is bigger then it is needed
	//index - first available degree
	//odrer - needed degree
	int num_of_partitions = index - order;
	int partition_size = power_of_two(order);
	for (int p = 0; p < num_of_partitions; p++, partition_size <<= 1) {
		Block* next_partition = beginning_of_the_allocated_space + partition_size;
		/*next_partition->next = bmd->free_space_list[order + p];
		bmd->free_space_list[order + p] = next_partition;*/
		put_in_free_space_list(bmd, order + p, next_partition);
	}

	bmd->number_of_available_blocks -= alloc_bloks_count;
	return beginning_of_the_allocated_space;
}


Block* buddy_merge(BuddyMetaData* bmd, Block* start_address, int num_of_bloks_to_be_released, int order, int *merged_status) {
	unsigned displacement = (unsigned)start_address - (unsigned)(bmd->first_block);
	unsigned space_size = BLOCK_SIZE << order;//BLOCK_SIZE*num_of_bloks_to_be_released
	unsigned buddy_displacement;
	if (space_size & displacement) {
		//buddy is on the left side
		buddy_displacement = displacement - space_size;
	}
	else {
		//buddy is on the right side
		buddy_displacement = displacement + space_size;
	}
	unsigned buddy_start_addr = (unsigned)(bmd->first_block) + buddy_displacement;
	Block* buddy_start_block = (Block*)buddy_start_addr;
	if (bmd->free_space_list[order] && (buddy_start_block < (bmd->first_block + bmd->total_number_of_blocks))) {
		Block* buddy_ret = remove_form_free_space_list(bmd, order, buddy_start_block);
		//buddy found
		if (buddy_ret) {
			bmd->number_of_available_blocks -= num_of_bloks_to_be_released;
			Block* merged_space_addr = (buddy_start_block < start_address) ? buddy_start_block : start_address;
			*merged_status = 1;
			return merged_space_addr;
		}
	}
	*merged_status = 0;
	return 0;
}

void buddy_free_with_merge_unsafe(BuddyMetaData* bmd, Block* start_address, int num_of_bloks_to_be_released) {
	
	if (start_address == NULL)return;
	
	Block* ret = 0;
	int merge_status = 0;
	Block* addr_to_be_merged = start_address;
	int block_num = num_of_bloks_to_be_released;
	int order = getDegreeOfTwo(num_of_bloks_to_be_released);
	do {
		ret = buddy_merge(bmd, addr_to_be_merged, block_num,order, &merge_status);
		if (!merge_status)break;
		block_num=block_num << 1;
		order++;
		addr_to_be_merged= (ret < addr_to_be_merged) ? ret : addr_to_be_merged;
	} while (1);
	addr_to_be_merged->next = 0;
	put_in_free_space_list(bmd, order, addr_to_be_merged);
	bmd->number_of_available_blocks += block_num;
	return;
}

Block* buddy_alloc(BuddyMetaData* bmd, unsigned int number_of_bloks_for_alloc, int* error_flag)
{
	WaitForSingleObject(bmd->mutex, INFINITE);
	Block* ret = buddy_alloc_unsafe(bmd, number_of_bloks_for_alloc, error_flag);
	ReleaseMutex(bmd->mutex);
	return ret;
}

void buddy_free_with_merge(BuddyMetaData* bmd, Block* start_address, int num_of_bloks_to_be_released)
{
	WaitForSingleObject(bmd->mutex, INFINITE);
	buddy_free_with_merge_unsafe(bmd, start_address, num_of_bloks_to_be_released);
	ReleaseMutex(bmd->mutex);
}

void buddy_free_unsafe(BuddyMetaData* bmd, Block* start_address, int num_of_bloks_to_be_released)
{
	if ((num_of_bloks_to_be_released <= 0) || (bmd == 0)||(start_address==0)) {
		return;
	}
	/*displacement is the offset to the start area alokatibilnog */
	unsigned displacement = (unsigned)start_address - (unsigned)(bmd->first_block);//we must use cast if we do not want a scaled offset
	int order = getDegreeOfTwo(num_of_bloks_to_be_released);

	/*displacement can be 2^x+2^y+....+2^order or just 2^x+2^y+....
		in the first case, the buddy of released part is on his left side(buddy is 2^order)
		in the other case, the buddy is on the right side
	*/
	unsigned space_size = BLOCK_SIZE<<order;//BLOCK_SIZE*num_of_bloks_to_be_released
	unsigned buddy_displacement;
	if (space_size & displacement) {
		//buddy is on the left side
		buddy_displacement = displacement - space_size;
	}
	else {
		//buddy is on the right side
		buddy_displacement = displacement + space_size;
	}
	unsigned buddy_start_addr = (unsigned)(bmd->first_block) + buddy_displacement;
	Block* buddy_start_block = (Block*)buddy_start_addr;

	/*check is buddy free
		if buddy is free then merge and try to merge again with new buddy
		buddy has the same size and sam degree, so we are trying to find him in list with index=order
	*/
	//maybe buddy is not in available space, so we should check it to speed up process
	if (bmd->free_space_list[order] && (buddy_start_block<(bmd->first_block+bmd->total_number_of_blocks))) {
		Block *buddy_ret=remove_form_free_space_list(bmd, order, buddy_start_block);
		//buddy found
		if (buddy_ret) {
			bmd->number_of_available_blocks -= num_of_bloks_to_be_released;
			Block* merged_space_addr = (buddy_start_block < start_address) ? buddy_start_block : start_address;
			buddy_free_unsafe(bmd, merged_space_addr, num_of_bloks_to_be_released << 1);
			return 0;
		}
	}
	start_address->next = 0;
	put_in_free_space_list(bmd, order, start_address);
	bmd->number_of_available_blocks += num_of_bloks_to_be_released;
	return 0;
}

/*Interface(buddy.h) implementation end of section*/

void printBuddy(BuddyMetaData* bmd)
{
	int i = 0;
	for (i = 0; i <= bmd->last_used_degree_of_two; i++)
	{
		printf("free_space_list[%d]: ", i);
		Block* curr = bmd->free_space_list[i];
		while (curr)
		{
			printf("%x -> ", curr);
			curr = curr->next;
		}
		printf("null\n");
		printf("\n");
	}
}
