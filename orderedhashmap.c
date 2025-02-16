#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>

#include "orderedhashmap.h"
#include "wrapper.h"

#define HASH_SIZE 1024

long long get_timestamp()
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (long long)tv.tv_sec * 1000000LL + tv.tv_usec;
}


// Hash function for the keys
unsigned long hash_function(unsigned long key) {
	return key % HASH_SIZE;
}

// Create a new Node
Node *create_node(unsigned long key, size_t value) {
	Node *node = (Node *)malloc(sizeof(Node));
	node->key = key;
	node->value = value;
	node->prev = NULL;
	node->next = NULL;

	node->timestamp = get_timestamp();

	return node;
}

// Create a new LRU Cache
LRUCache *create_cache(size_t capacity) {
	LRUCache *cache = (LRUCache *)malloc(sizeof(LRUCache));
	cache->head = NULL;
	cache->tail = NULL;
	cache->capacity = capacity;
	cache->size = 0;
	memset(cache->hashmap, 0, sizeof(cache->hashmap));
	return cache;
}

// Remove a node from the linked list
void remove_node(LRUCache *cache, Node *node) {
	if (node->prev) {
		node->prev->next = node->next;
	} else {
		cache->head = node->next;
	}

	if (node->next) {
		node->next->prev = node->prev;
	} else {
		cache->tail = node->prev;
	}
}

// Add a node to the head of the linked list
void add_to_head(LRUCache *cache, Node *node) {
	node->next = cache->head;
	node->prev = NULL;

	node->timestamp = get_timestamp();

	if (cache->head) {
		cache->head->prev = node;
	}
	cache->head = node;
	if (!cache->tail) {
		cache->tail = node;
	}
}

// Find a node in the hashmap
Node *find_in_hashmap(LRUCache *cache, unsigned long key) {
	unsigned long hash = hash_function(key);
	HashMapEntry *entry = cache->hashmap[hash];

	while (entry) {
		if (entry->key == key) {
			return entry->node;
		}
		entry = entry->next;
	}
	return NULL;
}

// Add or update an entry in the hashmap
void update_hashmap(LRUCache *cache, unsigned long key, Node *node) {
	unsigned long hash = hash_function(key);
	HashMapEntry *entry = cache->hashmap[hash];

	while (entry) {
		if (entry->key == key) {
			entry->node = node;
			return;
		}
		entry = entry->next;
	}

	// Add new entry
	HashMapEntry *new_entry = (HashMapEntry *)malloc(sizeof(HashMapEntry));
	new_entry->key = key;
	new_entry->node = node;
	new_entry->next = cache->hashmap[hash];

	node->timestamp = get_timestamp();

	cache->hashmap[hash] = new_entry;
}

long long get_first_node_time(LRUCache *cache)
{
	if(!cache->tail){
		return 0;
	}

	return cache->tail->timestamp;
}

// Remove the least recently used node
Node *remove_lru(LRUCache *cache) {
	if (!cache->tail) {
		return NULL;
	}

	Node *node = cache->tail;
	remove_node(cache, node);

	unsigned long hash = hash_function(node->key);
	HashMapEntry *entry = cache->hashmap[hash];
	HashMapEntry *prev = NULL;

	while (entry) {
		if (entry->key == node->key) {
			if (prev) {
				prev->next = entry->next;
			} else {
				cache->hashmap[hash] = entry->next;
			}
			free(entry);
			break;
		}
		prev = entry;
		entry = entry->next;
	}

	cache->size--;
	return node;
}

// Put a key-value pair in the cache
void put(LRUCache *cache, unsigned long key, size_t value) {
	Node *node = find_in_hashmap(cache, key);

	if (node) {
		// Key exists, update value and move to head
		node->value = value;
		remove_node(cache, node);
		add_to_head(cache, node);
	} else {
		// Key does not exist, create a new node
		if (cache->size == cache->capacity) {
			Node *removed = remove_lru(cache);
			free(removed);
		}

		node = create_node(key, value);
		add_to_head(cache, node);
		update_hashmap(cache, key, node);
		cache->size++;
	}
}

// Check if a key is present in the cache
int check(LRUCache *cache, unsigned long key) {
	if(find_in_hashmap(cache,key) != NULL)
		return 1;
	return 0;
}

// Remove the least recently used pair and return it
Node remove_least_used(LRUCache *cache) {
	Node *removed = remove_lru(cache);
	if (removed) {
		Node result = *removed;
		free(removed);
		return result;
	}
	return (Node){0, 0, 0, NULL, NULL};
}

void print_cache(LRUCache *cache) {
    Node *current = cache->head;
    while (current) {
        printf("Key: %lu, Value: %zu\n", current->key, current->value);
        current = current->next;
    }
}

