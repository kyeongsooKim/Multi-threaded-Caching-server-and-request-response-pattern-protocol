#include "queue.h"
#include <stdio.h>
#include <errno.h>



void Sem_wait(sem_t *sem) {
    try_again:
    if (sem_wait(sem) < 0)
    {
        if (errno == EINTR)
            goto try_again;
    }
}

bool QIsEmpty(queue_t * self)
{
    if(self->front == NULL)
        return true;
    else
        return false;
}

queue_t * create_queue(void) {

    queue_t *temp;
    temp = (queue_t *)calloc(1,sizeof(queue_t)); //allocate memory using calloc()
    if (temp == NULL)
        return NULL;
    temp->front = NULL;
    temp->rear = NULL;
    pthread_mutex_init(&temp->lock, NULL);
    sem_init(&temp->items, 0,0);
    temp->invalid = false;


    return temp;//return the new node
}

bool invalidate_queue(queue_t *self, item_destructor_f destroy_function) {

    if(self == NULL)
    {
        errno = EINVAL;
        return false;
    }

    pthread_mutex_lock(&self->lock); //protect buffer
    if(self->invalid == true || destroy_function == NULL)
    {
        errno = EINVAL;
        pthread_mutex_unlock(&self->lock); //release the buffer
        return false;
    }

    queue_node_t * delNode;

    while(!QIsEmpty(self))
    {
        delNode = self->front;
        destroy_function(delNode->item);
        free(delNode);
        self->front = self->front->next;
    }

    //******************** do we need to decrease semaphore ?? - not sure
    self->invalid = true;
    pthread_mutex_unlock(&self->lock); //protect buffer

    return true;


}

bool enqueue(queue_t *self, void *item) {


    if (self == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    pthread_mutex_lock(&self->lock); //protect buffer

    if (self->invalid == true || item == NULL)
    {
        errno = EINVAL;
        pthread_mutex_unlock(&self->lock); //protect buffer
        return NULL;
    }


    //assume that item is a valid pointer
    queue_node_t * newNode = (queue_node_t *)calloc(1,sizeof(queue_node_t)); // allocate memory using calloc()
    if (newNode == NULL)
    {
        pthread_mutex_unlock(&self->lock); //release the buffer
        return false;
    }
    newNode->item = item;
    newNode->next = NULL;
    sem_post(&self->items); //increase semaphore.

    if(QIsEmpty(self))
    {
        self->front = newNode;
        self->rear = newNode;
    }
    else
    {
        self->rear->next = newNode;
        self->rear = newNode;
    }

    pthread_mutex_unlock(&self->lock);  //release buffer

    return true;
}

void *dequeue(queue_t *self) {

    Sem_wait(&self->items); //decrease semaphore. when semaphore is 0,
                            //wait till it becomes 1


    if (self == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    pthread_mutex_lock(&self->lock); //protect buffer

    if (self->invalid == true)
    {
        errno = EINVAL;
        pthread_mutex_unlock(&self->lock); //release the buffer
        return NULL;
    }

    queue_node_t * delNode;
    void * retData;


    delNode = self->front;
    retData = delNode->item;
    self->front = self->front->next;

    free(delNode);

    pthread_mutex_unlock(&self->lock); //release the buffer


    return retData;
}
