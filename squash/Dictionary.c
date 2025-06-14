#include "Dictionary.h"

#include <stdlib.h>
#include <stdbool.h>

////////////////////////////////////////////////////////////////////////////////
// https://gist.github.com/rosshays/3233521#file-dictionary-c-L1
// static function forward declarations
// dictionary node static
static DictionaryNode* DictionaryNodeAlloc(void);

static DictionaryNode* DictionaryNodeInit(DictionaryNode* node, void* pair);

static DictionaryNode* DictionaryNodeNew(void* pair);

static void DictionaryNodeDestroy(DictionaryNode* node, void** pairOut);

// since the pairs are external, they are returned to avoid mem leak
// remember to do something with them!
static void DictionaryNodeFree(DictionaryNode* node, void** pairOut);

static DictionaryNode* DictionaryNodeSibling(DictionaryNode* node);

static DictionaryNode* DictionaryNodeParent(DictionaryNode* node);

static DictionaryNode* DictionaryNodeUncle(DictionaryNode* node);

static DictionaryNode* DictionaryNodeGrandparent(DictionaryNode* node);

// dictionary static
// static func used by the insert pair function
static bool DictionaryInsertNode(Dictionary* dictionary,
	DictionaryNode** treeNode, DictionaryNode* newNode, DictionaryNode** nodeOut);

static void DictionaryRotateLeft(DictionaryNode* node);

static void DictionaryRotateRight(DictionaryNode* node);

static void DictionaryInsertCase1(DictionaryNode* node);

static void DictionaryInsertCase2(DictionaryNode* node);

static void DictionaryInsertCase3(DictionaryNode* node);

static void DictionaryInsertCase4(DictionaryNode* node);

static void DictionaryInsertCase5(DictionaryNode* node);


////////////////////////////////////////////////////////////////////////////////
// dictionary pair functions
DictionaryPair* DictionaryPairAlloc(void)
{
	return (DictionaryPair*)malloc(sizeof(DictionaryPair));
}

DictionaryPair* DictionaryPairInit(DictionaryPair* pair)
{
	pair->key = NULL;
	pair->value = NULL;
	pair->parentNode = NULL;
	return pair;
}

DictionaryPair* DictionaryPairNew(void)
{
	return DictionaryPairInit(DictionaryPairAlloc());
}


////////////////////////////////////////////////////////////////////////////////
// node functions

DictionaryNode* DictionaryNodeAlloc(void)
{
	return (DictionaryNode*)malloc(sizeof(DictionaryNode));
}

DictionaryNode* DictionaryNodeInit(DictionaryNode* node, void* pair)
{
	node->left = NULL;
	node->right = NULL;
	node->parent = NULL;
	node->color = COLOR_RED;
	node->pair = pair;


	return node;
}

DictionaryNode* DictionaryNodeNew(void* pair)
{
	return DictionaryNodeInit(DictionaryNodeAlloc(), pair);
}

void DictionaryNodeDestroy(DictionaryNode* node, void** pairOut)
{
	node->left = NULL;
	node->right = NULL;
	node->parent = NULL;
	node->color = COLOR_RED;
	*pairOut = node->pair;
}

void DictionaryNodeFree(DictionaryNode* node, void** pairOut)
{
	if (node != NULL)
	{
		DictionaryNodeDestroy(node, pairOut);
		free(node);
	}
}


DictionaryNode* DictionaryNodeParent(DictionaryNode* node)
{
	return node->parent;
}

DictionaryNode* DictionaryNodeSibling(DictionaryNode* node)
{
	if (node->parent == NULL)
	{
		return NULL;
	}
	else if (node == node->parent->left)
	{
		return node->parent->right;
	}
	else if (node == node->parent->left)
	{
		return node->parent->right;
	}
}

DictionaryNode* DictionaryNodeUncle(DictionaryNode* node)
{
	DictionaryNode* grandparent = DictionaryNodeGrandparent(node);
	if (grandparent == NULL)
		return NULL; // No grandparent means no uncle
	if (node->parent == grandparent->left)
		return grandparent->right;
	else
		return grandparent->left;
}

DictionaryNode* DictionaryNodeGrandparent(DictionaryNode* node)
{
	if ((node != NULL) && (node->parent != NULL))
		return node->parent->parent;
	else
		return NULL;
}

////////////////////////////////////////////////////////////////////////////////
// dictionary functions


Dictionary* DictionaryAlloc(void)
{
	return (Dictionary*)malloc(sizeof(Dictionary));
}

Dictionary* DictionaryInit(Dictionary* dictionary, DictionaryPairCompare comparator)
{
	dictionary->compareFunc = comparator;
	dictionary->rootNode = NULL;

	return dictionary;
}

Dictionary* DictionaryNew(DictionaryPairCompare comparator)
{
	return DictionaryInit(DictionaryAlloc(), comparator);
}

bool DictionaryInsertNode(struct Dictionary* dictionary,
	struct DictionaryNode** treeNode, struct DictionaryNode* newNode,
	struct DictionaryNode** nodeOut)
{
	if (*treeNode == NULL)
	{
		*treeNode = newNode;
		*nodeOut = newNode;
		newNode->pair->parentNode = newNode;
		return true;
	}
	struct DictionaryNode* node;
	int compareResult = dictionary->compareFunc(newNode->pair, (*treeNode)->pair);
	if (compareResult < 0)
	{
		node = (*treeNode)->right;
		DictionaryInsertNode(dictionary, &node, newNode, nodeOut);
	}
	else if (compareResult > 0)
	{
		node = (*treeNode)->left;
		DictionaryInsertNode(dictionary, &node, newNode, nodeOut);
	}
	else
	{
		*nodeOut = *treeNode;
		newNode->pair->parentNode = NULL;
		return false;
	}
}

bool DictionaryInsertPair(Dictionary* dictionary, DictionaryPair* pair,
	DictionaryPair** pairOut)
{
	DictionaryNode* node = DictionaryNodeNew(pair), * insertedNode;
	bool inserted = DictionaryInsertNode(dictionary,
		&dictionary->rootNode,
		node,
		&insertedNode);
	*pairOut = insertedNode->pair;
	if (inserted)
	{
		DictionaryInsertCase1(node);
	}
	return inserted;
}

void DictionaryRotateLeft(DictionaryNode* node)
{
	DictionaryNode* pivot = node->right;
	// if (pivot == NULL)
	// {
	// 	pivot == DictionaryNodeNew()
	// }
	// todo what to do if the pivot is null?
	node->right = pivot->left;
	if (node->parent != NULL && node == node->parent->left)
	{
		node->parent->left = pivot;
	}
	else if (node->parent != NULL && node == node->parent->right)
	{
		node->parent->right = pivot;
	}
	pivot->left = node;
}

void DictionaryRotateRight(DictionaryNode* node)
{
	DictionaryNode* pivot = node->left;
	node->left = pivot->right;
	if (node->parent != NULL && node == node->parent->left)
	{
		node->parent->left = pivot;
	}
	else if (node->parent != NULL && node == node->parent->right)
	{
		node->parent->right = pivot;
	}
	pivot->right = node;
}

void DictionaryInsertCase1(DictionaryNode* node)
{
	if (node->parent == NULL)
	{
		node->color = COLOR_BLACK;
	}
	else
	{
		DictionaryInsertCase2(node);
	}
}

void DictionaryInsertCase2(DictionaryNode* node)
{
	if (node->parent->color = COLOR_RED)
	{
		DictionaryInsertCase3(node);
	}
}

void DictionaryInsertCase3(DictionaryNode* node)
{
	DictionaryNode* uncle = DictionaryNodeUncle(node), * grandparent;
	if (uncle != NULL && uncle->color == COLOR_RED)
	{
		node->parent->color = COLOR_BLACK;
		uncle->color = COLOR_BLACK;
		grandparent = DictionaryNodeGrandparent(node);
		grandparent->color = COLOR_RED;
		DictionaryInsertCase1(grandparent);
	}
	else
	{
		DictionaryInsertCase4(node);
	}
}

void DictionaryInsertCase4(DictionaryNode* node)
{
	DictionaryNode* grandparent = DictionaryNodeGrandparent(node);
	if (node == node->parent->right && node->parent == grandparent->left)
	{
		DictionaryRotateLeft(node->parent);
		node = node->left;
	}
	else if (node == node->parent->left && node->parent == grandparent->right)
	{
		DictionaryRotateRight(node->parent);
		node = node->right;
	}
	DictionaryInsertCase5(node);
}

void DictionaryInsertCase5(DictionaryNode* node)
{
	DictionaryNode* grandparent = DictionaryNodeGrandparent(node);
	node->parent->color = COLOR_BLACK;
	grandparent->color = COLOR_RED;
	if (node == node->parent->left)
	{
		DictionaryRotateRight(grandparent);
	}
	else
	{
		DictionaryRotateLeft(grandparent);
	}
}

// void DictionaryRemovePair(Dictionary *dictionary, DictionaryPair *pair)
// {



// }




static DictionaryNode* DictionaryGetNode(Dictionary* dictionary,
	DictionaryNode** treeNode, DictionaryNode* findNode)
{
	if (*treeNode == NULL || findNode == NULL || findNode->pair->key == NULL)
	{
		return NULL;
	}

	int result = dictionary->compareFunc(findNode->pair, (*treeNode)->pair);
	if (result < 0)
	{
		DictionaryGetNode(dictionary, &(*treeNode)->left, findNode);
	}
	else if (result > 0)
	{
		DictionaryGetNode(dictionary, &(*treeNode)->right, findNode);
	}
	else if (result == 0)
	{
		return *treeNode;
	}
}

DictionaryPair* DictionaryGetPair(Dictionary* dictionary, void* key)
{
	DictionaryPair* pair = DictionaryPairNew();
	pair->key = key;
	DictionaryNode* node = DictionaryNodeNew(pair);

	return DictionaryGetNode(dictionary, &dictionary->rootNode, node)->pair;
}