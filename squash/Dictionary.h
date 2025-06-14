/* https://gist.github.com/rosshays/3233521#file-dictionary-h-L1
 * Okay so here is the deal, I made this for a personal project I am working on,
 * but I figured that others would get some benefit from it and also maybe help
 * me make it better.
 *
 * What is it you ask?
 * Well it is an implementation of a dictionary (technically a map) in pure C.
 * I did use an object oriented approach when I made this, so...
 * Used these links for reference when building my dictionary, map, magic thing
 *
 * http://en.wikipedia.org/wiki/Red%E2%80%93black_tree
 * http://en.wikipedia.org/wiki/Binary_search_tree
 */

#ifndef DICTIONARY_H
#define DICTIONARY_H

#include <stdbool.h>

//#include "Symbols.h"

typedef int (*DictionaryPairCompare)(void*, void*);

// color indicating if a node is red or black
typedef enum NodeColor
{
	COLOR_BLACK,
	COLOR_RED,
} NodeColor;

enum VarSymbolType {
	VAR_Long = 5,
	VAR_Char = 4,
	VAR_Int = 3,
	VAR_Double = 2,
	VAR_Float = 1,
	VAR_Undefined = 0
};

typedef struct DictionaryPair
{
	void* key;
	void* value;
	enum VarSymbolType type;

	struct DictionaryNode* parentNode;
} DictionaryPair;

// a node on the dictionary tree
typedef struct DictionaryNode
{
	struct DictionaryNode* left;
	struct DictionaryNode* right;
	struct DictionaryNode* parent;
	enum NodeColor color;
	struct DictionaryPair* pair;
} DictionaryNode;

// structure used as the interface for the dictionary
typedef struct Dictionary
{
	DictionaryPairCompare compareFunc;
	struct DictionaryNode* rootNode;
} Dictionary;

////////////////////////////////////////////////////////////////////////////////
// dictionary pair functions
DictionaryPair* DictionaryPairAlloc(void);

DictionaryPair* DictionaryPairInit(DictionaryPair* pair);

DictionaryPair* DictionaryPairNew(void);


////////////////////////////////////////////////////////////////////////////////
// dictionary functions

Dictionary* DictionaryAlloc(void);

Dictionary* DictionaryInit(Dictionary* dictionary, DictionaryPairCompare comparator);

// create a new dictionary based on the passed comparator function
Dictionary* DictionaryNew(DictionaryPairCompare comparator);

// returns the pair that is either inserted or already was there
// if one was there already, inserted will be set to false
bool DictionaryInsertPair(Dictionary* dictionary, DictionaryPair* pair,
	DictionaryPair** pairOut);

// when this functions finishes, pair will not be in dictionary, one way or 
// another...
// void DictionaryRemovePair(Dictionary *dictionary, DictionaryPair *pair);

DictionaryPair* DictionaryGetPair(Dictionary* dictionary, void* key);

#endif // DICTIONARY_H