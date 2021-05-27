/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Authors: Alexander Jung <a.jung@lancs.ac.uk>
 *
 * Copyright (c) 2021, Lancaster University.  All rights reserved.
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

#include <stdio.h>
#include <limits.h>

#include <uk/test.h>
#include <uk/list.h>
#include <uk/assert.h>
#include <uk/config.h>
#include <uk/essentials.h>

struct uk_testsuite_list uk_testsuite_list =
	UK_TAILQ_HEAD_INITIALIZER(uk_testsuite_list);
uint16_t testsuite_count;

int
uk_testsuite_add(struct uk_testsuite *suite)
{
	UK_ASSERT(suite);
	UK_TAILQ_INSERT_TAIL(&uk_testsuite_list, suite, next);

	return testsuite_count++;
}

int
uk_testsuite_run(struct uk_testsuite *suite)
{
	int ret = 0;
	struct uk_testcase *testcase;

	if (suite->init) {
		ret = suite->init(suite);

		if (ret != 0) {
			uk_pr_err("Could not initialize test suite: %s",
				  suite->name);
			goto ERR_EXIT;
		}
	}

	/* Reset the number of failed cases before running each case. */
	suite->failed_cases = 0;

	uk_testsuite_for_each_case(suite, testcase) {
#ifdef CONFIG_LIBUKTEST_LOG_TESTS
		uk_printd(LVLC_TESTNAME "test:" UK_ANSI_MOD_RESET
			  " [%s:%s]\n",
			  suite->name,
			  testcase->name
		);
#endif /* CONFIG_LIBUKTEST_LOG_TESTS */

		testcase->func(testcase);

		/* If one case fails, the whole suite fails. */
		if (testcase->failed_asserts > 0)
			suite->failed_cases++;
	}

ERR_EXIT:
	return ret;
}
