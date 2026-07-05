#include "qhw_admission_internal.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond) \
	do { \
		if (!(cond)) { \
			fprintf(stderr, "check failed at %s:%d: %s\n", \
				__FILE__, __LINE__, #cond); \
			return 1; \
		} \
	} while (0)

static int test_default_create(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_threading_t threading = QHW_ADM_THREAD_SAFE;

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(ctx != NULL);
	CHECK(qhw_adm_get_threading(ctx, &threading) == QHW_ADM_OK);
	CHECK(threading == QHW_ADM_THREAD_USER);
	CHECK(ctx->registries_initialized);
	CHECK(ctx->devices.bucket_count >= 8);
	CHECK(ctx->reservations.bucket_count >= 8);
	CHECK(ctx->policies.bucket_count >= 8);
	CHECK(ctx->estimators.bucket_count >= 8);
	CHECK(ctx->devices.count == 0);
	CHECK(ctx->reservations.count == 0);
	CHECK(ctx->policies.count == 0);
	CHECK(ctx->estimators.count == 0);
	CHECK(strcmp(qhw_adm_last_error(ctx), "") == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_explicit_threading(void)
{
	qhw_adm_attr_t attr = {
		.struct_size = sizeof(attr),
		.threading = QHW_ADM_THREAD_SAFE,
	};
	qhw_adm_t *ctx = NULL;
	qhw_adm_threading_t threading = QHW_ADM_THREAD_USER;

	CHECK(qhw_adm_create(&attr, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_get_threading(ctx, &threading) == QHW_ADM_OK);
	CHECK(threading == QHW_ADM_THREAD_SAFE);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_invalid_inputs(void)
{
	qhw_adm_attr_t attr = {
		.struct_size = sizeof(attr),
		.threading = (qhw_adm_threading_t)99,
	};
	qhw_adm_t *ctx = NULL;
	qhw_adm_threading_t threading = QHW_ADM_THREAD_USER;

	CHECK(qhw_adm_create(NULL, NULL) == QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_create(&attr, &ctx) == QHW_ADM_ERR_INVAL);
	CHECK(ctx == NULL);
	CHECK(qhw_adm_get_threading(NULL, &threading) == QHW_ADM_ERR_INVAL);
	CHECK(qhw_adm_get_threading(ctx, NULL) == QHW_ADM_ERR_INVAL);
	qhw_adm_destroy(NULL);
	return 0;
}

static int test_option_copy(void)
{
	qhw_adm_kv_t options[1];
	char option_value[16] = "session-a";
	qhw_adm_attr_t attr = {
		.struct_size = sizeof(attr),
		.threading = QHW_ADM_THREAD_USER,
		.options = options,
		.option_count = 1,
	};
	qhw_adm_t *ctx = NULL;

	memset(options, 0, sizeof(options));
	options[0].key = QHW_ADM_META_SESSION_ID;
	options[0].value.type = QHW_ADM_VALUE_STRING;
	options[0].value.value.string = option_value;

	CHECK(qhw_adm_create(&attr, &ctx) == QHW_ADM_OK);
	(void)snprintf(option_value, sizeof(option_value), "changed");
	CHECK(ctx->options != NULL);
	CHECK(ctx->options[0].value.value.string != option_value);
	CHECK(strcmp(ctx->options[0].value.value.string, "session-a") == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

static int test_diagnostics(void)
{
	qhw_adm_t *ctx = NULL;
	qhw_adm_threading_t threading = QHW_ADM_THREAD_SAFE;

	CHECK(qhw_adm_create(NULL, &ctx) == QHW_ADM_OK);
	CHECK(qhw_adm_get_threading(ctx, NULL) == QHW_ADM_ERR_INVAL);
	CHECK(strcmp(qhw_adm_last_error(ctx), "") != 0);
	CHECK(qhw_adm_get_threading(ctx, &threading) == QHW_ADM_OK);
	CHECK(threading == QHW_ADM_THREAD_USER);
	CHECK(strcmp(qhw_adm_last_error(ctx), "") == 0);
	qhw_adm_destroy(ctx);
	return 0;
}

int main(void)
{
	CHECK(test_default_create() == 0);
	CHECK(test_explicit_threading() == 0);
	CHECK(test_invalid_inputs() == 0);
	CHECK(test_option_copy() == 0);
	CHECK(test_diagnostics() == 0);
	return 0;
}
