#include "qhw_admission_internal.h"

#include <stdlib.h>

static void *qhw_adm_alloc(size_t size, void *user_data)
{
	(void)user_data;
	return malloc(size);
}

static void qhw_adm_free(void *ptr, void *user_data)
{
	(void)user_data;
	free(ptr);
}

qhw_adm_rc_t qhw_adm_init_hash_table(struct qhw_hash_table *table)
{
	if (qhw_hash_table_init(
		table,
		8,
		qhw_adm_alloc,
		qhw_adm_free,
		NULL) != 0) {
		return QHW_ADM_ERR_NOMEM;
	}

	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_init_registries(qhw_adm_t *ctx)
{
	if (ctx == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (qhw_adm_init_hash_table(&ctx->devices) != QHW_ADM_OK) {
		return QHW_ADM_ERR_NOMEM;
	}
	if (qhw_adm_init_hash_table(&ctx->reservations) != QHW_ADM_OK) {
		qhw_hash_table_fini(&ctx->devices, NULL, NULL);
		return QHW_ADM_ERR_NOMEM;
	}
	if (qhw_adm_init_hash_table(&ctx->policies) != QHW_ADM_OK) {
		qhw_hash_table_fini(&ctx->reservations, NULL, NULL);
		qhw_hash_table_fini(&ctx->devices, NULL, NULL);
		return QHW_ADM_ERR_NOMEM;
	}
	if (qhw_adm_init_hash_table(&ctx->estimators) != QHW_ADM_OK) {
		qhw_hash_table_fini(&ctx->policies, NULL, NULL);
		qhw_hash_table_fini(&ctx->reservations, NULL, NULL);
		qhw_hash_table_fini(&ctx->devices, NULL, NULL);
		return QHW_ADM_ERR_NOMEM;
	}

	ctx->registries_initialized = 1;
	return QHW_ADM_OK;
}

void qhw_adm_fini_registries(qhw_adm_t *ctx)
{
	if (ctx == NULL || !ctx->registries_initialized) {
		return;
	}

	qhw_hash_table_fini(
		&ctx->reservations,
		qhw_adm_free_reservation_entry,
		NULL);
	qhw_hash_table_fini(&ctx->devices, qhw_adm_free_device_entry, NULL);
	qhw_hash_table_fini(&ctx->policies, qhw_adm_free_policy_entry, NULL);
	qhw_hash_table_fini(
		&ctx->estimators,
		qhw_adm_free_estimator_entry,
		NULL);
	ctx->registries_initialized = 0;
}
