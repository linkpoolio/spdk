/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2021 Intel Corporation.
 *   All rights reserved.
 *   Copyright (c) 2021, 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 */

#include "spdk/stdinc.h"
#include "spdk_internal/cunit.h"
#include "nvme/nvme_transport.c"
#include "common/lib/ut_multithread.c"

SPDK_LOG_REGISTER_COMPONENT(nvme)

DEFINE_STUB(nvme_poll_group_connect_qpair, int, (struct spdk_nvme_qpair *qpair), 0);
DEFINE_STUB_V(nvme_qpair_abort_all_queued_reqs, (struct spdk_nvme_qpair *qpair));
DEFINE_STUB(nvme_poll_group_disconnect_qpair, int, (struct spdk_nvme_qpair *qpair), 0);
DEFINE_STUB(spdk_nvme_ctrlr_free_io_qpair, int, (struct spdk_nvme_qpair *qpair), 0);
DEFINE_STUB(spdk_nvme_transport_id_trtype_str, const char *,
	    (enum spdk_nvme_transport_type trtype), NULL);
DEFINE_STUB(spdk_nvme_qpair_process_completions, int32_t, (struct spdk_nvme_qpair *qpair,
		uint32_t max_completions), 0);
DEFINE_STUB(spdk_nvme_poll_group_process_completions, int64_t, (struct spdk_nvme_poll_group *group,
		uint32_t completions_per_qpair,
		spdk_nvme_disconnected_qpair_cb disconnected_qpair_cb), 0);
DEFINE_STUB(nvme_ctrlr_get_current_process, struct spdk_nvme_ctrlr_process *,
	    (struct spdk_nvme_ctrlr *ctrlr), NULL);
DEFINE_STUB(spdk_nvme_ctrlr_is_fabrics, bool, (struct spdk_nvme_ctrlr *ctrlr), false);

static void
ut_construct_transport(struct spdk_nvme_transport *transport, const char name[])
{
	memcpy(transport->ops.name, name, strlen(name));
	TAILQ_INSERT_TAIL(&g_spdk_nvme_transports, transport, link);
}

static void
test_nvme_get_transport(void)
{
	const struct spdk_nvme_transport *nvme_transport = NULL;
	struct spdk_nvme_transport new_transport = {};

	ut_construct_transport(&new_transport, "new_transport");

	nvme_transport = nvme_get_transport("new_transport");
	CU_ASSERT(nvme_transport == &new_transport);
	TAILQ_REMOVE(&g_spdk_nvme_transports, nvme_transport, link);
	CU_ASSERT(TAILQ_EMPTY(&g_spdk_nvme_transports));

	/* Unavailable transport entry */
	nvme_transport = nvme_get_transport("new_transport");
	SPDK_CU_ASSERT_FATAL(nvme_transport == NULL);
}

static int
ut_poll_group_connect_qpair(struct spdk_nvme_qpair *qpair)
{
	return 0;
}

static void
test_nvme_transport_poll_group_connect_qpair(void)
{
	int rc;
	struct spdk_nvme_qpair qpair = {};
	struct spdk_nvme_transport_poll_group	tgroup = {};
	struct spdk_nvme_transport transport = {};

	qpair.poll_group = &tgroup;
	tgroup.transport = &transport;
	transport.ops.poll_group_connect_qpair = ut_poll_group_connect_qpair;
	STAILQ_INIT(&tgroup.connected_qpairs);
	STAILQ_INIT(&tgroup.disconnected_qpairs);

	/* Connected qpairs */
	qpair.poll_group_tailq_head = &tgroup.connected_qpairs;

	rc = nvme_transport_poll_group_connect_qpair(&qpair);
	CU_ASSERT(rc == 0);

	/* Disconnected qpairs */

	qpair.poll_group_tailq_head = &tgroup.disconnected_qpairs;
	STAILQ_INSERT_TAIL(&tgroup.disconnected_qpairs, &qpair, poll_group_stailq);

	rc = nvme_transport_poll_group_connect_qpair(&qpair);
	CU_ASSERT(rc == 0);
	CU_ASSERT(STAILQ_EMPTY(&tgroup.disconnected_qpairs));
	CU_ASSERT(!STAILQ_EMPTY(&tgroup.connected_qpairs));
	STAILQ_REMOVE(&tgroup.connected_qpairs, &qpair, spdk_nvme_qpair, poll_group_stailq);
	CU_ASSERT(STAILQ_EMPTY(&tgroup.connected_qpairs));

	/* None qpairs */
	qpair.poll_group_tailq_head = NULL;

	rc = nvme_transport_poll_group_connect_qpair(&qpair);
	SPDK_CU_ASSERT_FATAL(rc == -EINVAL);
}

static int
ut_poll_group_disconnect_qpair(struct spdk_nvme_qpair *qpair)
{
	return 0;
}

static void
test_nvme_transport_poll_group_disconnect_qpair(void)
{
	int rc;
	struct spdk_nvme_qpair qpair = {};
	struct spdk_nvme_transport_poll_group	tgroup = {};
	struct spdk_nvme_transport transport = {};

	qpair.poll_group = &tgroup;
	tgroup.transport = &transport;
	transport.ops.poll_group_disconnect_qpair = ut_poll_group_disconnect_qpair;
	STAILQ_INIT(&tgroup.connected_qpairs);
	STAILQ_INIT(&tgroup.disconnected_qpairs);

	/* Disconnected qpairs */
	qpair.poll_group_tailq_head = &tgroup.disconnected_qpairs;

	rc = nvme_transport_poll_group_disconnect_qpair(&qpair);
	CU_ASSERT(rc == 0);

	/* Connected qpairs */
	qpair.poll_group_tailq_head = &tgroup.connected_qpairs;
	STAILQ_INSERT_TAIL(&tgroup.connected_qpairs, &qpair, poll_group_stailq);
	tgroup.num_connected_qpairs++;

	rc = nvme_transport_poll_group_disconnect_qpair(&qpair);
	CU_ASSERT(rc == 0);
	CU_ASSERT(STAILQ_EMPTY(&tgroup.connected_qpairs));
	CU_ASSERT(tgroup.num_connected_qpairs == 0);
	CU_ASSERT(!STAILQ_EMPTY(&tgroup.disconnected_qpairs));
	STAILQ_REMOVE(&tgroup.disconnected_qpairs, &qpair, spdk_nvme_qpair, poll_group_stailq);
	CU_ASSERT(STAILQ_EMPTY(&tgroup.disconnected_qpairs));

	/* None qpairs */
	qpair.poll_group_tailq_head = NULL;

	rc = nvme_transport_poll_group_disconnect_qpair(&qpair);
	SPDK_CU_ASSERT_FATAL(rc == -EINVAL);
}

static int
ut_poll_group_add_remove(struct spdk_nvme_transport_poll_group *tgroup,
			 struct spdk_nvme_qpair *qpair)
{
	return 0;
}

static void
test_nvme_transport_poll_group_add_remove(void)
{
	int rc;
	struct spdk_nvme_transport_poll_group tgroup = {};
	struct spdk_nvme_qpair qpair = {};
	const struct spdk_nvme_transport transport = {
		.ops.poll_group_add = ut_poll_group_add_remove,
		.ops.poll_group_remove = ut_poll_group_add_remove
	};

	tgroup.transport = &transport;
	qpair.poll_group = &tgroup;
	qpair.state = NVME_QPAIR_DISCONNECTED;
	STAILQ_INIT(&tgroup.connected_qpairs);
	STAILQ_INIT(&tgroup.disconnected_qpairs);

	/* Add qpair */
	rc = nvme_transport_poll_group_add(&tgroup, &qpair);
	CU_ASSERT(rc == 0);
	CU_ASSERT(qpair.poll_group_tailq_head == &tgroup.disconnected_qpairs);
	CU_ASSERT(STAILQ_FIRST(&tgroup.disconnected_qpairs) == &qpair);

	/*  Remove disconnected_qpairs */
	SPDK_CU_ASSERT_FATAL(!STAILQ_EMPTY(&tgroup.disconnected_qpairs));

	rc = nvme_transport_poll_group_remove(&tgroup, &qpair);
	CU_ASSERT(rc == 0);
	CU_ASSERT(STAILQ_EMPTY(&tgroup.disconnected_qpairs));
	CU_ASSERT(qpair.poll_group == NULL);
	CU_ASSERT(qpair.poll_group_tailq_head == NULL);

	/* Remove connected_qpairs */
	qpair.poll_group_tailq_head = &tgroup.connected_qpairs;
	STAILQ_INSERT_TAIL(&tgroup.connected_qpairs, &qpair, poll_group_stailq);

	rc = nvme_transport_poll_group_remove(&tgroup, &qpair);
	CU_ASSERT(rc == -EINVAL);

	STAILQ_REMOVE(&tgroup.connected_qpairs, &qpair, spdk_nvme_qpair, poll_group_stailq);

	/* Invalid qpair */
	qpair.poll_group_tailq_head = NULL;

	rc = nvme_transport_poll_group_remove(&tgroup, &qpair);
	CU_ASSERT(rc == -ENOENT);
}

static int
g_ut_ctrlr_get_memory_domains(const struct spdk_nvme_ctrlr *ctrlr,
			      struct spdk_memory_domain **domains, int array_size)
{
	return 1;
}

static void
test_ctrlr_get_memory_domains(void)
{
	struct spdk_nvme_ctrlr ctrlr = {
		.trid = {
			.trstring = "new_transport"
		}
	};
	struct spdk_nvme_transport new_transport = {
		.ops = { .ctrlr_get_memory_domains = g_ut_ctrlr_get_memory_domains }
	};

	ut_construct_transport(&new_transport, "new_transport");

	/* transport contains necessary op */
	CU_ASSERT(nvme_transport_ctrlr_get_memory_domains(&ctrlr, NULL, 0) == 1);

	/* transport doesn't contain necessary op */
	new_transport.ops.ctrlr_get_memory_domains = NULL;
	CU_ASSERT(nvme_transport_ctrlr_get_memory_domains(&ctrlr, NULL, 0) == 0);

	TAILQ_REMOVE(&g_spdk_nvme_transports, &new_transport, link);
}


static void
ut_connect_cb(struct spdk_nvme_qpair *qpair, int status, void *cb_arg)
{
	int *out = cb_arg;

	*out = status;
}

/* The async-connect poller holds a raw pointer to its qpair. A qpair that
 * fails and is destroyed between poller ticks must not leave the poller
 * running (production crash: tick dereferenced the freed qpair). Ownership is
 * via qpair->connect_ctx: completion and destruction both clear it.
 */
static void
test_qpair_async_connect_ownership(void)
{
	struct spdk_nvme_ctrlr ctrlr = {};
	struct spdk_nvme_transport_poll_group tgroup = {};
	struct spdk_nvme_poll_group group = {};
	struct spdk_nvme_qpair qpair = {};
	int status;

	allocate_threads(1);
	set_thread(0);

	tgroup.group = &group;
	qpair.ctrlr = &ctrlr;
	qpair.poll_group = &tgroup;

	/* Torn down between ticks: the state check completes with -ENXIO instead
	 * of processing completions on a dead qpair.
	 */
	status = 1;
	nvme_qpair_set_state(&qpair, NVME_QPAIR_CONNECTING);
	CU_ASSERT(start_async_qpair_connect(&qpair, ut_connect_cb, &status) == 0);
	SPDK_CU_ASSERT_FATAL(qpair.connect_ctx != NULL);
	nvme_qpair_set_state(&qpair, NVME_QPAIR_DISCONNECTING);
	spdk_delay_us(1000);
	poll_threads();
	CU_ASSERT(status == -ENXIO);
	CU_ASSERT(qpair.connect_ctx == NULL);

	/* Successful connect completes with 0 and releases ownership. */
	status = 1;
	nvme_qpair_set_state(&qpair, NVME_QPAIR_CONNECTING);
	CU_ASSERT(start_async_qpair_connect(&qpair, ut_connect_cb, &status) == 0);
	nvme_qpair_set_state(&qpair, NVME_QPAIR_CONNECTED);
	spdk_delay_us(1000);
	poll_threads();
	CU_ASSERT(status == 0);
	CU_ASSERT(qpair.connect_ctx == NULL);

	/* Destruction path: abort cancels the poller without invoking the
	 * callback; a subsequent poll must be a no-op.
	 */
	status = 1;
	nvme_qpair_set_state(&qpair, NVME_QPAIR_CONNECTING);
	CU_ASSERT(start_async_qpair_connect(&qpair, ut_connect_cb, &status) == 0);
	SPDK_CU_ASSERT_FATAL(qpair.connect_ctx != NULL);
	nvme_qpair_abort_async_connect(&qpair);
	CU_ASSERT(qpair.connect_ctx == NULL);
	spdk_delay_us(1000);
	poll_threads();
	CU_ASSERT(status == 1);

	/* Abort is idempotent on a qpair with no in-flight connect. */
	nvme_qpair_abort_async_connect(&qpair);
	CU_ASSERT(qpair.connect_ctx == NULL);

	/* A reconnect supersedes a stale in-flight connect context: the old
	 * poller is cancelled (no double completion -- the callback must fire
	 * exactly once) and the new context completes normally.
	 */
	status = 1;
	nvme_qpair_set_state(&qpair, NVME_QPAIR_CONNECTING);
	CU_ASSERT(start_async_qpair_connect(&qpair, ut_connect_cb, &status) == 0);
	SPDK_CU_ASSERT_FATAL(qpair.connect_ctx != NULL);
	CU_ASSERT(start_async_qpair_connect(&qpair, ut_connect_cb, &status) == 0);
	SPDK_CU_ASSERT_FATAL(qpair.connect_ctx != NULL);
	nvme_qpair_set_state(&qpair, NVME_QPAIR_CONNECTED);
	spdk_delay_us(1000);
	poll_threads();
	CU_ASSERT(status == 0);
	CU_ASSERT(qpair.connect_ctx == NULL);
	status = 1;
	spdk_delay_us(2000);
	poll_threads();
	CU_ASSERT(status == 1);

	free_threads();
}

int
main(int argc, char **argv)
{
	CU_pSuite	suite = NULL;
	unsigned int	num_failures;

	CU_initialize_registry();

	suite = CU_add_suite("nvme_transport", NULL, NULL);
	CU_ADD_TEST(suite, test_nvme_get_transport);
	CU_ADD_TEST(suite, test_nvme_transport_poll_group_connect_qpair);
	CU_ADD_TEST(suite, test_nvme_transport_poll_group_disconnect_qpair);
	CU_ADD_TEST(suite, test_nvme_transport_poll_group_add_remove);
	CU_ADD_TEST(suite, test_ctrlr_get_memory_domains);
	CU_ADD_TEST(suite, test_qpair_async_connect_ownership);

	num_failures = spdk_ut_run_tests(argc, argv, NULL);
	CU_cleanup_registry();
	return num_failures;
}
