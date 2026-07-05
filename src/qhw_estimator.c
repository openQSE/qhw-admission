#include "qhw_admission_internal.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

qhw_adm_rc_t qhw_adm_validate_estimate_output(
	const qhw_adm_estimate_t *out_estimate)
{
	if (out_estimate == NULL ||
	    out_estimate->struct_size < sizeof(*out_estimate)) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

uint64_t qhw_adm_hash_string(const char *str)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	const unsigned char *p = (const unsigned char *)str;

	while (p != NULL && *p != '\0') {
		hash ^= *p;
		hash *= UINT64_C(1099511628211);
		p++;
	}

	return hash;
}

void qhw_adm_free_estimator_entry(void *value, void *user_data)
{
	qhw_adm_estimator_t *entry = value;

	(void)user_data;

	if (entry == NULL) {
		return;
	}

	if (entry->handle != NULL) {
		(void)dlclose(entry->handle);
	}
	free(entry->name);
	free(entry->path);
	free(entry);
}

qhw_adm_rc_t qhw_adm_validate_estimator_desc(
	const qhw_adm_estimator_desc_t *desc)
{
	if (desc == NULL ||
	    desc->struct_size < sizeof(*desc) ||
	    desc->abi_version != QHW_ADM_ABI_VERSION ||
	    desc->name == NULL ||
	    desc->estimate_task == NULL ||
	    desc->estimate_baseline == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if ((desc->capabilities & QHW_ADM_EST_CAP_FEEDBACK) != 0 &&
	    desc->record_actual == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t qhw_adm_alloc_estimator_entry(
	const qhw_adm_estimator_desc_t *desc,
	const char *path,
	void *handle,
	qhw_adm_estimator_t **out_entry)
{
	qhw_adm_estimator_t *entry;
	qhw_adm_rc_t rc;

	if (out_entry == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_entry = NULL;
	rc = qhw_adm_validate_estimator_desc(desc);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	entry->name = qhw_adm_strdup(desc->name);
	if (entry->name == NULL) {
		free(entry);
		return QHW_ADM_ERR_NOMEM;
	}
	if (path != NULL) {
		entry->path = qhw_adm_strdup(path);
		if (entry->path == NULL) {
			qhw_adm_free_estimator_entry(entry, NULL);
			return QHW_ADM_ERR_NOMEM;
		}
	}
	entry->handle = handle;
	entry->desc = desc;
	*out_entry = entry;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t qhw_adm_register_estimator_entry(
	qhw_adm_t *ctx,
	qhw_adm_estimator_t *entry)
{
	uint64_t key;

	key = qhw_adm_hash_string(entry->name);
	if (qhw_hash_table_find(&ctx->estimators, key) != NULL) {
		return QHW_ADM_ERR_EXISTS;
	}
	if (qhw_hash_table_insert(&ctx->estimators, key, entry) != 0) {
		return QHW_ADM_ERR_NOMEM;
	}

	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_register_builtin_estimators(qhw_adm_t *ctx)
{
	qhw_adm_estimator_t *entry;
	qhw_adm_rc_t rc;

	rc = qhw_adm_alloc_estimator_entry(
		qhw_adm_baseline_estimator_desc(),
		NULL,
		NULL,
		&entry);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = qhw_adm_register_estimator_entry(ctx, entry);
	if (rc != QHW_ADM_OK) {
		qhw_adm_free_estimator_entry(entry, NULL);
		return rc;
	}

	return QHW_ADM_OK;
}

static qhw_adm_estimator_t *qhw_adm_find_estimator(
	qhw_adm_t *ctx,
	const char *name)
{
	if (ctx == NULL || name == NULL) {
		return NULL;
	}

	return qhw_hash_table_find(&ctx->estimators, qhw_adm_hash_string(name));
}

static qhw_adm_rc_t qhw_adm_estimator_set_baseline(
	struct qhw_adm_device_entry *entry,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	const qhw_adm_estimator_desc_t *desc;
	void *state = NULL;
	qhw_adm_rc_t rc;

	desc = qhw_adm_baseline_estimator_desc();
	rc = qhw_adm_validate_estimator_desc(desc);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	if (desc->init != NULL) {
		rc = desc->init(
			&entry->profile,
			options,
			option_count,
			&state);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
	}

	if (entry->estimator != NULL && entry->estimator->destroy != NULL) {
		entry->estimator->destroy(entry->estimator_state);
	}

	entry->estimator = desc;
	entry->estimator_state = state;
	entry->estimator_version++;
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_set_device_estimator_entry(
	struct qhw_adm_device_entry *entry,
	const qhw_adm_estimator_t *estimator,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	void *state = NULL;
	qhw_adm_rc_t rc;

	if (estimator == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (estimator->desc == qhw_adm_baseline_estimator_desc()) {
		return qhw_adm_estimator_set_baseline(
			entry,
			options,
			option_count);
	}

	if (estimator->desc->init != NULL) {
		rc = estimator->desc->init(
			&entry->profile,
			options,
			option_count,
			&state);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
	}

	if (entry->estimator != NULL && entry->estimator->destroy != NULL) {
		entry->estimator->destroy(entry->estimator_state);
	}

	entry->estimator = estimator->desc;
	entry->estimator_state = state;
	entry->estimator_version++;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t qhw_adm_load_estimator_unlocked(
	qhw_adm_t *ctx,
	const char *path,
	qhw_adm_estimator_t **out_estimator)
{
	qhw_adm_estimator_plugin_fn plugin_fn;
	const qhw_adm_estimator_desc_t *desc;
	qhw_adm_estimator_t *entry;
	void *handle;
	qhw_adm_rc_t rc;

	handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL) {
		return QHW_ADM_ERR_NOT_FOUND;
	}

	plugin_fn = (qhw_adm_estimator_plugin_fn)
		dlsym(handle, QHW_ADM_ESTIMATOR_PLUGIN_SYMBOL);
	if (plugin_fn == NULL) {
		(void)dlclose(handle);
		return QHW_ADM_ERR_INVAL;
	}

	desc = plugin_fn();
	rc = qhw_adm_alloc_estimator_entry(desc, path, handle, &entry);
	if (rc != QHW_ADM_OK) {
		(void)dlclose(handle);
		return rc;
	}

	rc = qhw_adm_register_estimator_entry(ctx, entry);
	if (rc != QHW_ADM_OK) {
		qhw_adm_free_estimator_entry(entry, NULL);
		return rc;
	}

	if (out_estimator != NULL) {
		*out_estimator = entry;
	}
	return QHW_ADM_OK;
}

static qhw_adm_rc_t qhw_adm_build_estimator_path(
	const char *dir,
	const char *name,
	char **out_path)
{
	size_t len;
	char *path;
	int rc;

	if (dir == NULL || name == NULL || out_path == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	len = strlen(dir) + strlen("/qhw_adm_estimator_") + strlen(name) +
		strlen(".so") + 1;
	path = malloc(len);
	if (path == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	rc = snprintf(path, len, "%s/qhw_adm_estimator_%s.so", dir, name);
	if (rc < 0 || (size_t)rc >= len) {
		free(path);
		return QHW_ADM_ERR_INVAL;
	}

	*out_path = path;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t qhw_adm_try_estimator_paths(
	qhw_adm_t *ctx,
	const char *name,
	char **paths,
	size_t path_count,
	qhw_adm_estimator_t **out_estimator,
	int *out_loaded)
{
	size_t i;
	qhw_adm_rc_t rc = QHW_ADM_ERR_NOT_FOUND;

	if (out_loaded != NULL) {
		*out_loaded = 0;
	}

	for (i = 0; i < path_count; i++) {
		char *plugin_path = NULL;

		rc = qhw_adm_build_estimator_path(paths[i], name, &plugin_path);
		if (rc != QHW_ADM_OK) {
			return rc;
		}

		rc = qhw_adm_load_estimator_unlocked(
			ctx,
			plugin_path,
			out_estimator);
		free(plugin_path);
		if (rc == QHW_ADM_OK) {
			if (out_loaded != NULL) {
				*out_loaded = 1;
			}
			return QHW_ADM_OK;
		}
		if (rc == QHW_ADM_ERR_EXISTS) {
			if (out_estimator != NULL) {
				*out_estimator = qhw_adm_find_estimator(ctx, name);
			}
			return QHW_ADM_OK;
		}
	}

	return rc;
}

qhw_adm_rc_t qhw_adm_find_or_load_estimator(
	qhw_adm_t *ctx,
	const char *name,
	char **extra_paths,
	size_t extra_path_count,
	qhw_adm_estimator_t **out_estimator,
	int *out_loaded)
{
	qhw_adm_estimator_t *estimator;
	qhw_adm_rc_t rc;

	if (ctx == NULL || name == NULL || out_estimator == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (out_loaded != NULL) {
		*out_loaded = 0;
	}

	estimator = qhw_adm_find_estimator(ctx, name);
	if (estimator != NULL) {
		*out_estimator = estimator;
		return QHW_ADM_OK;
	}

	rc = qhw_adm_try_estimator_paths(
		ctx,
		name,
		extra_paths,
		extra_path_count,
		out_estimator,
		out_loaded);
	if (rc == QHW_ADM_OK) {
		return QHW_ADM_OK;
	}

	return qhw_adm_try_estimator_paths(
		ctx,
		name,
		ctx->estimator_paths,
		ctx->estimator_path_count,
		out_estimator,
		out_loaded);
}

void qhw_adm_remove_estimator_entry(
	qhw_adm_t *ctx,
	qhw_adm_estimator_t *entry)
{
	qhw_adm_estimator_t *removed;

	if (ctx == NULL || entry == NULL || entry->name == NULL) {
		return;
	}

	removed = qhw_hash_table_remove(
		&ctx->estimators,
		qhw_adm_hash_string(entry->name));
	if (removed == entry) {
		qhw_adm_free_estimator_entry(removed, NULL);
	}
}

qhw_adm_rc_t qhw_adm_load_estimator(
	qhw_adm_t *ctx,
	const char *path,
	qhw_adm_estimator_t **out_estimator)
{
	qhw_adm_rc_t rc;

	if (ctx == NULL || path == NULL || out_estimator == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_estimator = NULL;
	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = qhw_adm_load_estimator_unlocked(ctx, path, out_estimator);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "failed to load estimator");
		qhw_adm_unlock(ctx);
		return rc;
	}

	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_add_estimator_path(qhw_adm_t *ctx, const char *path)
{
	char **paths;
	char *copy;
	qhw_adm_rc_t rc;

	if (ctx == NULL || path == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	copy = qhw_adm_strdup(path);
	if (copy == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		free(copy);
		return rc;
	}

	if (ctx->estimator_path_count >= SIZE_MAX / sizeof(*paths)) {
		free(copy);
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOMEM;
	}

	paths = realloc(
		ctx->estimator_paths,
		(ctx->estimator_path_count + 1) * sizeof(*paths));
	if (paths == NULL) {
		free(copy);
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOMEM;
	}

	ctx->estimator_paths = paths;
	ctx->estimator_paths[ctx->estimator_path_count] = copy;
	ctx->estimator_path_count++;
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

void qhw_adm_free_estimator_paths(qhw_adm_t *ctx)
{
	size_t i;

	if (ctx == NULL) {
		return;
	}

	for (i = 0; i < ctx->estimator_path_count; i++) {
		free(ctx->estimator_paths[i]);
	}
	free(ctx->estimator_paths);
	ctx->estimator_paths = NULL;
	ctx->estimator_path_count = 0;
}

qhw_adm_rc_t qhw_adm_set_estimator(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const char *name,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_estimator_t *estimator;
	qhw_adm_rc_t rc;

	if (ctx == NULL || name == NULL ||
	    (option_count > 0 && options == NULL)) {
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
	if (qhw_adm_device_has_active_reservation(ctx, device_id)) {
		qhw_adm_set_error(ctx, "device has active reservations");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_STATE;
	}

	rc = qhw_adm_find_or_load_estimator(
		ctx,
		name,
		NULL,
		0,
		&estimator,
		NULL);
	if (rc != QHW_ADM_OK || estimator == NULL) {
		qhw_adm_set_error(ctx, "estimator was not found");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOT_FOUND;
	}

	rc = qhw_adm_set_device_estimator_entry(
		entry,
		estimator,
		options,
		option_count);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "failed to configure estimator");
		qhw_adm_unlock(ctx);
		return rc;
	}

	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_estimate_qtask_class(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const qhw_adm_qtask_class_t *task_class,
	qhw_adm_estimate_t *out_estimate)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_rc_t rc;

	if (ctx == NULL || task_class == NULL ||
	    qhw_adm_validate_estimate_output(out_estimate) != QHW_ADM_OK) {
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

	rc = entry->estimator->estimate_task(
		entry->estimator_state,
		&entry->profile,
		task_class,
		out_estimate);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "estimator failed");
		qhw_adm_unlock(ctx);
		return rc;
	}

	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_estimate_baseline(
	qhw_adm_t *ctx,
	uint64_t device_id,
	qhw_adm_estimate_t *out_estimate)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_rc_t rc;

	if (ctx == NULL ||
	    qhw_adm_validate_estimate_output(out_estimate) != QHW_ADM_OK) {
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

	rc = entry->estimator->estimate_baseline(
		entry->estimator_state,
		&entry->profile,
		&entry->profile.baseline,
		out_estimate);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "estimator failed");
		qhw_adm_unlock(ctx);
		return rc;
	}

	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}
