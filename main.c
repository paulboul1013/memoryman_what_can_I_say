#define _DEFAULT_SOURCE // for glibc test macro

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ALIGNOF(type) offsetof(struct {char c; type member;}, member)

typedef union max_align_union{
    char c;
    short s;
    int i;
    long l;
    long long ll;
    float f;
    double d;
    long double ld;
    void *p;
    void (*fp)(void);
} max_align_union;

#define ALIGNMENT ((size_t)ALIGNOF(max_align_union))

struct block_meta{
    size_t size;
    struct block_meta *next;
    int free;
    int magic; //debugging
};

// #define META_SIZE sizeof(struct block_meta)

static struct block_meta *global_base=NULL;
/*global_base
    |
    v
+----------+      +----------+      +----------+
| block 1  | ---> | block 2  | ---> | block 3  | ---> NULL
+----------+      +----------+      +----------+
*/

//can allocate，but can't free
void *malloc_man(size_t size){
    void *p=sbrk(0); // get current location of the top heap
    void *req=sbrk(size); //give n bytes growing heap size，return pointer of heap

    // if req fail
    if (req==(void*)-1) return NULL;
    return p; // top of heap
}

static bool add_overflow_size(size_t a,size_t b,size_t *out){
    if (a>SIZE_MAX-b){
        return true;
    }

    *out=a+b;
    return false;
}

static bool align_up_size(size_t n,size_t *out) {
    size_t rem=n%ALIGNMENT;

    if (rem==0){
        *out=n;
        return true;
    }

    size_t add=ALIGNMENT-rem;

    if (n>SIZE_MAX-add){
        return false;
    }

    *out=n+add;
    return true;
}

static bool align_up_addr(uintptr_t addr,uintptr_t *out){
    uintptr_t alignment=(uintptr_t)ALIGNMENT;
    uintptr_t rem=addr%alignment;

    if (rem==0){
        *out=addr;
        return true;
    }

    uintptr_t add=alignment-rem;

    if (addr > UINTPTR_MAX -add){
        return false;
    }

    *out=addr+add;
    return true;
}

static size_t meta_size(void){
    size_t result=0;
    bool ok = align_up_size(sizeof(struct block_meta),&result);
    assert(ok);
    return result;
}



struct block_meta *find_free_block(struct block_meta **last, size_t size){
    struct block_meta *cur=global_base;

    while(cur && !(cur->free && cur->size>=size)){
        *last=cur;
        cur=cur->next;
    }

    return cur;
}

//if can't find free block,ask os to more space 
struct block_meta * request_space(struct block_meta *last, size_t  size){
    size_t aligned_size=0;
    if (!align_up_size(size,&aligned_size)){
        return NULL;
    }

    size_t meta=meta_size();

    
    void  *old_brk=sbrk(0);
    if (old_brk==(void*)-1){
        return NULL;
    }

    uintptr_t start=(uintptr_t)old_brk;
    uintptr_t aligned_start=0;

    if (!align_up_addr(start,&aligned_start)) {
        return NULL;
    }

    size_t padding=(size_t)(aligned_start-start);

    size_t tmp=0;
    size_t total=0;

    if (add_overflow_size(padding,meta,&tmp)){
        return NULL;
    }

    if (add_overflow_size(tmp,aligned_size,&total)){
        return NULL;
    }

    if (total>(size_t )INTPTR_MAX){
        return NULL;
    }

    void *req=sbrk((intptr_t)total);
    if (req==(void*)-1){
        return NULL;
    }

    struct block_meta *block=(struct block_meta*)((char*)req+padding);

    if (last){
        last->next=block;
    }

    
    block->size=aligned_size;
    block->next=NULL;
    block->free=0;
    block->magic=0x12345678;

    return block;
}

void *malloc_man_v2(size_t size){
    if (size==0) return NULL;

    size_t aligned_size=0;
    if (!align_up_size(size,&aligned_size)){
        return NULL;
    }

    struct block_meta *block=NULL;

    if (!global_base){
        block=request_space(NULL,aligned_size);
        if (!block) return NULL;

        global_base=block;
    }else{
        struct block_meta *last=global_base;

        block=find_free_block(&last,aligned_size);

        if (!block){
            block=request_space(last,aligned_size);
            if (!block) return NULL;
        }else{
            block->free=0;
            block->magic=0x77777777;
        }
    }

    return (void*)((char*)block+meta_size());//skip meta data，pointer to the uesr usable memory
}

struct block_meta *get_block_ptr(void *ptr){
    return (struct block_meta *)((char*)ptr-meta_size()); // back to metadata position
}

void free_man(void *ptr){
    if (!ptr) return;
    struct block_meta *block=get_block_ptr(ptr); //find back metadata

    assert(block->free==0);
    assert(block->magic==0x77777777 || block->magic==0x12345678);

    block->free=1;
    block->magic=0x55555555;
}

void *realloc_man(void *ptr,size_t size){
    if (!ptr) return malloc_man_v2(size);

    if (size==0){
        free_man(ptr);
        return NULL;
    }
    
    struct block_meta *block=get_block_ptr(ptr);
    
    void *new=malloc_man_v2(size);

    if (!new) return NULL;

    size_t copy_size=block->size < size ? block->size : size;
    memcpy(new,ptr,copy_size);

    free_man(ptr);
    return new;
}

void *calloc_man(size_t n,size_t size){
    if (n!=0 && size > SIZE_MAX/n){
        return NULL;
    }

    size_t total=n*size;

    void *ptr=malloc_man_v2(total);
    if (!ptr){
        return NULL;
    }
    
    memset(ptr,0,total);
    return ptr;
}

static int is_aligned(void *ptr,size_t alignment){
    return ((uintptr_t)ptr%alignment)==0;
}

int main(int argc,char *argv[]){

    int *arr = calloc_man(4, sizeof(int));
    assert(arr != NULL);
    assert(is_aligned(arr, ALIGNMENT));

    for (int i = 0; i < 4; i++) {
        assert(arr[i] == 0);
    }

    arr[0] = 10;
    arr[1] = 20;
    arr[2] = 30;
    arr[3] = 40;

    int *bigger = realloc_man(arr, 8 * sizeof(int));
    assert(bigger != NULL);
    assert(is_aligned(bigger, ALIGNMENT));

    assert(bigger[0] == 10);
    assert(bigger[1] == 20);
    assert(bigger[2] == 30);
    assert(bigger[3] == 40);

    bigger[4] = 50;
    bigger[5] = 60;
    bigger[6] = 70;
    bigger[7] = 80;

    for (int i = 4; i < 8; i++) {
        assert(bigger[i] == (i + 1) * 10);
    }

    free_man(bigger);

    assert(malloc_man_v2(0) == NULL);

    max_align_union *m = malloc_man_v2(sizeof(max_align_union));
    assert(m != NULL);
    assert(is_aligned(m, ALIGNMENT));
    free_man(m);

    write(1, "allocator tests passed\n", 23);

    return 0;
}