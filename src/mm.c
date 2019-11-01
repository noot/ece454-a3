/*
 * This package implements malloc, realloc, and free using segregated lists.
 * Each block contains a 1 word header, 1 word footer, which each consist of size | allocation bit.
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "noot",
    /* First member's full name */
    "Elizabeth Binks",
    /* First member's email address */
    "elizabeth.binks@mail.utoronto.ca",
    /* Second member's full name (do not modify this as this is an individual lab) */
    "",
    /* Second member's email address (do not modify this as this is an individual lab)*/
    ""
};

/*************************************************************************
 * Basic Constants and Macros
 * You are not required to use these macros but may find them helpful.
*************************************************************************/
#define WSIZE       sizeof(void *)            /* word size (bytes) */
#define DSIZE       (2 * WSIZE)            /* doubleword size (bytes) */
#define CHUNKSIZE   (1<<7)      /* initial heap size (bytes) */
#define MINSIZE     (2 * DSIZE)

#define MAX(x,y) ((x) > (y)?(x) : (y))
#define MIN(x,y) ((x) < (y)?(x) : (y))

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc) ((size) | (alloc))

/* Read and write a word at address p */
#define GET(p)          (*(uintptr_t *)(p))
#define PUT(p,val)      (*(uintptr_t *)(p) = (val))

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)     (GET(p) & ~(DSIZE - 1))
#define GET_ALLOC(p)    (GET(p) & 0x1)

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)        ((char *)(bp) - WSIZE)
#define FTRP(bp)        ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

/* Previous and next blocks on the free list */
#define PREV_FREE_BLK(bp) (*(char **)(PREV_FREE_PTR(bp)))
#define NEXT_FREE_BLK(bp) (*(char **)(NEXT_FREE_PTR(bp)))

/* Previous and next block's addresses */
#define PREV_FREE_PTR(bp) ((char *)(bp))
#define NEXT_FREE_PTR(bp) ((char *)(bp) + WSIZE)

#define NUM_LISTS 28

//#define DEBUG

#ifdef DEBUG
    #define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
    #define DEBUG_PRINTF(...)
#endif

// the number of different segregated free lists
// sizes start from 1 word (8 bytes) up to 2**28
void* heap_listp = NULL;
void *free_list[NUM_LISTS];

// insert and delete from free lists
void insert(void *ptr, size_t size);
void delete(void *ptr);
// return index of free list given a size
int list_index(size_t size);
void *split_block(void *bp, size_t size);
int mm_check();

/**********************************************************
 * mm_init
 * Initialize the heap, including "allocation" of the
 * prologue and epilogue
 **********************************************************/
 int mm_init(void)
 {
   if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1)
         return -1;
     PUT(heap_listp, 0);                         // alignment padding
     PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1));   // prologue header
     PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1));   // prologue footer
     PUT(heap_listp + (3 * WSIZE), PACK(0, 1));    // epilogue header
     heap_listp += DSIZE;

     for (int i = 0; i < NUM_LISTS; i++) {
        free_list[i] = NULL;
     }

     return 0;
 }

/**********************************************************
 * coalesce
 * Covers the 4 cases discussed in the text:
 * - both neighbours are allocated
 * - the next block is available for coalescing
 * - the previous block is available for coalescing
 * - both neighbours are available for coalescing
 **********************************************************/
void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {       /* Case 1 */
        return bp;
    }

    // remove block from free list
    delete(bp);

    void *prev_block = PREV_BLKP(bp);
    void *next_block = NEXT_BLKP(bp);
    if (prev_alloc && !next_alloc) { /* Case 2 */
        DEBUG_PRINTF("coalescing current (size=%d) and next (size=%d)\n", size, GET_SIZE(HDRP(next_block)));
        delete(next_block);
        size += GET_SIZE(HDRP(next_block));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        insert(bp, size);
        return bp;
    } else if (!prev_alloc && next_alloc) { /* Case 3 */
        DEBUG_PRINTF("coalescing current (size=%d) and prev (size=%d)\n", size, GET_SIZE(HDRP(prev_block)));
        delete(prev_block);
        size += GET_SIZE(HDRP(prev_block));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(prev_block), PACK(size, 0));
        insert(prev_block, size);
        return prev_block;
    } else {            /* Case 4 */
        DEBUG_PRINTF("coalescing current (size=%d), next (size=%d), and prev (size=%d) blocks\n", size, GET_SIZE(HDRP(next_block)), GET_SIZE(HDRP(prev_block)));
        delete(prev_block);
        delete(next_block);
        size += GET_SIZE(HDRP(prev_block)) + GET_SIZE(HDRP(next_block));
        PUT(HDRP(prev_block), PACK(size,0));
        PUT(FTRP(next_block), PACK(size,0));
        insert(prev_block, size);
        return prev_block;
    }
}

/**********************************************************
 * extend_heap
 * Extend the heap by "words" words, maintaining alignment
 * requirements of course. Free the former epilogue block
 * and reallocate its new header
 **********************************************************/
void *extend_heap(size_t words)
{
    char *bp;
    size_t size;

    DEBUG_PRINTF("extending heap: size=%d words\n", words);

    /* Allocate an even number of words to maintain alignments */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ( (bp = mem_sbrk(size)) == (void *)-1 ) {
        DEBUG_PRINTF("extending heap: out of mem\n");
        return NULL;
    }

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));                // free block header
    PUT(FTRP(bp), PACK(size, 0));                // free block footer
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));        // new epilogue header

    insert(bp, size);

    /* Coalesce if the previous block was free */
    return coalesce(bp);
}


/**********************************************************
 * find_fit
 * Traverse the heap searching for a block to fit asize
 * Return NULL if no free blocks can handle that size
 * Assumed that asize is aligned
 **********************************************************/
void * find_fit(size_t asize)
{
    int idx = list_index(asize);

    // iterate through free list to find list that fits block
    void *bp = NULL;
    // TODO: possibly search in reverse so we can split big blocks?
    for (int i = idx; i < NUM_LISTS; i++) {
        void *bp = free_list[i];
        while (bp != NULL) {
            // if free block fits requested size, break and return
            if (asize <= GET_SIZE(HDRP(bp))) {
                DEBUG_PRINTF("find_fit found fit block %x size %d\n", bp, GET_SIZE(HDRP(bp)));
                // split block if i != idx 
                // free block is in a list that holds blocks larger than needed
                //if (i != idx && GET_SIZE(HDRP(bp)) > asize + MINSIZE) bp = split_block(bp, asize);
                return bp;
            }
            bp = PREV_FREE_BLK(bp);
        }  
        //if (bp != NULL) return bp;
    }

    return bp;
}

// split a block into a block that has size `size` and a remainder which is the original size of
// the block minus `size`
// add the remainder block to free list
void *split_block(void *bp, size_t size) {
    size_t current_size = GET_SIZE(HDRP(bp));
    size_t remainder = current_size - size;
    if (remainder < MINSIZE) {
        // can't split block
        return bp;
    }   

    DEBUG_PRINTF("split_block size %d remainder %d\n", size, remainder);

    delete(bp);

    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    void *remainder_ptr = NEXT_BLKP(bp);
    PUT(HDRP(remainder_ptr), PACK(remainder, 0));
    PUT(FTRP(remainder_ptr), PACK(remainder, 0));

    insert(bp, size);
    insert(remainder_ptr, remainder);

    return bp;
}

// return index of free list corresponding to size
int list_index(size_t size) {
    int i;
    for(i = 0; i < NUM_LISTS; i++) {
        size >>= 1;
        if (size < 1 || i == NUM_LISTS-1) break;
    }

    return i;
}

/**********************************************************
 * place
 * Mark the block as allocated
 **********************************************************/
void place(void* bp, size_t asize)
{
    /* Get the current block size */
    size_t bsize = GET_SIZE(HDRP(bp));

    delete(bp);

    size_t remainder = bsize - asize;
    if (remainder < MINSIZE) {
        // can't split block
      PUT(HDRP(bp), PACK(bsize, 1));
      PUT(FTRP(bp), PACK(bsize, 1));
      return;
    }   

    DEBUG_PRINTF("split_block size %d remainder %d\n", asize, remainder);

    PUT(HDRP(bp), PACK(asize, 1));
    PUT(FTRP(bp), PACK(asize, 1));
    void *remainder_ptr = NEXT_BLKP(bp);
    PUT(HDRP(remainder_ptr), PACK(remainder, 0));
    PUT(FTRP(remainder_ptr), PACK(remainder, 0));

    //insert(bp, size);
    insert(remainder_ptr, remainder);

    // if (bsize >= asize + MINSIZE) bp = split_block(bp, asize);

    // DEBUG_PRINTF("place block ptr %x size %d\n", bp, asize);
    // delete(bp);

    // bsize = GET_SIZE(HDRP(bp));
    // PUT(HDRP(bp), PACK(bsize, 1));
    // PUT(FTRP(bp), PACK(bsize, 1));

}

/**********************************************************
 * mm_free
 * Free the block and coalesce with neighbouring blocks
 **********************************************************/
void mm_free(void *bp)
{
    if(bp == NULL){
      return;
    }
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size,0));
    PUT(FTRP(bp), PACK(size,0));

    DEBUG_PRINTF("free: size=%d bytes\n", size);

    insert(bp, size);

    coalesce(bp);
}


void insert(void *bp, size_t size) {
    if (bp == NULL) return;

    int idx = list_index(size);

    void *parent_ptr = free_list[idx];
    void *child_ptr = NULL;

    // traverse free list until we find the spot to insert the block
    while ((parent_ptr != NULL) && (size > GET_SIZE(HDRP(parent_ptr)))) {
        child_ptr = parent_ptr;
        parent_ptr = PREV_FREE_BLK(parent_ptr);
    }

    // node goes after the parent and before the entry 
    // parent <- node <- child <- ... <- list_head
    if (child_ptr != NULL && parent_ptr != NULL) {
        PUT(PREV_FREE_PTR(parent_ptr), bp);
        PUT(NEXT_FREE_PTR(child_ptr), bp);
        PUT(PREV_FREE_PTR(bp), child_ptr);
        PUT(NEXT_FREE_PTR(bp), parent_ptr);
        //return;
    } else if (child_ptr != NULL && parent_ptr == NULL) {
        PUT(PREV_FREE_PTR(bp), child_ptr);
        PUT(NEXT_FREE_PTR(child_ptr), bp);
        PUT(NEXT_FREE_PTR(bp), NULL);
        //return;
    } else if (child_ptr == NULL && parent_ptr != NULL) {
        PUT(NEXT_FREE_PTR(bp), parent_ptr);
        PUT(PREV_FREE_PTR(parent_ptr), bp);
        PUT(PREV_FREE_PTR(bp), NULL);
        free_list[idx] = bp;
    } else {
        PUT(PREV_FREE_PTR(bp), NULL);
        PUT(NEXT_FREE_PTR(bp), NULL);
        free_list[idx] = bp;
    }

    DEBUG_PRINTF("inserting into free list: size=%d bytes\n", size);
}

void delete(void *bp) {
    if (bp == NULL) return;
    size_t size = GET_SIZE(HDRP(bp));
    int idx = list_index(size);

    // child <- bp <- parent <- ... <- list_head
    void *parent = PREV_FREE_BLK(bp);
    void *child = NEXT_FREE_BLK(bp);
    if (parent != NULL && child != NULL) {
        PUT(PREV_FREE_PTR(child), parent);
        PUT(NEXT_FREE_PTR(parent), child);
    } else if (parent == NULL && child != NULL) {
        PUT(PREV_FREE_PTR(child), NULL);
        free_list[idx] = child;
    } else if (child == NULL && parent != NULL) {
        PUT(NEXT_FREE_PTR(parent), NULL);
    } else {
        free_list[idx] = NULL;
    }

    DEBUG_PRINTF("deleting from free list: size=%d bytes\n", size);
}

/**********************************************************
 * mm_malloc
 * Allocate a block of size bytes.
 * The type of search is determined by find_fit
 * The decision of splitting the block, or not is determined
 *   in place(..)
 * If no block satisfies the request, the heap is extended
 **********************************************************/
void *mm_malloc(size_t size)
{
    size_t asize; /* adjusted block size */
    size_t extendsize; /* amount to extend heap if no fit */
    char * bp;

    print_free_list();

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);

    DEBUG_PRINTF("malloc: size=%d bytes\n", size);
    DEBUG_PRINTF("malloc list_idx %d\n", list_index(asize));

    bp = find_fit(asize);

    // extend heap if we can't find the right size block
    if (bp == NULL) {
        extendsize = MAX(asize/WSIZE, CHUNKSIZE);
        if ((bp = extend_heap(extendsize)) == NULL)
            return NULL;       
    }

    place(bp, asize);
    if(mm_check() != 0) {
        printf("mm_check failed :(");
        PUT(0x1, 0);
    }

    return bp;

}

/**********************************************************
 * mm_realloc
 * Implemented simply in terms of mm_malloc and mm_free
 *********************************************************/
void *mm_realloc(void *ptr, size_t size)
{
    /* If size == 0 then this is just free, and we return NULL. */
    if(size == 0){
      mm_free(ptr);
      return NULL;
    }

    /* If oldptr is NULL, then this is just malloc. */
    if (ptr == NULL)
      return (mm_malloc(size));

    //void *oldptr = ptr;
    void *newptr = NULL;
    // //size_t copySize;
    size_t newSize = size;

    if (newSize < DSIZE) {
        newSize = DSIZE;
    } else {
        newSize = DSIZE * ((newSize + (DSIZE) + (DSIZE-1))/ DSIZE);
    }

    newSize += CHUNKSIZE;

    // size diff between current allocated size and requested size
    int diff = newSize - GET_SIZE(HDRP(ptr));

    if (diff > 0) {
        int extra;
        // check if next block is epilogue, if so, extend the heap
        if (!GET_SIZE(HDRP(NEXT_BLKP(ptr))) || !GET_ALLOC(HDRP(NEXT_BLKP(ptr)))) {
            // check if the extra amount of space we need is greater than what's in the next block
            extra = diff - GET_SIZE(HDRP(NEXT_BLKP(ptr)));
            if (extra > 0) {
                int extension = MAX(extra, DSIZE);
                if (extend_heap(extra/WSIZE) == NULL) {
                    printf("cannot extend heap");
                    return NULL;
                }

               extra += extension;
            }

            // put block header
            PUT(HDRP(ptr), PACK(newSize + extra, 1));
            // put block footer
            PUT(FTRP(ptr), PACK(newSize + extra, 1));
        } else {
            newptr = mm_malloc(newSize - DSIZE);
            memmove(newptr, ptr, MIN(size, newSize));
            mm_free(ptr);   
        }
    }

    return newptr;
}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistant.
 *********************************************************/
int mm_check(){
    if (free_list_check() != 0) return 1;

    return 0;
}

int free_list_check() {
    for (int i = 0; i < NUM_LISTS; i++) {
        void *ptr = free_list[i];
        while (ptr != NULL) {
            //DEBUG_PRINTF("free block idx %d 0x%x size %d\n", i, ptr, GET_SIZE(HDRP(ptr)));
            if (GET_ALLOC(HDRP(ptr))) {
                DEBUG_PRINTF("free list check failed: block was allocated");
                return 1; 
            }
            size_t size = GET_SIZE(HDRP(ptr));
            if (size >= 1<<(i+1)) {
                DEBUG_PRINTF("free list check failed: size=%d should be between %d to %d", size, 1<<i, (1<<i)-1);
                return 1;
            }

            ptr = PREV_FREE_BLK(ptr);
        }
    }

    return 0;
}

void print_free_list() {
    for (int i = 0; i < NUM_LISTS; i++) {
        void *ptr = free_list[i];
        while (ptr != NULL) {
            DEBUG_PRINTF("free block idx %d 0x%x size %d\n", i, ptr, GET_SIZE(HDRP(ptr)));
            ptr = PREV_FREE_BLK(ptr);
        }
    }
}
