#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
/*
 * Optimized algorithm: In my algorithm, I used a linked list to keep 
 * track of the free blocks. And I used "first fit" to allocate the
 * malloc request. I also put the freed block at the second position 
 * in linked list. These three ways can improve the speed. Coalescing 
 * will deal with space optimization.
 */

#define FREE 0
#define ALLOCATED 1

typedef struct _node_t {
		int size;
		struct _node_t *next;
		int magic;
} node_t;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
node_t *head = NULL;

/* mymalloc_init: initialize any data structures that your malloc needs in
                  order to keep track of allocated and free blocks of 
                  memory.  Get an initial chunk of memory for the heap from
                  the OS using sbrk() and mark it as free so that it can  be 
                  used in future calls to mymalloc()
*/
int mymalloc_init() {
	 
	head = sbrk(4096);
	head->size  = 4096 - sizeof(node_t);
	head->next =  NULL;
	head->magic = FREE;
	
	return 0; // non-zero return value indicates an error
}


/* mymalloc: allocates memory on the heap of the requested size. The block
             of memory returned should always be padded so that it begins
             and ends on a word boundary.
     unsigned int size: the number of bytes to allocate.
     retval: a pointer to the block of memory allocated or NULL if the 
             memory could not be allocated. 
             (NOTE: the system also sets errno, but we are not the system, 
                    so you are not required to do so.)
*/
void *mymalloc(unsigned int size) {
	//Logic: In my implementation, there is a header for each allocated 
	//block. And I used linked list to track the free memory block
	//Use "first fit" to find the free block

	pthread_mutex_lock(&mutex); //lock the process here
	
	node_t *ret_ptr; //Return pointer
	
	//The real size of the malloc request should be initial size + header size
	size += sizeof(node_t);
	//8-bit alignment here
	if (size != size/8 * 8) {
		size = ((size/8) + 1) * 8;
	}
	
	if (head->size >= size) {
		//Creates two new heads .
		node_t *new_head = NULL; //This is header for new free block
		node_t *new_allocated_head = NULL; //This is header for the new mem	
		//Set up the header for new free block (shrinked)
		new_head = (void *)head + size;
		//printf("-----------------------\n");
		//printf("old head addr: %p\n", head);
		//printf("request size: %d\n", size);
		//printf("new_head addr: %p\n", new_head);
		//printf("old head free size: %d\n", head->size);
		new_head->size = head->size - size;
		new_head->next = head->next;
		new_head->magic = FREE;
		
		//Set up the header for new allocated block:
		//NOTE: This part has to be after the new_head part, because 
		//the new_allocated_head will diretly rewrite head pointer.
		new_allocated_head = (void *)head;
		new_allocated_head->magic = ALLOCATED;
		new_allocated_head->size = size - sizeof(node_t); //This size not contains header size.
		new_allocated_head->next = NULL;
		
		head = new_head;
		//printf("new head free size: %d\n", head->size);
		
		ret_ptr = (void *)new_allocated_head + sizeof(node_t);
		//printf("return point: %p\n", ret_ptr);
		//printf("-----------------------\n");
		
		pthread_mutex_unlock(&mutex); //unlock here!  --1
		return ret_ptr;
	}
	
	node_t *cur = (void *)head;
	while (cur->next != NULL) {
		if (cur->next->size < size) {
			cur = cur->next;
		} else {
			//Create a new header for the left free memory
			//This is a new free block ptr
			node_t *new_node = (void *)cur->next + size;
			new_node->size = cur->next->size - size;
			new_node->next = cur->next->next;
			new_node->magic = FREE;
			
			//This is a new allocated block ptr
			node_t *new_allocated_ptr = (void *)cur->next;
			new_allocated_ptr->magic = ALLOCATED;
			new_allocated_ptr->size = size - sizeof(node_t); //This size not includes header size
			
			cur->next = new_node;
			ret_ptr = (void *)new_allocated_ptr + sizeof(node_t);
			
			pthread_mutex_unlock(&mutex); //unlock here  --2
			return ret_ptr;
		}
	}
	
	//Connect the head->next to next page
	//First, decide how many pages to add
	int num_of_page = 1;
	while (num_of_page * 4096 < size + sizeof(node_t)) {
		num_of_page += 1;
	}
	
	//This is new free block pointer
	node_t *temp_new_head = sbrk(4096 * num_of_page);
	node_t *new_node = (void *)temp_new_head + size;
	new_node->size = 4096*num_of_page - size - sizeof(node_t);
	new_node->next = NULL;
	new_node->magic = FREE;
	head->next = new_node;
	
	//This is new allocated block pointer
	node_t *new_allocated_ptr = (void *)temp_new_head;
	new_allocated_ptr->magic = ALLOCATED;
	new_allocated_ptr->size = size - sizeof(node_t); //This size not includes header size
	
	ret_ptr = (void *)new_allocated_ptr + sizeof(node_t);
	pthread_mutex_unlock(&mutex); //unlock here!  --3
	return ret_ptr;
}

/* coalescing_free_blocks: Coalesces the free blocks that are next to each other.
     void *ptr: pointer to the header of a block of memory allocated by 
                mymalloc.
     retval: 0 if the memory was successfully coalesced and 1 if there 
				nothing to be coalesced.
*/
int coalescing_free_blocks(node_t *ptr) {
	//printf("------------------------------------------\n");
	//printf("Coalescing here\n");
	//printf("old_allocated_ptr: %p, ptr->size: %d\n", ptr, ptr->size);
	node_t *start = (void *)ptr;
	node_t *end = (void *)ptr + sizeof(node_t) + ptr->size;
	//printf("start: %p, end: %p, node_t size: %lu, ptr->size: %d \n", start, end, sizeof(node_t), ptr->size);
	
	//Check the special case: head
	void *head_end = (void *)head + sizeof(node_t) + head->size;
	if ((void *)head == (void *)end) {
		node_t *new_head = ptr;
		new_head->magic = FREE;
		new_head->next = head->next;
		new_head->size = ptr->size + sizeof(node_t) + head->size;
		head = new_head;
		//printf("head: %p is behind ptr: %p, the size of ptr is: %d\n", head, ptr, ptr->size);
		return 0;
	}
	else if (head_end == (void *)start) {
		head->size = head->size + ptr->size + sizeof(node_t);
		//printf("head: %p is ahead of ptr: %p, the size of head is: %d\n", head, ptr, head->size);
		return 0;
	}
	
	//Loop the rest of linked list
	node_t *cur = head;
	while (cur->next != NULL) {
		void *cur_next_start = (void *)cur->next;
		void *cur_next_end = (void *)cur->next + sizeof(node_t) + cur->next->size;
		if (cur_next_end == start) {
			cur->next->size = cur->next->size + sizeof(node_t) + ptr->size;
			//printf("cur->next: %p is ahead of ptr: %p, the size of cur->next is: %d\n", cur->next, ptr, cur->next->size);
			return 0;
		} 
		else if (cur_next_start == end) {
			node_t *new_node = (void *)ptr;
			new_node->magic = FREE;
			new_node->next = cur->next->next;
			new_node->size = ptr->size + sizeof(node_t) + cur->next->size;
			//printf("cur->next: %p is behind of ptr: %p, the size of ptr is: %d, cur->next->size: %d, the end of ptr is: %p\n", cur->next, ptr, ptr->size, cur->next->size, end);
			cur->next = new_node;
			return 0;
		}
		cur = cur->next;
	}
	//printf("Nothing to be coalesced here !\n");
	return 1;
}

/* myfree: unallocates memory that has been allocated with mymalloc.
     void *ptr: pointer to the first byte of a block of memory allocated by 
                mymalloc.
     retval: 0 if the memory was successfully freed and 1 otherwise.
             (NOTE: the system version of free returns no error.)
*/
unsigned int myfree(void *ptr) {
	// placeholder so that the program will compilet
	//Gets the original header of the allocated block
	
	pthread_mutex_lock(&mutex); //lock the process here
	node_t *old_allocated_ptr = (void *)ptr - sizeof(node_t);
	
	if (old_allocated_ptr->magic != ALLOCATED) {
		//printf("Error: This is not an allocated block!\n");
		pthread_mutex_unlock(&mutex); //unlock here!  --0
		return 1;
	}
	//Coalescing starts from here to ..................................
	if (coalescing_free_blocks(old_allocated_ptr) == 0) {
		pthread_mutex_unlock(&mutex); //unlock here!  --1
		return 0;
	}
	// Here is the end of coalescing...................................
	
	//printf("----------------------------------------\n");
	//printf("old allcated ptr addr: %p\n", old_allocated_ptr);
	//printf("old head->next addr: %p\n", head->next);
	
	//This is a new free block ptr
	node_t *new_node = (void *)old_allocated_ptr;
	new_node->size = old_allocated_ptr->size;
	new_node->next = head->next;
	new_node->magic = FREE;
	
	head->next = new_node;
	//printf("new head->next addr: %p\n", head->next);
	//printf("----------------------------------------\n");
	
	pthread_mutex_unlock(&mutex); //unlock here!  --2
	return 0;
}

