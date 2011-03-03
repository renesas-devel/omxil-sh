/**
   src/vpu5/shvpu5_common_queue.h

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

#include "queue.h"

/** Initialize a queue descriptor
 *
 * @param queue The queue descriptor to initialize.
 * The user needs to allocate the queue
 */
int shvpu_queue_init(queue_t* queue);

/** Deinitialize a queue descriptor
 * flushing all of its internal data
 *
 * @param queue the queue descriptor to dump
 */
void shvpu_queue_deinit(queue_t* queue);

/** Enqueue an element to the given queue descriptor
 *
 * @param queue the queue descriptor where to queue data
 *
 * @param data the data to be enqueued
 *
 * @return -1 if the queue is full
 */
int shvpu_queue(queue_t* queue, void* data);

/** Dequeue an element from the given queue descriptor
 *
 * @param queue the queue descriptor from which to dequeue the element
 *
 * @return the element that has bee dequeued. If the queue is empty
 *  a NULL value is returned
 */
void* shvpu_dequeue(queue_t* queue);

/** Peek an element from the given queue descriptor
 *
 * @param queue the queue descriptor from which to peek the element
 *
 * @return the element that has not been dequeued. If the queue is empty
 *  a NULL value is returned
 */
void* shvpu_peek(queue_t* queue);

/** Returns the number of elements held in the queue
 *
 * @param queue the requested queue
 *
 * @return the number of elements in the queue
 */
int shvpu_getquenelem(queue_t* queue);
