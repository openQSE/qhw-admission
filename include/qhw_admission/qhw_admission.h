#ifndef QHW_ADMISSION_H
#define QHW_ADMISSION_H

#include <qhw_admission/qhw_admission_types.h>

#ifdef __cplusplus
extern "C" {
#endif

qhw_adm_rc_t qhw_adm_create(
	const qhw_adm_attr_t *attr,
	qhw_adm_t **out_ctx);

void qhw_adm_destroy(qhw_adm_t *ctx);

const char *qhw_adm_last_error(const qhw_adm_t *ctx);

qhw_adm_rc_t qhw_adm_get_threading(
	qhw_adm_t *ctx,
	qhw_adm_threading_t *out_threading);

qhw_adm_rc_t qhw_adm_load_config(
	qhw_adm_t *ctx,
	const char *path,
	uint64_t flags);

qhw_adm_rc_t qhw_adm_load_config_string(
	qhw_adm_t *ctx,
	const char *yaml_text,
	size_t yaml_len,
	uint64_t flags);

qhw_adm_rc_t qhw_adm_register_device(
	qhw_adm_t *ctx,
	const qhw_adm_device_profile_t *profile);

qhw_adm_rc_t qhw_adm_unregister_device(
	qhw_adm_t *ctx,
	uint64_t device_id);

qhw_adm_rc_t qhw_adm_get_device(
	qhw_adm_t *ctx,
	uint64_t device_id,
	qhw_adm_device_profile_t *out_profile);

qhw_adm_rc_t qhw_adm_set_baseline(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const qhw_adm_baseline_t *baseline);

qhw_adm_rc_t qhw_adm_set_estimator(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const char *name,
	const qhw_adm_kv_t *options,
	size_t option_count);

qhw_adm_rc_t qhw_adm_load_estimator(
	qhw_adm_t *ctx,
	const char *path,
	qhw_adm_estimator_t **out_estimator);

qhw_adm_rc_t qhw_adm_add_estimator_path(
	qhw_adm_t *ctx,
	const char *path);

qhw_adm_rc_t qhw_adm_estimate_qtask_class(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const qhw_adm_qtask_class_t *task_class,
	qhw_adm_estimate_t *out_estimate);

qhw_adm_rc_t qhw_adm_estimate_baseline(
	qhw_adm_t *ctx,
	uint64_t device_id,
	qhw_adm_estimate_t *out_estimate);

#ifdef __cplusplus
}
#endif

#endif
