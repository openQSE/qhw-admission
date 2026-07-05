#include <qhw_admission/qhw_admission.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define CREDIT_PPM_SCALE UINT64_C(1000000)
#define CREDIT_MAX_OVERCOMMIT_PPM CREDIT_PPM_SCALE

struct credit_state {
	uint64_t reservation_ttl_ns;
	uint64_t overcommit_credits;
	uint64_t overcommit_ppm;
	int allow_overcommit;
};

static uint64_t credit_saturating_add(uint64_t a, uint64_t b)
{
	if (UINT64_MAX - a < b) {
		return UINT64_MAX;
	}

	return a + b;
}

static uint64_t credit_saturating_sub(uint64_t a, uint64_t b)
{
	if (a < b) {
		return 0;
	}

	return a - b;
}

static uint64_t credit_capped_available(
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

static uint64_t credit_min_nonzero(uint64_t a, uint64_t b)
{
	if (a == 0) {
		return b;
	}
	if (b == 0) {
		return a;
	}
	if (a < b) {
		return a;
	}
	return b;
}

static int credit_option_bool(const qhw_adm_value_t *value, int *out_value)
{
	if (value->type == QHW_ADM_VALUE_BOOL) {
		*out_value = value->value.boolean != 0;
		return 1;
	}
	if (value->type == QHW_ADM_VALUE_U64) {
		*out_value = value->value.u64 != 0;
		return 1;
	}

	return 0;
}

static int credit_option_u64(
	const qhw_adm_value_t *value,
	uint64_t *out_value)
{
	if (value->type != QHW_ADM_VALUE_U64) {
		return 0;
	}

	*out_value = value->value.u64;
	return 1;
}

static qhw_adm_rc_t credit_apply_option(
	struct credit_state *state,
	const qhw_adm_kv_t *option)
{
	if (option->key == QHW_ADM_OPT_CREDIT_RESERVATION_TTL_NS) {
		if (!credit_option_u64(
			&option->value,
			&state->reservation_ttl_ns)) {
			return QHW_ADM_ERR_INVAL;
		}
		return QHW_ADM_OK;
	}
	if (option->key == QHW_ADM_OPT_CREDIT_ALLOW_OVERCOMMIT) {
		if (!credit_option_bool(
			&option->value,
			&state->allow_overcommit)) {
			return QHW_ADM_ERR_INVAL;
		}
		return QHW_ADM_OK;
	}
	if (option->key == QHW_ADM_OPT_CREDIT_OVERCOMMIT_CREDITS) {
		if (!credit_option_u64(
			&option->value,
			&state->overcommit_credits)) {
			return QHW_ADM_ERR_INVAL;
		}
		return QHW_ADM_OK;
	}
	if (option->key == QHW_ADM_OPT_CREDIT_OVERCOMMIT_PPM) {
		if (!credit_option_u64(
			&option->value,
			&state->overcommit_ppm)) {
			return QHW_ADM_ERR_INVAL;
		}
		if (state->overcommit_ppm > CREDIT_MAX_OVERCOMMIT_PPM) {
			return QHW_ADM_ERR_INVAL;
		}
		return QHW_ADM_OK;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t credit_configure_state(
	struct credit_state *state,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	size_t i;

	if (state == NULL || (option_count > 0 && options == NULL)) {
		return QHW_ADM_ERR_INVAL;
	}

	for (i = 0; i < option_count; i++) {
		qhw_adm_rc_t rc;

		rc = credit_apply_option(state, &options[i]);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t credit_ppm_value(
	uint64_t total_credits,
	uint64_t overcommit_ppm,
	uint64_t *out_value)
{
	uint64_t quotient;
	uint64_t remainder;
	uint64_t high;
	uint64_t low;

	if (out_value == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	quotient = total_credits / CREDIT_PPM_SCALE;
	remainder = total_credits % CREDIT_PPM_SCALE;
	if (quotient != 0 && overcommit_ppm > UINT64_MAX / quotient) {
		return QHW_ADM_ERR_INVAL;
	}
	high = quotient * overcommit_ppm;
	if (remainder != 0 && overcommit_ppm > UINT64_MAX / remainder) {
		return QHW_ADM_ERR_INVAL;
	}
	low = (remainder * overcommit_ppm) / CREDIT_PPM_SCALE;
	if (UINT64_MAX - high < low) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_value = high + low;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t credit_overcommit_limit(
	const struct credit_state *state,
	uint64_t total_credits,
	uint64_t *out_limit)
{
	uint64_t ppm_limit = 0;
	uint64_t configured;

	if (state == NULL || out_limit == NULL) {
		return QHW_ADM_ERR_INVAL;
	}
	if (total_credits == 0) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_limit = total_credits;
	if (!state->allow_overcommit) {
		return QHW_ADM_OK;
	}

	if (state->overcommit_ppm != 0) {
		qhw_adm_rc_t rc;

		rc = credit_ppm_value(
			total_credits,
			state->overcommit_ppm,
			&ppm_limit);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
	}

	configured = credit_min_nonzero(
		state->overcommit_credits,
		ppm_limit);
	if (UINT64_MAX - total_credits < configured) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_limit = total_credits + configured;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t credit_validate_state(
	const struct credit_state *state,
	const qhw_adm_device_profile_t *device)
{
	uint64_t limit;

	if (state == NULL || device == NULL) {
		return QHW_ADM_ERR_INVAL;
	}
	if (device->total_credits == 0) {
		return QHW_ADM_OK;
	}

	return credit_overcommit_limit(state, device->total_credits, &limit);
}

static qhw_adm_rc_t credit_init(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_kv_t *options,
	size_t option_count,
	void **out_state)
{
	struct credit_state *state;
	qhw_adm_rc_t rc;

	if (device == NULL || out_state == NULL ||
	    (device->total_credits == 0 && device->time_span_ns == 0) ||
	    (option_count > 0 && options == NULL)) {
		return QHW_ADM_ERR_INVAL;
	}

	state = calloc(1, sizeof(*state));
	if (state == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	rc = credit_configure_state(state, options, option_count);
	if (rc != QHW_ADM_OK) {
		free(state);
		return rc;
	}

	rc = credit_validate_state(state, device);
	if (rc != QHW_ADM_OK) {
		free(state);
		return rc;
	}

	*out_state = state;
	return QHW_ADM_OK;
}

static void credit_destroy(void *state)
{
	free(state);
}

static qhw_adm_rc_t credit_configure(
	void *state,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	return credit_configure_state(state, options, option_count);
}

static uint64_t credit_retry_after(
	const qhw_adm_capacity_view_t *capacity)
{
	if (capacity->next_available_ns <= capacity->now_ns) {
		return 0;
	}

	return capacity->next_available_ns - capacity->now_ns;
}

static void credit_fill_device_blocked(
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
		out_decision->retry_after_ns = credit_retry_after(capacity);
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

static int credit_device_state_blocks(
	const qhw_adm_capacity_view_t *capacity)
{
	return capacity->device_state != QHW_ADM_DEVICE_AVAILABLE;
}

static qhw_adm_rc_t credit_fill_decision(
	const struct credit_state *state,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_decision_t *out_decision)
{
	uint64_t effective_limit;
	uint64_t credits_required;
	size_t struct_size;
	qhw_adm_rc_t rc;

	credits_required = estimate->baseline_units;
	struct_size = out_decision->struct_size;
	memset(out_decision, 0, sizeof(*out_decision));
	out_decision->struct_size = struct_size;
	out_decision->request_id = request->request_id;
	out_decision->device_id = request->device_id;
	out_decision->scope_id = request->scope_id;
	out_decision->credits_required = credits_required;
	out_decision->capacity_available = capacity->effective_available_credits;
	out_decision->estimated_total_ns = estimate->total_ns;
	out_decision->capacity_granted = credits_required;
	out_decision->confidence_ppm = estimate->confidence_ppm;

	if (capacity->total_credits == 0) {
		out_decision->decision = QHW_ADM_DECISION_REJECTED;
		out_decision->reason_code = QHW_ADM_REASON_REQUEST_TOO_LARGE;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
		out_decision->message = "credit capacity is unavailable";
		return QHW_ADM_OK;
	}

	rc = credit_overcommit_limit(
		state,
		capacity->total_credits,
		&effective_limit);
	if (rc != QHW_ADM_OK) {
		out_decision->decision = QHW_ADM_DECISION_REJECTED;
		out_decision->reason_code = QHW_ADM_REASON_INVALID_REQUEST;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
		out_decision->message = "credit overcommit limit is invalid";
		return QHW_ADM_OK;
	}

	if (credits_required > effective_limit) {
		out_decision->decision = QHW_ADM_DECISION_REJECTED;
		out_decision->reason_code = QHW_ADM_REASON_REQUEST_TOO_LARGE;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
		out_decision->message = "credit request exceeds policy limit";
		return QHW_ADM_OK;
	}
	if (credits_required > capacity->effective_available_credits) {
		out_decision->decision = QHW_ADM_DECISION_DELAYED;
		out_decision->reason_code =
			QHW_ADM_REASON_INSUFFICIENT_CREDITS;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_DELAY;
		out_decision->retry_after_ns = credit_retry_after(capacity);
		out_decision->message = "insufficient credits";
		return QHW_ADM_OK;
	}

	out_decision->decision = QHW_ADM_DECISION_ACCEPTED;
	out_decision->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_decision->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	out_decision->message = "accepted by credit policy";
	return QHW_ADM_OK;
}

static qhw_adm_rc_t credit_evaluate(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_decision_t *out_decision)
{
	struct credit_state *credit = state;

	(void)device;

	if (credit == NULL || request == NULL || estimate == NULL ||
	    capacity == NULL || out_decision == NULL ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	if (credit_device_state_blocks(capacity)) {
		credit_fill_device_blocked(request, capacity, out_decision);
		return QHW_ADM_OK;
	}

	return credit_fill_decision(
		credit,
		request,
		estimate,
		capacity,
		out_decision);
}

static qhw_adm_rc_t credit_reserve(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_policy_grant_t *out_grant,
	qhw_adm_decision_t *out_decision)
{
	struct credit_state *credit = state;
	qhw_adm_rc_t rc;

	(void)device;

	if (credit == NULL || request == NULL || estimate == NULL ||
	    capacity == NULL || out_grant == NULL || out_decision == NULL ||
	    out_grant->struct_size < sizeof(*out_grant) ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	memset(out_grant, 0, sizeof(*out_grant));
	out_grant->struct_size = sizeof(*out_grant);
	if (credit_device_state_blocks(capacity)) {
		credit_fill_device_blocked(request, capacity, out_decision);
		return QHW_ADM_OK;
	}

	rc = credit_fill_decision(
		credit,
		request,
		estimate,
		capacity,
		out_decision);
	if (rc != QHW_ADM_OK ||
	    out_decision->decision != QHW_ADM_DECISION_ACCEPTED) {
		return rc;
	}

	out_grant->device_id = request->device_id;
	out_grant->scope_id = request->scope_id;
	out_grant->credits_granted = estimate->baseline_units;
	out_grant->baseline_units_granted = estimate->baseline_units;
	out_grant->ttl_ns = credit->reservation_ttl_ns;
	out_grant->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_grant->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t credit_release(
	void *state,
	const qhw_adm_reservation_t *reservation,
	uint64_t reason_code)
{
	(void)state;
	(void)reservation;
	(void)reason_code;

	return QHW_ADM_OK;
}

static qhw_adm_rc_t credit_consume(
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

	consumed = reservation->credits_consumed;
	requested = usage->credits;
	if (requested == 0) {
		requested = usage->baseline_units;
	}

	struct_size = out_decision->struct_size;
	memset(out_decision, 0, sizeof(*out_decision));
	out_decision->struct_size = struct_size;
	out_decision->reservation_id = reservation->reservation_id;
	out_decision->device_id = reservation->device_id;
	out_decision->scope_id = reservation->scope_id;
	out_decision->credits_required = requested;
	out_decision->capacity_available = credit_saturating_sub(
		reservation->credits_reserved,
		consumed);
	if (credit_saturating_add(consumed, requested) >
	    reservation->credits_reserved) {
		out_decision->decision = QHW_ADM_DECISION_REJECTED;
		out_decision->reason_code = QHW_ADM_REASON_OVER_LIMIT;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
		out_decision->message = "credit usage exceeds reservation";
		return QHW_ADM_OK;
	}

	out_decision->decision = QHW_ADM_DECISION_ACCEPTED;
	out_decision->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_decision->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	out_decision->message = "credit usage accepted";
	return QHW_ADM_OK;
}

static qhw_adm_rc_t credit_return_usage(
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

static qhw_adm_rc_t credit_capacity(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_capacity_view_t *core_view,
	qhw_adm_capacity_view_t *out_capacity)
{
	struct credit_state *credit = state;
	uint64_t effective_limit;
	qhw_adm_rc_t rc;

	(void)device;

	if (credit == NULL || core_view == NULL || out_capacity == NULL ||
	    out_capacity->struct_size < sizeof(*out_capacity)) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_capacity = *core_view;
	if (core_view->total_credits == 0) {
		out_capacity->core_available_credits = 0;
		out_capacity->effective_available_credits = 0;
		return QHW_ADM_OK;
	}

	rc = credit_overcommit_limit(
		credit,
		core_view->total_credits,
		&effective_limit);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	out_capacity->core_available_credits = credit_saturating_sub(
		effective_limit,
		core_view->credits_reserved);
	out_capacity->effective_available_credits = credit_capped_available(
		out_capacity->core_available_credits,
		core_view->external_credit_limit,
		core_view->scoped_reserved_credits);
	return QHW_ADM_OK;
}

static const qhw_adm_policy_desc_t credit_desc = {
	.struct_size = sizeof(credit_desc),
	.abi_version = QHW_ADM_ABI_VERSION,
	.name = "credit",
	.capabilities = QHW_ADM_POLICY_CAP_USAGE_ACCOUNTING |
		QHW_ADM_POLICY_CAP_CAPACITY_REPORT |
		QHW_ADM_POLICY_CAP_SCOPED_CAPACITY,
	.init = credit_init,
	.destroy = credit_destroy,
	.configure = credit_configure,
	.evaluate = credit_evaluate,
	.reserve = credit_reserve,
	.release = credit_release,
	.authorize_usage = credit_consume,
	.consume = credit_consume,
	.return_usage = credit_return_usage,
	.capacity = credit_capacity,
};

const qhw_adm_policy_desc_t *qhw_adm_policy_plugin(void)
{
	return &credit_desc;
}
