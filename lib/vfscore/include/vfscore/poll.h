/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Authors: Hugo Lefeuvre <hugo.lefeuvre@manchester.ac.uk>
 *
 * Copyright (c) 2020, NEC Laboratories Europe GmbH, NEC Corporation,
 *                     All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _VFSCORE_POLL_H_
#define _VFSCORE_POLL_H_

#include <poll.h> /* poll() and struct pollfd */

#define POLL_SET_ERRNO(errcode) (errno = -(errcode))

struct vfscore_file;

struct vfscore_poll_wtable {
	/* wait queue for the file descriptor */
	struct uk_waitq *wq;
	/* entry for this thread in the wait queue */
	struct uk_waitq_entry *entry;
	/* cleanup function for this subpoll */
	void (*cleanup)(void *);
	/* argument for the cleanup function */
	void *cleanup_arg;
	/* the wait table contains one wait queue per file descriptor */
	struct uk_list_head _list;
};

/**
 * Add a queue to the wait list. Note that we do not add the current thread to
 * the queue itself. This should be done all at once with interrupts disabled to
 * avoid race conditions (wakeup before block).
 */
static int vfscore_wtable_add_with_cleanup(struct uk_list_head *wtable,
		struct uk_waitq *wq, void (*cleanup)(void *), void *cleanup_arg)
{
	struct vfscore_poll_wtable *wtable_entry;

	/* Note: no need to zero-out memory, everything will be set soon */
	wtable_entry = uk_malloc(uk_alloc_get_default(),
				 sizeof(struct vfscore_poll_wtable));

	if (!wtable_entry) {
		return -ENOMEM;
	}

	wtable_entry->wq = wq;
	wtable_entry->cleanup = cleanup;
	wtable_entry->cleanup_arg = cleanup_arg;

	/* Note: only one uk_waitq_entry per thread, set by the main poll
	 * implementation. */

	uk_list_add(&wtable_entry->_list, wtable);

	return 0;
}

static int vfscore_wtable_add(struct uk_list_head *wtable,
		struct uk_waitq *wq)
{
	return vfscore_wtable_add_with_cleanup(wtable, wq, NULL, NULL);
}

static int
vfscore_nopoll(struct vnode * vn __unused, struct vfscore_file * vf __unused,
	short events __unused, struct uk_list_head * wait_table __unused,
	int addq __unused)
{
	POLL_SET_ERRNO(-EBADF);
	return -1;
}

#endif /* _VFSCORE_POLL_H_ */
