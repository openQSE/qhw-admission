#include "qhw_admission_internal.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void qhw_adm_free_policy_entry(void *value, void *user_data)
{
	qhw_adm_policy_t *entry = value;

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

qhw_adm_rc_t qhw_adm_validate_policy_desc(
	const qhw_adm_policy_desc_t *desc)
{
	if (desc == NULL ||
	    desc->struct_size < sizeof(*desc) ||
	    desc->abi_version != QHW_ADM_ABI_VERSION ||
	    desc->name == NULL ||
	    desc->evaluate == NULL ||
	    desc->reserve == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t qhw_adm_alloc_policy_entry(
	const qhw_adm_policy_desc_t *desc,
	const char *path,
	void *handle,
	qhw_adm_policy_t **out_entry)
{
	qhw_adm_policy_t *entry;
	qhw_adm_rc_t rc;

	if (out_entry == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_entry = NULL;
	rc = qhw_adm_validate_policy_desc(desc);
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
			qhw_adm_free_policy_entry(entry, NULL);
			return QHW_ADM_ERR_NOMEM;
		}
	}

	entry->handle = handle;
	entry->desc = desc;
	*out_entry = entry;
	return QHW_ADM_OK;
}

static qhw_adm_policy_t *qhw_adm_find_policy(
	qhw_adm_t *ctx,
	const char *name)
{
	if (ctx == NULL || name == NULL) {
		return NULL;
	}

	return qhw_hash_table_find(&ctx->policies, qhw_adm_hash_string(name));
}

static qhw_adm_rc_t qhw_adm_register_policy_entry(
	qhw_adm_t *ctx,
	qhw_adm_policy_t *entry)
{
	uint64_t key;

	key = qhw_adm_hash_string(entry->name);
	if (qhw_hash_table_find(&ctx->policies, key) != NULL) {
		return QHW_ADM_ERR_EXISTS;
	}
	if (qhw_hash_table_insert(&ctx->policies, key, entry) != 0) {
		return QHW_ADM_ERR_NOMEM;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t qhw_adm_load_policy_unlocked(
	qhw_adm_t *ctx,
	const char *path,
	qhw_adm_policy_t **out_policy)
{
	qhw_adm_policy_plugin_fn plugin_fn;
	const qhw_adm_policy_desc_t *desc;
	qhw_adm_policy_t *entry;
	void *handle;
	qhw_adm_rc_t rc;

	handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
	if (handle == NULL) {
		return QHW_ADM_ERR_NOT_FOUND;
	}

	plugin_fn = (qhw_adm_policy_plugin_fn)
		dlsym(handle, QHW_ADM_POLICY_PLUGIN_SYMBOL);
	if (plugin_fn == NULL) {
		(void)dlclose(handle);
		return QHW_ADM_ERR_INVAL;
	}

	desc = plugin_fn();
	rc = qhw_adm_alloc_policy_entry(desc, path, handle, &entry);
	if (rc != QHW_ADM_OK) {
		(void)dlclose(handle);
		return rc;
	}

	rc = qhw_adm_register_policy_entry(ctx, entry);
	if (rc != QHW_ADM_OK) {
		qhw_adm_free_policy_entry(entry, NULL);
		return rc;
	}

	if (out_policy != NULL) {
		*out_policy = entry;
	}
	return QHW_ADM_OK;
}

static qhw_adm_rc_t qhw_adm_build_policy_path(
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

	len = strlen(dir) + strlen("/qhw_adm_") + strlen(name) +
		strlen(".so") + 1;
	path = malloc(len);
	if (path == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	rc = snprintf(path, len, "%s/qhw_adm_%s.so", dir, name);
	if (rc < 0 || (size_t)rc >= len) {
		free(path);
		return QHW_ADM_ERR_INVAL;
	}

	*out_path = path;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t qhw_adm_try_policy_paths(
	qhw_adm_t *ctx,
	const char *name,
	char **paths,
	size_t path_count,
	qhw_adm_policy_t **out_policy,
	int *out_loaded)
{
	size_t i;
	qhw_adm_rc_t rc = QHW_ADM_ERR_NOT_FOUND;

	if (out_loaded != NULL) {
		*out_loaded = 0;
	}

	for (i = 0; i < path_count; i++) {
		char *plugin_path = NULL;

		rc = qhw_adm_build_policy_path(paths[i], name, &plugin_path);
		if (rc != QHW_ADM_OK) {
			return rc;
		}

		rc = qhw_adm_load_policy_unlocked(ctx, plugin_path, out_policy);
		free(plugin_path);
		if (rc == QHW_ADM_OK) {
			if (out_loaded != NULL) {
				*out_loaded = 1;
			}
			return QHW_ADM_OK;
		}
		if (rc == QHW_ADM_ERR_EXISTS) {
			if (out_policy != NULL) {
				*out_policy = qhw_adm_find_policy(ctx, name);
			}
			return QHW_ADM_OK;
		}
	}

	return rc;
}

qhw_adm_rc_t qhw_adm_find_or_load_policy(
	qhw_adm_t *ctx,
	const char *name,
	char **extra_paths,
	size_t extra_path_count,
	qhw_adm_policy_t **out_policy,
	int *out_loaded)
{
	qhw_adm_policy_t *policy;
	qhw_adm_rc_t rc;

	if (ctx == NULL || name == NULL || out_policy == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (out_loaded != NULL) {
		*out_loaded = 0;
	}

	policy = qhw_adm_find_policy(ctx, name);
	if (policy != NULL) {
		*out_policy = policy;
		return QHW_ADM_OK;
	}

	rc = qhw_adm_try_policy_paths(
		ctx,
		name,
		extra_paths,
		extra_path_count,
		out_policy,
		out_loaded);
	if (rc == QHW_ADM_OK) {
		return QHW_ADM_OK;
	}

	return qhw_adm_try_policy_paths(
		ctx,
		name,
		ctx->policy_paths,
		ctx->policy_path_count,
		out_policy,
		out_loaded);
}

void qhw_adm_remove_policy_entry(qhw_adm_t *ctx, qhw_adm_policy_t *entry)
{
	qhw_adm_policy_t *removed;

	if (ctx == NULL || entry == NULL || entry->name == NULL) {
		return;
	}

	removed = qhw_hash_table_remove(
		&ctx->policies,
		qhw_adm_hash_string(entry->name));
	if (removed == entry) {
		qhw_adm_free_policy_entry(removed, NULL);
	}
}

qhw_adm_rc_t qhw_adm_set_device_policy_entry(
	struct qhw_adm_device_entry *entry,
	const qhw_adm_policy_t *policy,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	void *state = NULL;
	qhw_adm_rc_t rc;

	if (entry == NULL || policy == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (policy->desc->init != NULL) {
		rc = policy->desc->init(
			&entry->profile,
			options,
			option_count,
			&state);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
	}

	if (entry->policy != NULL && entry->policy->destroy != NULL) {
		entry->policy->destroy(entry->policy_state);
	}

	entry->policy = policy->desc;
	entry->policy_state = state;
	entry->policy_version++;
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_load_policy(
	qhw_adm_t *ctx,
	const char *path,
	qhw_adm_policy_t **out_policy)
{
	qhw_adm_rc_t rc;

	if (ctx == NULL || path == NULL || out_policy == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_policy = NULL;
	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = qhw_adm_load_policy_unlocked(ctx, path, out_policy);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "failed to load policy");
		qhw_adm_unlock(ctx);
		return rc;
	}

	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_add_policy_path(qhw_adm_t *ctx, const char *path)
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

	if (ctx->policy_path_count >= SIZE_MAX / sizeof(*paths)) {
		free(copy);
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOMEM;
	}

	paths = realloc(
		ctx->policy_paths,
		(ctx->policy_path_count + 1) * sizeof(*paths));
	if (paths == NULL) {
		free(copy);
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOMEM;
	}

	ctx->policy_paths = paths;
	ctx->policy_paths[ctx->policy_path_count] = copy;
	ctx->policy_path_count++;
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_set_policy(
	qhw_adm_t *ctx,
	uint64_t device_id,
	const char *name,
	const qhw_adm_kv_t *options,
	size_t option_count)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_policy_t *policy;
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

	rc = qhw_adm_find_or_load_policy(ctx, name, NULL, 0, &policy, NULL);
	if (rc != QHW_ADM_OK || policy == NULL) {
		qhw_adm_set_error(ctx, "policy was not found");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOT_FOUND;
	}

	rc = qhw_adm_set_device_policy_entry(
		entry,
		policy,
		options,
		option_count);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "failed to configure policy");
		qhw_adm_unlock(ctx);
		return rc;
	}

	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

void qhw_adm_free_policy_paths(qhw_adm_t *ctx)
{
	size_t i;

	if (ctx == NULL) {
		return;
	}

	for (i = 0; i < ctx->policy_path_count; i++) {
		free(ctx->policy_paths[i]);
	}
	free(ctx->policy_paths);
	ctx->policy_paths = NULL;
	ctx->policy_path_count = 0;
}
