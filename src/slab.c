#include"slab.h"
#include"buddy.h"

#include<stdio.h>
#include<string.h>

#include <windows.h>

#define MIN_BUFFER_ORDER 5
#define MAX_BUFFER_ORDER 17
#define NUMBER_OF_SMALL_BUFFERS (MAX_BUFFER_ORDER - MIN_BUFFER_ORDER + 1)
#define MIN_NUMBER_OF_OBJ_PER_SLAB 1
#define MAX_CACHE_NAME_LENGHT (16)
#define NUMBER_OF_LIST_TYPES (3)//FULL, NOT EMPTY, EMPTY
#define BITS_IN_BYTE 8
#define SLAB_FULL 0xFF


typedef enum {
	NO_ERROR_FLAG=0, 
	ARGUMENT_EXCEPTION, 
	DESTRUCTOR_MISSING, 
	CANNOT_ALLOC_SLAB, 
	SLAB_IS_FULL, 
	BUDDY_MEMORY_FULL,
	FREE_ERROR_POINTER_OUT_OF_MEMORY,
	CANNOT_ALOCATE_CACHE_OBJ,
	BAD_POINTER_ADDRESS,
	BAD_POINTER_ADDRESS_KFREE
}ErrorCode;

typedef struct slab_header_s {

	struct SlabMetaData* next_slab;

	kmem_cache_t* cache;

	//unsigned L1_cache_displacement;

	unsigned number_of_free_slots;

	void* slab_first_obj_address;
	//void* memory_block_start_addr;

	byte* slots_status_map;

}slab_header_t;

typedef enum { F, T } boolean_t;
typedef enum { FULL,PARTIAL,EMPTY } slab_status_t;

typedef struct kmem_cache_s {
	char name[MAX_CACHE_NAME_LENGHT];

	struct kmem_cache_s* next;

	slab_header_t* slabs_empty;
	slab_header_t* slabs_partial;
	slab_header_t* slabs_full;

	
	//unsigned cache_size;
	unsigned cache_used;		//num of alloc obj
	unsigned cache_space_lost_per_slab;
	unsigned slab_count;		//number of slabs-> redudantno
	size_t obj_size;			//size of objects in this cache in bytes
	unsigned slab_size;			//number of blocks used for slab
	unsigned obj_per_slab;
	unsigned slots_status_map_size; //in bytes -> 8 objects , 8 bits in 1 byte

	unsigned next_L1_cache_settlement_offset;

	ErrorCode error_code;

	boolean_t added_empty;		//shrink optimization

	void (*ctor)(void*);		//object constructor
	void (*dtor)(void*);		//object destructor

	//cache mutex
	HANDLE mutex;


}kmem_cache_t;

typedef struct kmem_small_buff_cache_s {
	kmem_cache_t cache;
}kmem_small_buff_cache_t;

typedef struct kmem_memory_manager_s {
	BuddyMetaData* buddy_manager;
	kmem_cache_t global_cache;
	kmem_small_buff_cache_t small_memory_buffers[NUMBER_OF_SMALL_BUFFERS];
	//mutex
	HANDLE mutex;
}kmem_memory_manager_t;

kmem_memory_manager_t* memory_manager;

/*
void sizemem() {
	printf("%d", sizeof(kmem_memory_manager_t));
}
*/

//in blocks -PROVERENO
unsigned min_slab_block_count(size_t obj_size) {
	unsigned slots_status_map_size = MIN_NUMBER_OF_OBJ_PER_SLAB / BITS_IN_BYTE + ((MIN_NUMBER_OF_OBJ_PER_SLAB % BITS_IN_BYTE) ? 1 : 0);//num of bytes
	unsigned ret = sizeof(slab_header_t) + obj_size * MIN_NUMBER_OF_OBJ_PER_SLAB + slots_status_map_size;//num of bytes
	unsigned block_num=ret/BLOCK_SIZE+ ((ret % BLOCK_SIZE) ? 1 : 0);
	return block_num;
}

//PROVERENO
unsigned round_slab_block_count_to_pow2(unsigned block_num) {
	int order = 0;
	while (power_of_two(order) < block_num) {
		order++;
	}
	return power_of_two(order);
}

// Initialize kmem_cache_t structure -PROVERENO
void cache_init(kmem_cache_t* cache, const char* name, size_t obj_size, void(*ctor)(void*), void(*dtor)(void*))
{
	strcpy(cache->name, name);
	cache->obj_size = obj_size;
	cache->ctor = ctor;
	cache->dtor = dtor;
	cache->next = 0;
	cache->error_code = NO_ERROR_FLAG;
	cache->slabs_full = 0;
	cache->slabs_partial = 0;
	cache->slabs_empty = 0;
	unsigned block_alloc_count = min_slab_block_count(obj_size);
	block_alloc_count = round_slab_block_count_to_pow2(block_alloc_count);
	cache->slab_size = block_alloc_count;// in blocks
	cache->slab_count = 0;
	cache->cache_used = 0;
	cache->added_empty = F;

	//calculate size of slots_bitmap_size
	size_t rest_of_space_in_slab = block_alloc_count * BLOCK_SIZE - sizeof(slab_header_t);

	size_t slots_status_map_size=0;
	unsigned obj_count=0;

	while (slots_status_map_size + obj_count * obj_size <= rest_of_space_in_slab) {
		if (obj_count % BITS_IN_BYTE == 0) {
			slots_status_map_size++;
		}
		obj_count++;
	}

	obj_count--;//we have reserved space for one object at least yet
	//slots_status_map_size = obj_count / BITS_IN_BYTE + ((obj_count % BITS_IN_BYTE) ? 1 : 0);


	//cache->slots_status_map_size = slots_status_map_size; - problem kod brojeva koji daju ostatak 0 570 obj na primer ce dati 72, umesto 71
	cache->slots_status_map_size = slots_status_map_size- ((obj_count%BITS_IN_BYTE==0)?1:0);
	cache->obj_per_slab = obj_count;

	cache->cache_space_lost_per_slab = rest_of_space_in_slab - slots_status_map_size - obj_count * obj_size;
	cache->next_L1_cache_settlement_offset = 0;

	cache->mutex = CreateMutex(NULL, FALSE, NULL);
}

//PROVERENO
void kmem_init(void* space, int block_num)
{
	BuddyMetaData *bmd = buddy_init(space, block_num);

	//put memory manager in first blok after buddy meta data
	memory_manager = (kmem_memory_manager_t*)(bmd + 1);
	memory_manager->buddy_manager = bmd;
	memory_manager->mutex= CreateMutex(NULL, FALSE, NULL);

	cache_init(&memory_manager->global_cache, "kmem_cache", sizeof(kmem_cache_t), 0, 0);

	char cache_name[MAX_CACHE_NAME_LENGHT];
	int obj_size = power_of_two(MIN_BUFFER_ORDER);
	for (int index = 0; index < NUMBER_OF_SMALL_BUFFERS; index++, obj_size<<=1)
	{
		sprintf(cache_name, "SMbuff%d", index + MIN_BUFFER_ORDER);
		cache_init(&memory_manager->small_memory_buffers[index].cache, cache_name, obj_size, NULL, NULL);
	}
}

//PROVERENO
kmem_cache_t* find_cache(const char* cache_name)
{
	WaitForSingleObject(memory_manager->global_cache.mutex,INFINITE);
	kmem_cache_t* curr = memory_manager->global_cache.next;

	while (curr)
	{
		if (strcmp(cache_name, curr->name) == 0) {
			ReleaseMutex(memory_manager->global_cache.mutex);
			return curr;
		}
		curr = curr->next;
	}
	ReleaseMutex(memory_manager->global_cache.mutex);
	return 0;
}

//PROVERENO
boolean_t remove_slab_from_slabs(slab_header_t* slab, slab_status_t list_type) {
	slab_header_t* curr = 0, * prev = 0, ** change_head=0;
	switch (list_type) {
	case FULL: {
		curr = slab->cache->slabs_full;
		change_head = &slab->cache->slabs_full;
		break;
	}
	case PARTIAL: {
		curr = slab->cache->slabs_partial;
		change_head = &slab->cache->slabs_partial;
		break;
	}
	case EMPTY: {
		curr = slab->cache->slabs_empty;
		change_head = &slab->cache->slabs_empty;
		break;
	}
	}

	while (curr) {
		if (curr == slab) {
			if (!prev)*change_head = (*change_head)->next_slab;
			else prev->next_slab = curr->next_slab;
			return T;
		}
		prev = curr;
		curr = curr->next_slab;
	}
	return F;

}

//PROVERENO
void insert_slab_in_slabs(slab_header_t* slab, slab_status_t list_type) 
{
	switch (list_type) {
	case FULL: {
		slab->next_slab = slab->cache->slabs_full;
		slab->cache->slabs_full = slab;
		break;
	}
	case PARTIAL: {
		slab->next_slab = slab->cache->slabs_partial;
		slab->cache->slabs_partial = slab;
		break;
	}
	case EMPTY: {
		slab->next_slab = slab->cache->slabs_empty;
		slab->cache->slabs_empty = slab;
		break;
	}
	}
}


// Allocate new slab for the cache - PROVERENO
boolean_t alloc_slab(kmem_cache_t* cache)
{
	//Calculate L1 sttlement for this slab
	unsigned L1_offset = cache->next_L1_cache_settlement_offset;
	cache->next_L1_cache_settlement_offset=cache->next_L1_cache_settlement_offset+ CACHE_L1_LINE_SIZE;
	if ((int)cache->next_L1_cache_settlement_offset > ((int)cache->cache_space_lost_per_slab - CACHE_L1_LINE_SIZE))
		cache->next_L1_cache_settlement_offset = 0;


	//lets allocate slab
	int error_flag=0;
	Block* alloc_addr =buddy_alloc(memory_manager->buddy_manager, cache->slab_size,&error_flag);
	
	if (cache->error_code < 0||alloc_addr==0) {
		cache->error_code = BUDDY_MEMORY_FULL;
		return F;
	}
	
	slab_header_t* start_slab_addr = (slab_header_t*)(alloc_addr);
	//start_slab_addr->L1_cache_displacement = L1_offset;
	start_slab_addr->next_slab = 0;
	start_slab_addr->cache = cache;
	//start_slab_addr->memory_block_start_addr = alloc_addr;
	start_slab_addr->number_of_free_slots = cache->obj_per_slab;
	start_slab_addr->slots_status_map = (byte*)(start_slab_addr +1);//skip slab header
	start_slab_addr->slab_first_obj_address = start_slab_addr->slots_status_map + cache->slots_status_map_size+L1_offset;


	for (int i = 0; i < cache->slots_status_map_size; i++) {
		start_slab_addr->slots_status_map[i] = 0x00;
	}

	if (cache->ctor)
	{
		for (int i = 0; i < cache->obj_per_slab; i++)
		{
			cache->ctor((byte*)start_slab_addr->slab_first_obj_address + i * (cache->obj_size));
		}
	}
	insert_slab_in_slabs(start_slab_addr, EMPTY);
	cache->slab_count++;
	return T;
}

//We have to be carefull with binding this argument; It must be partial or empty slab; - PROVERENO
void* alloc_slab_slot(slab_header_t* slab_meta_data)
{
	boolean_t empty_flag = F;
	if (slab_meta_data->number_of_free_slots == slab_meta_data->cache->obj_per_slab)empty_flag = T;


	unsigned obj_index_in_slab;
	void* alloc_object;
	int index=0;
	unsigned bit_in_byte = 0x80;
	while ((index < slab_meta_data->cache->slots_status_map_size)&& (slab_meta_data->slots_status_map[index] == SLAB_FULL)) {
		index++;
	}
	if (index == slab_meta_data->cache->slots_status_map_size) { slab_meta_data->cache->error_code = SLAB_IS_FULL; return 0; }//just security gard; its used only internal
	unsigned pos_in_byte = 0;
	while (slab_meta_data->slots_status_map[index] & bit_in_byte) {
		bit_in_byte >>= 1;
		pos_in_byte++;
	}
	obj_index_in_slab = index * BITS_IN_BYTE + pos_in_byte;
	if (obj_index_in_slab >= slab_meta_data->cache->obj_per_slab) { slab_meta_data->cache->error_code = SLAB_IS_FULL; return 0; }//just security gard;
	slab_meta_data->slots_status_map[index] = slab_meta_data->slots_status_map[index] | bit_in_byte;


	slab_meta_data->number_of_free_slots--;

	if (slab_meta_data->number_of_free_slots&& empty_flag)
	{
		remove_slab_from_slabs(slab_meta_data, EMPTY);
		insert_slab_in_slabs(slab_meta_data, PARTIAL);
	}
	else if (!slab_meta_data->number_of_free_slots)
	{
		if(empty_flag)remove_slab_from_slabs(slab_meta_data, EMPTY);//case: obj per slab == 1
		else remove_slab_from_slabs(slab_meta_data, PARTIAL);
		insert_slab_in_slabs(slab_meta_data, FULL);
	}

	alloc_object = ((byte*)slab_meta_data->slab_first_obj_address + obj_index_in_slab * slab_meta_data->cache->obj_size);
	return alloc_object;
}

// Allocate one object from cache ->cache used ++ -PROVERENO
void* alloc_oject_slot_in_slab_cache(kmem_cache_t* cache)
{
	void* cache_obj_addr = NULL;
	if (cache->slabs_partial)
	{
		cache_obj_addr = alloc_slab_slot(cache->slabs_partial);
	}
	else 
	{
		//Alocate new slab for the *cache
		if (!cache->slabs_empty) {
			if (alloc_slab(cache) == F)
			{
				cache->error_code = CANNOT_ALLOC_SLAB;
				return 0;
			}
			cache->added_empty = T;
		}
		cache_obj_addr = alloc_slab_slot(cache->slabs_empty);
	}
	cache->cache_used++;
	return cache_obj_addr;
}

//PROVERENO
kmem_cache_t* kmem_cache_create(const char* name, size_t size, void(*ctor)(void*), void(*dtor)(void*))
{
	
	kmem_cache_t* cache = find_cache(name);
	if (cache) {
		return cache;//cache exists
	}
	WaitForSingleObject(memory_manager->global_cache.mutex, INFINITE);
	cache = (kmem_cache_t*)alloc_oject_slot_in_slab_cache(&memory_manager->global_cache);
	if (!cache) {
		ReleaseMutex(memory_manager->global_cache.mutex);
		return 0;
	}
	cache_init(cache, name, size, ctor, dtor);
	cache->next = memory_manager->global_cache.next;
	memory_manager->global_cache.next = cache;
	ReleaseMutex(memory_manager->global_cache.mutex);
	return cache;
}


// Allocate one object from cache -PROVERENO
void* kmem_cache_alloc(kmem_cache_t* cache)
{
	if (!cache) {
		printf("\nCache doesnt exist!\n");
		return 0;
	}
	WaitForSingleObject(cache->mutex,INFINITE);
	void* object_space_alloc  = alloc_oject_slot_in_slab_cache(cache);
	if (!object_space_alloc)cache->error_code = CANNOT_ALOCATE_CACHE_OBJ;
	ReleaseMutex(cache->mutex);
	return object_space_alloc;
}


// Alloacate one small memory buffer - PROVERENO
void* kmalloc(size_t size)
{
	if (size<power_of_two(MIN_BUFFER_ORDER) || size>power_of_two(MAX_BUFFER_ORDER))return 0;

	unsigned int order = 0;
	void* small_buffer = NULL;
	while (power_of_two(order) < size) order++;

	WaitForSingleObject(memory_manager->small_memory_buffers[order - MIN_BUFFER_ORDER].cache.mutex, INFINITE);

	small_buffer = alloc_oject_slot_in_slab_cache(&(memory_manager->small_memory_buffers[order - MIN_BUFFER_ORDER]));

	ReleaseMutex(memory_manager->small_memory_buffers[order - MIN_BUFFER_ORDER].cache.mutex);

	return small_buffer;
}


//Return number of deallocated blocks - PROVERENO
int kmem_cache_shrink(kmem_cache_t* cachep) {
	if (cachep == 0)return 0;
	WaitForSingleObject(cachep->mutex, INFINITE);
	//printf("Usao da skrati!");
	if (cachep->added_empty == T) {
		cachep->added_empty = F;
		//printf("Izasao!");
		ReleaseMutex(cachep->mutex);
		return 0;
	}
	unsigned free_space_block_count = 0;
	while (cachep->slabs_empty) {
		slab_header_t* old = cachep->slabs_empty;
		//Block* slab_buddy_addr = (Block*)old->memory_block_start_addr;
		//buddy_free_with_merge(memory_manager->buddy_manager, slab_buddy_addr, cachep->slab_size);
		cachep->slabs_empty = cachep->slabs_empty->next_slab;
		buddy_free_with_merge(memory_manager->buddy_manager, old, cachep->slab_size);
		free_space_block_count += cachep->slab_size;
		cachep->slab_count--;
	}
	//printf("Izasao!");
	ReleaseMutex(cachep->mutex);
	return free_space_block_count;
	//return 0;
}


int object_free(slab_header_t* slab,  void* object) {

	unsigned obj_per_slab = slab->cache->obj_per_slab; 
	size_t obj_size = slab->cache->obj_size;
	//check address range
	if (slab->slab_first_obj_address > object || ((byte*)slab->slab_first_obj_address + obj_per_slab * obj_size) < object) {
		return 0;//obect is not in this slab
	}
	unsigned displacement = (byte*)object - (byte*)slab->slab_first_obj_address;
	if ((displacement % obj_size) != 0) return -1; //WRONG ADDRESS
	int obj_pos_in_slab= displacement / obj_size;
	unsigned status_map_index = obj_pos_in_slab / BITS_IN_BYTE;
	unsigned status_bit_pos = obj_pos_in_slab % BITS_IN_BYTE;
	slab->slots_status_map[status_map_index] &= ~(1 << (BITS_IN_BYTE - status_bit_pos-1));//set status bit to 0

	if (slab->number_of_free_slots == 0) {
		remove_slab_from_slabs(slab, FULL);
		if(obj_per_slab>1)insert_slab_in_slabs(slab, PARTIAL);
		else insert_slab_in_slabs(slab, EMPTY);

	}
	else if (slab->number_of_free_slots == (obj_per_slab-1)) {
		remove_slab_from_slabs(slab, PARTIAL);
		insert_slab_in_slabs(slab, EMPTY);
	}
	slab->number_of_free_slots++;
	if (slab->cache->dtor) slab->cache->dtor(object);
	if (slab->cache->ctor)slab->cache->ctor(object);
	return 1;
}



// Deallocate one object from cache
void kmem_cache_free(kmem_cache_t* cachep, void* objp) {
	if (cachep == 0)return;
	WaitForSingleObject(cachep->mutex, INFINITE);
	//check if cache exists
	//kmem_cache_t* check_cache = find_cache(cachep->name);
	//if (cachep != check_cache)return;
	slab_header_t* curr = cachep->slabs_full;
	while (curr) {
		int ret = object_free(curr, objp);
		if (ret == 1) {
			cachep->cache_used--;
			ReleaseMutex(cachep->mutex);
			return;
		}
		if (ret == -1) {
			ReleaseMutex(cachep->mutex);
			return;
		}
		curr = curr->next_slab;
	}
	curr = cachep->slabs_partial;
	while (curr) {
		int ret = object_free(curr, objp);
		if (ret == 1) {
			cachep->cache_used--;
			ReleaseMutex(cachep->mutex);
			return;
		}
		if (ret == -1) {
			ReleaseMutex(cachep->mutex);
			return;
		}
		curr = curr->next_slab;
	}
	ReleaseMutex(cachep->mutex);
	return;
}

// Deallocate one small memory buffer
void kfree(const void* objp) {
	for (int i = 0; i < NUMBER_OF_SMALL_BUFFERS; i++) {
		//Ovo treba optimizovati, jer mozda nije radjen shrink-> cache used ne koriscen do sada
		WaitForSingleObject(memory_manager->small_memory_buffers[i].cache.mutex, INFINITE);
		slab_header_t* curr = memory_manager->small_memory_buffers[i].cache.slabs_full;
		while (curr) {
			int ret = object_free(curr, objp);
			if (ret == 1) {
				memory_manager->small_memory_buffers[i].cache.cache_used--;
				ReleaseMutex(memory_manager->small_memory_buffers[i].cache.mutex);
				return;
			}
			if (ret == -1) {
				memory_manager->small_memory_buffers[i].cache.error_code = BAD_POINTER_ADDRESS_KFREE;
				ReleaseMutex(memory_manager->small_memory_buffers[i].cache.mutex);
				return;
			}
			curr = curr->next_slab;
		}
		curr = memory_manager->small_memory_buffers[i].cache.slabs_partial;
		while (curr) {
			int ret = object_free(curr, objp);
			if (ret == 1) {
				memory_manager->small_memory_buffers[i].cache.cache_used--;
				ReleaseMutex(memory_manager->small_memory_buffers[i].cache.mutex);
				return;
			}
			if (ret == -1) {
				memory_manager->small_memory_buffers[i].cache.error_code = BAD_POINTER_ADDRESS_KFREE;
				ReleaseMutex(memory_manager->small_memory_buffers[i].cache.mutex);
				return;
			}
			curr = curr->next_slab;
		}
		ReleaseMutex(memory_manager->small_memory_buffers[i].cache.mutex);
	}
	return;//object not found
}

// Print error message
int kmem_cache_error(kmem_cache_t* cachep) {
	//Mozda prebaciti u tekstualni oblik, razmisliti o tome
	if (cachep == 0)return -1;
	WaitForSingleObject(cachep->mutex,INFINITE);
	if (cachep->error_code!=NO_ERROR_FLAG)
	{
		printf_s("Error: %d\n", cachep->error_code);
	}
	ReleaseMutex(cachep->mutex);
}

void destroy_slab_list(slab_header_t* slab) {
	slab_header_t* old;
	while (slab)
	{
		old = slab;
		slab = slab->next_slab;
		buddy_free_with_merge(memory_manager->buddy_manager, old, old->cache->slab_size);
	}
}
// Deallocate cache
void kmem_cache_destroy(kmem_cache_t* cachep) {
	if (cachep == 0)return;
	WaitForSingleObject(memory_manager->global_cache.mutex, INFINITE);
	kmem_cache_t* curr = memory_manager->global_cache.next;
	kmem_cache_t* prev = &memory_manager->global_cache;
	while (curr && (strcmp(cachep->name, curr->name)!=0)) {
		prev = curr;
		curr = curr->next;
	}
	if (!curr) {
		ReleaseMutex(memory_manager->global_cache.mutex);
		return;
	}
	
	prev->next = curr->next;
	ReleaseMutex(memory_manager->global_cache.mutex);

	WaitForSingleObject(cachep->mutex, INFINITE);

	destroy_slab_list(cachep->slabs_full);
	destroy_slab_list(cachep->slabs_partial);
	destroy_slab_list(cachep->slabs_empty);

	kmem_cache_free(&memory_manager->global_cache, cachep);
	ReleaseMutex(cachep->mutex);
}


unsigned cache_size_to_block(unsigned size) {
	return size / BLOCK_SIZE + (size % BLOCK_SIZE)?1:0;
}

void kmem_cache_info(kmem_cache_t* cachep) {
	if (cachep == 0)return;
	WaitForSingleObject(memory_manager->global_cache.mutex, INFINITE);
	printf_s("\nCache info\n");
	printf_s("Name: %s\n", cachep->name);
	printf_s("Object size [bytes]: %d\n", cachep->obj_size);
	printf_s("Cache size  [blocks=4096B]: %d\n", cache_size_to_block(sizeof(kmem_cache_t)) + cachep->slab_count * cachep->slab_size);
	printf_s("Slabs count: %d\n", cachep->slab_count);
	printf_s("Objects per slab: %d\n", cachep->obj_per_slab);
	int usage = cachep->slab_count * cachep->obj_per_slab;
	double perc = 0;
	if (usage != 0)
		perc = 100 * ((double)cachep->cache_used / (cachep->slab_count * cachep->obj_per_slab));
	printf_s("Cache usage percentage: %.1f%%\n\n", perc);
	ReleaseMutex(memory_manager->global_cache.mutex);
}