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
		.total_credits = 5,
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

static qhw_adm_usage_state_t make_usage_output(void)
{
	qhw_adm_usage_state_t usage;

	memset(&usage, 0, sizeof(usage));
	usage.struct_size = sizeof(usage);
	return usage;
}

static qhw_adm_compliance_t make_compliance_output(void)
{
	qhw_adm_compliance_t compliance;

	memset(&compliance, 0, sizeof(compliance));
	compliance.struct_size = sizeof(compliance);
	return compliance;
}

static qhw_adm_reservation_t make_reservation_output(void)
{
	qhw_adm_reservation_t reservation;

	memset(&reservation, 0, sizeof(reservation));
	reservation.struct_size = sizeof(reservation);
	return reservation;
}

static qhw_adm_usage_t make_usage(uint64_t task_id, uint64_t credits)
{
	qhw_adm_usage_t usage = {
		.struct_size = sizeof(usage),
		.reservation_id = 0,
		.task_id = task_id,
		.class_id = 11,
		.event_time_ns = 0,
		.estimated_ns = 100,
		.baseline_units = credits,
		.credits = credits,
	};

	return usage;
}

static qhw_adm_actual_usage_t make_actual(
	uint64_t task_id,
	uint64_t observed_device_ns)
{
	qhw_adm_actual_usage_t actual = {
		.struct_size = sizeof(actual),
		.task_id = task_id,
		.observed_device_ns = observed_device_ns,
		.observed_compile_ns = 20,
		.observed_transfer_ns = 5,
		.observed_control_overhead_ns = 5,
	};

	return actual;
}

static int setup_context(qhw_adm_t **out_ctx)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();
	qhw_adm_kv_t estimator_options[1];

	memset(estimator_options, 0, sizeof(estimator_options));
	estimator_options[0].key = QHW_ADM_META_CONSUMED_CREDITS;
	estimator_options[0].value.type = QHW_ADM_VALUE_U64;
	estimator_options[0].value.value.u64 = 3;

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_estimator_path(ctx, QHW_ADM_TEST_ESTIMATOR_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_estimator(ctx, profile.device_id, "mock",
		estimator_options, 1) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_CREDIT_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "credit",
		NULL, 0) == QHW_ADM_OK);
	*out_ctx = ctx;
	return 0;
}

static int setup_guard_context(qhw_adm_t **out_ctx)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_USAGE_GUARD_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "usage_guard",
		NULL, 0) == QHW_ADM_OK);
	*out_ctx = ctx;
	return 0;
}

static int reserve_one(qhw_adm_t *ctx, uint64_t *out_reservation_id)
{
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.capacity_granted == 3);
	*out_reservation_id = decision.reservation_id;
	return 0;
}

static int test_authorize_consume_return(void)
{
	qhw_adm_t *ctx = NULL;
	uint64_t reservation_id;
	qhw_adm_usage_t usage = make_usage(101, 1);
	qhw_adm_usage_t equivalent_usage = make_usage(101, 0);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_usage_state_t state = make_usage_output();
	qhw_adm_reservation_t reservation = make_reservation_output();

	equivalent_usage.baseline_units = 1;
	CHECK(setup_context(&ctx) == 0);
	CHECK(reserve_one(ctx, &reservation_id) == 0);
	CHECK(qhw_adm_authorize_usage(ctx, reservation_id, &usage,
		&decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(qhw_adm_get_usage(ctx, reservation_id, &state) == QHW_ADM_OK);
	CHECK(state.credits_consumed == 0);

	CHECK(qhw_adm_consume(ctx, reservation_id, &usage, &decision) ==
		QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(qhw_adm_get_usage(ctx, reservation_id, &state) == QHW_ADM_OK);
	CHECK(state.credits_consumed == 1);
	CHECK(state.remaining_credits == 2);

	CHECK(qhw_adm_consume(ctx, reservation_id, &equivalent_usage,
		&decision) == QHW_ADM_OK);
	CHECK(qhw_adm_get_usage(ctx, reservation_id, &state) == QHW_ADM_OK);
	CHECK(state.credits_consumed == 1);

	CHECK(qhw_adm_return_usage(ctx, reservation_id, &equivalent_usage) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_get_usage(ctx, reservation_id, &state) == QHW_ADM_OK);
	CHECK(state.credits_consumed == 0);
	CHECK(state.remaining_credits == 3);
	CHECK(qhw_adm_return_usage(ctx, reservation_id, &usage) == QHW_ADM_OK);
	CHECK(qhw_adm_get_usage(ctx, reservation_id, &state) == QHW_ADM_OK);
	CHECK(state.credits_consumed == 0);

	CHECK(qhw_adm_release(ctx, reservation_id, QHW_ADM_REASON_NONE) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_get_reservation(ctx, reservation_id, &reservation) ==
		QHW_ADM_OK);
	CHECK(reservation.unused_capacity == 3);

	qhw_adm_destroy(ctx);
	return 0;
}

static int test_overlimit_and_compliance(void)
{
	qhw_adm_t *ctx = NULL;
	uint64_t reservation_id;
	qhw_adm_usage_t usage = make_usage(102, 4);
	qhw_adm_decision_t decision = make_decision_output();
	qhw_adm_compliance_t compliance = make_compliance_output();

	CHECK(setup_context(&ctx) == 0);
	CHECK(reserve_one(ctx, &reservation_id) == 0);
	CHECK(qhw_adm_consume(ctx, reservation_id, &usage, &decision) ==
		QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_REJECTED);
	CHECK(decision.reason_code == QHW_ADM_REASON_OVER_LIMIT);
	CHECK(qhw_adm_consume(ctx, reservation_id, &usage, &decision) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_get_compliance(ctx, reservation_id, &compliance) ==
		QHW_ADM_OK);
	CHECK(compliance.overuse_count == 1);
	CHECK(compliance.action == QHW_ADM_COMPLIANCE_REJECT);

	qhw_adm_destroy(ctx);
	return 0;
}

static int test_actual_usage_and_feedback(void)
{
	qhw_adm_t *ctx = NULL;
	uint64_t reservation_id;
	qhw_adm_actual_usage_t actual = make_actual(103, 100);
	qhw_adm_actual_usage_t failing = make_actual(104, 999);
	qhw_adm_usage_state_t state = make_usage_output();

	CHECK(setup_context(&ctx) == 0);
	CHECK(reserve_one(ctx, &reservation_id) == 0);
	CHECK(qhw_adm_record_actual(ctx, reservation_id, &actual) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_record_actual(ctx, reservation_id, &actual) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_get_usage(ctx, reservation_id, &state) == QHW_ADM_OK);
	CHECK(state.actual_total_ns == 130);

	CHECK(qhw_adm_record_actual(ctx, reservation_id, &failing) ==
		QHW_ADM_ERR_ESTIMATOR);
	CHECK(qhw_adm_get_usage(ctx, reservation_id, &state) == QHW_ADM_OK);
	CHECK(state.actual_total_ns == 1159);
	CHECK(qhw_adm_record_actual(ctx, reservation_id, &failing) ==
		QHW_ADM_ERR_ESTIMATOR);
	CHECK(qhw_adm_get_usage(ctx, reservation_id, &state) == QHW_ADM_OK);
	CHECK(state.actual_total_ns == 1159);

	qhw_adm_destroy(ctx);
	return 0;
}

static int test_authorize_does_not_commit_policy_usage(void)
{
	qhw_adm_t *ctx = NULL;
	uint64_t reservation_id;
	qhw_adm_usage_t usage = make_usage(106, 1);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(setup_guard_context(&ctx) == 0);
	CHECK(reserve_one(ctx, &reservation_id) == 0);
	CHECK(qhw_adm_authorize_usage(ctx, reservation_id, &usage,
		&decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(qhw_adm_consume(ctx, reservation_id, &usage, &decision) ==
		QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);

	qhw_adm_destroy(ctx);
	return 0;
}

static int test_invalid_return_does_not_notify_policy(void)
{
	qhw_adm_t *ctx = NULL;
	uint64_t reservation_id;
	qhw_adm_usage_t usage = make_usage(107, 1);
	qhw_adm_usage_t invalid_return = make_usage(0, 2);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(setup_guard_context(&ctx) == 0);
	CHECK(reserve_one(ctx, &reservation_id) == 0);
	CHECK(qhw_adm_consume(ctx, reservation_id, &usage, &decision) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_return_usage(ctx, reservation_id, &invalid_return) ==
		QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_return_usage(ctx, reservation_id, &usage) == QHW_ADM_OK);

	qhw_adm_destroy(ctx);
	return 0;
}

static int test_return_rejects_unconsumed_task(void)
{
	qhw_adm_t *ctx = NULL;
	uint64_t reservation_id;
	qhw_adm_usage_t usage = make_usage(105, 1);

	CHECK(setup_context(&ctx) == 0);
	CHECK(reserve_one(ctx, &reservation_id) == 0);
	CHECK(qhw_adm_return_usage(ctx, reservation_id, &usage) ==
		QHW_ADM_ERR_STATE);

	qhw_adm_destroy(ctx);
	return 0;
}

int main(void)
{
	CHECK(test_authorize_consume_return() == 0);
	CHECK(test_overlimit_and_compliance() == 0);
	CHECK(test_actual_usage_and_feedback() == 0);
	CHECK(test_authorize_does_not_commit_policy_usage() == 0);
	CHECK(test_invalid_return_does_not_notify_policy() == 0);
	CHECK(test_return_rejects_unconsumed_task() == 0);
	return 0;
}
