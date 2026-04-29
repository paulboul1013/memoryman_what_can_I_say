#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

//can allocate，but can't free
void *malloc_man(size_t size){
    void *p=sbrk(0); // get current location of the top heap
    void *req=sbrk(size); //give n bytes growing heap size，return pointer of heap

    // if req fail
    if (req==(void*)-1) return NULL;
    return p; // top of heap
}

struct block_meta{
    size_t size;
    struct block_meta *next;
    int free;
    int magic; //debugging
};

#define META_SIZE sizeof(struct block_meta)

void *global_base=NULL;
/*global_base
    |
    v
+----------+      +----------+      +----------+
| block 1  | ---> | block 2  | ---> | block 3  | ---> NULL
+----------+      +----------+      +----------+
*/

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
    struct block_meta * block=sbrk(0);
    void * req=sbrk(size+META_SIZE);

    if (req==(void*)-1) return NULL;
    if (last) last->next=block;
    
    block->size=size;
    block->next=NULL;
    block->free=0;
    block->magic=0x12345678;

    return block;
}

void *malloc_man_v2(size_t size){
    if (size<=0) return NULL;
    struct block_meta *block;

    if (!global_base){
        block=request_space(NULL,size);
        if (!block) return NULL;
        global_base=block;
    }else{
        struct block_meta *last=global_base;
        block=find_free_block(&last,size);

        if (!block){
            block=request_space(last,size);
            if (!block) return NULL;
        }else{
            block->free=0;
            block->magic=0x77777777;
        }
    }

    return (block+1);//skip meta data，pointer to the uesr usable memory
}

struct block_meta *get_block_ptr(void *ptr){
    return (struct block_meta *) ptr-1; // back to metadata position
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
    
    struct block_meta *block=get_block_ptr(ptr);

    if (block->size>=size) return ptr;
    
    void *new=malloc_man_v2(size);

    if (!new) return NULL;

    memcpy(new,ptr,block->size);

    free_man(ptr);
    return new;
}

void *calloc_man(size_t n,size_t size){
    void *ptr=malloc_man_v2(n*size);
    
    memset(ptr,0,n*size);
    return ptr;
}

int main(int argc,char *argv[]){

    char *p=malloc_man_v2(16);
    assert(p!=NULL);

    for(int i=0;i<16;i++){
        p[i]='A'+i;
    }

    for(int i=0;i<16;i++){
        assert(p[i]=='A'+i);
    }

    free_man(p);

    char *q=malloc_man_v2(8);
    assert(q!=NULL);


    assert(q==p);

    free_man(q);

    int *arr=calloc_man(4,sizeof(int));
    assert(arr!=NULL);

    for(int i=0;i<4;i++){
        assert(arr[i]==0);
    }

    arr[0]=10;
    arr[1]=20;
    arr[2]=30;
    arr[3]=40;
    
    int *bigger=realloc_man(arr,8*sizeof(int));
    assert(bigger!=NULL);

    assert(bigger[0]==10);
    assert(bigger[1] == 20);
    assert(bigger[2] == 30);
    assert(bigger[3] == 40);

    bigger[4]=50;
    bigger[5]=60;
    bigger[6]=70;
    bigger[7]=80;

    int *smaller=realloc_man(bigger,4*sizeof(int));
    assert(smaller==bigger);
    
    assert(smaller[0] == 10);
    assert(smaller[1] == 20);
    assert(smaller[2] == 30);
    assert(smaller[3] == 40);

    free_man(smaller);

    void *zero=malloc_man_v2(0);
    assert(zero==NULL);


    return 0;
}