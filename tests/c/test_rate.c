#include <qhw_admission/qhw_admission.h>

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

struct capacity_state {
	uint64_t external_rate_limit;
};

static qhw_adm_rc_t capacity_snapshot(
	uint64_t device_id,
	uint64_t scope_id,
	qhw_adm_capacity_snapshot_t *out_snapshot,
	void *user_data)
{
	struct capacity_state *state = user_data;

	if (state == NULL || out_snapshot == NULL ||
	    out_snapshot->struct_size < sizeof(*out_snapshot)) {
		return QHW_ADM_ERR_INVAL;
	}

	out_snapshot->device_id = device_id;
	out_snapshot->scope_id = scope_id;
	out_snapshot->external_rate_limit = state->external_rate_limit;
	return QHW_ADM_OK;
}

static qhw_adm_capacity_provider_t make_capacity_provider(
	struct capacity_state *state)
{
	qhw_adm_capacity_provider_t provider = {
		.struct_size = sizeof(provider),
		.get_snapshot = capacity_snapshot,
		.user_data = state,
	};

	return provider;
}

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

static qhw_adm_device_profile_t make_profile(
	uint64_t device_rate,
	uint32_t concurrent_jobs,
	uint64_t time_span_ns)
{
	qhw_adm_device_profile_t profile = {
		.struct_size = sizeof(profile),
		.device_id = 7,
		.time_span_ns = time_span_ns,
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
		.device_rate = device_rate,
		.concurrent_jobs = concurrent_jobs,
		.default_ttl_ns = 60000000000ULL,
	};

	return profile;
}

static qhw_adm_qtask_class_t make_task(uint64_t count)
{
	qhw_adm_qtask_class_t task = {
		.struct_size = sizeof(task),
		.class_id = 11,
		.count = count,
		.qubit_count = 4,
		.depth = 12,
		.one_q_gate_count = 20,
		.two_q_gate_count = 10,
		.shots = 100,
		.measurement_count = 2,
	};

	return task;
}

static qhw_adm_request_t make_request(
	qhw_adm_qtask_class_t *task,
	uint64_t request_id,
	uint64_t walltime_ns)
{
	qhw_adm_request_t request = {
		.struct_size = sizeof(request),
		.request_id = request_id,
		.device_id = 7,
		.user_id = 1000,
		.job_id = 2000,
		.scope_id = 3,
		.workload_kind = QHW_ADM_WORKLOAD_HYBRID_JOB,
		.walltime_ns = walltime_ns,
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

static qhw_adm_usage_state_t make_usage_output(void)
{
	qhw_adm_usage_state_t usage;

	memset(&usage, 0, sizeof(usage));
	usage.struct_size = sizeof(usage);
	return usage;
}

static qhw_adm_usage_t make_rate_usage(
	uint64_t task_id,
	uint64_t event_time_ns,
	uint64_t rate_units)
{
	qhw_adm_usage_t usage = {
		.struct_size = sizeof(usage),
		.task_id = task_id,
		.class_id = 11,
		.event_time_ns = event_time_ns,
		.rate_units = rate_units,
	};

	return usage;
}

static int setup_context_with_profile(
	const qhw_adm_device_profile_t *profile,
	const qhw_adm_kv_t *options,
	size_t option_count,
	qhw_adm_t **out_ctx)
{
	qhw_adm_t *ctx = NULL;

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_RATE_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile->device_id, "rate",
		options, option_count) == QHW_ADM_OK);
	*out_ctx = ctx;
	return 0;
}

static int setup_context(
	uint64_t device_rate,
	uint32_t concurrent_jobs,
	uint64_t time_span_ns,
	qhw_adm_t **out_ctx)
{
	qhw_adm_device_profile_t profile;

	profile = make_profile(device_rate, concurrent_jobs, time_span_ns);
	return setup_context_with_profile(&profile, NULL, 0, out_ctx);
}

static int test_exact_slice_and_release(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42, 2000000000ULL);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_reservation_t reservation = make_reservation_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();

	CHECK(setup_context(4, 2, 1000000000ULL, &ctx) == 0);
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.rate_required == 2);
	CHECK(decision.capacity_available == 4);
	CHECK(decision.capacity_granted == 2);
	CHECK(strcmp(decision.message, "accepted by rate policy") == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.reservation_id != 0);
	CHECK(qhw_adm_get_reservation(ctx, decision.reservation_id,
		&reservation) == QHW_ADM_OK);
	CHECK(reservation.rate_reserved == 2);
	CHECK(reservation.quantum_budget_ns == 2000000000ULL);
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.total_rate == 4);
	CHECK(capacity.rate_reserved == 2);
	CHECK(capacity.core_available_rate == 2);
	CHECK(capacity.effective_available_rate == 2);
	CHECK(qhw_adm_release(ctx, decision.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	capacity = make_capacity_output();
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.rate_reserved == 0);
	CHECK(capacity.effective_available_rate == 4);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_multi_slice_admission(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42, 1000000000ULL);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_reservation_t reservation = make_reservation_output();

	CHECK(setup_context(4, 2, 1000000000ULL, &ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.rate_required == 3);
	CHECK(decision.capacity_granted == 4);
	CHECK(qhw_adm_get_reservation(ctx, decision.reservation_id,
		&reservation) == QHW_ADM_OK);
	CHECK(reservation.rate_reserved == 4);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_derived_rate_capacity(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile;
	qhw_adm_capacity_view_t capacity = make_capacity_output();

	profile = make_profile(0, 3, 814650);
	CHECK(setup_context_with_profile(&profile, NULL, 0, &ctx) == 0);
	CHECK(qhw_adm_get_capacity(ctx, profile.device_id, 3, &capacity) ==
		QHW_ADM_OK);
	CHECK(capacity.total_rate == 3);
	CHECK(capacity.effective_available_rate == 3);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_delayed_when_rate_is_reserved(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t first = make_request(&task, 42, 1000000000ULL);
	qhw_adm_request_t second = make_request(&task, 43, 1000000000ULL);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(setup_context(4, 2, 1000000000ULL, &ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &first, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);

	decision = make_decision_output();
	CHECK(qhw_adm_evaluate(ctx, &second, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_DELAYED);
	CHECK(decision.reason_code == QHW_ADM_REASON_INSUFFICIENT_RATE);
	CHECK(decision.rate_required == 3);
	CHECK(decision.capacity_available == 0);
	CHECK(strcmp(decision.message, "insufficient rate") == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_scoped_external_rate_cap(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t first = make_request(&task, 42, 2000000000ULL);
	qhw_adm_request_t second = make_request(&task, 43, 2000000000ULL);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();
	struct capacity_state state = {
		.external_rate_limit = 4,
	};
	qhw_adm_capacity_provider_t provider = make_capacity_provider(&state);

	CHECK(setup_context(8, 2, 1000000000ULL, &ctx) == 0);
	CHECK(qhw_adm_set_capacity_provider(ctx, &provider) == QHW_ADM_OK);
	CHECK(qhw_adm_reserve(ctx, &first, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.capacity_granted == 4);

	CHECK(qhw_adm_get_capacity(ctx, first.device_id, first.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.core_available_rate == 4);
	CHECK(capacity.scoped_reserved_rate == 4);
	CHECK(capacity.effective_available_rate == 0);

	decision = make_decision_output();
	CHECK(qhw_adm_evaluate(ctx, &second, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_DELAYED);
	CHECK(decision.capacity_available == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_oversized_request_is_rejected(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42, 1000000000ULL);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(setup_context(2, 2, 1000000000ULL, &ctx) == 0);
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_REJECTED);
	CHECK(decision.reason_code == QHW_ADM_REASON_REQUEST_TOO_LARGE);
	CHECK(decision.rate_required == 3);
	CHECK(strcmp(decision.message,
		"rate request exceeds policy limit") == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_cancel_and_expire_return_capacity(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42, 2000000000ULL);
	qhw_adm_decision_t cancelled = make_decision_output();
	qhw_adm_decision_t expired = make_decision_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();
	qhw_adm_reservation_t reservation = make_reservation_output();
	size_t expired_count = 0;

	CHECK(setup_context(4, 2, 1000000000ULL, &ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &cancelled) == QHW_ADM_OK);
	CHECK(qhw_adm_cancel(ctx, cancelled.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.rate_reserved == 0);

	request.request_id++;
	CHECK(qhw_adm_reserve(ctx, &request, &expired) == QHW_ADM_OK);
	CHECK(qhw_adm_renew(ctx, expired.reservation_id, 1000, 1000) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_expire(ctx, 2000, &expired_count) == QHW_ADM_OK);
	CHECK(expired_count == 1);
	CHECK(qhw_adm_get_reservation(ctx, expired.reservation_id,
		&reservation) == QHW_ADM_OK);
	CHECK(reservation.state == QHW_ADM_RESERVATION_EXPIRED);
	capacity = make_capacity_output();
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.rate_reserved == 0);
	CHECK(capacity.effective_available_rate == 4);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_usage_windows_follow_event_time(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42, 2000000000ULL);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_usage_t first = make_rate_usage(501, 1, 2);
	qhw_adm_usage_t same_window = make_rate_usage(502, 2, 1);
	qhw_adm_usage_t next_window;
	qhw_adm_usage_t returned = make_rate_usage(0, 1, 1);
	qhw_adm_usage_t after_return = make_rate_usage(504, 2, 1);
	qhw_adm_usage_state_t usage_state = make_usage_output();
	uint64_t reservation_id;

	next_window = make_rate_usage(503, 1000000001ULL, 2);
	CHECK(setup_context(4, 2, 1000000000ULL, &ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.capacity_granted == 2);
	reservation_id = decision.reservation_id;

	CHECK(qhw_adm_consume(ctx, reservation_id, &first,
		&decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(qhw_adm_consume(ctx, reservation_id, &same_window,
		&decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_REJECTED);
	CHECK(qhw_adm_return_usage(ctx, reservation_id, &returned) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_get_usage(ctx, reservation_id, &usage_state) ==
		QHW_ADM_OK);
	CHECK(usage_state.rate_consumed == 1);
	CHECK(usage_state.remaining_rate == 1);
	CHECK(qhw_adm_consume(ctx, reservation_id, &after_return,
		&decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(qhw_adm_consume(ctx, reservation_id, &next_window,
		&decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);

	qhw_adm_destroy(ctx);
	return 0;
}

static int test_usage_reports_latest_rate_window(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42, 2000000000ULL);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_usage_t first = make_rate_usage(601, 1, 1);
	qhw_adm_usage_t second = make_rate_usage(602, 1000000001ULL, 1);
	qhw_adm_usage_t third = make_rate_usage(603, 1000000002ULL, 1);
	qhw_adm_usage_state_t usage_state = make_usage_output();

	CHECK(setup_context(4, 2, 1000000000ULL, &ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.capacity_granted == 2);

	CHECK(qhw_adm_consume(ctx, decision.reservation_id, &first,
		&decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(qhw_adm_consume(ctx, decision.reservation_id, &second,
		&decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(qhw_adm_get_usage(ctx, decision.reservation_id, &usage_state) ==
		QHW_ADM_OK);
	CHECK(usage_state.rate_consumed == 1);
	CHECK(usage_state.remaining_rate == 1);
	CHECK(qhw_adm_consume(ctx, decision.reservation_id, &third,
		&decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);

	qhw_adm_destroy(ctx);
	return 0;
}

static int test_invalid_configuration(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile;
	qhw_adm_kv_t option;
	const char *yaml =
		"plugin_paths:\n"
		"  policies: [\"" QHW_ADM_TEST_RATE_DIR "\"]\n"
		"devices:\n"
		"  - device_id: 7\n"
		"    max_qubits: 20\n"
		"    time_span_ns: 1\n"
		"    baseline:\n"
		"      qubit_count: 4\n"
		"      depth: 10\n"
		"      one_q_gate_count: 10\n"
		"      two_q_gate_count: 5\n"
		"      shots: 100\n"
		"      measurement_count: 2\n"
		"    timing:\n"
		"      one_q_gate_ns: 20\n"
		"      two_q_gate_ns: 100\n"
		"      measurement_ns: 1000\n"
		"    rate:\n"
		"      device_rate: 0\n"
		"      concurrent_jobs: 1\n"
		"    policy:\n"
		"      name: rate\n"
		"      options:\n"
		"        rate_slice: 2\n";

	profile = make_profile(4, 2, 0);
	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_RATE_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "rate",
		NULL, 0) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);

	memset(&option, 0, sizeof(option));
	option.key = QHW_ADM_OPT_RATE_SLICE;
	option.value.type = QHW_ADM_VALUE_U64;
	option.value.value.u64 = 5;
	profile = make_profile(4, 2, 1000000000ULL);
	ctx = NULL;
	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_RATE_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "rate",
		&option, 1) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);

	profile = make_profile(0, 1, 1);
	ctx = NULL;
	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_RATE_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "rate",
		&option, 1) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);

	ctx = NULL;
	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, yaml, 0,
		QHW_ADM_CONFIG_MERGE) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_yaml_configuration(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42, 2000000000ULL);
	qhw_adm_decision_t decision = make_decision_output();
	const char *yaml =
		"plugin_paths:\n"
		"  policies: [\"" QHW_ADM_TEST_RATE_DIR "\"]\n"
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
		"      shots: 100\n"
		"      measurement_count: 2\n"
		"    timing:\n"
		"      one_q_gate_ns: 20\n"
		"      two_q_gate_ns: 100\n"
		"      measurement_ns: 1000\n"
		"    rate:\n"
		"      device_rate: 4\n"
		"      concurrent_jobs: 2\n"
		"    policy:\n"
		"      name: rate\n"
		"      options:\n"
		"        rate_slice: 2\n";

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, yaml, 0,
		QHW_ADM_CONFIG_MERGE) == QHW_ADM_OK);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.rate_required == 2);
	CHECK(decision.capacity_granted == 2);
	qhw_adm_destroy(ctx);
	return 0;
}

int main(void)
{
	CHECK(test_exact_slice_and_release() == 0);
	CHECK(test_multi_slice_admission() == 0);
	CHECK(test_derived_rate_capacity() == 0);
	CHECK(test_delayed_when_rate_is_reserved() == 0);
	CHECK(test_scoped_external_rate_cap() == 0);
	CHECK(test_oversized_request_is_rejected() == 0);
	CHECK(test_cancel_and_expire_return_capacity() == 0);
	CHECK(test_usage_windows_follow_event_time() == 0);
	CHECK(test_usage_reports_latest_rate_window() == 0);
	CHECK(test_invalid_configuration() == 0);
	CHECK(test_yaml_configuration() == 0);
	return 0;
}
