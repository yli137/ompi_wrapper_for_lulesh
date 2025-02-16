#ifndef _LRU_HEADER_
#define _LRU_HEADER_

#define HASH_SIZE 1024

// Node structure for the doubly linked list
typedef struct Node {
	unsigned long key;
	size_t value;
	long long timestamp;
	struct Node *prev;
	struct Node *next;
} Node;

// Structure for hashmap entry
typedef struct HashMapEntry {
	unsigned long key;
	Node *node;
	struct HashMapEntry *next;
} HashMapEntry;

// LRU Cache structure
typedef struct {
	Node *head; // Most recently used
	Node *tail; // Least recently used
	HashMapEntry *hashmap[HASH_SIZE];
	size_t capacity;
	size_t size;
} LRUCache;

long long get_first_node_time(LRUCache *cache);

// Hash function for the keys
unsigned long hash_function(unsigned long key);

// Create a new Node
Node *create_node(unsigned long key, size_t value);

// Create a new LRU Cache
LRUCache *create_cache(size_t capacity);

// Remove a node from the linked list
void remove_node(LRUCache *cache, Node *node);

// Add a node to the head of the linked list
void add_to_head(LRUCache *cache, Node *node);

// Find a node in the hashmap
Node *find_in_hashmap(LRUCache *cache, unsigned long key);

// Add or update an entry in the hashmap
void update_hashmap(LRUCache *cache, unsigned long key, Node *node);

// Remove the least recently used node
Node *remove_lru(LRUCache *cache);

// Put a key-value pair in the cache
void put(LRUCache *cache, unsigned long key, size_t value);

// Check if a key is present in the cache
int check(LRUCache *cache, unsigned long key);

// Remove the least recently used pair and return it
Node remove_least_used(LRUCache *cache);

void print_cache(LRUCache *cache);

#endif
