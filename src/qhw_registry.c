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

static int qhw_adm_registry_init(struct qhw_hash_table *table)
{
	return qhw_hash_table_init(table, 8, qhw_adm_alloc, qhw_adm_free, NULL);
}

qhw_adm_rc_t qhw_adm_init_registries(qhw_adm_t *ctx)
{
	if (ctx == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (qhw_adm_registry_init(&ctx->devices) != 0) {
		return QHW_ADM_ERR_NOMEM;
	}
	if (qhw_adm_registry_init(&ctx->reservations) != 0) {
		qhw_hash_table_fini(&ctx->devices, NULL, NULL);
		return QHW_ADM_ERR_NOMEM;
	}
	if (qhw_adm_registry_init(&ctx->policies) != 0) {
		qhw_hash_table_fini(&ctx->reservations, NULL, NULL);
		qhw_hash_table_fini(&ctx->devices, NULL, NULL);
		return QHW_ADM_ERR_NOMEM;
	}
	if (qhw_adm_registry_init(&ctx->estimators) != 0) {
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

	qhw_hash_table_fini(&ctx->estimators, NULL, NULL);
	qhw_hash_table_fini(&ctx->policies, NULL, NULL);
	qhw_hash_table_fini(&ctx->reservations, NULL, NULL);
	qhw_hash_table_fini(&ctx->devices, NULL, NULL);
	ctx->registries_initialized = 0;
}
