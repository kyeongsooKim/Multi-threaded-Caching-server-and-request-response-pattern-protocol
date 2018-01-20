#include "utils.h"
#include <errno.h>
#include <stdio.h>
#include <string.h> // memcmp

void print_map_info(hashmap_t * self){
    printf("\n********\tCurrent map info\t********\n");
    printf("map capacity : %d\n", self->capacity);
    printf("map size : %d\n", self->size);
    for (int i =0; i < self->capacity ;i++)
    {

        if (self->nodes[i].tombstone == true )
            printf("map[%d] : tomstone(deleted before)\n",i);
        else if (self->nodes[i].key.key_base == NULL)
            printf("map[%d] : empty\n",i );
        else
            printf("map[%d] : (%s,%s)\n", i,(char*)self->nodes[i].key.key_base,
                (char*)self->nodes[i].val.val_base);
    }
    printf("*************************************************\n\n");
}


hashmap_t *create_map(uint32_t capacity, hash_func_f hash_function, destructor_f destroy_function) {

    if (hash_function == NULL || destroy_function == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    hashmap_t * hashmap = (hashmap_t *)calloc(1, sizeof(hashmap_t));

    if (hashmap == NULL)
    {
        return NULL;
    }

    hashmap->capacity = capacity;
    hashmap->size = 0;
    hashmap->nodes = (map_node_t *)calloc(capacity, sizeof(map_node_t)); //make an array.
    hashmap->hash_function = hash_function;
    hashmap->destroy_function = destroy_function;
    hashmap->num_readers = 0;

    pthread_mutex_init(&hashmap->write_lock, NULL);
    pthread_mutex_init(&hashmap->fields_lock, NULL);
    hashmap->invalid = false;

    #ifdef DEBUG
        printf("initialized the map!\n");
        print_map_info(hashmap);
    #endif
    return hashmap;
}

//return -1 if key doesn't exist on Map
int linearProbing(hashmap_t *self, map_key_t key, int idx)
{
    #ifdef DEBUG
           printf("linearProbling function is called\n");
    #endif

    int movCnt = 0;
    while(1)
    {
        //if couldn't find target key after searching all elements
        //in full map, make decision that key doest not exist!
        if (movCnt == self->capacity)
        {
            #ifdef DEBUG
            printf("result : key does not exist!\n");
            #endif
            return -1;
        }
        //skip over a tombstone
        else if (self->nodes[idx].tombstone == true)
        {
            idx++; //linear probing
            if(idx == self->capacity)
                idx = 0;
            movCnt++;
            #ifdef DEBUG
                 printf("skipping tombstone, next idex : %d", idx);
            #endif

            continue;
        }
        //1)when slot is empty make decision that key doest not exist!
        else if (self->nodes[idx].key.key_base == NULL)
        {
            #ifdef DEBUG
            printf("result : key does not exist!\n");
            #endif
            return -1;
        }
        else if ( self->nodes[idx].key.key_base != NULL
            && memcmp(self->nodes[idx].key.key_base, key.key_base, key.key_len) == 0
            && memcmp(self->nodes[idx].key.key_base, key.key_base, self->nodes[idx].key.key_len) == 0
            && self->nodes[idx].tombstone == false)
        {
            #ifdef DEBUG
            printf("result : key exists!\n");
            #endif
            return idx;
        }

        idx++; //skip nonempty slot.
        if(idx == self->capacity)
            idx = 0;
        movCnt++;
    }
}


bool put(hashmap_t *self, map_key_t key, map_val_t val, bool force) {


    pthread_mutex_lock(&self->write_lock);

    if (self == NULL)
    {
        errno = EINVAL;
        pthread_mutex_unlock(&self->write_lock);

        return false;
    }
    else if ( val.val_base == NULL || key.key_base == NULL || self->invalid == true)
    {
        errno = EINVAL;
        pthread_mutex_unlock(&self->write_lock);
        return false;
    }
    else if (self->capacity == self->size && force == false)
    {
        errno = ENOMEM;
        pthread_mutex_unlock(&self->write_lock);
        return false;
    }

    int idx = get_index(self, key); //get an index from key.
    int tmp;


    //if the key already exists in the map, update the value associated with it.
    if ( (tmp = linearProbing(self, key, idx)) != -1)
    {
        pthread_mutex_lock(&self->fields_lock);
        #ifdef DEBUG
           printf("key already exists in map, update the value\n");
        #endif
        self->nodes[tmp].val = val;
        self->nodes[tmp].tombstone = false;
        pthread_mutex_unlock(&self->fields_lock);
    }
    //if the map is full and force is true, overwrite the entry at the index given by get_index
    else if (self->capacity == self->size && force == true)
    {


        pthread_mutex_lock(&self->fields_lock);
        #ifdef DEBUG
          printf("map is full, overwrite the entry at the index given by get_index\n");
        #endif
        self->destroy_function(self->nodes[idx].key,self->nodes[idx].val);
        self->nodes[idx].key.key_base = NULL;
        self->nodes[idx].val.val_base = NULL;
        self->nodes[idx].key.key_len = 0;
        self->nodes[idx].val.val_len = 0;
        self->nodes[idx].key = key;
        self->nodes[idx].val = val;
        pthread_mutex_unlock(&self->fields_lock);
    }
    //insert (key, value) set in empty or tombstone. skip the slot if it's already used.
    else
    {
        while(1)
        {
            //tombstone flag is true, insert (key, value) set.
            if (self->nodes[idx].tombstone == true)
            {
                pthread_mutex_lock(&self->fields_lock);
                #ifdef DEBUG
                 // printf("tombstone flag is true, insert (key, value) set.\n");
                  //  printf("key : %d\n", *((int *)key.key_base));
                #endif
                self->nodes[idx].key = key;
                self->nodes[idx].val = val;
                self->nodes[idx].tombstone = false;
                self->size++;
                pthread_mutex_unlock(&self->fields_lock);
                break;
            }
            //or when element in array is empty, insert (key, value) set.
            else if ( self->nodes[idx].key.key_base == NULL)
            {

                pthread_mutex_lock(&self->fields_lock);
                #ifdef DEBUG
                    //printf("when element in array is empty, insert (key, value) set.\n");
                   // printf("key : %d\n", *((int *)key.key_base));
                #endif
                self->nodes[idx].key = key;
                self->nodes[idx].val = val;
                self->nodes[idx].tombstone = false;
                self->size++;
                pthread_mutex_unlock(&self->fields_lock);
                break;
            }

            #ifdef DEBUG
               printf("%dth slot is already taken, skip this slot\n",idx);
            #endif

            idx++; //skip the slot if it's already used.
            if(idx == self->capacity)
                idx = 0;

        }
    }



    pthread_mutex_unlock(&self->write_lock);
    #ifdef DEBUG
       printf("insert (%s,%s) into %dth slot in map (map_size : %d)\n\n",
         (char *)key.key_base, (char *)val.val_base, idx, self->size);
       print_map_info(self);
    #endif

    return true;
}


map_val_t get(hashmap_t *self, map_key_t key) {

    pthread_mutex_lock(&self->fields_lock);
    self->num_readers++;
    if(self->num_readers == 1) //first in
       pthread_mutex_lock(&self->write_lock);
    pthread_mutex_unlock(&self->fields_lock);

    //Error case : if any of the parameters are invalid, set errno to EINVAL.
    if (self == NULL || key.key_base == NULL || self->invalid == true)
    {
        errno = EINVAL;
        pthread_mutex_unlock(&self->write_lock);
        return MAP_VAL(NULL, 0);
    }

    int idx = get_index(self, key); //get an index from key.

    //Retrieve the value associated with a key
    if ( (idx = linearProbing(self, key, idx)) != -1)
    {
        pthread_mutex_lock(&self->fields_lock);
        self->num_readers--;
        if(self->num_readers == 0) //last out
            pthread_mutex_unlock(&self->write_lock);
        pthread_mutex_unlock(&self->fields_lock);

        return self->nodes[idx].val;
    }
    //if key is not found in the map, the map_val_t instance will contain
    //a NULL pointer and a val_len of 0
    else
    {
        #ifdef DEBUG
            printf("key : %s doesn't exit in map\n", (char*)key.key_base);
        #endif
        pthread_mutex_lock(&self->fields_lock);
        self->num_readers--;
        if(self->num_readers == 0) //last out
            pthread_mutex_unlock(&self->write_lock);
        pthread_mutex_unlock(&self->fields_lock);
        return MAP_VAL(NULL, 0);
    }

}

map_node_t delete(hashmap_t *self, map_key_t key) {

    pthread_mutex_lock(&self->write_lock);

    //Error case : if any of the parameters are invalid, set errno to EINVAL.
    if (self == NULL || key.key_base == NULL || self->invalid == true)
    {
        errno = EINVAL;
        pthread_mutex_unlock(&self->write_lock);
        return MAP_NODE(MAP_KEY(NULL, 0), MAP_VAL(NULL, 0), false);
    }

    int idx = get_index(self, key); //get an index from key.

    //Retrieve the value associated with a key
    if ( (idx = linearProbing(self, key, idx)) != -1)
    {

        pthread_mutex_lock(&self->fields_lock);
        self->nodes[idx].tombstone = true;
        self->size--;
        pthread_mutex_unlock(&self->fields_lock);
        #ifdef DEBUG
            printf("delete map[%d] where key %s is saved. (current size : %d)\n\n",
             idx, (char *)key.key_base, self->size);
            print_map_info(self);
        #endif

        pthread_mutex_unlock(&self->write_lock);
        return self->nodes[idx];
    }
    //if key is not found in the map
    else
    {
        pthread_mutex_unlock(&self->write_lock);
        return MAP_NODE(MAP_KEY(NULL, 0), MAP_VAL(NULL, 0), false);
    }

}

bool clear_map(hashmap_t *self) {

    pthread_mutex_lock(&self->write_lock);

    //Error case : if any of the parameters are invalid, set errno to EINVAL.
    if (self == NULL || self->invalid == true)
    {
        errno = EINVAL;
        pthread_mutex_unlock(&self->write_lock);
        return false;
    }

    for( int idx = 0; idx < self->capacity ; idx++)
    {
        //if slot is empty
        if(self->nodes[idx].key.key_base == NULL || self->nodes[idx].tombstone == true)
        {
            continue;
        }
        else //destroy element.
        {
            if (self->size == 0)
                continue;
            pthread_mutex_lock(&self->fields_lock);
            self->destroy_function(self->nodes[idx].key,self->nodes[idx].val);
            self->nodes[idx].key.key_base = NULL;
            self->nodes[idx].key.key_base = NULL;
            self->nodes[idx].key.key_len = 0;
            self->nodes[idx].val.val_len = 0;
            self->nodes[idx].tombstone = false;
            self->size--;
            #ifdef DEBUG
                printf("clear the map[%d] (current size : %d)\n", idx, self->size);
            #endif
            pthread_mutex_unlock(&self->fields_lock);
        }
    }
    self->size = 0; //just in case.

    #ifdef DEBUG
        print_map_info(self);
    #endif


    pthread_mutex_unlock(&self->write_lock);
	return true;
}

bool invalidate_map(hashmap_t *self) {

    pthread_mutex_unlock(&self->write_lock);

    //Error case : if any of the parameters are invalid, set errno to EINVAL.
    if (self == NULL || self->invalid == true)
    {
        errno = EINVAL;
        pthread_mutex_unlock(&self->write_lock);
        return false;
    }

    for( int idx = 0; idx < self->capacity ; idx++)
    {
        if(self->nodes[idx].key.key_base == NULL) //if slot is empty
        {
            continue;
        }


        pthread_mutex_lock(&self->fields_lock);
        self->destroy_function(self->nodes[idx].key,self->nodes[idx].val);
        self->nodes[idx].tombstone = false;
        self->size--;
        pthread_mutex_unlock(&self->fields_lock);
    }

    pthread_mutex_lock(&self->fields_lock);
    self->invalid = true; //it sets the invalid flag in self to true.
    free(self->nodes); //it frees the nodes pointer in self.
    pthread_mutex_unlock(&self->fields_lock);

    pthread_mutex_unlock(&self->write_lock);
    return false;
}
