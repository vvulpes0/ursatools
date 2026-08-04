#ifndef DYNARR_H
#define DYNARR_H


/***********************************************************************
 * Types
 ***********************************************************************/

struct dynarr {
	void *content;
	int elemsize;
	int capacity;
	int length;
};


/***********************************************************************
 * Constructors
 ***********************************************************************/

/**
 * Constructs a dynamic array with the specified element size.
 * @param size  the element size
 * @return the newly created dynamic array
 */
struct dynarr da_new(int size);


/* ******************************************************************* *
 * Destructors
 * ******************************************************************* */

/**
 * Releases memory held by this dynamic array.
 * @param dynarr  this dynamic array
 */
void da_free(struct dynarr);


/***********************************************************************
 * Functions
 ***********************************************************************/
/**
 * Inserts the given value into this dynamic array.
 * The contents may be reallocated and moved.
 * In this case, all pointers to the contents are invalidated.
 * @param arr   this dynamic array
 * @param data  a reference to the object that should be inserted
 * @return whether insertion was successful
 */
_Bool da_append(struct dynarr *arr, void const *data);

/**
 * Retrieves the value at the given index in this dynamic array.
 * @param arr  this dynamic array
 * @param i    the target index
 * @return a reference to the specified value
 */
void *da_get(struct dynarr arr, int i);

#endif
