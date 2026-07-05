#include <qhw_admission/qhw_admission.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "check failed at %s:%d: %s\n", \
				__FILE__, __LINE__, #cond); \
			return 1; \
		} \
	} while (0)

static qhw_adm_baseline_t make_baseline(void)
{
	qhw_adm_baseline_t baseline = {
		.struct_size = sizeof(baseline),
		.qubit_count = 4,
		.depth = 10,
		.one_q_gate_count = 10,
		.two_q_gate_count = 5,
		.shots = 100,
		.measurement_count = 2,
	};

	return baseline;
}

static qhw_adm_device_profile_t make_profile(void)
{
	qhw_adm_device_profile_t profile = {
		.struct_size = sizeof(profile),
		.device_id = 7,
		.time_span_ns = 1000000000ULL,
		.baseline = make_baseline(),
		.max_qubits = 20,
		.max_shots = 10000,
		.one_q_gate_ns = 20,
		.two_q_gate_ns = 100,
		.measurement_ns = 1000,
		.one_q_gate_transfer_ns = 1,
		.two_q_gate_transfer_ns = 4,
		.measurement_transfer_ns = 10,
		.compile_ns = 1000,
		.control_overhead_ns = 200,
		.provider_overhead_ns = 300,
		.total_credits = 100,
		.device_rate = 50,
		.concurrent_jobs = 4,
		.default_ttl_ns = 60000000000ULL,
	};

	return profile;
}

static qhw_adm_qtask_class_t make_task(void)
{
	qhw_adm_qtask_class_t task = {
		.struct_size = sizeof(task),
		.class_id = 11,
		.count = 2,
		.qubit_count = 4,
		.depth = 12,
		.one_q_gate_count = 20,
		.two_q_gate_count = 10,
		.shots = 100,
		.measurement_count = 2,
	};

	return task;
}

static qhw_adm_request_t make_request(qhw_adm_qtask_class_t *task)
{
	qhw_adm_request_t request = {
		.struct_size = sizeof(request),
		.request_id = 42,
		.device_id = 7,
		.user_id = 1000,
		.job_id = 2000,
		.scope_id = 3,
		.workload_kind = QHW_ADM_WORKLOAD_HYBRID_JOB,
		.walltime_ns = 2000000000ULL,
		.ttl_ns = 1000000ULL,
		.task_class_count = 1,
		.task_classes = task,
	};

	return request;
}

static qhw_adm_decision_t make_decision_output(void)
{
	qhw_adm_decision_t decision;

	memset(&decision, 0, sizeof(decision));
	decision.struct_size = sizeof(decision);
	return decision;
}

static qhw_adm_capacity_view_t make_capacity_output(void)
{
	qhw_adm_capacity_view_t capacity;

	memset(&capacity, 0, sizeof(capacity));
	capacity.struct_size = sizeof(capacity);
	return capacity;
}

static qhw_adm_reservation_t make_reservation_output(void)
{
	qhw_adm_reservation_t reservation;

	memset(&reservation, 0, sizeof(reservation));
	reservation.struct_size = sizeof(reservation);
	return reservation;
}

static int check_string_metadata(
	const qhw_adm_kv_t *metadata,
	size_t metadata_count,
	uint64_t key,
	const char *value)
{
	if (metadata_count != 1 || metadata == NULL) {
		return 0;
	}
	if (metadata[0].key != key ||
	    metadata[0].value.type != QHW_ADM_VALUE_STRING ||
	    metadata[0].value.value.string == NULL ||
	    strcmp(metadata[0].value.value.string, value) != 0) {
		return 0;
	}

	return 1;
}

static int setup_context(qhw_adm_t **out_ctx)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_POLICY_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "mock", NULL, 0) ==
		QHW_ADM_OK);
	*out_ctx = ctx;
	return 0;
}

static int test_policy_load_and_evaluate(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_policy_t *policy = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();
	qhw_adm_device_profile_t stored = make_profile();
	const qhw_adm_kv_t *decision_metadata;
	const char *decision_message;

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_policy(ctx, QHW_ADM_TEST_POLICY_PATH, &policy) ==
		QHW_ADM_OK);
	CHECK(policy != NULL);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, request.device_id, "mock", NULL, 0) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.request_id == request.request_id);
	CHECK(decision.estimated_total_ns == 683160);
	CHECK(decision.capacity_granted == 3);
	CHECK(decision.reservation_id == 0);
	CHECK(strcmp(decision.message, "mock accepted") == 0);
	CHECK(check_string_metadata(
		decision.metadata,
		decision.metadata_count,
		1001,
		"decision-metadata"));
	decision_metadata = decision.metadata;
	decision_message = decision.message;
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(qhw_adm_get_device(ctx, request.device_id, &stored) ==
		QHW_ADM_OK);
	CHECK(strcmp(decision_message, "mock accepted") == 0);
	CHECK(check_string_metadata(
		decision_metadata,
		decision.metadata_count,
		1001,
		"decision-metadata"));
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_reserve_and_release(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_baseline_t baseline = make_baseline();
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_reservation_t reservation = make_reservation_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();

	CHECK(setup_context(&ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.reservation_id != 0);
	CHECK(qhw_adm_get_reservation(ctx, decision.reservation_id,
		&reservation) == QHW_ADM_OK);
	CHECK(reservation.state == QHW_ADM_RESERVATION_ACTIVE);
	CHECK(reservation.credits_reserved == 3);
	CHECK(reservation.rate_reserved == 3);
	CHECK(reservation.quantum_budget_ns == request.walltime_ns);
	CHECK(reservation.expires_at_ns > reservation.created_at_ns);
	CHECK(check_string_metadata(
		reservation.metadata,
		reservation.metadata_count,
		1002,
		"grant-metadata"));
	CHECK(qhw_adm_set_policy(ctx, request.device_id, "mock", NULL, 0) ==
		QHW_ADM_ERR_STATE);
	CHECK(qhw_adm_set_estimator(ctx, request.device_id, "baseline",
		NULL, 0) == QHW_ADM_ERR_STATE);
	CHECK(qhw_adm_set_baseline(ctx, request.device_id, &baseline) ==
		QHW_ADM_ERR_STATE);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_ERR_STATE);
	CHECK(qhw_adm_unregister_device(ctx, request.device_id) ==
		QHW_ADM_ERR_STATE);

	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.credits_reserved == 3);
	CHECK(capacity.scoped_reserved_credits == 3);
	CHECK(capacity.scheduler_policy_id == 1234);
	CHECK(check_string_metadata(
		capacity.metadata,
		capacity.metadata_count,
		1003,
		"capacity-metadata"));

	CHECK(qhw_adm_release(ctx, decision.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	reservation = make_reservation_output();
	CHECK(qhw_adm_get_reservation(ctx, decision.reservation_id,
		&reservation) == QHW_ADM_OK);
	CHECK(reservation.state == QHW_ADM_RESERVATION_RELEASED);
	CHECK(qhw_adm_release(ctx, decision.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_reserve_rejected_clears_error(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();

	request.request_id = 43;
	CHECK(setup_context(&ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_REJECTED);
	CHECK(decision.reservation_id == 0);
	CHECK(strcmp(decision.message, "mock rejected") == 0);
	CHECK(check_string_metadata(
		decision.metadata,
		decision.metadata_count,
		1001,
		"decision-metadata"));
	CHECK(strcmp(qhw_adm_last_error(ctx), "") == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_cancel_renew_and_expire(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_reservation_t reservation = make_reservation_output();
	size_t expired_count = 0;

	CHECK(setup_context(&ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(qhw_adm_renew(ctx, decision.reservation_id, 1000, 1000) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_expire(ctx, 1999, &expired_count) == QHW_ADM_OK);
	CHECK(expired_count == 0);
	CHECK(qhw_adm_expire(ctx, 2000, &expired_count) == QHW_ADM_OK);
	CHECK(expired_count == 1);
	CHECK(qhw_adm_get_reservation(ctx, decision.reservation_id,
		&reservation) == QHW_ADM_OK);
	CHECK(reservation.state == QHW_ADM_RESERVATION_EXPIRED);
	CHECK(qhw_adm_cancel(ctx, decision.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_ERR_STATE);
	qhw_adm_destroy(ctx);

	CHECK(setup_context(&ctx) == 0);
	decision = make_decision_output();
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(qhw_adm_cancel(ctx, decision.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	CHECK(qhw_adm_cancel(ctx, decision.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_config_policy_and_replace_guard(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();
	char config[2048];
	char replacement[1024];
	int written;

	written = snprintf(
		config,
		sizeof(config),
		"plugin_paths:\n"
		"  policies: [\"%s\"]\n"
		"  estimators: [\"%s\"]\n"
		"devices:\n"
		"  - device_id: 7\n"
		"    max_qubits: 20\n"
		"    max_shots: 10000\n"
		"    time_span_ns: 1000000000\n"
		"    baseline:\n"
		"      qubit_count: 4\n"
		"      depth: 10\n"
		"      one_q_gate_count: 10\n"
		"      two_q_gate_count: 5\n"
		"      measurement_count: 2\n"
		"      shots: 100\n"
		"    timing:\n"
		"      one_q_gate_ns: 20\n"
		"      two_q_gate_ns: 100\n"
		"      measurement_ns: 1000\n"
		"      one_q_gate_transfer_ns: 1\n"
		"      two_q_gate_transfer_ns: 4\n"
		"      measurement_transfer_ns: 10\n"
		"      compile_ns: 1000\n"
		"      control_overhead_ns: 200\n"
		"      provider_overhead_ns: 300\n"
		"    credit:\n"
		"      total_credits: 100\n"
		"    rate:\n"
		"      device_rate: 50\n"
		"      concurrent_jobs: 4\n"
		"    estimator:\n"
		"      name: mock\n"
		"    default_ttl_ns: 60000000000\n"
		"    policy:\n"
		"      name: mock\n",
		QHW_ADM_TEST_POLICY_DIR,
		QHW_ADM_TEST_PLUGIN_DIR);
	CHECK(written > 0 && (size_t)written < sizeof(config));

	written = snprintf(
		replacement,
		sizeof(replacement),
		"devices:\n"
		"  - device_id: 8\n"
		"    max_qubits: 20\n"
		"    max_shots: 10000\n"
		"    baseline:\n"
		"      qubit_count: 4\n"
		"      depth: 10\n"
		"      one_q_gate_count: 10\n"
		"      two_q_gate_count: 5\n"
		"      measurement_count: 2\n"
		"      shots: 100\n"
		"    timing:\n"
		"      one_q_gate_ns: 20\n"
		"      two_q_gate_ns: 100\n"
		"      measurement_ns: 1000\n");
	CHECK(written > 0 && (size_t)written < sizeof(replacement));

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(
		ctx,
		config,
		0,
		QHW_ADM_CONFIG_REPLACE) == QHW_ADM_OK);
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	decision = make_decision_output();
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(
		ctx,
		replacement,
		0,
		QHW_ADM_CONFIG_REPLACE) == QHW_ADM_ERR_STATE);
	CHECK(qhw_adm_release(ctx, decision.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_policy_validation(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_policy_t *policy = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_policy(ctx, QHW_ADM_TEST_BAD_CAPS_PATH, &policy) ==
		QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_BAD_GRANT_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "bad_grant",
		NULL, 0) == QHW_ADM_OK);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) ==
		QHW_ADM_ERR_POLICY);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_policy_error_output_is_cleared(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_ERROR_OUTPUT_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "error_output",
		NULL, 0) == QHW_ADM_OK);
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) ==
		QHW_ADM_ERR_POLICY);
	CHECK(decision.message == NULL);
	CHECK(decision.metadata == NULL);
	CHECK(decision.metadata_count == 0);

	decision = make_decision_output();
	CHECK(qhw_adm_reserve(ctx, &request, &decision) ==
		QHW_ADM_ERR_POLICY);
	CHECK(decision.message == NULL);
	CHECK(decision.metadata == NULL);
	CHECK(decision.metadata_count == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

int main(void)
{
	CHECK(test_policy_load_and_evaluate() == 0);
	CHECK(test_reserve_and_release() == 0);
	CHECK(test_reserve_rejected_clears_error() == 0);
	CHECK(test_cancel_renew_and_expire() == 0);
	CHECK(test_config_policy_and_replace_guard() == 0);
	CHECK(test_policy_validation() == 0);
	CHECK(test_policy_error_output_is_cleared() == 0);
	return 0;
}
