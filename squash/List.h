#ifndef LIST_H
#define LIST_H

#include <stdlib.h>
#include <stddef.h>
#include <stdbool.h>

/// <summary>
/// Doubly Linked List implementation for use as a list, stack, or queue.
/// </summary>
typedef struct list_d
{
	struct list_d* next;
	struct list_d* prev;
	void* data;
} list_t;

/// <summary>
/// Instantiates a list node with allocator.
/// </summary>
/// <returns></returns>
list_t* list_new();

/// <summary>
/// Instantiates a list item and sets its data attribute.
/// </summary>
/// <param name="list_front">
/// The front of the list
/// </param>
/// <param name="data">
/// The underlying data that is specified for the list item.
/// </param>
void list_init(list_t** list_front, void* data);

/// <summary>
/// Frees an entire linked list given the starting front of the list to free from.
/// </summary>
/// <param name="list_front">
/// The specified start of the list known as the list front.
/// </param>
void list_free(list_t** list_front);

/// <summary>
/// Traverses the linked list to find its size.
/// </summary>
/// <param name="list_front">
/// The front of the list to start traversal.
/// </param>
/// <returns>
/// Returns the total size of the list same as count.
/// </returns>
size_t list_size(list_t* list_front);

/// <summary>
/// Gets the top item known as the front of the list without modifying the list.
/// </summary>
/// <param name="list_front">
/// The front of the list.
/// </param>
/// <returns>
/// The front of the list.
/// </returns>
list_t* list_peek(list_t* list_front);

/// <summary>
/// Gets the data from a list node known as a list item.
/// </summary>
/// <param name="list_item">
/// The specified list item list node to extract the data from the item instance.
/// </param>
/// <returns>
/// The data expressed as a void pointer.
/// </returns>
void* list_get_data(list_t* list_item);

/// <summary>
/// Adds to the front of the list which is the top of the list known as the list front.
/// </summary>
/// <param name="list_front">
/// The top of the list as the insertion point.
/// </param>
/// <param name="data">
/// The data for the specified new front of the list.
/// </param>
void list_push(list_t* list_front, void* data);

/// <summary>
/// Enqueues a new list item at the end point in the list found by a traversal to the end of the list.
/// </summary>
/// <param name="list_front">
/// The specified front of the list.
/// </param>
/// <param name="data">
/// The new list item data to insert and enqueue at the end of the list.
/// </param>
void list_enqueue(list_t* list_front, void* data);

/// <summary>
/// Gets the front of the list and removes the front of the list from the beginning of the list.
/// </summary>
/// <param name="list_front">
/// The speicifed front of the list known as the beginning and top of list.
/// </param>
/// <returns>
/// Returns the top of the list while removing it from the list.
/// </returns>
list_t* list_pop(list_t** list_front);

/// <summary>
/// Gets the front of the list 
/// </summary>
/// <param name="list_front"></param>
/// <returns></returns>
list_t* list_dequeue(list_t* list_front);

#endif