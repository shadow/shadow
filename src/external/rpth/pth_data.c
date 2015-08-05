/*
**  GNU Pth - The GNU Portable Threads
**  Copyright (c) 1999-2006 Ralf S. Engelschall <rse@engelschall.com>
**
**  This file is part of GNU Pth, a non-preemptive thread scheduling
**  library which can be found at http://www.gnu.org/software/pth/.
**
**  This library is free software; you can redistribute it and/or
**  modify it under the terms of the GNU Lesser General Public
**  License as published by the Free Software Foundation; either
**  version 2.1 of the License, or (at your option) any later version.
**
**  This library is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
**  Lesser General Public License for more details.
**
**  You should have received a copy of the GNU Lesser General Public
**  License along with this library; if not, write to the Free Software
**  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
**  USA, or contact Ralf S. Engelschall <rse@engelschall.com>.
**
**  pth_data.c: Pth per-thread specific data
*/
                             /* ``Breakthrough ideas
                                  are not from teams.''
                                       --- Hans von Ohain */
#include "pth_p.h"

struct pth_keytab_st {
    int used;
    void (*destructor)(void *);
};

static struct pth_keytab_st pth_keytab[PTH_KEY_MAX];

int pth_key_create(pth_key_t *key, void (*func)(void *))
{
    if (key == NULL)
        return pth_error(FALSE, EINVAL);
    for ((*key) = 0; (*key) < PTH_KEY_MAX; (*key)++) {
        if (pth_keytab[(*key)].used == FALSE) {
            pth_keytab[(*key)].used = TRUE;
            pth_keytab[(*key)].destructor = func;
            return TRUE;
        }
    }
    return pth_error(FALSE, EAGAIN);
}

int pth_key_delete(pth_key_t key)
{
    if (key < 0 || key >= PTH_KEY_MAX)
        return pth_error(FALSE, EINVAL);
    if (!pth_keytab[key].used)
        return pth_error(FALSE, ENOENT);
    pth_keytab[key].used = FALSE;
    return TRUE;
}

int pth_key_setdata(pth_key_t key, const void *value)
{
    if (key < 0 || key >= PTH_KEY_MAX)
        return pth_error(FALSE, EINVAL);
    if (!pth_keytab[key].used)
        return pth_error(FALSE, ENOENT);
    if (pth_current->data_value == NULL) {
        pth_current->data_value = (const void **)calloc(1, sizeof(void *)*PTH_KEY_MAX);
        if (pth_current->data_value == NULL)
            return pth_error(FALSE, ENOMEM);
    }
    if (pth_current->data_value[key] == NULL) {
        if (value != NULL)
            pth_current->data_count++;
    }
    else {
        if (value == NULL)
            pth_current->data_count--;
    }
    pth_current->data_value[key] = value;
    return TRUE;
}

void *pth_key_getdata(pth_key_t key)
{
    if (key < 0 || key >= PTH_KEY_MAX)
        return pth_error((void *)NULL, EINVAL);
    if (!pth_keytab[key].used)
        return pth_error((void *)NULL, ENOENT);
    if (pth_current->data_value == NULL)
        return (void *)NULL;
    return (void *)pth_current->data_value[key];
}

intern void pth_key_destroydata(pth_t t)
{
    void *data;
    int key;
    int itr;
    void (*destructor)(void *);

    if (t == NULL)
        return;
    if (t->data_value == NULL)
        return;
    /* POSIX thread iteration scheme */
    for (itr = 0; itr < PTH_DESTRUCTOR_ITERATIONS; itr++) {
        for (key = 0; key < PTH_KEY_MAX; key++) {
            if (t->data_count > 0) {
                destructor = NULL;
                data = NULL;
                if (pth_keytab[key].used) {
                    if (t->data_value[key] != NULL) {
                        data = (void *)t->data_value[key];
                        t->data_value[key] = NULL;
                        t->data_count--;
                        destructor = pth_keytab[key].destructor;
                    }
                }
                if (destructor != NULL)
                    destructor(data);
            }
            if (t->data_count == 0)
                break;
        }
        if (t->data_count == 0)
            break;
    }
    free(t->data_value);
    t->data_value = NULL;
    return;
}

