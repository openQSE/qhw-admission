#include <qhw_admission/qhw_admission.h>

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
	(void)request;
	(void)estimate;
	(void)capacity;
	(void)out_decision;
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
	(void)request;
	(void)estimate;
	(void)capacity;
	(void)out_grant;
	(void)out_decision;
	return QHW_ADM_OK;
}

static const qhw_adm_policy_desc_t bad_desc = {
	.struct_size = sizeof(bad_desc),
	.abi_version = QHW_ADM_ABI_VERSION,
	.name = "bad_caps",
	.capabilities = QHW_ADM_POLICY_CAP_CAPACITY_REPORT,
	.evaluate = bad_evaluate,
	.reserve = bad_reserve,
};

const qhw_adm_policy_desc_t *qhw_adm_policy_plugin(void)
{
	return &bad_desc;
}
