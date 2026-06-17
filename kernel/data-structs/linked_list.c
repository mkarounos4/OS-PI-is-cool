#include "linked_list.h"

#include "memory/kmalloc.h"

// Creates and returns a new list
list_t* list_create()
{
	/* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
	list_t *list = kmalloc(sizeof(list_t));
	list->head = NULL;
        return list; 
}

// Destroys a node
void node_destroy(list_node_t *node) {
	while (node != NULL) {
		list_node_t *next = node->next;
		kfree(node);
		node = next;
	}
}

// Destroys a list
void list_destroy(list_t* list)
{
    	/* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
	if (list == NULL) return;
	node_destroy(list->head);
	kfree(list);
	return;
    
}

// Returns beginning of the list
list_node_t* list_begin(list_t* list)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    return list->head;
}

// Returns next element in the list
list_node_t* list_next(list_node_t* node)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    return node->next;
}

// Returns data in the given list node
void* list_data(list_node_t* node)
{
    /* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    return node->data;
}

// Returns the number of elements in the list
size_t list_count(list_t* list)
{
	/* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
	size_t count = 0;
	if (list->head == NULL) return count;
	else {
		count++;
		list_node_t *current = list->head;
		while (current->next != NULL) {
			count++;
			current = current->next;
		}
	}
	return count;
}

// Finds the first node in the list with the given data
// Returns NULL if data could not be found
list_node_t* list_find(list_t* list, void* data)
{
    	/* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
    	if (list->head != NULL) {
		list_node_t *current = list->head;
		if (current->data == data) return current;
		while (current->next != NULL) {
			current = current->next;
	                if (current->data == data) return current;
		}
	}
	return NULL;
}

// Inserts a new node in the list with the given data
void list_insert(list_t* list, void* data)
{
    	/* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
	if (list->head == NULL) {
		list->head = kmalloc(sizeof(list_node_t));
		list->head->prev = NULL;
		list->head->next = NULL;
		list->head->data = data;
	} else {
		list_node_t *current = list->head;
		while (current->next != NULL) {
			current = current->next;
		}
		current->next = kmalloc(sizeof(list_node_t));
		current->next->prev = current;
		current->next->next = NULL;
		current->next->data = data;
	}
}

// Removes a node from the list and frees the node resources
void list_remove(list_t* list, list_node_t* node)
{
	/* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
	if (node->prev != NULL) {
		node->prev->next = node->next;
	} else {
		list->head = node->next;
	}

	if (node->next != NULL) {
		node->next->prev = node->prev;
	}

	kfree(node);
}

// Executes a function for each element in the list
void list_foreach(list_t* list, void (*func)(void* data))
{
	/* IMPLEMENT THIS IF YOU WANT TO USE LINKED LISTS */
	if (list->head != NULL) {
		list_node_t *current = list->head;
		func(current->data);
		while(current->next != NULL) {
			current = current->next;
	                func(current->data);
		}
	}
}
