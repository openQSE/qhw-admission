#ifndef QHW_ADMISSION_INTERNAL_H
#define QHW_ADMISSION_INTERNAL_H

#include <pthread.h>
#include <stddef.h>

#include <qhw_datastructures/qhw_hash_table.h>
#include <qhw_datastructures/qhw_list.h>

#include <qhw_admission/qhw_admission.h>

#define QHW_ADM_ERROR_LEN 256

struct qhw_adm_device_entry {
	qhw_adm_device_profile_t profile;
	qhw_adm_kv_t *metadata;
	uint64_t profile_version;
	const qhw_adm_estimator_desc_t *estimator;
	void *estimator_state;
	uint64_t estimator_version;
};

struct qhw_adm_estimator {
	char *name;
	char *path;
	void *handle;
	const qhw_adm_estimator_desc_t *desc;
};

struct qhw_adm {
	qhw_adm_threading_t threading;
	qhw_adm_kv_t *options;
	size_t option_count;
	struct qhw_hash_table devices;
	struct qhw_hash_table reservations;
	struct qhw_hash_table policies;
	struct qhw_hash_table estimators;
	char **estimator_paths;
	size_t estimator_path_count;
	int registries_initialized;
	char last_error[QHW_ADM_ERROR_LEN];
	pthread_mutex_t lock;
	int lock_initialized;
};

qhw_adm_rc_t qhw_adm_validate_attr(const qhw_adm_attr_t *attr);

qhw_adm_rc_t qhw_adm_copy_metadata(
	const qhw_adm_kv_t *src,
	size_t count,
	qhw_adm_kv_t **out_copy);

char *qhw_adm_strdup(const char *src);

void qhw_adm_free_metadata_count(qhw_adm_kv_t *metadata, size_t count);

qhw_adm_rc_t qhw_adm_metadata_get_u64(
	const qhw_adm_kv_t *metadata,
	size_t count,
	uint64_t key,
	uint64_t fallback,
	uint64_t *out_value);

qhw_adm_rc_t qhw_adm_init_registries(qhw_adm_t *ctx);

void qhw_adm_fini_registries(qhw_adm_t *ctx);

void qhw_adm_set_error(qhw_adm_t *ctx, const char *message);

void qhw_adm_clear_error(qhw_adm_t *ctx);

qhw_adm_rc_t qhw_adm_lock(qhw_adm_t *ctx);

void qhw_adm_unlock(qhw_adm_t *ctx);

qhw_adm_rc_t qhw_adm_add_u64(uint64_t a, uint64_t b, uint64_t *out);

qhw_adm_rc_t qhw_adm_mul_u64(uint64_t a, uint64_t b, uint64_t *out);

qhw_adm_rc_t qhw_adm_ceil_div_u64(uint64_t a, uint64_t b, uint64_t *out);

qhw_adm_rc_t qhw_adm_validate_baseline(const qhw_adm_baseline_t *baseline);

qhw_adm_rc_t qhw_adm_validate_device_profile(
	const qhw_adm_device_profile_t *profile);

qhw_adm_rc_t qhw_adm_validate_estimate_output(
	const qhw_adm_estimate_t *out_estimate);

qhw_adm_rc_t qhw_adm_validate_profile_output(
	const qhw_adm_device_profile_t *out_profile);

qhw_adm_rc_t qhw_adm_create_device_entry(
	const qhw_adm_device_profile_t *profile,
	struct qhw_adm_device_entry **out_entry);

void qhw_adm_replace_device_entry(
	struct qhw_adm_device_entry *dst,
	struct qhw_adm_device_entry *src);

qhw_adm_rc_t qhw_adm_clone_device_entry(
	const struct qhw_adm_device_entry *src,
	struct qhw_adm_device_entry **out_entry);

void qhw_adm_free_device_entry(void *value, void *user_data);

const qhw_adm_estimator_desc_t *qhw_adm_baseline_estimator_desc(void);

qhw_adm_rc_t qhw_adm_validate_estimator_desc(
	const qhw_adm_estimator_desc_t *desc);

qhw_adm_rc_t qhw_adm_register_builtin_estimators(qhw_adm_t *ctx);

uint64_t qhw_adm_hash_string(const char *str);

qhw_adm_rc_t qhw_adm_find_or_load_estimator(
	qhw_adm_t *ctx,
	const char *name,
	char **extra_paths,
	size_t extra_path_count,
	qhw_adm_estimator_t **out_estimator,
	int *out_loaded);

qhw_adm_rc_t qhw_adm_set_device_estimator_entry(
	struct qhw_adm_device_entry *entry,
	const qhw_adm_estimator_t *estimator,
	const qhw_adm_kv_t *options,
	size_t option_count);

void qhw_adm_remove_estimator_entry(
	qhw_adm_t *ctx,
	qhw_adm_estimator_t *entry);

void qhw_adm_free_estimator_entry(void *value, void *user_data);

void qhw_adm_free_estimator_paths(qhw_adm_t *ctx);

#endif
