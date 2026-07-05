#include <qhw_admission/qhw_admission.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct rate_state {
	uint64_t reservation_ttl_ns;
	uint64_t rate_slice;
};

static uint64_t rate_saturating_add(uint64_t a, uint64_t b)
{
	if (UINT64_MAX - a < b) {
		return UINT64_MAX;
	}

	return a + b;
}

static uint64_t rate_saturating_sub(uint64_t a, uint64_t b)
{
	if (a < b) {
		return 0;
	}

	return a - b;
}

static uint64_t rate_capped_available(
	uint64_t core_available,
	uint64_t external_limit,
	uint64_t scoped_reserved)
{
	uint64_t scoped_available;

	if (external_limit == 0) {
		return core_available;
	}
	if (scoped_reserved >= external_limit) {
		return 0;
	}

	scoped_available = external_limit - scoped_reserved;
	if (core_available < scoped_available) {
		return core_available;
	}
	return scoped_available;
}

static int rate_option_u64(
	const qhw_adm_value_t *value,
	uint64_t *out_value)
{
	if (value->type != QHW_ADM_VALUE_U64) {
		return 0;
	}

	*out_value = value->value.u64;
	return 1;
}

static qhw_adm_rc_t rate_apply_option(
	struct rate_state *state,
	const qhw_adm_kv_t *option)
{
	if (option->key == QHW_ADM_OPT_RATE_RESERVATION_TTL_NS ||
	    option->key == QHW_ADM_OPT_CREDIT_RESERVATION_TTL_NS) {
		if (!rate_option_u64(
			&option->value,
			&state->reservation_ttl_ns)) {
			return QHW_ADM_ERR_INVAL;
		}
		return QHW_ADM_OK;
	}
	if (option->key == QHW_ADM_OPT_RATE_SLICE) {
		if (!rate_option_u64(
			&option->value,
			&state->rate_slice)) {
			return QHW_ADM_ERR_INVAL;
		}
		return QHW_ADM_OK;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_configure_state(
	struct rate_state *state,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	size_t i;

	if (state == NULL || (option_count > 0 && options == NULL)) {
		return QHW_ADM_ERR_INVAL;
	}

	for (i = 0; i < option_count; i++) {
		qhw_adm_rc_t rc;

		rc = rate_apply_option(state, &options[i]);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_validate_state(
	const struct rate_state *state,
	const qhw_adm_device_profile_t *device)
{
	if (state == NULL || device == NULL || device->time_span_ns == 0) {
		return QHW_ADM_ERR_INVAL;
	}
	if (state->rate_slice != 0 &&
	    device->device_rate != 0 &&
	    state->rate_slice > device->device_rate) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_init(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_kv_t *options,
	size_t option_count,
	void **out_state)
{
	struct rate_state *state;
	qhw_adm_rc_t rc;

	if (device == NULL || out_state == NULL ||
	    (option_count > 0 && options == NULL)) {
		return QHW_ADM_ERR_INVAL;
	}

	state = calloc(1, sizeof(*state));
	if (state == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	rc = rate_configure_state(state, options, option_count);
	if (rc != QHW_ADM_OK) {
		free(state);
		return rc;
	}
	rc = rate_validate_state(state, device);
	if (rc != QHW_ADM_OK) {
		free(state);
		return rc;
	}

	*out_state = state;
	return QHW_ADM_OK;
}

static void rate_destroy(void *state)
{
	free(state);
}

static qhw_adm_rc_t rate_configure(
	void *state,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	return rate_configure_state(state, options, option_count);
}

static qhw_adm_rc_t rate_checked_add(
	uint64_t a,
	uint64_t b,
	uint64_t *out_value)
{
	if (out_value == NULL || UINT64_MAX - a < b) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_value = a + b;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_quantum_budget(
	const qhw_adm_request_t *request,
	uint64_t *out_budget)
{
	uint64_t reserved;
	qhw_adm_rc_t rc;

	if (request == NULL || out_budget == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = rate_checked_add(
		request->classical_runtime_ns,
		request->overhead_ns,
		&reserved);
	if (rc != QHW_ADM_OK || request->walltime_ns <= reserved) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_budget = request->walltime_ns - reserved;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_ceil_mul_div(
	uint64_t a,
	uint64_t b,
	uint64_t divisor,
	uint64_t *out_value)
{
#if defined(__SIZEOF_INT128__)
	__uint128_t product;
	__uint128_t value;

	if (out_value == NULL || divisor == 0) {
		return QHW_ADM_ERR_INVAL;
	}

	product = (__uint128_t)a * (__uint128_t)b;
	value = (product + divisor - 1) / divisor;
	if (value > UINT64_MAX) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_value = (uint64_t)value;
	return QHW_ADM_OK;
#else
	(void)a;
	(void)b;
	(void)divisor;
	(void)out_value;
	return QHW_ADM_ERR_UNSUPPORTED;
#endif
}

static qhw_adm_rc_t rate_round_up(
	uint64_t value,
	uint64_t slice,
	uint64_t *out_value)
{
	uint64_t multiplier;

	if (out_value == NULL || slice == 0) {
		return QHW_ADM_ERR_INVAL;
	}
	if (value == 0) {
		*out_value = 0;
		return QHW_ADM_OK;
	}

	multiplier = value / slice;
	if (value % slice != 0) {
		if (multiplier == UINT64_MAX) {
			return QHW_ADM_ERR_INVAL;
		}
		multiplier++;
	}
	if (multiplier > UINT64_MAX / slice) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_value = multiplier * slice;
	return QHW_ADM_OK;
}

static uint64_t rate_default_slice(
	const struct rate_state *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_capacity_view_t *capacity)
{
	uint64_t slice;

	if (state->rate_slice != 0) {
		return state->rate_slice;
	}
	if (device->concurrent_jobs == 0 || capacity->total_rate == 0) {
		return 1;
	}

	slice = capacity->total_rate / device->concurrent_jobs;
	if (slice == 0) {
		return 1;
	}
	return slice;
}

static uint64_t rate_retry_after(
	const qhw_adm_capacity_view_t *capacity)
{
	if (capacity->next_available_ns <= capacity->now_ns) {
		return 0;
	}

	return capacity->next_available_ns - capacity->now_ns;
}

static void rate_fill_device_blocked(
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
		out_decision->retry_after_ns = rate_retry_after(capacity);
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

static int rate_device_state_blocks(
	const qhw_adm_capacity_view_t *capacity)
{
	return capacity->device_state != QHW_ADM_DEVICE_AVAILABLE;
}

static qhw_adm_rc_t rate_required(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	uint64_t *out_quantum_budget,
	uint64_t *out_rate_required)
{
	uint64_t quantum_budget;
	qhw_adm_rc_t rc;

	rc = rate_quantum_budget(request, &quantum_budget);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = rate_ceil_mul_div(
		estimate->baseline_units,
		device->time_span_ns,
		quantum_budget,
		out_rate_required);
	if (rc != QHW_ADM_OK || *out_rate_required == 0) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_quantum_budget = quantum_budget;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_fill_decision(
	const struct rate_state *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_decision_t *out_decision,
	uint64_t *out_granted_rate)
{
	uint64_t quantum_budget = 0;
	uint64_t required = 0;
	uint64_t slice;
	uint64_t granted = 0;
	size_t struct_size;
	qhw_adm_rc_t rc;

	if (out_granted_rate != NULL) {
		*out_granted_rate = 0;
	}

	struct_size = out_decision->struct_size;
	memset(out_decision, 0, sizeof(*out_decision));
	out_decision->struct_size = struct_size;
	out_decision->request_id = request->request_id;
	out_decision->device_id = request->device_id;
	out_decision->scope_id = request->scope_id;
	out_decision->capacity_available = capacity->effective_available_rate;
	out_decision->estimated_total_ns = estimate->total_ns;
	out_decision->confidence_ppm = estimate->confidence_ppm;

	rc = rate_required(
		device,
		request,
		estimate,
		&quantum_budget,
		&required);
	out_decision->quantum_budget_ns = quantum_budget;
	if (rc != QHW_ADM_OK) {
		out_decision->decision = QHW_ADM_DECISION_REJECTED;
		out_decision->reason_code = QHW_ADM_REASON_WALLTIME_INFEASIBLE;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
		out_decision->message = "rate request has no quantum budget";
		return QHW_ADM_OK;
	}

	out_decision->rate_required = required;
	if (capacity->total_rate == 0) {
		out_decision->decision = QHW_ADM_DECISION_REJECTED;
		out_decision->reason_code = QHW_ADM_REASON_REQUEST_TOO_LARGE;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
		out_decision->message = "rate capacity is unavailable";
		return QHW_ADM_OK;
	}
	if (required > capacity->total_rate) {
		out_decision->decision = QHW_ADM_DECISION_REJECTED;
		out_decision->reason_code = QHW_ADM_REASON_REQUEST_TOO_LARGE;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
		out_decision->message = "rate request exceeds policy limit";
		return QHW_ADM_OK;
	}

	slice = rate_default_slice(state, device, capacity);
	rc = rate_round_up(required, slice, &granted);
	if (rc != QHW_ADM_OK || granted > capacity->total_rate) {
		out_decision->decision = QHW_ADM_DECISION_REJECTED;
		out_decision->reason_code = QHW_ADM_REASON_REQUEST_TOO_LARGE;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
		out_decision->message = "rate slice exceeds policy limit";
		return QHW_ADM_OK;
	}

	out_decision->capacity_granted = granted;
	if (capacity->effective_available_rate < slice ||
	    granted > capacity->effective_available_rate) {
		out_decision->decision = QHW_ADM_DECISION_DELAYED;
		out_decision->reason_code = QHW_ADM_REASON_INSUFFICIENT_RATE;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_DELAY;
		out_decision->retry_after_ns = rate_retry_after(capacity);
		out_decision->message = "insufficient rate";
		return QHW_ADM_OK;
	}

	out_decision->decision = QHW_ADM_DECISION_ACCEPTED;
	out_decision->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_decision->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	out_decision->message = "accepted by rate policy";
	if (out_granted_rate != NULL) {
		*out_granted_rate = granted;
	}
	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_evaluate(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_decision_t *out_decision)
{
	struct rate_state *rate = state;

	if (rate == NULL || device == NULL || request == NULL ||
	    estimate == NULL || capacity == NULL || out_decision == NULL ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	if (rate_device_state_blocks(capacity)) {
		rate_fill_device_blocked(request, capacity, out_decision);
		return QHW_ADM_OK;
	}

	return rate_fill_decision(
		rate,
		device,
		request,
		estimate,
		capacity,
		out_decision,
		NULL);
}

static qhw_adm_rc_t rate_reserve(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_policy_grant_t *out_grant,
	qhw_adm_decision_t *out_decision)
{
	struct rate_state *rate = state;
	uint64_t granted_rate = 0;
	qhw_adm_rc_t rc;

	if (rate == NULL || device == NULL || request == NULL ||
	    estimate == NULL || capacity == NULL || out_grant == NULL ||
	    out_decision == NULL ||
	    out_grant->struct_size < sizeof(*out_grant) ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	memset(out_grant, 0, sizeof(*out_grant));
	out_grant->struct_size = sizeof(*out_grant);
	if (rate_device_state_blocks(capacity)) {
		rate_fill_device_blocked(request, capacity, out_decision);
		return QHW_ADM_OK;
	}

	rc = rate_fill_decision(
		rate,
		device,
		request,
		estimate,
		capacity,
		out_decision,
		&granted_rate);
	if (rc != QHW_ADM_OK ||
	    out_decision->decision != QHW_ADM_DECISION_ACCEPTED) {
		return rc;
	}

	out_grant->device_id = request->device_id;
	out_grant->scope_id = request->scope_id;
	out_grant->rate_granted = granted_rate;
	out_grant->baseline_units_granted = estimate->baseline_units;
	out_grant->ttl_ns = rate->reservation_ttl_ns;
	out_grant->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_grant->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_release(
	void *state,
	const qhw_adm_reservation_t *reservation,
	uint64_t reason_code)
{
	(void)state;
	(void)reservation;
	(void)reason_code;

	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_consume(
	void *state,
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage,
	qhw_adm_decision_t *out_decision)
{
	uint64_t consumed;
	uint64_t requested;
	size_t struct_size;

	(void)state;

	if (reservation == NULL || usage == NULL ||
	    usage->struct_size < sizeof(*usage) ||
	    (usage->metadata_count > 0 && usage->metadata == NULL)) {
		return QHW_ADM_ERR_INVAL;
	}
	if (out_decision != NULL &&
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}
	if (out_decision == NULL) {
		return QHW_ADM_OK;
	}

	consumed = reservation->rate_consumed;
	requested = usage->rate_units;
	if (requested == 0) {
		requested = usage->baseline_units;
	}

	struct_size = out_decision->struct_size;
	memset(out_decision, 0, sizeof(*out_decision));
	out_decision->struct_size = struct_size;
	out_decision->reservation_id = reservation->reservation_id;
	out_decision->device_id = reservation->device_id;
	out_decision->scope_id = reservation->scope_id;
	out_decision->rate_required = requested;
	out_decision->capacity_available = rate_saturating_sub(
		reservation->rate_reserved,
		consumed);
	if (rate_saturating_add(consumed, requested) >
	    reservation->rate_reserved) {
		out_decision->decision = QHW_ADM_DECISION_REJECTED;
		out_decision->reason_code = QHW_ADM_REASON_OVER_LIMIT;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
		out_decision->message = "rate usage exceeds reservation";
		return QHW_ADM_OK;
	}

	out_decision->decision = QHW_ADM_DECISION_ACCEPTED;
	out_decision->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_decision->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	out_decision->message = "rate usage accepted";
	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_return_usage(
	void *state,
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage)
{
	(void)state;

	if (reservation == NULL || usage == NULL ||
	    usage->struct_size < sizeof(*usage) ||
	    (usage->metadata_count > 0 && usage->metadata == NULL)) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t rate_capacity(
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
	out_capacity->effective_available_rate = rate_capped_available(
		core_view->core_available_rate,
		core_view->external_rate_limit,
		core_view->scoped_reserved_rate);
	return QHW_ADM_OK;
}

static const qhw_adm_policy_desc_t rate_desc = {
	.struct_size = sizeof(rate_desc),
	.abi_version = QHW_ADM_ABI_VERSION,
	.name = "rate",
	.capabilities = QHW_ADM_POLICY_CAP_USAGE_ACCOUNTING |
		QHW_ADM_POLICY_CAP_CAPACITY_REPORT |
		QHW_ADM_POLICY_CAP_SCOPED_CAPACITY,
	.init = rate_init,
	.destroy = rate_destroy,
	.configure = rate_configure,
	.evaluate = rate_evaluate,
	.reserve = rate_reserve,
	.release = rate_release,
	.consume = rate_consume,
	.return_usage = rate_return_usage,
	.capacity = rate_capacity,
};

const qhw_adm_policy_desc_t *qhw_adm_policy_plugin(void)
{
	return &rate_desc;
}
