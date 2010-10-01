/**
   src/vpu5/shvpu5_common_queue.c

   Implements a simple FIFO structure 

   Copyright (C) 2010 IGEL Co., Ltd

   Copyright (C) 2007-2009 STMicroelectronics
   Copyright (C) 2007-2009 Nokia Corporation and/or its subsidiary(-ies).

   This library is free software; you can redistribute it and/or modify it under
   the terms of the GNU Lesser General Public License as published by the Free
   Software Foundation; either version 2.1 of the License, or (at your option)
   any later version.

   This library is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
   FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
   details.

   You should have received a copy of the GNU Lesser General Public License
   along with this library; if not, write to the Free Software Foundation, Inc.,
   51 Franklin St, Fifth Floor, Boston, MA
   02110-1301  USA

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "queue.h"
#include "omx_comp_debug_levels.h"

/** Initialize a queue descriptor
 *
 * @param queue The queue descriptor to initialize.
 * The user needs to allocate the queue
 */
int shvpu_queue_init(queue_t* queue) {
	int i;
	qelem_t* newelem;
	qelem_t* current;
	i = pthread_mutex_init(&queue->mutex, NULL);
	if (i!=0) {
		return -1;
	}
	queue->last = queue->first = NULL;
	queue->nelem = 0;
	return 0;
}

/** Deinitialize a queue descriptor
 * flushing all of its internal data
 *
 * @param queue the queue descriptor to dump
 */
void shvpu_queue_deinit(queue_t* queue) {
	qelem_t* qelem;

	pthread_mutex_lock(&queue->mutex);
	while (queue->nelem > 0) {
		qelem = queue->first;
		queue->first = qelem->q_forw;
		free(qelem);
		queue->nelem--;
	}
	queue->first = queue->last = NULL;
	pthread_mutex_unlock(&queue->mutex);
	pthread_mutex_destroy(&queue->mutex);
}

/** Enqueue an element to the given queue descriptor
 *
 * @param queue the queue descriptor where to queue data
 *
 * @param data the data to be enqueued
 *
 * @return -1 if the queue is full
 */
int shvpu_queue(queue_t* queue, void* data) {
	qelem_t* newelem;

	if (data == NULL)
		return -1;
	newelem = calloc(1, sizeof(qelem_t));
	if (newelem == NULL)
		return -1;
	memset(newelem, 0, sizeof(qelem_t));
	pthread_mutex_lock(&queue->mutex);
	if (queue->first == NULL) {
		queue->last = queue->first = newelem;
		newelem->q_forw = newelem;
	} else {
		newelem->q_forw = queue->last->q_forw;
		queue->last->q_forw = newelem;
		queue->last = newelem;
	}
	newelem->data = data;
	queue->nelem++;
	pthread_mutex_unlock(&queue->mutex);

	return 0;
}

/** Dequeue an element from the given queue descriptor
 *
 * @param queue the queue descriptor from which to dequeue the element
 *
 * @return the element that has bee dequeued. If the queue is empty
 *  a NULL value is returned
 */
void* shvpu_dequeue(queue_t* queue) {
	qelem_t* qelem;
	void* data;

	pthread_mutex_lock(&queue->mutex);
	if (queue->nelem == 0) {
		pthread_mutex_unlock(&queue->mutex);
		return NULL;
	}
	qelem = queue->first;
	data = qelem->data;
	if (queue->first != queue->last) {
		queue->first = queue->first->q_forw;
		queue->last->q_forw = queue->first;
	} else {
		queue->first = queue->last = NULL;
	}
	free(qelem);
	queue->nelem--;
	pthread_mutex_unlock(&queue->mutex);

	return data;
}

/** Returns the number of elements hold in the queue
 *
 * @param queue the requested queue
 *
 * @return the number of elements in the queue
 */
int shvpu_getquenelem(queue_t* queue) {
	int qelem;
	pthread_mutex_lock(&queue->mutex);
	qelem = queue->nelem;
	pthread_mutex_unlock(&queue->mutex);
	return qelem;
}
