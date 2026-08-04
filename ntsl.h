#ifndef NTSL_H
#define NTSL_H

/***********************************************************************
 * Types
 ***********************************************************************/

/**
 * Null-terminated-string list: a linked list of null-terminated strings.
 */

struct ntsl {
	struct ntsl *next;
	int length;
	char *content;
};


/***********************************************************************
 * Constructors
 ***********************************************************************/

/**
 * Prepends an owned copy of the given string to the given list.
 * @param  list    the list to prepend to
 * @param  string  the string to add
 * @return the newly created node; NULL if allocation failed.
 */
struct ntsl *ntsladd(struct ntsl *list, char const *string);


/***********************************************************************
 * Destructors
 ***********************************************************************/

/**
 * Frees this list and all strings that it owns.
 * @param  list  this list
 */
void ntslfree(struct ntsl *list);


/***********************************************************************
 * Functions
 ***********************************************************************/

/**
 * Appends an owned copy of the given string to this list.
 * @param  list    pointer to this list
 * @param  string  the string to add
 */
void ntslappend(struct ntsl **list, char const *string);

/**
 * Determines the index at which the given string occurs in this list.
 * @param  list    this list
 * @param  string  the string to find
 * @return the first index matching the given string; -1 if not found
 */
int ntslfind(struct ntsl const *list, char const *string);

/**
 * Retrieves the null-terminated string at the given index from this list.
 * @param  list  this list
 * @param  i     the index to retrieve
 * @return the associated content if {@code i} is in range, else NULL.
 */
char *ntslget(struct ntsl const *list, int i);

#endif
