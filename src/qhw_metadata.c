#include "qhw_admission_internal.h"

#include <stdlib.h>
#include <string.h>

char *qhw_adm_strdup(const char *src)
{
	size_t len;
	char *copy;

	if (src == NULL) {
		return NULL;
	}

	len = strlen(src) + 1;
	copy = (char *)malloc(len);
	if (copy == NULL) {
		return NULL;
	}

	memcpy(copy, src, len);
	return copy;
}

qhw_adm_rc_t qhw_adm_validate_attr(const qhw_adm_attr_t *attr)
{
	if (attr == NULL) {
		return QHW_ADM_OK;
	}

	if (attr->struct_size < sizeof(*attr)) {
		return QHW_ADM_ERR_INVAL;
	}

	if (attr->threading != QHW_ADM_THREAD_USER &&
	    attr->threading != QHW_ADM_THREAD_SAFE) {
		return QHW_ADM_ERR_INVAL;
	}

	if (attr->option_count > 0 && attr->options == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_copy_metadata(
	const qhw_adm_kv_t *src,
	size_t count,
	qhw_adm_kv_t **out_copy)
{
	qhw_adm_kv_t *copy;
	size_t i;

	if (out_copy == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_copy = NULL;

	if (count == 0) {
		return QHW_ADM_OK;
	}

	if (src == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (count > ((size_t)-1) / sizeof(*copy)) {
		return QHW_ADM_ERR_NOMEM;
	}

	copy = (qhw_adm_kv_t *)calloc(count, sizeof(*copy));
	if (copy == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	memcpy(copy, src, count * sizeof(*copy));
	for (i = 0; i < count; i++) {
		if (src[i].value.type == QHW_ADM_VALUE_STRING &&
		    src[i].value.value.string != NULL) {
			copy[i].value.value.string =
				qhw_adm_strdup(src[i].value.value.string);
			if (copy[i].value.value.string == NULL) {
				qhw_adm_free_metadata_count(copy, i);
				return QHW_ADM_ERR_NOMEM;
			}
		}
	}

	*out_copy = copy;
	return QHW_ADM_OK;
}

void qhw_adm_free_metadata_count(qhw_adm_kv_t *metadata, size_t count)
{
	size_t i;

	if (metadata == NULL) {
		return;
	}

	for (i = 0; i < count; i++) {
		if (metadata[i].value.type == QHW_ADM_VALUE_STRING) {
			free((void *)metadata[i].value.value.string);
		}
	}

	free(metadata);
}

void qhw_adm_clear_output(qhw_adm_t *ctx)
{
	struct qhw_list_node *node;

	if (ctx == NULL) {
		return;
	}

	while ((node = qhw_list_pop_front(&ctx->output_views)) != NULL) {
		struct qhw_adm_output_entry *entry;

		entry = qhw_container_of(
			node,
			struct qhw_adm_output_entry,
			node);
		qhw_adm_free_metadata_count(
			entry->metadata,
			entry->metadata_count);
		free(entry->message);
		free(entry);
	}
}

qhw_adm_rc_t qhw_adm_copy_output_metadata(
	qhw_adm_t *ctx,
	const qhw_adm_kv_t *metadata,
	size_t metadata_count,
	const qhw_adm_kv_t **out_metadata,
	size_t *out_metadata_count)
{
	struct qhw_adm_output_entry *entry;
	qhw_adm_kv_t *copy = NULL;
	qhw_adm_rc_t rc;

	if (ctx == NULL || out_metadata == NULL ||
	    out_metadata_count == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_metadata = NULL;
	*out_metadata_count = 0;
	if (metadata_count == 0) {
		return QHW_ADM_OK;
	}

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	rc = qhw_adm_copy_metadata(metadata, metadata_count, &copy);
	if (rc != QHW_ADM_OK) {
		free(entry);
		return rc;
	}

	entry->metadata = copy;
	entry->metadata_count = metadata_count;
	qhw_list_push_back(&ctx->output_views, &entry->node);
	*out_metadata = entry->metadata;
	*out_metadata_count = entry->metadata_count;
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_copy_output_message(
	qhw_adm_t *ctx,
	const char *message,
	const char **out_message)
{
	struct qhw_adm_output_entry *entry;
	char *copy = NULL;

	if (ctx == NULL || out_message == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_message = NULL;
	if (message == NULL) {
		return QHW_ADM_OK;
	}

	copy = qhw_adm_strdup(message);
	if (copy == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		free(copy);
		return QHW_ADM_ERR_NOMEM;
	}

	entry->message = copy;
	qhw_list_push_back(&ctx->output_views, &entry->node);
	*out_message = entry->message;
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_metadata_get_u64(
	const qhw_adm_kv_t *metadata,
	size_t count,
	uint64_t key,
	uint64_t fallback,
	uint64_t *out_value)
{
	size_t i;

	if (out_value == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_value = fallback;

	if (count == 0) {
		return QHW_ADM_OK;
	}

	if (metadata == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	for (i = 0; i < count; i++) {
		if (metadata[i].key != key) {
			continue;
		}
		if (metadata[i].value.type != QHW_ADM_VALUE_U64) {
			return QHW_ADM_ERR_INVAL;
		}
		*out_value = metadata[i].value.value.u64;
		return QHW_ADM_OK;
	}

	return QHW_ADM_OK;
}
