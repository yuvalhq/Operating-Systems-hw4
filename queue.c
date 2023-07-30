#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <threads.h>

#define EMPTY 0

typedef struct Node{
    void* data;
    struct Node* next;
} Node;

typedef struct CndNode{
    cnd_t cond;
    struct CndNode* next;
} CndNode;

typedef struct Queue{
    Node* head;
    Node* tail;
    CndNode* oldest_cnd;
    CndNode* newest_cnd;
    size_t node_count;
    size_t waiting_count;
    size_t visited_count;
    mtx_t mutex;
} Queue;

static Queue queue;

void initQueue(void){
    queue.head = NULL;
    queue.tail = NULL;
    queue.oldest_cnd = NULL;
    queue.newest_cnd = NULL;
    queue.node_count = 0;
    queue.waiting_count = 0;
    queue.visited_count = 0;
    mtx_init(&queue.mutex, mtx_plain);
}

void destroyQueue(void){
    Node* node = NULL;
    CndNode* cnd_node = NULL;

    // we need to lock to make sure no one is
    // editing the queue while we are destroying it
    mtx_lock(&queue.mutex);

    // Free all nodes in the queue
    while (queue.head != NULL){
        node = queue.head;
        queue.head = node -> next;
        free(node);
    }
    while (queue.oldest_cnd != NULL){
        cnd_node = queue.oldest_cnd;
        queue.oldest_cnd = cnd_node -> next;
        cnd_destroy(&(cnd_node -> cond));
        free(cnd_node);
    }

    mtx_unlock(&queue.mutex);
    mtx_destroy(&queue.mutex);
}

void enqueue(void* data){
    Node* newNode = malloc(sizeof(Node));
    newNode -> data = data;
    newNode -> next = NULL;
    mtx_lock(&queue.mutex);

    if (queue.head == NULL){
        queue.head = newNode;
        queue.tail = newNode;
    }else{
        queue.tail -> next = newNode;
        queue.tail = newNode;
    }
    queue.node_count++;

    if (queue.waiting_count > EMPTY && queue.node_count == 1){
        cnd_signal(&queue.oldest_cnd -> cond);
    }
    mtx_unlock(&queue.mutex);
}

void* dequeue(void){
    Node* node = NULL;
    CndNode* cnd_node = NULL;
    void* data = NULL;
    mtx_lock(&queue.mutex);
    
    if (queue.waiting_count == EMPTY && queue.node_count > EMPTY) {
        node = queue.head;
        data = node->data;
        queue.head = node -> next;
        queue.node_count--;
        queue.visited_count++;
    } else {   
        // add a cond to the cond queue for
        // the thread that is going to sleep
        cnd_node = malloc(sizeof(CndNode));
        cnd_init(&(cnd_node -> cond));
        cnd_node -> next = NULL;
        if (queue.oldest_cnd == NULL){
            queue.oldest_cnd = cnd_node;
            queue.newest_cnd = cnd_node;
        }else{
            queue.newest_cnd -> next = cnd_node;
            queue.newest_cnd = cnd_node;
        }
        
        queue.waiting_count++;
        cnd_wait(&(cnd_node -> cond), &queue.mutex);
        queue.waiting_count--;

        // get the data 
        node = queue.head;
        data = node->data;
        queue.head = node -> next;
        queue.node_count--;
        queue.visited_count++;

        // free the used condition node
        cnd_node = queue.oldest_cnd;
        queue.oldest_cnd = queue.oldest_cnd -> next;
        cnd_destroy(&(cnd_node -> cond));
        free(cnd_node);
        
        // continue dequing if possible
        if (queue.oldest_cnd != NULL && queue.node_count > EMPTY) {
            cnd_signal(&queue.oldest_cnd -> cond);
        }
    }
   
    mtx_unlock(&queue.mutex);
    free(node);
    return data;
}

bool tryDequeue(void** data){
    Node* node = NULL;
    mtx_lock(&queue.mutex);

    // If the queue is empty, abort
    if (queue.node_count == EMPTY){
        mtx_unlock(&queue.mutex);
        return false;
    }
    node = queue.head;
    *data = node -> data;
    queue.head = node -> next;
    queue.node_count--;
    queue.visited_count++;

    mtx_unlock(&queue.mutex);
    free(node);
    return true;
}

size_t size(void){
    size_t count;

    // to get the "current" amount of queued items,
    // we need to lock to get the value
    mtx_lock(&queue.mutex);
    count = queue.node_count;
    mtx_unlock(&queue.mutex);
    return count;
}

size_t waiting(void){
    size_t count;

    // to get the "current" amount of waiting threads,
    // we need to lock to get the value
    mtx_lock(&queue.mutex);
    count = queue.waiting_count;
    mtx_unlock(&queue.mutex);
    return count;
}

size_t visited(void){
    // No locks allowed here
    return queue.visited_count;
}
