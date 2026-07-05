#include <qhw_admission/qhw_admission.h>

#include <string.h>

static const qhw_adm_kv_t error_metadata[] = {
	{
		.key = 2001,
		.value = {
			.type = QHW_ADM_VALUE_STRING,
			.value.string = "raw-error-metadata",
		},
	},
};

static void fill_error_output(qhw_adm_decision_t *out_decision)
{
	size_t struct_size = out_decision->struct_size;

	memset(out_decision, 0, sizeof(*out_decision));
	out_decision->struct_size = struct_size;
	out_decision->decision = QHW_ADM_DECISION_REJECTED;
	out_decision->reason_code = QHW_ADM_REASON_POLICY_FAILED;
	out_decision->message = "raw-error-message";
	out_decision->metadata = error_metadata;
	out_decision->metadata_count = 1;
}

static qhw_adm_rc_t error_evaluate(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_decision_t *out_decision)
{
	(void)state;
	(void)device;
	(void)request;
	(void)estimate;
	(void)capacity;

	if (out_decision == NULL ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	fill_error_output(out_decision);
	return QHW_ADM_ERR_POLICY;
}

static qhw_adm_rc_t error_reserve(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	qhw_adm_policy_grant_t *out_grant,
	qhw_adm_decision_t *out_decision)
{
	(void)out_grant;
	return error_evaluate(
		state,
		device,
		request,
		estimate,
		capacity,
		out_decision);
}

static const qhw_adm_policy_desc_t error_desc = {
	.struct_size = sizeof(error_desc),
	.abi_version = QHW_ADM_ABI_VERSION,
	.name = "error_output",
	.evaluate = error_evaluate,
	.reserve = error_reserve,
};

const qhw_adm_policy_desc_t *qhw_adm_policy_plugin(void)
{
	return &error_desc;
}
