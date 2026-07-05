#include "qhw_admission_internal.h"

#include <stdlib.h>
#include <string.h>

qhw_adm_rc_t qhw_adm_validate_baseline(const qhw_adm_baseline_t *baseline)
{
	if (baseline == NULL ||
	    baseline->struct_size < sizeof(*baseline) ||
	    baseline->qubit_count == 0 ||
	    baseline->shots == 0 ||
	    baseline->measurement_count == 0) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_validate_device_profile(
	const qhw_adm_device_profile_t *profile)
{
	if (profile == NULL ||
	    profile->struct_size < sizeof(*profile) ||
	    profile->device_id == 0 ||
	    profile->max_qubits == 0 ||
	    profile->baseline.qubit_count > profile->max_qubits ||
	    (profile->max_shots != 0 &&
	     profile->baseline.shots > profile->max_shots) ||
	    profile->one_q_gate_ns == 0 ||
	    profile->two_q_gate_ns == 0 ||
	    profile->measurement_ns == 0 ||
	    (profile->metadata_count > 0 && profile->metadata == NULL)) {
		return QHW_ADM_ERR_INVAL;
	}

	return qhw_adm_validate_baseline(&profile->baseline);
}

qhw_adm_rc_t qhw_adm_validate_profile_output(
	const qhw_adm_device_profile_t *out_profile)
{
	if (out_profile == NULL ||
	    out_profile->struct_size < sizeof(*out_profile)) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static void qhw_adm_free_device_contents(
	struct qhw_adm_device_entry *entry)
{
	if (entry == NULL) {
		return;
	}

	if (entry->estimator != NULL && entry->estimator->destroy != NULL) {
		entry->estimator->destroy(entry->estimator_state);
	}

	qhw_adm_free_metadata_count(
		entry->metadata,
		entry->profile.metadata_count);
	memset(entry, 0, sizeof(*entry));
}

void qhw_adm_free_device_entry(void *value, void *user_data)
{
	struct qhw_adm_device_entry *entry = value;

	(void)user_data;

	qhw_adm_free_device_contents(entry);
	free(entry);
}

qhw_adm_rc_t qhw_adm_create_device_entry(
	const qhw_adm_device_profile_t *profile,
	struct qhw_adm_device_entry **out_entry)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_rc_t rc;

	if (out_entry == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_entry = NULL;
	rc = qhw_adm_validate_device_profile(profile);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	entry->profile = *profile;
	rc = qhw_adm_copy_metadata(
		profile->metadata,
		profile->metadata_count,
		&entry->metadata);
	if (rc != QHW_ADM_OK) {
		free(entry);
		return rc;
	}
	entry->profile.metadata = entry->metadata;
	entry->profile_version = 1;
	entry->estimator = qhw_adm_baseline_estimator_desc();
	rc = qhw_adm_validate_estimator_desc(entry->estimator);
	if (rc != QHW_ADM_OK) {
		qhw_adm_free_device_entry(entry, NULL);
		return rc;
	}
	if (entry->estimator->init != NULL) {
		rc = entry->estimator->init(
			&entry->profile,
			NULL,
			0,
			&entry->estimator_state);
		if (rc != QHW_ADM_OK) {
			qhw_adm_free_device_entry(entry, NULL);
			return rc;
		}
	}
	entry->estimator_version = 1;

	*out_entry = entry;
	return QHW_ADM_OK;
}

void qhw_adm_replace_device_entry(
	struct qhw_adm_device_entry *dst,
	struct qhw_adm_device_entry *src)
{
	uint64_t profile_version = dst->profile_version + 1;

	qhw_adm_free_device_contents(dst);
	*dst = *src;
	dst->profile_version = profile_version;
	free(src);
}

qhw_adm_rc_t qhw_adm_clone_device_entry(
	const struct qhw_adm_device_entry *src,
	struct qhw_adm_device_entry **out_entry)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_rc_t rc;

	if (src == NULL || out_entry == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_entry = NULL;
	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	entry->profile = src->profile;
	rc = qhw_adm_copy_metadata(
		src->profile.metadata,
		src->profile.metadata_count,
		&entry->metadata);
	if (rc != QHW_ADM_OK) {
		free(entry);
		return rc;
	}
	entry->profile.metadata = entry->metadata;
	entry->profile_version = src->profile_version;
	entry->estimator = src->estimator;
	entry->estimator_version = src->estimator_version;
	if (entry->estimator != NULL && entry->estimator->init != NULL) {
		rc = entry->estimator->init(
			&entry->profile,
			NULL,
			0,
			&entry->estimator_state);
		if (rc != QHW_ADM_OK) {
			qhw_adm_free_device_entry(entry, NULL);
			return rc;
		}
	}

	*out_entry = entry;
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_register_device(
	qhw_adm_t *ctx,
	const qhw_adm_device_profile_t *profile)
{
	struct qhw_adm_device_entry *new_entry;
	struct qhw_adm_device_entry *entry;
	qhw_adm_rc_t rc;

	if (ctx == NULL || profile == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_create_device_entry(profile, &new_entry);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		qhw_adm_free_device_entry(new_entry, NULL);
		return rc;
	}

	entry = qhw_hash_table_find(&ctx->devices, profile->device_id);
	if (entry != NULL) {
		qhw_adm_replace_device_entry(entry, new_entry);
		qhw_adm_clear_error(ctx);
		qhw_adm_unlock(ctx);
		return QHW_ADM_OK;
	}

	if (qhw_hash_table_insert(
		&ctx->devices,
		profile->device_id,
		new_entry) != 0) {
		qhw_adm_free_device_entry(new_entry, NULL);
		qhw_adm_set_error(ctx, "failed to register device");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOMEM;
	}

	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_unregister_device(qhw_adm_t *ctx, uint64_t device_id)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_rc_t rc;

	if (ctx == NULL || device_id == 0) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	entry = qhw_hash_table_remove(&ctx->devices, device_id);
	if (entry == NULL) {
		qhw_adm_set_error(ctx, "device was not found");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOT_FOUND;
	}

	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	qhw_adm_free_device_entry(entry, NULL);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_get_device(
	qhw_adm_t *ctx,
	uint64_t device_id,
	qhw_adm_device_profile_t *out_profile)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_rc_t rc;

	if (ctx == NULL || device_id == 0 ||
	    qhw_adm_validate_profile_output(out_profile) != QHW_ADM_OK) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	entry = qhw_hash_table_find(&ctx->devices, device_id);
	if (entry == NULL) {
		qhw_adm_set_error(ctx, "device was not found");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOT_FOUND;
	}

	*out_profile = entry->profile;
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_set_baseline(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const qhw_adm_baseline_t *baseline)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_rc_t rc;

	if (ctx == NULL || device_id == 0 ||
	    qhw_adm_validate_baseline(baseline) != QHW_ADM_OK) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	entry = qhw_hash_table_find(&ctx->devices, device_id);
	if (entry == NULL) {
		qhw_adm_set_error(ctx, "device was not found");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOT_FOUND;
	}

	if (baseline->qubit_count > entry->profile.max_qubits ||
	    (entry->profile.max_shots != 0 &&
	     baseline->shots > entry->profile.max_shots)) {
		qhw_adm_set_error(ctx, "baseline exceeds device limits");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_INVAL;
	}

	entry->profile.baseline = *baseline;
	entry->profile_version++;
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}
