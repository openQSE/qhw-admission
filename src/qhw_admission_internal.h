#ifndef QHW_ADMISSION_INTERNAL_H
#define QHW_ADMISSION_INTERNAL_H

#include <pthread.h>
#include <stddef.h>

#include <qhw_datastructures/qhw_hash_table.h>

#include <qhw_admission/qhw_admission.h>

#define QHW_ADM_ERROR_LEN 256

struct qhw_adm {
	qhw_adm_threading_t threading;
	qhw_adm_kv_t *options;
	size_t option_count;
	struct qhw_hash_table devices;
	struct qhw_hash_table reservations;
	struct qhw_hash_table policies;
	struct qhw_hash_table estimators;
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

void qhw_adm_free_metadata_count(qhw_adm_kv_t *metadata, size_t count);

qhw_adm_rc_t qhw_adm_init_registries(qhw_adm_t *ctx);

void qhw_adm_fini_registries(qhw_adm_t *ctx);

void qhw_adm_set_error(qhw_adm_t *ctx, const char *message);

void qhw_adm_clear_error(qhw_adm_t *ctx);

qhw_adm_rc_t qhw_adm_lock(qhw_adm_t *ctx);

void qhw_adm_unlock(qhw_adm_t *ctx);

#endif
