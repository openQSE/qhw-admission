#include <qhw_admission/qhw_admission.h>

#include <stdlib.h>
#include <string.h>

#define MOCK_REJECT_REQUEST_ID 43

struct mock_policy_state {
	uint64_t release_count;
};

static const qhw_adm_kv_t mock_decision_metadata[] = {
	{
		.key = 1001,
		.value = {
			.type = QHW_ADM_VALUE_STRING,
			.value.string = "decision-metadata",
		},
	},
};

static const qhw_adm_kv_t mock_grant_metadata[] = {
	{
		.key = 1002,
		.value = {
			.type = QHW_ADM_VALUE_STRING,
			.value.string = "grant-metadata",
		},
	},
};

static const qhw_adm_kv_t mock_capacity_metadata[] = {
	{
		.key = 1003,
		.value = {
			.type = QHW_ADM_VALUE_STRING,
			.value.string = "capacity-metadata",
		},
	},
};

static qhw_adm_rc_t mock_init(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_kv_t *options,
	size_t option_count,
	void **out_state)
{
	struct mock_policy_state *state;

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

static void mock_destroy(void *state)
{
	free(state);
}

static void fill_accepted(
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
	out_decision->credits_required = estimate->baseline_units;
	out_decision->rate_required = estimate->baseline_units;
	out_decision->capacity_available = capacity->effective_available_credits;
	out_decision->estimated_total_ns = estimate->total_ns;
	out_decision->capacity_granted = estimate->baseline_units;
	out_decision->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	out_decision->confidence_ppm = estimate->confidence_ppm;
	out_decision->message = "mock accepted";
	out_decision->metadata = mock_decision_metadata;
	out_decision->metadata_count = 1;
}

static qhw_adm_rc_t mock_evaluate(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_decision_t *out_decision)
{
	(void)state;
	(void)device;

	if (request == NULL || estimate == NULL || capacity == NULL ||
	    out_decision == NULL ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	fill_accepted(request, estimate, capacity, out_decision);
	return QHW_ADM_OK;
}

static qhw_adm_rc_t mock_reserve(
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

	if (request == NULL || estimate == NULL || capacity == NULL ||
	    out_grant == NULL || out_decision == NULL ||
	    out_grant->struct_size < sizeof(*out_grant) ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	memset(out_grant, 0, sizeof(*out_grant));
	out_grant->struct_size = sizeof(*out_grant);
	if (request->request_id == MOCK_REJECT_REQUEST_ID) {
		size_t struct_size = out_decision->struct_size;

		memset(out_decision, 0, sizeof(*out_decision));
		out_decision->struct_size = struct_size;
		out_decision->decision = QHW_ADM_DECISION_REJECTED;
		out_decision->reason_code = QHW_ADM_REASON_SCOPE_LIMIT;
		out_decision->compliance_action = QHW_ADM_COMPLIANCE_REJECT;
		out_decision->message = "mock rejected";
		out_decision->metadata = mock_decision_metadata;
		out_decision->metadata_count = 1;
		return QHW_ADM_OK;
	}

	out_grant->device_id = request->device_id;
	out_grant->scope_id = request->scope_id;
	out_grant->credits_granted = estimate->baseline_units;
	out_grant->rate_granted = estimate->baseline_units;
	out_grant->baseline_units_granted = estimate->baseline_units;
	out_grant->reason_code = QHW_ADM_REASON_ACCEPTED;
	out_grant->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	out_grant->metadata = mock_grant_metadata;
	out_grant->metadata_count = 1;

	fill_accepted(request, estimate, capacity, out_decision);
	return QHW_ADM_OK;
}

static qhw_adm_rc_t mock_release(
	void *state,
	const qhw_adm_reservation_t *reservation,
	uint64_t reason_code)
{
	struct mock_policy_state *mock_state = state;

	(void)reservation;
	(void)reason_code;

	if (mock_state != NULL) {
		mock_state->release_count++;
	}
	return QHW_ADM_OK;
}

static qhw_adm_rc_t mock_capacity(
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
	out_capacity->scheduler_policy_id = 1234;
	out_capacity->metadata = mock_capacity_metadata;
	out_capacity->metadata_count = 1;
	return QHW_ADM_OK;
}

static const qhw_adm_policy_desc_t mock_desc = {
	.struct_size = sizeof(mock_desc),
	.abi_version = QHW_ADM_ABI_VERSION,
	.name = "mock",
	.capabilities = QHW_ADM_POLICY_CAP_CAPACITY_REPORT,
	.init = mock_init,
	.destroy = mock_destroy,
	.evaluate = mock_evaluate,
	.reserve = mock_reserve,
	.release = mock_release,
	.capacity = mock_capacity,
};

const qhw_adm_policy_desc_t *qhw_adm_policy_plugin(void)
{
	return &mock_desc;
}
