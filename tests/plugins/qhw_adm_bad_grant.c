#include <qhw_admission/qhw_admission.h>

#include <string.h>

static qhw_adm_rc_t bad_evaluate(
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

	memset(out_decision, 0, sizeof(*out_decision));
	out_decision->struct_size = sizeof(*out_decision);
	out_decision->decision = QHW_ADM_DECISION_ACCEPTED;
	out_decision->request_id = request->request_id;
	out_decision->device_id = request->device_id;
	out_decision->scope_id = request->scope_id;
	out_decision->capacity_granted = estimate->baseline_units;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t bad_reserve(
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

	memset(out_grant, 0, sizeof(*out_grant));
	out_grant->struct_size = sizeof(*out_grant);
	out_grant->device_id = request->device_id;
	out_grant->scope_id = request->scope_id;
	out_grant->credits_granted = estimate->baseline_units;
	out_grant->rate_granted = estimate->baseline_units;
	out_grant->baseline_units_granted = estimate->baseline_units + 1;

	return bad_evaluate(
		state,
		device,
		request,
		estimate,
		capacity,
		out_decision);
}

static const qhw_adm_policy_desc_t bad_desc = {
	.struct_size = sizeof(bad_desc),
	.abi_version = QHW_ADM_ABI_VERSION,
	.name = "bad_grant",
	.evaluate = bad_evaluate,
	.reserve = bad_reserve,
};

const qhw_adm_policy_desc_t *qhw_adm_policy_plugin(void)
{
	return &bad_desc;
}
