#include <qhw_admission/qhw_admission.h>

#include <stdlib.h>
#include <string.h>

struct usage_guard_state {
	uint64_t consume_count;
	uint64_t return_count;
};

static qhw_adm_rc_t guard_init(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_kv_t *options,
	size_t option_count,
	void **out_state)
{
	struct usage_guard_state *state;

	(void)device;
	(void)options;

	if (out_state == NULL || (option_count > 0 && options == NULL)) {
		return QHW_ADM_ERR_INVAL;
	}

	state = calloc(1, sizeof(*state));
	if (state == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	*out_state = state;
	return QHW_ADM_OK;
}

static void guard_destroy(void *state)
{
	free(state);
}

static void guard_fill_decision(
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	qhw_adm_decision_t *out_decision)
{
	size_t struct_size = out_decision->struct_size;

	memset(out_decision, 0, sizeof(*out_decision));
	out_decision->struct_size = struct_size;
	out_decision->decision = QHW_ADM_DECISION_ACCEPTED;
	out_decision->request_id = request->request_id;
	out_decision->device_id = request->device_id;
	out_decision->scope_id = request->scope_id;
	out_decision->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_decision->credits_required = estimate->baseline_units;
	out_decision->capacity_available = UINT64_MAX;
	out_decision->estimated_total_ns = estimate->total_ns;
	out_decision->capacity_granted = estimate->baseline_units;
	out_decision->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	out_decision->confidence_ppm = estimate->confidence_ppm;
	out_decision->message = "usage guard accepted";
}

static qhw_adm_rc_t guard_evaluate(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_decision_t *out_decision)
{
	(void)state;
	(void)device;
	(void)capacity;

	if (request == NULL || estimate == NULL || out_decision == NULL ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	guard_fill_decision(request, estimate, out_decision);
	return QHW_ADM_OK;
}

static qhw_adm_rc_t guard_reserve(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_policy_grant_t *out_grant,
	qhw_adm_decision_t *out_decision)
{
	(void)state;
	(void)device;
	(void)capacity;

	if (request == NULL || estimate == NULL || out_grant == NULL ||
	    out_decision == NULL ||
	    out_grant->struct_size < sizeof(*out_grant) ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	memset(out_grant, 0, sizeof(*out_grant));
	out_grant->struct_size = sizeof(*out_grant);
	out_grant->device_id = request->device_id;
	out_grant->scope_id = request->scope_id;
	out_grant->credits_granted = estimate->baseline_units;
	out_grant->baseline_units_granted = estimate->baseline_units;
	out_grant->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_grant->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	guard_fill_decision(request, estimate, out_decision);
	return QHW_ADM_OK;
}

static qhw_adm_rc_t guard_release(
	void *state,
	const qhw_adm_reservation_t *reservation,
	uint64_t reason_code)
{
	(void)state;
	(void)reservation;
	(void)reason_code;

	return QHW_ADM_OK;
}

static qhw_adm_rc_t guard_authorize_usage(
	void *state,
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage,
	qhw_adm_decision_t *out_decision)
{
	(void)state;
	(void)usage;

	if (reservation == NULL || out_decision == NULL ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	memset(out_decision, 0, sizeof(*out_decision));
	out_decision->struct_size = sizeof(*out_decision);
	out_decision->decision = QHW_ADM_DECISION_ACCEPTED;
	out_decision->reservation_id = reservation->reservation_id;
	out_decision->device_id = reservation->device_id;
	out_decision->scope_id = reservation->scope_id;
	out_decision->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_decision->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	out_decision->message = "usage guard authorized";
	return QHW_ADM_OK;
}

static qhw_adm_rc_t guard_consume(
	void *state,
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage,
	qhw_adm_decision_t *out_decision)
{
	struct usage_guard_state *guard = state;
	qhw_adm_rc_t rc;

	if (guard == NULL) {
		return QHW_ADM_ERR_INVAL;
	}
	if (guard->consume_count != 0) {
		return QHW_ADM_ERR_STATE;
	}

	rc = guard_authorize_usage(NULL, reservation, usage, out_decision);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	guard->consume_count++;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t guard_return_usage(
	void *state,
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage)
{
	struct usage_guard_state *guard = state;

	(void)reservation;
	(void)usage;

	if (guard == NULL) {
		return QHW_ADM_ERR_INVAL;
	}
	if (guard->return_count != 0) {
		return QHW_ADM_ERR_STATE;
	}

	guard->return_count++;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t guard_capacity(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_capacity_view_t *core_view,
	qhw_adm_capacity_view_t *out_capacity)
{
	(void)state;
	(void)device;

	if (core_view == NULL || out_capacity == NULL ||
	    out_capacity->struct_size < sizeof(*out_capacity)) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_capacity = *core_view;
	return QHW_ADM_OK;
}

static const qhw_adm_policy_desc_t guard_desc = {
	.struct_size = sizeof(guard_desc),
	.abi_version = QHW_ADM_ABI_VERSION,
	.name = "usage_guard",
	.capabilities = QHW_ADM_POLICY_CAP_USAGE_ACCOUNTING |
		QHW_ADM_POLICY_CAP_CAPACITY_REPORT,
	.init = guard_init,
	.destroy = guard_destroy,
	.evaluate = guard_evaluate,
	.reserve = guard_reserve,
	.release = guard_release,
	.authorize_usage = guard_authorize_usage,
	.consume = guard_consume,
	.return_usage = guard_return_usage,
	.capacity = guard_capacity,
};

const qhw_adm_policy_desc_t *qhw_adm_policy_plugin(void)
{
	return &guard_desc;
}
