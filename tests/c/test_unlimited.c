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
	qhw_adm_device_state_t device_state;
	uint64_t now_ns;
	uint64_t next_available_ns;
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
	out_snapshot->device_state = state->device_state;
	out_snapshot->now_ns = state->now_ns;
	out_snapshot->next_available_ns = state->next_available_ns;
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

static qhw_adm_device_profile_t make_profile(void)
{
	qhw_adm_device_profile_t profile = {
		.struct_size = sizeof(profile),
		.device_id = 7,
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

static int setup_context(qhw_adm_t **out_ctx)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_device_profile_t profile = make_profile();

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_register_device(ctx, &profile) == QHW_ADM_OK);
	CHECK(qhw_adm_add_policy_path(ctx, QHW_ADM_TEST_UNLIMITED_DIR) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_set_policy(ctx, profile.device_id, "unlimited",
		NULL, 0) == QHW_ADM_OK);
	*out_ctx = ctx;
	return 0;
}

static int test_evaluate_accepts_without_capacity(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(setup_context(&ctx) == 0);
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_ACCEPTED);
	CHECK(decision.request_id == request.request_id);
	CHECK(decision.credits_required == 0);
	CHECK(decision.rate_required == 0);
	CHECK(decision.capacity_available == UINT64_MAX);
	CHECK(decision.estimated_total_ns == 683160);
	CHECK(decision.capacity_granted == 3);
	CHECK(strcmp(decision.message, "accepted by unlimited policy") == 0);
	CHECK(strcmp(qhw_adm_last_error(ctx), "") == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_reserve_and_release(void)
{
	qhw_adm_t *ctx = NULL;
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
	CHECK(reservation.credits_reserved == 0);
	CHECK(reservation.rate_reserved == 0);
	CHECK(reservation.estimated_total_ns == 683160);
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.active_reservation_count == 1);
	CHECK(capacity.credits_reserved == 0);
	CHECK(capacity.rate_reserved == 0);
	CHECK(qhw_adm_release(ctx, decision.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	CHECK(qhw_adm_get_reservation(ctx, decision.reservation_id,
		&reservation) == QHW_ADM_OK);
	CHECK(reservation.state == QHW_ADM_RESERVATION_RELEASED);
	CHECK(qhw_adm_get_capacity(ctx, request.device_id, request.scope_id,
		&capacity) == QHW_ADM_OK);
	CHECK(capacity.active_reservation_count == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_cancel_and_expire(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t cancelled = make_decision_output();
	qhw_adm_decision_t expired = make_decision_output();
	qhw_adm_reservation_t reservation = make_reservation_output();
	size_t expired_count = 0;

	CHECK(setup_context(&ctx) == 0);
	CHECK(qhw_adm_reserve(ctx, &request, &cancelled) == QHW_ADM_OK);
	CHECK(qhw_adm_cancel(ctx, cancelled.reservation_id,
		QHW_ADM_REASON_NONE) == QHW_ADM_OK);
	CHECK(qhw_adm_get_reservation(ctx, cancelled.reservation_id,
		&reservation) == QHW_ADM_OK);
	CHECK(reservation.state == QHW_ADM_RESERVATION_CANCELLED);

	request.request_id++;
	CHECK(qhw_adm_reserve(ctx, &request, &expired) == QHW_ADM_OK);
	CHECK(qhw_adm_renew(ctx, expired.reservation_id, 1000, 1000) ==
		QHW_ADM_OK);
	CHECK(qhw_adm_expire(ctx, 1999, &expired_count) == QHW_ADM_OK);
	CHECK(expired_count == 0);
	CHECK(qhw_adm_expire(ctx, 2000, &expired_count) == QHW_ADM_OK);
	CHECK(expired_count == 1);
	CHECK(qhw_adm_get_reservation(ctx, expired.reservation_id,
		&reservation) == QHW_ADM_OK);
	CHECK(reservation.state == QHW_ADM_RESERVATION_EXPIRED);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_device_state_blocks_admission(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();
	struct capacity_state state = {
		.device_state = QHW_ADM_DEVICE_DRAINING,
		.now_ns = 1000,
		.next_available_ns = 2000,
	};
	qhw_adm_capacity_provider_t provider = make_capacity_provider(&state);

	CHECK(setup_context(&ctx) == 0);
	CHECK(qhw_adm_set_capacity_provider(ctx, &provider) == QHW_ADM_OK);

	CHECK(qhw_adm_evaluate(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_DELAYED);
	CHECK(decision.reason_code == QHW_ADM_REASON_DEVICE_UNAVAILABLE);
	CHECK(decision.compliance_action == QHW_ADM_COMPLIANCE_DELAY);
	CHECK(decision.retry_after_ns == 1000);
	CHECK(strcmp(decision.message, "device is draining") == 0);

	decision = make_decision_output();
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_DELAYED);
	CHECK(decision.reservation_id == 0);
	CHECK(strcmp(qhw_adm_last_error(ctx), "") == 0);

	state.device_state = QHW_ADM_DEVICE_UNAVAILABLE;
	decision = make_decision_output();
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_REJECTED);
	CHECK(decision.reason_code == QHW_ADM_REASON_DEVICE_UNAVAILABLE);
	CHECK(decision.reservation_id == 0);
	CHECK(strcmp(decision.message, "device is unavailable") == 0);

	state.device_state = QHW_ADM_DEVICE_MAINTENANCE;
	decision = make_decision_output();
	CHECK(qhw_adm_reserve(ctx, &request, &decision) == QHW_ADM_OK);
	CHECK(decision.decision == QHW_ADM_DECISION_REJECTED);
	CHECK(decision.reservation_id == 0);
	CHECK(strcmp(decision.message, "device is in maintenance") == 0);

	qhw_adm_destroy(ctx);
	return 0;
}

static int test_invalid_request(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_qtask_class_t task = make_task();
	qhw_adm_request_t request = make_request(&task);
	qhw_adm_decision_t decision = make_decision_output();

	CHECK(setup_context(&ctx) == 0);
	request.task_class_count = 0;
	CHECK(qhw_adm_evaluate(ctx, &request, &decision) ==
		QHW_ADM_ERR_INVAL);
	decision = make_decision_output();
	CHECK(qhw_adm_reserve(ctx, &request, &decision) ==
		QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(ctx);
	return 0;
}

int main(void)
{
	CHECK(test_evaluate_accepts_without_capacity() == 0);
	CHECK(test_reserve_and_release() == 0);
	CHECK(test_cancel_and_expire() == 0);
	CHECK(test_device_state_blocks_admission() == 0);
	CHECK(test_invalid_request() == 0);
	return 0;
}
