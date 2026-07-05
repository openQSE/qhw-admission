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
