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

static qhw_adm_device_profile_t make_profile(uint64_t total_credits)
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
		.total_credits = total_credits,
		.default_ttl_ns = 60000000000ULL,
	};

	return profile;
}

static qhw_adm_device_profile_t make_profile_with_window(
	uint64_t total_credits,
	uint64_t time_span_ns)
{
	qhw_adm_device_profile_t profile;

	profile = make_profile(total_credits);
	profile.time_span_ns = time_span_ns;
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
	uint64_t request_id)
{
	qhw_adm_request_t request = {
		.struct_size = sizeof(request),
		.request_id = request_id,
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

static void make_overcommit_options(qhw_adm_kv_t *options)
{
	memset(options, 0, 2 * sizeof(*options));
	options[0].key = QHW_ADM_OPT_CREDIT_ALLOW_OVERCOMMIT;
	options[0].value.type = QHW_ADM_VALUE_BOOL;
	options[0].value.value.boolean = 1;
	options[1].key = QHW_ADM_OPT_CREDIT_OVERCOMMIT_CREDITS;
	options[1].value.type = QHW_ADM_VALUE_U64;
	options[1].value.value.u64 = 1;
}

static void make_invalid_overcommit_ppm_options(qhw_adm_kv_t *options)
{
	memset(options, 0, 2 * sizeof(*options));
	options[0].key = QHW_ADM_OPT_CREDIT_ALLOW_OVERCOMMIT;
	options[0].value.type = QHW_ADM_VALUE_BOOL;
	options[0].value.value.boolean = 1;
	options[1].key = QHW_ADM_OPT_CREDIT_OVERCOMMIT_PPM;
	options[1].value.type = QHW_ADM_VALUE_U64;
	options[1].value.value.u64 = UINT64_MAX;
}

static void make_invalid_overcommit_credit_options(qhw_adm_kv_t *options)
{
	memset(options, 0, 2 * sizeof(*options));
	options[0].key = QHW_ADM_OPT_CREDIT_ALLOW_OVERCOMMIT;
	options[0].value.type = QHW_ADM_VALUE_BOOL;
	options[0].value.value.boolean = 1;
	options[1].key = QHW_ADM_OPT_CREDIT_OVERCOMMIT_CREDITS;
	options[1].value.type = QHW_ADM_VALUE_U64;
	options[1].value.value.u64 = UINT64_MAX;
}

static int setup_context(
	uint64_t total_credits,
	const qhw_adm_kv_t *options,
	size_t option_count,
	qhw_adm_t **out_ctx)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile(total_credits);

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_CREDIT_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "credit",
		options, option_count) == QHW_ADM_OK);
	*out_ctx = ctx;
	return 0;
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
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_CREDIT_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile->device_id, "credit",
		options, option_count) == QHW_ADM_OK);
	*out_ctx = ctx;
	return 0;
}

static int test_exact_fit_and_release(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();

	CHECK(setup_context(3, NULL, 0, &ctx) == 0);
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.credits_required == 3);
	CHECK(decision.capacity_available == 3);
	CHECK(strcmp(decision.message, "accepted by credit policy") == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.reservation_id != 0);
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.total_credits == 3);
	CHECK(capacity.credits_reserved == 3);
	CHECK(capacity.core_available_credits == 0);
	CHECK(capacity.effective_available_credits == 0);

	CHECK(qhw_adm_release(ctx, decision.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	capacity = make_capacity_output();
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.credits_reserved == 0);
	CHECK(capacity.effective_available_credits == 3);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_derived_credit_capacity(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();

	profile = make_profile_with_window(0, 814650);
	CHECK(setup_context_with_profile(&profile, NULL, 0, &ctx) == 0);
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.total_credits == 3);
	CHECK(capacity.effective_available_credits == 3);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.credits_required == 3);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_zero_derived_capacity_rejects(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile;
	qhw_adm_qtask_class_t task = make_task(1);
	qhw_adm_request_t request = make_request(&task, 42);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();

	profile = make_profile_with_window(0, 1);
	CHECK(setup_context_with_profile(&profile, NULL, 0, &ctx) == 0);
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.total_credits == 0);
	CHECK(capacity.effective_available_credits == 0);
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_REJECTED);
	CHECK(decision.reason_code == QHW_ADM_REASON_REQUEST_TOO_LARGE);
	CHECK(decision.capacity_available == 0);
	CHECK(strcmp(decision.message, "credit capacity is unavailable") == 0);

	decision = make_decision_output();
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_REJECTED);
	CHECK(decision.reason_code == QHW_ADM_REASON_REQUEST_TOO_LARGE);
	CHECK(decision.reservation_id == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_explicit_credits_override_derivation(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();

	profile = make_profile_with_window(5, 814650);
	CHECK(setup_context_with_profile(&profile, NULL, 0, &ctx) == 0);
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.total_credits == 5);
	CHECK(capacity.effective_available_credits == 5);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_yaml_credits_override_derivation(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_capacity_view_t capacity = make_capacity_output();
	const char *yaml =
		"plugin_paths:\n"
		"  policies: [\"" QHW_ADM_TEST_CREDIT_DIR "\"]\n"
		"devices:\n"
		"  - device_id: 7\n"
		"    max_qubits: 20\n"
		"    max_shots: 10000\n"
		"    time_span_ns: 814650\n"
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
		"      one_q_gate_transfer_ns: 1\n"
		"      two_q_gate_transfer_ns: 4\n"
		"      measurement_transfer_ns: 10\n"
		"      compile_ns: 1000\n"
		"      control_overhead_ns: 200\n"
		"      provider_overhead_ns: 300\n"
		"    credit:\n"
		"      total_credits: 5\n"
		"    policy:\n"
		"      name: credit\n";

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, yaml, 0,
		QHW_ADM_CONFIG_MERGE) == QHW_ADM_OK);
	CHECK(qhw_adm_get_capacity(ctx, 7, 3, &capacity) == QHW_ADM_OK);
	CHECK(capacity.total_credits == 5);
	CHECK(capacity.effective_available_credits == 5);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_delayed_when_capacity_is_reserved(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t first = make_request(&task, 42);
	qhw_adm_request_t second = make_request(&task, 43);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(setup_context(5, NULL, 0, &ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &first, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);

	decision = make_decision_output();
	CHECK(qhw_adm_evaluate(ctx, &second, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_DELAYED);
	CHECK(decision.reason_code == QHW_ADM_REASON_INSUFFICIENT_CREDITS);
	CHECK(decision.credits_required == 3);
	CHECK(decision.capacity_available == 2);
	CHECK(strcmp(decision.message, "insufficient credits") == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_oversized_request_is_rejected(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(setup_context(2, NULL, 0, &ctx) == 0);
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_REJECTED);
	CHECK(decision.reason_code == QHW_ADM_REASON_REQUEST_TOO_LARGE);
	CHECK(decision.credits_required == 3);
	CHECK(strcmp(decision.message,
		"credit request exceeds policy limit") == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_bounded_overcommit_accepts_exact_limit(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_kv_t options[2];
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();

	make_overcommit_options(options);
	CHECK(setup_context(2, options, 2, &ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.credits_required == 3);
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.total_credits == 2);
	CHECK(capacity.credits_reserved == 3);
	CHECK(capacity.core_available_credits == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_overcommit_limit_rejects_oversized_request(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_kv_t options[2];
	qhw_adm_qtask_class_t task = make_task(3);
	qhw_adm_request_t request = make_request(&task, 42);
	qhw_adm_decision_t decision = make_decision_output();

	make_overcommit_options(options);
	CHECK(setup_context(2, options, 2, &ctx) == 0);
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_REJECTED);
	CHECK(decision.reason_code == QHW_ADM_REASON_REQUEST_TOO_LARGE);
	CHECK(decision.credits_required == 4);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_invalid_overcommit_configuration(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile(2);
	qhw_adm_kv_t options[2];

	make_invalid_overcommit_ppm_options(options);
	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_CREDIT_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "credit",
		options, 2) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);

	ctx = NULL;
	make_invalid_overcommit_credit_options(options);
	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_CREDIT_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "credit",
		options, 2) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_yaml_invalid_overcommit_configuration(void)
{
	qhw_adm_t *ctx = NULL;
	const char *yaml =
		"plugin_paths:\n"
		"  policies: [\"" QHW_ADM_TEST_CREDIT_DIR "\"]\n"
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
		"    credit:\n"
		"      total_credits: 2\n"
		"    policy:\n"
		"      name: credit\n"
		"      options:\n"
		"        allow_overcommit: true\n"
		"        overcommit_ppm: 18446744073709551615\n";

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_load_config_string(ctx, yaml, 0,
		QHW_ADM_CONFIG_MERGE) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_cancel_and_expire_return_capacity(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task(2);
	qhw_adm_request_t request = make_request(&task, 42);
	qhw_adm_decision_t cancelled = make_decision_output();
	qhw_adm_decision_t expired = make_decision_output();
	qhw_adm_capacity_view_t capacity = make_capacity_output();
	qhw_adm_reservation_t reservation = make_reservation_output();
	size_t expired_count = 0;

	CHECK(setup_context(3, NULL, 0, &ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &cancelled) == QHW_ADM_OK);
	CHECK(qhw_adm_cancel(ctx, cancelled.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.credits_reserved == 0);

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
	CHECK(capacity.credits_reserved == 0);
	CHECK(capacity.effective_available_credits == 3);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_invalid_configuration(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile(0);

	profile.time_span_ns = 0;
	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_CREDIT_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "credit",
		NULL, 0) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);
	return 0;
}

int main(void)
{
	CHECK(test_exact_fit_and_release() == 0);
	CHECK(test_derived_credit_capacity() == 0);
	CHECK(test_zero_derived_capacity_rejects() == 0);
	CHECK(test_explicit_credits_override_derivation() == 0);
	CHECK(test_yaml_credits_override_derivation() == 0);
	CHECK(test_delayed_when_capacity_is_reserved() == 0);
	CHECK(test_oversized_request_is_rejected() == 0);
	CHECK(test_bounded_overcommit_accepts_exact_limit() == 0);
	CHECK(test_overcommit_limit_rejects_oversized_request() == 0);
	CHECK(test_invalid_overcommit_configuration() == 0);
	CHECK(test_yaml_invalid_overcommit_configuration() == 0);
	CHECK(test_cancel_and_expire_return_capacity() == 0);
	CHECK(test_invalid_configuration() == 0);
	return 0;
}
