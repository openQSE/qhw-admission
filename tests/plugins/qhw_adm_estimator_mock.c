#include <qhw_admission/qhw_admission.h>

#include <stdlib.h>
#include <string.h>

struct mock_state {
	uint64_t total_ns;
	uint64_t baseline_units;
};

static qhw_adm_rc_t mock_init(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_kv_t *options,
	size_t option_count,
	void **out_state)
{
	struct mock_state *state;
	size_t i;

	(void)device;

	if (out_state == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (option_count > 0 && options == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	state = calloc(1, sizeof(*state));
	if (state == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	state->total_ns = 333;
	state->baseline_units = 7;
	for (i = 0; i < option_count; i++) {
		if (options[i].key == QHW_ADM_META_OBSERVED_DEVICE_NS &&
		    options[i].value.type == QHW_ADM_VALUE_U64) {
			state->total_ns = options[i].value.value.u64;
		}
		if (options[i].key == QHW_ADM_META_CONSUMED_CREDITS &&
		    options[i].value.type == QHW_ADM_VALUE_U64) {
			state->baseline_units = options[i].value.value.u64;
		}
	}

	*out_state = state;
	return QHW_ADM_OK;
}

static void mock_destroy(void *state)
{
	free(state);
}

static qhw_adm_rc_t mock_estimate_task(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_qtask_class_t *task_class,
	qhw_adm_estimate_t *out_estimate)
{
	const struct mock_state *mock_state = state;

	(void)device;
	(void)task_class;

	if (out_estimate == NULL ||
	    out_estimate->struct_size < sizeof(*out_estimate)) {
		return QHW_ADM_ERR_INVAL;
	}

	memset(out_estimate, 0, sizeof(*out_estimate));
	out_estimate->struct_size = sizeof(*out_estimate);
	out_estimate->execution_ns = 111;
	out_estimate->measurement_ns = 22;
	out_estimate->compile_ns = 33;
	out_estimate->transfer_ns = 44;
	out_estimate->control_overhead_ns = 55;
	out_estimate->total_ns = mock_state->total_ns;
	out_estimate->baseline_units = mock_state->baseline_units;
	out_estimate->confidence_ppm = 900000U;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t mock_estimate_baseline(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_baseline_t *baseline,
	qhw_adm_estimate_t *out_estimate)
{
	const struct mock_state *mock_state = state;

	(void)device;
	(void)baseline;

	if (out_estimate == NULL ||
	    out_estimate->struct_size < sizeof(*out_estimate)) {
		return QHW_ADM_ERR_INVAL;
	}

	memset(out_estimate, 0, sizeof(*out_estimate));
	out_estimate->struct_size = sizeof(*out_estimate);
	out_estimate->total_ns = mock_state->total_ns;
	out_estimate->baseline_units = mock_state->baseline_units;
	out_estimate->confidence_ppm = 900000U;
	return QHW_ADM_OK;
}

static const qhw_adm_estimator_desc_t mock_desc = {
	.struct_size = sizeof(mock_desc),
	.abi_version = QHW_ADM_ABI_VERSION,
	.name = "mock",
	.capabilities = QHW_ADM_EST_CAP_TASK | QHW_ADM_EST_CAP_BASELINE,
	.init = mock_init,
	.destroy = mock_destroy,
	.estimate_task = mock_estimate_task,
	.estimate_baseline = mock_estimate_baseline,
};

const qhw_adm_estimator_desc_t *qhw_adm_estimator_plugin(void)
{
	return &mock_desc;
}
