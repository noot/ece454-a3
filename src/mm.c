/*
 * This implementation replicates the implicit list implementation
 * provided in the textbook
 * "Computer Systems - A Programmer's Perspective"
 * Blocks are never coalesced or reused.
 * Realloc is implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
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
#define PREV_BLK(bp) (*(char **)(PREV_PTR(bp)))
#define NEXT_BLK(bp) (*(char **)(NEXT_PTR(bp)))

/* Previous and next block's addresses */
#define PREV_PTR(bp) ((char *)(bp))
#define NEXT_PTR(bp) ((char *)(bp) + WSIZE)

#define NUM_LISTS 20

// size_t sizes[] = {1, 1<<1, 1<<2, 1<<3, 1<<4, 1<<5, 1<<6, 1<<7, 1<<8, 1<<9, 1<<10, 1<<11, 
//     1<<12, 1<<13, 1<<14, 1<<15, 1<<16, 1<<17, 1<<18, 1<<19};

typedef struct free_block {
    size_t size;
    struct free_block *prev;
    struct free_block *next;
} free_block_t;

// the number of different segregated free lists
// sizes start from 1 word (8 bytes) up to 2**28
void* heap_listp = NULL;
void *free_list[NUM_LISTS];

// insert and delete from free lists
void insert(void *ptr, size_t size);
void delete(void *ptr);
// return index of free list given a size
int list_index(size_t size);
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
        // TODO: move coalesced block to corresponding free list
        //insert(bp, size);
        return bp;
    }

    else if (prev_alloc && !next_alloc) { /* Case 2 */
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
        return (bp);
    }

    else if (!prev_alloc && next_alloc) { /* Case 3 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        return (PREV_BLKP(bp));
    }

    else {            /* Case 4 */
        size += GET_SIZE(HDRP(PREV_BLKP(bp)))  +
            GET_SIZE(FTRP(NEXT_BLKP(bp)))  ;
        PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size,0));
        return (PREV_BLKP(bp));
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

    /* Allocate an even number of words to maintain alignments */
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE;
    if ( (bp = mem_sbrk(size)) == (void *)-1 )
        return NULL;

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
    void *bp;
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp))
    {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp))))
        {
            return bp;
        }
    }
    return NULL;
}

/**********************************************************
 * place
 * Mark the block as allocated
 **********************************************************/
void place(void* bp, size_t asize)
{
  /* Get the current block size */
  size_t bsize = GET_SIZE(HDRP(bp));

  PUT(HDRP(bp), PACK(bsize, 1));
  PUT(FTRP(bp), PACK(bsize, 1));

  delete(bp);
}

// return index of free list corresponding to size
int list_index(size_t size) {
    int i;
    for(i = 0; i < NUM_LISTS; i++) {
        if (size < 1) break;
        size >>= 1;
    }

    return i;
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

    // TODO: put node back in list
    insert(bp, size);

    coalesce(bp);
}


void insert(void *bp, size_t size) {
    int idx = list_index(size);

    void *parent_ptr = free_list[idx];
    void *child_ptr = NULL;

    // traverse free list until we find the spot to insert the block
    while ((parent_ptr != NULL) && (size > GET_SIZE(HDRP(parent_ptr)))) {
        child_ptr = parent_ptr;
        parent_ptr = PREV_BLK(parent_ptr);
    }

    // node goes after the parent and before the entry 
    // parent <- node <- child <- ... <- list_head
    if (child_ptr != NULL && parent_ptr != NULL) {
        PUT(PREV_PTR(parent_ptr), bp);
        PUT(NEXT_PTR(child_ptr), bp);
        PUT(PREV_PTR(bp), child_ptr);
        PUT(NEXT_PTR(bp), parent_ptr);
        return;
    } else if (child_ptr != NULL && parent_ptr == NULL) {
        PUT(PREV_PTR(bp), child_ptr);
        PUT(NEXT_PTR(child_ptr), bp);
        PUT(NEXT_PTR(bp), NULL);
        return;
    } else if (child_ptr == NULL && parent_ptr != NULL) {
        PUT(PREV_PTR(parent_ptr), bp);
        PUT(NEXT_PTR(bp), parent_ptr);
        PUT(PREV_PTR(bp), NULL);
    } else {
        PUT(PREV_PTR(bp), NULL);
        PUT(NEXT_PTR(bp), NULL);
    }

    free_list[idx] = bp;
}

void delete(void *bp) {
    int size = GET_SIZE(HDRP(bp));
    int idx = list_index(size);

    //void *list_head = free_list[idx];

    // child <- bp <- parent <- ... <- list_head

    void *parent = PREV_BLK(bp);
    void *child = NEXT_BLK(bp);
    if (parent != NULL && child != NULL) {
        PUT(PREV_PTR(child), parent);
        PUT(NEXT_PTR(parent), child);
    } else if (parent == NULL && child != NULL) {
        PUT(PREV_PTR(child), NULL);
        free_list[idx] = child;
    } else if (child == NULL && parent != NULL) {
        PUT(NEXT_PTR(parent), NULL);
    } else {
        free_list[idx] = NULL;
    }

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

    /* Ignore spurious requests */
    if (size == 0)
        return NULL;

    /* Adjust block size to include overhead and alignment reqs. */
    if (size <= DSIZE)
        asize = 2 * DSIZE;
    else
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1))/ DSIZE);

    int idx = list_index(asize);

    // iterate through free list to find list that fits block
    bp = free_list[idx];
    // iterate through chosen list to find block that is just less than requested size
    while (bp != NULL && asize > GET_SIZE(HDRP(bp))) {
        bp = PREV_BLK(bp);
    } 

    // extend heap if we can't find the right size block
    if (bp == NULL) {
        extendsize = MAX(asize, CHUNKSIZE);
        if ((bp = extend_heap(extendsize)) == NULL)
            return NULL;       
    }


    mm_check();
    place(bp, asize);

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
    //size_t copySize;
    size_t newSize = size;

    if (newSize < DSIZE) {
        newSize = DSIZE;
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
            // if it's greater, 
            if (extra > 0) {
                int extension = MAX(extra, DSIZE);
                if (extend_heap(extra) == NULL) {
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



    // newptr = mm_malloc(size);
    // if (newptr == NULL)
    //   return NULL;

    // /* Copy the old data. */
    //copySize = GET_SIZE(HDRP(oldptr));
    // if (size < copySize)
    //   copySize = size;
    // memcpy(newptr, oldptr, copySize);
    // mm_free(oldptr);
    return newptr;
}

/**********************************************************
 * mm_check
 * Check the consistency of the memory heap
 * Return nonzero if the heap is consistant.
 *********************************************************/
int mm_check(void){
  // char *bp = heap_listp;

    //printf("heap");

    return 0;
}
