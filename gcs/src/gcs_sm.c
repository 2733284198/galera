/*
 * Copyright (C) 2010 Codership Oy <info@codership.com>
 *
 * $Id$
 */

/*!
 * @file GCS Send Monitor. To ensure fair (FIFO) access to gcs_core_send()
 */

#include "gcs_sm.h"

#include <string.h>

extern gcs_sm_t*
gcs_sm_create (long len, long n)
{
    if ((len < 2 /* 2 is minimum */) || (len & (len - 1))) {
        gu_error ("Monitor length parameter is not a power of 2: %ld", len);
        return NULL;
    }

    if (n < 1) {
        gu_error ("Invalid monitor concurrency parameter: %ld", n);
        return NULL;
    }

    size_t sm_size = sizeof(gcs_sm_t) +
        len * sizeof(((gcs_sm_t*)(0))->wait_q[0]);

    gcs_sm_t* sm = gu_malloc(sm_size);

    if (sm) {
        gu_mutex_init (&sm->lock, NULL);
        sm->wait_q_len  = len;
        sm->wait_q_mask = sm->wait_q_len - 1;
        sm->wait_q_head = 1;
        sm->wait_q_tail = 0;
        sm->users       = 0;
        sm->entered     = 0;
        sm->ret         = 0;
#ifdef GCS_SM_CONCURRENCY
        sm->cc          = n; // concurrency param.
#endif /* GCS_SM_CONCURRENCY */
        sm->pause       = false;
        memset (sm->wait_q, 0, sm->wait_q_len * sizeof(sm->wait_q[0]));
    }

    return sm;
}

extern long
gcs_sm_close (gcs_sm_t* sm)
{
    gu_info ("Closing send monitor...");

    if (gu_unlikely(gu_mutex_lock (&sm->lock))) abort();

    sm->ret = -EBADFD;

    if (sm->pause) _gcs_sm_continue_common (sm);

    gu_cond_t cond;
    gu_cond_init (&cond, NULL);

    // in case the queue is full
    while (sm->users >= (long)sm->wait_q_len) {
        gu_mutex_unlock (&sm->lock);
        usleep(1000);
        gu_mutex_lock (&sm->lock);
    }

    while (sm->users > 0) { // wait for cleared queue
        sm->users++;
        GCS_SM_INCREMENT(sm->wait_q_tail);
        _gcs_sm_enqueue_common (sm, &cond);
        sm->users--;
        GCS_SM_INCREMENT(sm->wait_q_head);
    }

    gu_cond_destroy (&cond);

    gu_mutex_unlock (&sm->lock);

    gu_info ("Closed send monitor.");

    return 0;
}

extern void
gcs_sm_destroy (gcs_sm_t* sm)
{
    gu_mutex_destroy(&sm->lock);
    gu_free (sm);
}
