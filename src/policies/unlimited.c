#include <qhw_admission/qhw_admission.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct unlimited_state {
	uint64_t evaluate_count;
	uint64_t reserve_count;
	uint64_t release_count;
};

static qhw_adm_rc_t unlimited_init(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_kv_t *options,
	size_t option_count,
	void **out_state)
{
	struct unlimited_state *state;

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

static void unlimited_destroy(void *state)
{
	free(state);
}

static qhw_adm_rc_t unlimited_configure(
	void *state,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	(void)state;
	(void)options;

	if (option_count > 0 && options == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static uint64_t unlimited_add_saturating(uint64_t a, uint64_t b)
{
	if (UINT64_MAX - a < b) {
		return UINT64_MAX;
	}

	return a + b;
}

static void unlimited_fill_accept(
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
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
	out_decision->credits_required = 0;
	out_decision->rate_required = 0;
	out_decision->capacity_available = UINT64_MAX;
	out_decision->estimated_total_ns = estimate->total_ns;
	out_decision->estimated_start_ns = capacity->next_available_ns;
	out_decision->estimated_finish_ns = unlimited_add_saturating(
		capacity->next_available_ns,
		estimate->total_ns);
	out_decision->capacity_granted = estimate->baseline_units;
	out_decision->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	out_decision->confidence_ppm = estimate->confidence_ppm;
	out_decision->message = "accepted by unlimited policy";
}

static uint64_t unlimited_retry_after(
	const qhw_adm_capacity_view_t *capacity)
{
	if (capacity->next_available_ns <= capacity->now_ns) {
		return 0;
	}

	return capacity->next_available_ns - capacity->now_ns;
}

static void unlimited_fill_blocked(
	const qhw_adm_request_t *request,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_decision_t *out_decision)
{
	size_t struct_size = out_decision->struct_size;

	memset(out_decision, 0, sizeof(*out_decision));
	out_decision->struct_size = struct_size;
	out_decision->request_id = request->request_id;
	out_decision->device_id = request->device_id;
	out_decision->scope_id = request->scope_id;
	out_decision->reason_code = QHW_ADM_REASON_DEVICE_UNAVAILABLE;
	if (capacity->device_state == QHW_ADM_DEVICE_DRAINING) {
		out_decision->decision = QHW_ADM_DECISION_DELAYED;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_DELAY;
		out_decision->retry_after_ns = unlimited_retry_after(capacity);
		out_decision->message = "device is draining";
		return;
	}

	out_decision->decision = QHW_ADM_DECISION_REJECTED;
	out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
	if (capacity->device_state == QHW_ADM_DEVICE_MAINTENANCE) {
		out_decision->message = "device is in maintenance";
	} else if (capacity->device_state == QHW_ADM_DEVICE_UNAVAILABLE) {
		out_decision->message = "device is unavailable";
	} else {
		out_decision->reason_code = QHW_ADM_REASON_INVALID_REQUEST;
		out_decision->message = "device state is invalid";
	}
}

static int unlimited_device_state_blocks(
	const qhw_adm_capacity_view_t *capacity)
{
	return capacity->device_state != QHW_ADM_DEVICE_AVAILABLE;
}

static qhw_adm_rc_t unlimited_evaluate(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_decision_t *out_decision)
{
	struct unlimited_state *unlimited = state;

	(void)device;

	if (request == NULL || estimate == NULL || capacity == NULL ||
	    out_decision == NULL ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	if (unlimited != NULL) {
		unlimited->evaluate_count++;
	}

	if (unlimited_device_state_blocks(capacity)) {
		unlimited_fill_blocked(request, capacity, out_decision);
		return QHW_ADM_OK;
	}

	unlimited_fill_accept(request, estimate, capacity, out_decision);
	return QHW_ADM_OK;
}

static qhw_adm_rc_t unlimited_reserve(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_policy_grant_t *out_grant,
	qhw_adm_decision_t *out_decision)
{
	struct unlimited_state *unlimited = state;

	(void)device;

	if (request == NULL || estimate == NULL || capacity == NULL ||
	    out_grant == NULL || out_decision == NULL ||
	    out_grant->struct_size < sizeof(*out_grant) ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	if (unlimited != NULL) {
		unlimited->reserve_count++;
	}

	memset(out_grant, 0, sizeof(*out_grant));
	out_grant->struct_size = sizeof(*out_grant);
	if (unlimited_device_state_blocks(capacity)) {
		unlimited_fill_blocked(request, capacity, out_decision);
		return QHW_ADM_OK;
	}

	out_grant->device_id = request->device_id;
	out_grant->scope_id = request->scope_id;
	out_grant->baseline_units_granted = estimate->baseline_units;
	out_grant->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_grant->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;

	unlimited_fill_accept(request, estimate, capacity, out_decision);
	return QHW_ADM_OK;
}

static qhw_adm_rc_t unlimited_release(
	void *state,
	const qhw_adm_reservation_t *reservation,
	uint64_t reason_code)
{
	struct unlimited_state *unlimited = state;

	(void)reservation;
	(void)reason_code;

	if (unlimited != NULL) {
		unlimited->release_count++;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t unlimited_consume(
	void *state,
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage,
	qhw_adm_decision_t *out_decision)
{
	(void)state;
	(void)reservation;
	(void)usage;

	if (out_decision != NULL &&
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}
	if (out_decision != NULL) {
		size_t struct_size = out_decision->struct_size;

		memset(out_decision, 0, sizeof(*out_decision));
		out_decision->struct_size = struct_size;
		out_decision->decision = QHW_ADM_DECISION_ACCEPTED;
		out_decision->reason_code = QHW_ADM_REASON_ACCEPTED;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t unlimited_return_usage(
	void *state,
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage)
{
	(void)state;
	(void)reservation;
	(void)usage;

	return QHW_ADM_OK;
}

static qhw_adm_rc_t unlimited_capacity(
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

static const qhw_adm_policy_desc_t unlimited_desc = {
	.struct_size = sizeof(unlimited_desc),
	.abi_version = QHW_ADM_ABI_VERSION,
	.name = "unlimited",
	.capabilities = QHW_ADM_POLICY_CAP_CAPACITY_REPORT,
	.init = unlimited_init,
	.destroy = unlimited_destroy,
	.configure = unlimited_configure,
	.evaluate = unlimited_evaluate,
	.reserve = unlimited_reserve,
	.release = unlimited_release,
	.authorize_usage = unlimited_consume,
	.consume = unlimited_consume,
	.return_usage = unlimited_return_usage,
	.capacity = unlimited_capacity,
};

const qhw_adm_policy_desc_t *qhw_adm_policy_plugin(void)
{
	return &unlimited_desc;
}
