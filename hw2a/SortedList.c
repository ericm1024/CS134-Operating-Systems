// SortedList.c
//
// Eric Mueller -- emueller@hmc.edu
//
// A sorted list implemented to match the interface specification in
// SortedList.h, a provided file for this assignment.

#include "SortedList.h"

#include <sched.h>
#include <string.h>
#include <stdio.h>

void SortedList_insert(SortedList_t *list, SortedListElement_t *element)
{
        SortedListElement_t *next = list->next;
        while (next->key && strcmp(element->key, next->key) >= 0)
                next = next->next;

        next->prev->next = element;
        element->prev = next->prev;
        if (opt_yield & INSERT_YIELD)
                sched_yield();

        element->next = next;
        next->prev = element;
}

int SortedList_delete(SortedListElement_t *element)
{
        if (element->next->prev != element || element->prev->next != element)
                return 1;
        
        element->next->prev = element->prev;

        if (opt_yield & DELETE_YIELD)
                sched_yield();
        
        element->prev->next = element->next;

        element->prev = NULL;
        element->next = NULL;

        return 0;
}

SortedListElement_t *SortedList_lookup(SortedList_t *list, const char *key)
{
        for (;;) {
                // the critical section of lookup is not so clear, but
                // "right before we walk to the next element" seems like
                // a reasonable place.
                if (opt_yield & LOOKUP_YIELD)
                        sched_yield();

                // walk the list
                list = list->next;

                // end of list or empty list
                if (list->key == NULL)
                        break;
                
                int ret = strcmp(list->key, key);

                // we found the key!
                if (ret == 0)
                        return list;

                // list->key > key. Since we're sorted in ascending order,
                // this means we didn't find the key in out list
                if (ret > 0)
                        break;
        }
        return NULL;
}

int SortedList_length(SortedList_t *list)
{
        SortedList_t *node = list;
        int len = 0;

        for (;;) {
                // list corruption
                if (node->prev->next != node || node->next->prev != node)
                        return -1;

                // we got to the end of the list!
                if (node == list)
                        return len;

                len++;
                if (opt_yield & LOOKUP_YIELD)
                        sched_yield();

                node = node->next;
        }
}
