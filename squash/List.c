#include "List.h"

/// <summary>
/// Instantiates a list node with allocator.
/// </summary>
/// <returns></returns>
list_t* list_new()
{
	list_t* n = (list_t*)malloc(sizeof(list_t));
	if (n)
	{
		n->next = NULL;
		n->prev = NULL;
		n->data = NULL;
	}
	return n;
}

/// <summary>
/// Instantiates a list item and sets its data attribute.
/// </summary>
/// <param name="list_front">
/// The front of the list
/// </param>
/// <param name="data">
/// The underlying data that is specified for the list item.
/// </param>
void list_init(list_t** list_front, void* data)
{
	list_t* n = list_new();
	n->next = NULL;
	n->prev = NULL;
	n->data = data;
	*list_front = n;
}

/// <summary>
/// Frees an entire linked list given the starting front of the list to free from.
/// </summary>
/// <param name="list_front">
/// The specified start of the list known as the list front.
/// </param>
void list_free(list_t** list_front)
{
	if (list_front != NULL)
	{
		list_t* curr = *list_front;

		if (curr != NULL)
		{
			list_t* n = curr;
			while (n != NULL)
			{
				free(n);
				n = n->next;
			}
		}
		(*list_front) = NULL;
	}
}

/// <summary>
/// Traverses the linked list to find its size.
/// </summary>
/// <param name="list_front">
/// The front of the list to start traversal.
/// </param>
/// <returns>
/// Returns the total size of the list same as count.
/// </returns>
size_t list_size(list_t* list_front)
{
	size_t count = 0;

	list_t* next = list_front;
	list_t* prev = NULL;

	while (next != NULL)
	{
		count++;
		prev = next;
		next = next->next;
	}

	return count;
}

/// <summary>
/// Gets the top item known as the front of the list without modifying the list.
/// </summary>
/// <param name="list_front">
/// The front of the list.
/// </param>
/// <returns>
/// The front of the list.
/// </returns>
list_t* list_peek(list_t* list_front)
{
	return list_front;
}

/// <summary>
/// Gets the data from a list node known as a list item.
/// </summary>
/// <param name="list_item">
/// The specified list item list node to extract the data from the item instance.
/// </param>
/// <returns>
/// The data expressed as a void pointer.
/// </returns>
void* list_get_data(list_t* list_item)
{
	void* data = NULL;
	if (list_item != NULL)
	{
		data = list_item->data;
	}
	return data;
}

/// <summary>
/// Adds to the front of the list which is the top of the list known as the list front.
/// </summary>
/// <param name="list_front">
/// The top of the list as the insertion point.
/// </param>
/// <param name="data">
/// The data for the specified new front of the list.
/// </param>
void list_push(list_t* list_front, void* data)
{
	list_t* next = (list_t*)malloc(sizeof(list_t));
	if (next != NULL)
	{
		next->data = data;
		next->next = NULL;
		next->prev = list_front;
		list_front->next = next;
	}
}

/// <summary>
/// Enqueues a new list item at the end point in the list found by a traversal to the end of the list.
/// </summary>
/// <param name="list_front">
/// The specified front of the list.
/// </param>
/// <param name="data">
/// The new list item data to insert and enqueue at the end of the list.
/// </param>
void list_enqueue(list_t* list_front, void* data)
{
	char* pC = (char*)data;
	//char c = *pC;
	//printf("DATA: %s", pC);

	if (list_front->prev == NULL && list_front->data == NULL)
	{
		list_front->data = data;
		list_front->next = NULL;
		list_front->prev = NULL;
	}
	else
	{
		list_t* next = (list_t*)malloc(sizeof(list_t));

		if (next != NULL)
		{
			next->data = data;
			next->next = NULL;
			next->prev = NULL;

			list_t* insertion_point = NULL;
			list_t* n = list_front;
			list_t* p = NULL;
			while (n != NULL)
			{
				p = n;
				n = n->next;
			}

			if (p == NULL)
			{
				p = n;
			}

			insertion_point = p;
			next->prev = insertion_point;
			insertion_point->next = next;
		}
	}
}

/// <summary>
/// Gets the front of the list and removes the front of the list from the beginning of the list.
/// </summary>
/// <param name="list_front">
/// The speicifed front of the list known as the beginning and top of list.
/// </param>
/// <returns>
/// Returns the top of the list while removing it from the list.
/// </returns>
list_t* list_pop(list_t** list_front)
{
	list_t* result = *list_front;
	list_t* n = NULL;
	list_t* p = (*list_front)->next;

	p->prev = NULL;
	(*list_front) = p;

	return result;
}

/// <summary>
/// Gets the front of the list 
/// </summary>
/// <param name="list_front"></param>
/// <returns></returns>
list_t* list_dequeue(list_t* list_front)
{
	list_t* list_end = NULL;
	list_t* n = list_front;
	list_t* p = NULL;

	while (n != NULL)
	{
		p = n;
		n = n->next;
	}

	if (p == NULL)
	{
		p = list_front;
	}

	if (p != NULL)
	{
		list_end = p;

		list_t* p2 = list_end->prev;
		p2->next = NULL;
		list_end->prev = NULL;
	}

	return list_end;
}