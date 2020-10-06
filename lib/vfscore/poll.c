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

#include <vfscore/file.h>
#include <vfscore/poll.h>
#include <vfscore/vnode.h> /* VOP_POLL */
#include <uk/errptr.h> /* PTR2ERR */
#include <uk/wait.h> /* waitq */
#include <uk/thread.h> /* uk_thread_current */
#include <uk/plat/lcpu.h> /* ukplat_lcpu_(enable/disable)_irq */

/* TODO global description of this poll implementation */
/* TODO rework comments */

int poll_scan(struct pollfd *pfd, struct uk_list_head *wtable, int addq) {
	struct vfscore_file *vfs;
	struct vnode *vnode;
	int ret;

	/* retrieve vfscore file */
	vfs = vfscore_get_file(pfd->fd);

	if (!vfs) {
		ret = -1;
		POLL_SET_ERRNO(PTR2ERR(vfs));
		uk_pr_warn("poll() can't get file with fd %d\n", pfd->fd);
		goto end;
	}

	vnode = vfs->f_dentry->d_vnode;

	/* execute poll for this file */
	ret = VOP_POLL(vnode, vfs, pfd->events, wtable, addq);

	/* release refcount */
	vfscore_put_file(vfs);

       /* update pollfd */
	if (ret > 0)
		pfd->revents = ret;

end:
	return ret;
}

#define add_and_block(current) \
	__add_and_block_deadline(current, 0, 0)

#define add_and_block_deadline(current, deadline) \
	__add_and_block_deadline(current, (deadline), \
		(deadline) && ukplat_monotonic_clock() >= (deadline))

#define __add_and_block_deadline(current, deadline, deadline_condition) \
do {									\
	unsigned long flags, added = 0;					\
	struct vfscore_poll_wtable *iter;				\
	DEFINE_WAIT(__wait);						\
	struct uk_thread *__current;					\
									\
	for (;;) {							\
		__current = uk_thread_current();			\
		flags = ukplat_lcpu_save_irqf();			\
		if (!added) {						\
			added = 1;					\
			/* add to wait queues */			\
			uk_list_for_each_entry(iter, &wtable, _list) {	\
				iter->entry = &__wait;			\
				uk_waitq_add(iter->wq, iter->entry);	\
			}						\
		}							\
									\
		/* block */						\
		__current->wakeup_time = deadline;			\
		clear_runnable(__current);				\
                uk_sched_thread_blocked(__current->sched, __current);	\
		ukplat_lcpu_restore_irqf(flags);			\
		uk_sched_yield();					\
									\
		/* can we stop yet? */					\
		ret = 0;						\
		for (size_t i = 0; i < nfds; i++) {			\
			if (poll_scan(&fds[i], NULL, 0) > 0)		\
				ret++;					\
		}							\
									\
		if ((ret) || (deadline_condition))			\
			break;						\
	}								\
									\
	flags = ukplat_lcpu_save_irqf();				\
	uk_thread_wake(__current);					\
									\
	/* remove from wait queues */					\
	uk_list_for_each_entry(iter, &wtable, _list) {			\
		uk_waitq_remove(iter->wq, iter->entry);			\
		if (iter->cleanup)					\
			iter->cleanup(iter->cleanup_arg);		\
		uk_list_del(&iter->_list);				\
		free(iter);						\
	}								\
									\
	ukplat_lcpu_restore_irqf(flags);				\
} while (0)

/**
 * @timeout: minimum number of milliseconds that poll() will block. Negative
 * value in timeout means an infinite timeout. Zero timeout forces poll() to
 * return immediately.
 *
 * Return a positive number on success (number of structures with non-zero
 * revents). Return value 0 indicates a timeout. Return value -1 indicates an
 * error; errno is set appropriately.
 */
int poll(struct pollfd fds[], nfds_t nfds, int timeout) {
	struct uk_list_head wtable;
	int ret = 0;

	UK_INIT_LIST_HEAD(&wtable);

	if (!fds) {
		errno = EFAULT;
		ret = -1;
		goto end;
	}

	/* Pre-scan: some fds might already be ready */
	for (size_t i = 0; i < nfds; i++) {
		int success = poll_scan(&fds[i], &wtable, timeout ? 1 : 0);

		if (success < 0) {
                       uk_pr_warn("sub-poll(), fd %d returned with errno %d\n",
                                       fds[i].fd, errno);
			goto end;
		} else if (success > 0) {
			uk_pr_debug("sub-poll(), fd %d is ready\n",
					fds[i].fd);
			ret++;
		}
	}

	/* If we are already ready, return now. */
	if (ret) {
		goto end;
	}

	if (!timeout) {
		/* do not block, return now. */
		uk_pr_debug("poll() exiting, no timeout\n");
		goto end;
	} else if (timeout > 0) {
		__nsec deadline = ukplat_monotonic_clock() + timeout * 1000;
		add_and_block_deadline(current, deadline);
	} else {
		/* no timeout */
		add_and_block(current);
	}

end:
	uk_pr_debug("poll() returning %d\n", ret);
	return ret;
}
