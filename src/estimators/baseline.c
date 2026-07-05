#include "qhw_admission_internal.h"

#include <string.h>

struct qhw_adm_timing {
	uint64_t one_q_gate_ns;
	uint64_t two_q_gate_ns;
	uint64_t measurement_ns;
	uint64_t one_q_gate_transfer_ns;
	uint64_t two_q_gate_transfer_ns;
	uint64_t measurement_transfer_ns;
	uint64_t compile_ns;
	uint64_t control_overhead_ns;
	uint64_t provider_overhead_ns;
};

static qhw_adm_rc_t baseline_init(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_kv_t *options,
	size_t option_count,
	void **out_state)
{
	(void)device;
	(void)options;
	(void)option_count;

	if (out_state == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_state = NULL;
	return QHW_ADM_OK;
}

static void baseline_destroy(void *state)
{
	(void)state;
}

static qhw_adm_rc_t baseline_configure(
	void *state,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	(void)state;

	if (option_count > 0 && options == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t timing_from_metadata(
	const qhw_adm_kv_t *metadata,
	size_t metadata_count,
	struct qhw_adm_timing *timing)
{
	qhw_adm_rc_t rc;

	rc = qhw_adm_metadata_get_u64(metadata, metadata_count,
		QHW_ADM_META_ONE_Q_GATE_NS, timing->one_q_gate_ns,
		&timing->one_q_gate_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_metadata_get_u64(metadata, metadata_count,
		QHW_ADM_META_TWO_Q_GATE_NS, timing->two_q_gate_ns,
		&timing->two_q_gate_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_metadata_get_u64(metadata, metadata_count,
		QHW_ADM_META_MEASUREMENT_NS, timing->measurement_ns,
		&timing->measurement_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_metadata_get_u64(metadata, metadata_count,
		QHW_ADM_META_ONE_Q_GATE_TRANSFER_NS,
		timing->one_q_gate_transfer_ns,
		&timing->one_q_gate_transfer_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_metadata_get_u64(metadata, metadata_count,
		QHW_ADM_META_TWO_Q_GATE_TRANSFER_NS,
		timing->two_q_gate_transfer_ns,
		&timing->two_q_gate_transfer_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_metadata_get_u64(metadata, metadata_count,
		QHW_ADM_META_MEASUREMENT_TRANSFER_NS,
		timing->measurement_transfer_ns,
		&timing->measurement_transfer_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_metadata_get_u64(metadata, metadata_count,
		QHW_ADM_META_COMPILE_NS, timing->compile_ns,
		&timing->compile_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_metadata_get_u64(metadata, metadata_count,
		QHW_ADM_META_CONTROL_OVERHEAD_NS,
		timing->control_overhead_ns,
		&timing->control_overhead_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	return qhw_adm_metadata_get_u64(metadata, metadata_count,
		QHW_ADM_META_PROVIDER_OVERHEAD_NS,
		timing->provider_overhead_ns,
		&timing->provider_overhead_ns);
}

static qhw_adm_rc_t timing_from_device(
	const qhw_adm_device_profile_t *device,
	struct qhw_adm_timing *timing)
{
	if (device == NULL || timing == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	timing->one_q_gate_ns = device->one_q_gate_ns;
	timing->two_q_gate_ns = device->two_q_gate_ns;
	timing->measurement_ns = device->measurement_ns;
	timing->one_q_gate_transfer_ns = device->one_q_gate_transfer_ns;
	timing->two_q_gate_transfer_ns = device->two_q_gate_transfer_ns;
	timing->measurement_transfer_ns = device->measurement_transfer_ns;
	timing->compile_ns = device->compile_ns;
	timing->control_overhead_ns = device->control_overhead_ns;
	timing->provider_overhead_ns = device->provider_overhead_ns;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t accumulate_term(
	uint64_t count,
	uint64_t unit,
	uint64_t *total)
{
	uint64_t term;
	qhw_adm_rc_t rc;

	rc = qhw_adm_mul_u64(count, unit, &term);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	return qhw_adm_add_u64(*total, term, total);
}

static qhw_adm_rc_t estimate_shape(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_qtask_class_t *shape,
	struct qhw_adm_timing timing,
	qhw_adm_estimate_t *out_estimate)
{
	uint64_t gate_ns = 0;
	uint64_t measurement_per_shot_ns;
	uint64_t measurement_total_ns;
	uint64_t per_shot_ns;
	uint64_t execution_ns;
	uint64_t transfer_ns = 0;
	uint64_t total_ns;
	uint64_t repeat_count;
	qhw_adm_rc_t rc;

	if (device == NULL || shape == NULL || out_estimate == NULL ||
	    shape->struct_size < sizeof(*shape) ||
	    qhw_adm_validate_estimate_output(out_estimate) != QHW_ADM_OK ||
	    shape->count == 0 ||
	    shape->qubit_count == 0 ||
	    shape->shots == 0 ||
	    shape->qubit_count > device->max_qubits ||
	    (device->max_shots != 0 && shape->shots > device->max_shots)) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = accumulate_term(
		shape->one_q_gate_count,
		timing.one_q_gate_ns,
		&gate_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = accumulate_term(
		shape->two_q_gate_count,
		timing.two_q_gate_ns,
		&gate_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_mul_u64(
		shape->measurement_count,
		timing.measurement_ns,
		&measurement_per_shot_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(gate_ns, measurement_per_shot_ns, &per_shot_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_mul_u64(per_shot_ns, shape->shots, &execution_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = accumulate_term(
		shape->one_q_gate_count,
		timing.one_q_gate_transfer_ns,
		&transfer_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = accumulate_term(
		shape->two_q_gate_count,
		timing.two_q_gate_transfer_ns,
		&transfer_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = accumulate_term(
		shape->measurement_count,
		timing.measurement_transfer_ns,
		&transfer_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = qhw_adm_add_u64(execution_ns, transfer_ns, &total_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(total_ns, timing.compile_ns, &total_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(
		total_ns,
		timing.control_overhead_ns,
		&total_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(
		total_ns,
		timing.provider_overhead_ns,
		&total_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	repeat_count = shape->count;
	rc = qhw_adm_mul_u64(execution_ns, repeat_count, &execution_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_mul_u64(
		measurement_per_shot_ns,
		shape->shots,
		&measurement_total_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_mul_u64(
		measurement_total_ns,
		repeat_count,
		&measurement_total_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_mul_u64(transfer_ns, repeat_count, &transfer_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_mul_u64(total_ns, repeat_count, &total_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	memset(out_estimate, 0, sizeof(*out_estimate));
	out_estimate->struct_size = sizeof(*out_estimate);
	out_estimate->execution_ns = execution_ns;
	out_estimate->measurement_ns = measurement_total_ns;
	rc = qhw_adm_mul_u64(
		timing.compile_ns,
		repeat_count,
		&out_estimate->compile_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	out_estimate->transfer_ns = transfer_ns;
	rc = qhw_adm_mul_u64(
		timing.control_overhead_ns,
		repeat_count,
		&out_estimate->control_overhead_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	out_estimate->total_ns = total_ns;
	out_estimate->confidence_ppm = 1000000U;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t baseline_to_qtask(
	const qhw_adm_baseline_t *baseline,
	qhw_adm_qtask_class_t *task_class)
{
	if (qhw_adm_validate_baseline(baseline) != QHW_ADM_OK ||
	    task_class == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	memset(task_class, 0, sizeof(*task_class));
	task_class->struct_size = sizeof(*task_class);
	task_class->count = 1;
	task_class->qubit_count = baseline->qubit_count;
	task_class->depth = baseline->depth;
	task_class->one_q_gate_count = baseline->one_q_gate_count;
	task_class->two_q_gate_count = baseline->two_q_gate_count;
	task_class->shots = baseline->shots;
	task_class->measurement_count = baseline->measurement_count;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t estimate_baseline_shape(
	const qhw_adm_device_profile_t *device,
	const qhw_adm_baseline_t *baseline,
	qhw_adm_estimate_t *out_estimate)
{
	struct qhw_adm_timing timing;
	qhw_adm_qtask_class_t task_class;
	qhw_adm_rc_t rc;

	rc = timing_from_device(device, &timing);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = baseline_to_qtask(baseline, &task_class);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = estimate_shape(device, &task_class, timing, out_estimate);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	out_estimate->baseline_units = 1;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t baseline_estimate_task(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_qtask_class_t *task_class,
	qhw_adm_estimate_t *out_estimate)
{
	qhw_adm_estimate_t baseline;
	struct qhw_adm_timing timing;
	qhw_adm_rc_t rc;

	(void)state;

	memset(&baseline, 0, sizeof(baseline));
	baseline.struct_size = sizeof(baseline);

	rc = timing_from_device(device, &timing);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = timing_from_metadata(
		task_class->metadata,
		task_class->metadata_count,
		&timing);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = estimate_shape(device, task_class, timing, out_estimate);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = estimate_baseline_shape(device, &device->baseline, &baseline);
	if (rc != QHW_ADM_OK || baseline.total_ns == 0) {
		return QHW_ADM_ERR_ESTIMATOR;
	}
	rc = qhw_adm_ceil_div_u64(
		out_estimate->total_ns,
		baseline.total_ns,
		&out_estimate->baseline_units);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	if (out_estimate->baseline_units == 0) {
		out_estimate->baseline_units = 1;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t baseline_estimate_baseline(
	void *state,
	const qhw_adm_device_profile_t *device,
	const qhw_adm_baseline_t *baseline,
	qhw_adm_estimate_t *out_estimate)
{
	(void)state;

	return estimate_baseline_shape(device, baseline, out_estimate);
}

static const qhw_adm_estimator_desc_t baseline_desc = {
	.struct_size = sizeof(baseline_desc),
	.abi_version = QHW_ADM_ABI_VERSION,
	.name = "baseline",
	.capabilities = QHW_ADM_EST_CAP_TASK | QHW_ADM_EST_CAP_BASELINE,
	.init = baseline_init,
	.destroy = baseline_destroy,
	.configure = baseline_configure,
	.estimate_task = baseline_estimate_task,
	.estimate_baseline = baseline_estimate_baseline,
};

const qhw_adm_estimator_desc_t *qhw_adm_baseline_estimator_desc(void)
{
	return &baseline_desc;
}
