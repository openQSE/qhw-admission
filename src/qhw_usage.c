#include "qhw_admission_internal.h"

#include <stdlib.h>
#include <string.h>

struct qhw_adm_usage_event {
	struct qhw_list_node node;
	qhw_adm_usage_t consumed;
	qhw_adm_usage_t returned;
	qhw_adm_decision_t decision;
	qhw_adm_kv_t *consumed_metadata;
	qhw_adm_kv_t *returned_metadata;
	int consumed_set;
	int returned_set;
};

struct qhw_adm_actual_event {
	qhw_adm_actual_usage_t actual;
	qhw_adm_kv_t *metadata;
	int feedback_pending;
};

static uint64_t usage_saturating_sub(uint64_t a, uint64_t b)
{
	if (a < b) {
		return 0;
	}

	return a - b;
}

static uint64_t usage_capacity_units(
	const qhw_adm_reservation_t *reservation)
{
	if (reservation->credits_reserved != 0) {
		return reservation->credits_reserved;
	}
	if (reservation->rate_reserved != 0) {
		return reservation->rate_reserved;
	}

	return 0;
}

static uint64_t usage_consumed_units(
	const qhw_adm_reservation_t *reservation)
{
	if (reservation->credits_reserved != 0) {
		return reservation->credits_consumed;
	}
	if (reservation->rate_reserved != 0) {
		return reservation->rate_consumed;
	}

	return 0;
}

static uint64_t usage_credit_units(
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage)
{
	if (usage->credits != 0) {
		return usage->credits;
	}
	if (reservation->credits_reserved != 0) {
		return usage->baseline_units;
	}

	return 0;
}

static uint64_t usage_rate_units(
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage)
{
	if (usage->rate_units != 0) {
		return usage->rate_units;
	}
	if (reservation->rate_reserved != 0) {
		return usage->baseline_units;
	}

	return 0;
}

static qhw_adm_rc_t validate_metadata(
	const qhw_adm_kv_t *metadata,
	size_t metadata_count,
	int require_unique)
{
	size_t i;
	size_t j;

	if (metadata_count > 0 && metadata == NULL) {
		return QHW_ADM_ERR_INVAL;
	}
	if (!require_unique) {
		return QHW_ADM_OK;
	}

	for (i = 0; i < metadata_count; i++) {
		for (j = i + 1; j < metadata_count; j++) {
			if (metadata[i].key == metadata[j].key) {
				return QHW_ADM_ERR_INVAL;
			}
		}
	}

	return QHW_ADM_OK;
}

static int value_equal(
	const qhw_adm_value_t *a,
	const qhw_adm_value_t *b)
{
	if (a->type != b->type || a->flags != b->flags) {
		return 0;
	}

	switch (a->type) {
	case QHW_ADM_VALUE_U64:
		return a->value.u64 == b->value.u64;
	case QHW_ADM_VALUE_I64:
		return a->value.i64 == b->value.i64;
	case QHW_ADM_VALUE_F64:
		return a->value.f64 == b->value.f64;
	case QHW_ADM_VALUE_BOOL:
		return a->value.boolean == b->value.boolean;
	case QHW_ADM_VALUE_STRING:
		if (a->value.string == NULL || b->value.string == NULL) {
			return a->value.string == b->value.string;
		}
		return strcmp(a->value.string, b->value.string) == 0;
	case QHW_ADM_VALUE_PTR:
		return a->value.ptr == b->value.ptr;
	default:
		return 0;
	}
}

static int metadata_equal(
	const qhw_adm_kv_t *a,
	size_t a_count,
	const qhw_adm_kv_t *b,
	size_t b_count)
{
	size_t i;
	size_t j;
	int found;

	if (a_count != b_count) {
		return 0;
	}
	if (validate_metadata(a, a_count, 1) != QHW_ADM_OK ||
	    validate_metadata(b, b_count, 1) != QHW_ADM_OK) {
		return 0;
	}

	for (i = 0; i < a_count; i++) {
		found = 0;
		for (j = 0; j < b_count; j++) {
			if (a[i].key == b[j].key &&
			    value_equal(&a[i].value, &b[j].value)) {
				found = 1;
				break;
			}
		}
		if (!found) {
			return 0;
		}
	}

	return 1;
}

static int usage_equal(
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *a,
	const qhw_adm_usage_t *b)
{
	int capacity_equal;

	if (reservation->credits_reserved == 0 &&
	    reservation->rate_reserved == 0) {
		capacity_equal = a->baseline_units == b->baseline_units &&
			a->credits == b->credits &&
			a->rate_units == b->rate_units;
	} else {
		capacity_equal =
			usage_credit_units(reservation, a) ==
				usage_credit_units(reservation, b) &&
			usage_rate_units(reservation, a) ==
				usage_rate_units(reservation, b);
	}

	return a->reservation_id == b->reservation_id &&
		a->task_id == b->task_id &&
		a->class_id == b->class_id &&
		a->event_time_ns == b->event_time_ns &&
		a->estimated_ns == b->estimated_ns &&
		a->actual_ns == b->actual_ns &&
		capacity_equal &&
		metadata_equal(
			a->metadata,
			a->metadata_count,
			b->metadata,
			b->metadata_count);
}

static int actual_equal(
	const qhw_adm_actual_usage_t *a,
	const qhw_adm_actual_usage_t *b)
{
	return a->reservation_id == b->reservation_id &&
		a->task_id == b->task_id &&
		a->observed_device_ns == b->observed_device_ns &&
		a->observed_compile_ns == b->observed_compile_ns &&
		a->observed_transfer_ns == b->observed_transfer_ns &&
		a->observed_control_overhead_ns ==
			b->observed_control_overhead_ns &&
		metadata_equal(
			a->metadata,
			a->metadata_count,
			b->metadata,
			b->metadata_count);
}

static qhw_adm_rc_t copy_usage(
	const qhw_adm_usage_t *usage,
	qhw_adm_usage_t *out_usage,
	qhw_adm_kv_t **out_metadata)
{
	qhw_adm_rc_t rc;

	*out_usage = *usage;
	*out_metadata = NULL;
	rc = qhw_adm_copy_metadata(
		usage->metadata,
		usage->metadata_count,
		out_metadata);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	out_usage->metadata = *out_metadata;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t copy_actual(
	const qhw_adm_actual_usage_t *actual,
	qhw_adm_actual_usage_t *out_actual,
	qhw_adm_kv_t **out_metadata)
{
	qhw_adm_rc_t rc;

	*out_actual = *actual;
	*out_metadata = NULL;
	rc = qhw_adm_copy_metadata(
		actual->metadata,
		actual->metadata_count,
		out_metadata);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	out_actual->metadata = *out_metadata;
	return QHW_ADM_OK;
}

static void free_decision_copy(qhw_adm_decision_t *decision)
{
	if (decision == NULL) {
		return;
	}

	qhw_adm_free_metadata_count(
		(qhw_adm_kv_t *)decision->metadata,
		decision->metadata_count);
	free((void *)decision->message);
	decision->metadata = NULL;
	decision->metadata_count = 0;
	decision->message = NULL;
}

static qhw_adm_rc_t copy_decision_private(
	const qhw_adm_decision_t *src,
	qhw_adm_decision_t *dst)
{
	qhw_adm_kv_t *metadata = NULL;
	char *message = NULL;
	qhw_adm_rc_t rc;

	*dst = *src;
	rc = qhw_adm_copy_metadata(
		src->metadata,
		src->metadata_count,
		&metadata);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	if (src->message != NULL) {
		message = qhw_adm_strdup(src->message);
		if (message == NULL) {
			qhw_adm_free_metadata_count(
				metadata,
				src->metadata_count);
			return QHW_ADM_ERR_NOMEM;
		}
	}

	dst->metadata = metadata;
	dst->message = message;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t copy_decision_public(
	qhw_adm_t *ctx,
	const qhw_adm_decision_t *src,
	qhw_adm_decision_t *dst)
{
	qhw_adm_rc_t rc;
	size_t struct_size = dst->struct_size;

	*dst = *src;
	dst->struct_size = struct_size;
	rc = qhw_adm_copy_output_metadata(
		ctx,
		src->metadata,
		src->metadata_count,
		&dst->metadata,
		&dst->metadata_count);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	return qhw_adm_copy_output_message(
		ctx,
		src->message,
		&dst->message);
}

static qhw_adm_rc_t validate_decision_output(
	const qhw_adm_decision_t *out_decision)
{
	if (out_decision == NULL ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t validate_usage_output(
	const qhw_adm_usage_state_t *out_usage)
{
	if (out_usage == NULL ||
	    out_usage->struct_size < sizeof(*out_usage)) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t validate_compliance_output(
	const qhw_adm_compliance_t *out_compliance)
{
	if (out_compliance == NULL ||
	    out_compliance->struct_size < sizeof(*out_compliance)) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t normalize_usage(
	uint64_t reservation_id,
	const qhw_adm_usage_t *usage,
	uint64_t existing_time_ns,
	qhw_adm_usage_t *out_usage)
{
	if (usage == NULL ||
	    usage->struct_size < sizeof(*usage) ||
	    (usage->reservation_id != 0 &&
	     usage->reservation_id != reservation_id)) {
		return QHW_ADM_ERR_INVAL;
	}
	if (validate_metadata(
		usage->metadata,
		usage->metadata_count,
		usage->task_id != 0) != QHW_ADM_OK) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_usage = *usage;
	out_usage->reservation_id = reservation_id;
	if (out_usage->event_time_ns == 0) {
		if (existing_time_ns != 0) {
			out_usage->event_time_ns = existing_time_ns;
		} else {
			out_usage->event_time_ns = qhw_adm_now_ns();
		}
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t normalize_actual(
	uint64_t reservation_id,
	const qhw_adm_actual_usage_t *actual,
	qhw_adm_actual_usage_t *out_actual)
{
	if (actual == NULL ||
	    actual->struct_size < sizeof(*actual) ||
	    (actual->reservation_id != 0 &&
	     actual->reservation_id != reservation_id)) {
		return QHW_ADM_ERR_INVAL;
	}
	if (validate_metadata(
		actual->metadata,
		actual->metadata_count,
		actual->task_id != 0) != QHW_ADM_OK) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_actual = *actual;
	out_actual->reservation_id = reservation_id;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t find_active_reservation(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	struct qhw_adm_reservation_entry **out_entry)
{
	struct qhw_adm_reservation_entry *entry;

	if (reservation_id == 0 || out_entry == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	entry = qhw_hash_table_find(&ctx->reservations, reservation_id);
	if (entry == NULL) {
		return QHW_ADM_ERR_NOT_FOUND;
	}
	if (entry->reservation.state != QHW_ADM_RESERVATION_ACTIVE) {
		return QHW_ADM_ERR_STATE;
	}

	*out_entry = entry;
	return QHW_ADM_OK;
}

static uint64_t rate_window_consumed(
	const struct qhw_adm_reservation_entry *entry,
	uint64_t event_time_ns);

static void fill_default_usage_decision(
	const qhw_adm_reservation_t *reservation,
	qhw_adm_decision_t *decision)
{
	size_t struct_size = decision->struct_size;

	memset(decision, 0, sizeof(*decision));
	decision->struct_size = struct_size;
	decision->decision = QHW_ADM_DECISION_ACCEPTED;
	decision->reservation_id = reservation->reservation_id;
	decision->device_id = reservation->device_id;
	decision->scope_id = reservation->scope_id;
	decision->reason_code = QHW_ADM_REASON_ACCEPTED;
	decision->compliance_action = QHW_ADM_COMPLIANCE_ALLOW;
	decision->capacity_available = usage_saturating_sub(
		usage_capacity_units(reservation),
		usage_consumed_units(reservation));
}

static qhw_adm_rc_t policy_usage_decision(
	struct qhw_adm_reservation_entry *entry,
	const qhw_adm_usage_t *usage,
	qhw_adm_decision_t *decision,
	int commit)
{
	qhw_adm_reservation_t reservation = entry->reservation;
	qhw_adm_rc_t rc;

	if (reservation.rate_reserved != 0) {
		reservation.rate_consumed = rate_window_consumed(
			entry,
			usage->event_time_ns);
	}

	fill_default_usage_decision(&reservation, decision);
	if (entry->policy == NULL) {
		return QHW_ADM_OK;
	}
	if (!commit && entry->policy->authorize_usage == NULL) {
		return QHW_ADM_OK;
	}
	if (commit && entry->policy->consume == NULL) {
		return QHW_ADM_OK;
	}

	if (commit) {
		rc = entry->policy->consume(
			entry->policy_state,
			&reservation,
			usage,
			decision);
	} else {
		rc = entry->policy->authorize_usage(
			entry->policy_state,
			&reservation,
			usage,
			decision);
	}
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	if (decision->reservation_id == 0) {
		decision->reservation_id = entry->reservation.reservation_id;
	}
	if (decision->device_id == 0) {
		decision->device_id = entry->reservation.device_id;
	}
	if (decision->scope_id == 0) {
		decision->scope_id = entry->reservation.scope_id;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t add_usage_units(
	qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage)
{
	uint64_t value;
	qhw_adm_rc_t rc;

	rc = qhw_adm_add_u64(
		reservation->credits_consumed,
		usage_credit_units(reservation, usage),
		&value);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	reservation->credits_consumed = value;

	rc = qhw_adm_add_u64(
		reservation->rate_consumed,
		usage_rate_units(reservation, usage),
		&value);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	reservation->rate_consumed = value;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t subtract_usage_units(
	qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage)
{
	uint64_t credits = usage_credit_units(reservation, usage);
	uint64_t rate = usage_rate_units(reservation, usage);
	uint64_t returned;
	qhw_adm_rc_t rc;

	if (credits > reservation->credits_consumed ||
	    rate > reservation->rate_consumed) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_add_u64(
		reservation->unused_capacity,
		credits != 0 ? credits : rate,
		&returned);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	reservation->credits_consumed -= credits;
	reservation->rate_consumed -= rate;
	reservation->unused_capacity = returned;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t validate_return_units(
	const qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage)
{
	uint64_t credits = usage_credit_units(reservation, usage);
	uint64_t rate = usage_rate_units(reservation, usage);
	uint64_t returned;

	if (credits > reservation->credits_consumed ||
	    rate > reservation->rate_consumed) {
		return QHW_ADM_ERR_INVAL;
	}

	return qhw_adm_add_u64(
		reservation->unused_capacity,
		credits != 0 ? credits : rate,
		&returned);
}

static qhw_adm_rc_t rollback_usage_units(
	qhw_adm_reservation_t *reservation,
	const qhw_adm_usage_t *usage)
{
	uint64_t credits = usage_credit_units(reservation, usage);
	uint64_t rate = usage_rate_units(reservation, usage);

	if (credits > reservation->credits_consumed ||
	    rate > reservation->rate_consumed) {
		return QHW_ADM_ERR_INVAL;
	}

	reservation->credits_consumed -= credits;
	reservation->rate_consumed -= rate;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t add_actual_units(
	qhw_adm_reservation_t *reservation,
	const qhw_adm_actual_usage_t *actual)
{
	uint64_t total;
	uint64_t value;
	qhw_adm_rc_t rc;

	rc = qhw_adm_add_u64(
		actual->observed_device_ns,
		actual->observed_compile_ns,
		&total);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(total, actual->observed_transfer_ns, &total);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(
		total,
		actual->observed_control_overhead_ns,
		&total);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(
		reservation->actual_total_ns,
		total,
		&value);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	reservation->actual_total_ns = value;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t create_usage_event(
	const qhw_adm_usage_t *usage,
	const qhw_adm_decision_t *decision,
	int consumed_set,
	struct qhw_adm_usage_event **out_event)
{
	struct qhw_adm_usage_event *event;
	qhw_adm_rc_t rc;

	*out_event = NULL;
	event = calloc(1, sizeof(*event));
	if (event == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	rc = copy_usage(usage, &event->consumed, &event->consumed_metadata);
	if (rc != QHW_ADM_OK) {
		qhw_adm_free_usage_event(event, NULL);
		return rc;
	}
	rc = copy_decision_private(
		decision,
		&event->decision);
	if (rc != QHW_ADM_OK) {
		qhw_adm_free_usage_event(event, NULL);
		return rc;
	}
	event->consumed_set = consumed_set;

	*out_event = event;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t publish_usage_event(
	struct qhw_adm_reservation_entry *entry,
	struct qhw_adm_usage_event *event)
{
	if (event->consumed.task_id != 0 &&
	    qhw_hash_table_insert(
		&entry->usage_events,
		event->consumed.task_id,
		event) != 0) {
		qhw_adm_free_usage_event(event, NULL);
		return QHW_ADM_ERR_NOMEM;
	}

	qhw_list_push_back(&entry->usage_event_list, &event->node);
	return QHW_ADM_OK;
}

static uint64_t rate_window_consumed(
	const struct qhw_adm_reservation_entry *entry,
	uint64_t event_time_ns)
{
	const struct qhw_list_node *node;
	const qhw_adm_reservation_t *reservation = &entry->reservation;
	uint64_t consumed = 0;
	uint64_t window;

	if (entry->rate_window_ns == 0) {
		return reservation->rate_consumed;
	}

	window = event_time_ns / entry->rate_window_ns;
	for (node = entry->usage_event_list.next;
	     node != &entry->usage_event_list;
	     node = node->next) {
		const struct qhw_adm_usage_event *event;
		uint64_t event_window;
		uint64_t units;

		event = qhw_container_of(
			node,
			const struct qhw_adm_usage_event,
			node);
		if (event->consumed_set) {
			event_window = event->consumed.event_time_ns /
				entry->rate_window_ns;
			if (event_window == window) {
				units = usage_rate_units(
					reservation,
					&event->consumed);
				if (qhw_adm_add_u64(consumed, units,
				    &consumed) != QHW_ADM_OK) {
					return UINT64_MAX;
				}
			}
		}

		if (event->returned_set) {
			if (event->consumed_set) {
				event_window = event->consumed.event_time_ns /
					entry->rate_window_ns;
			} else {
				event_window = event->returned.event_time_ns /
					entry->rate_window_ns;
			}
			if (event_window != window) {
				continue;
			}
			units = usage_rate_units(
					reservation,
					&event->returned);
			consumed = usage_saturating_sub(consumed, units);
		}
	}

	return consumed;
}

static uint64_t latest_rate_event_time(
	const struct qhw_adm_reservation_entry *entry)
{
	const struct qhw_list_node *node;
	uint64_t latest = 0;

	for (node = entry->usage_event_list.next;
	     node != &entry->usage_event_list;
	     node = node->next) {
		const struct qhw_adm_usage_event *event;

		event = qhw_container_of(
			node,
			const struct qhw_adm_usage_event,
			node);
		if (event->consumed_set &&
		    event->consumed.event_time_ns > latest) {
			latest = event->consumed.event_time_ns;
		}
		if (event->returned_set &&
		    event->returned.event_time_ns > latest) {
			latest = event->returned.event_time_ns;
		}
	}

	if (latest == 0) {
		latest = qhw_adm_now_ns();
	}
	return latest;
}

static qhw_adm_rc_t store_return_usage(
	struct qhw_adm_usage_event *event,
	const qhw_adm_usage_t *usage)
{
	qhw_adm_rc_t rc;

	rc = copy_usage(usage, &event->returned, &event->returned_metadata);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	event->returned_set = 1;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t create_return_event(
	const qhw_adm_usage_t *usage,
	struct qhw_adm_usage_event **out_event)
{
	struct qhw_adm_usage_event *event;
	qhw_adm_rc_t rc;

	*out_event = NULL;
	event = calloc(1, sizeof(*event));
	if (event == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	rc = store_return_usage(event, usage);
	if (rc != QHW_ADM_OK) {
		qhw_adm_free_usage_event(event, NULL);
		return rc;
	}

	*out_event = event;
	return QHW_ADM_OK;
}

static void clear_return_usage(struct qhw_adm_usage_event *event)
{
	if (event == NULL || !event->returned_set) {
		return;
	}

	qhw_adm_free_metadata_count(
		event->returned_metadata,
		event->returned.metadata_count);
	memset(&event->returned, 0, sizeof(event->returned));
	event->returned_metadata = NULL;
	event->returned_set = 0;
}

static qhw_adm_rc_t call_feedback(
	qhw_adm_t *ctx,
	struct qhw_adm_reservation_entry *entry,
	const qhw_adm_actual_usage_t *actual)
{
	struct qhw_adm_device_entry *device;

	device = qhw_hash_table_find(
		&ctx->devices,
		entry->reservation.device_id);
	if (device == NULL ||
	    device->estimator == NULL ||
	    (device->estimator->capabilities & QHW_ADM_EST_CAP_FEEDBACK) == 0) {
		return QHW_ADM_OK;
	}

	return device->estimator->record_actual(
		device->estimator_state,
		&device->profile,
		&entry->reservation,
		actual);
}

qhw_adm_rc_t qhw_adm_authorize_usage(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	const qhw_adm_usage_t *usage,
	qhw_adm_decision_t *out_decision)
{
	struct qhw_adm_reservation_entry *entry;
	qhw_adm_usage_t normalized;
	qhw_adm_decision_t decision;
	qhw_adm_rc_t rc;

	if (ctx == NULL ||
	    validate_decision_output(out_decision) != QHW_ADM_OK) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = normalize_usage(reservation_id, usage, 0, &normalized);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = find_active_reservation(ctx, reservation_id, &entry);
	if (rc == QHW_ADM_OK) {
		memset(&decision, 0, sizeof(decision));
		decision.struct_size = sizeof(decision);
		rc = policy_usage_decision(entry, &normalized, &decision, 0);
		if (rc == QHW_ADM_OK) {
			rc = copy_decision_public(ctx, &decision, out_decision);
		}
	}

	if (rc == QHW_ADM_OK) {
		qhw_adm_clear_error(ctx);
	} else {
		qhw_adm_set_error(ctx, "usage authorization failed");
	}
	qhw_adm_unlock(ctx);
	return rc;
}

qhw_adm_rc_t qhw_adm_consume(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	const qhw_adm_usage_t *usage,
	qhw_adm_decision_t *out_decision)
{
	struct qhw_adm_reservation_entry *entry;
	struct qhw_adm_usage_event *event = NULL;
	qhw_adm_usage_t normalized;
	qhw_adm_decision_t decision;
	qhw_adm_rc_t rc;

	if (ctx == NULL ||
	    validate_decision_output(out_decision) != QHW_ADM_OK) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	qhw_adm_clear_output(ctx);

	rc = find_active_reservation(ctx, reservation_id, &entry);
	if (rc != QHW_ADM_OK) {
		goto out;
	}

	if (usage != NULL && usage->task_id != 0) {
		event = qhw_hash_table_find(
			&entry->usage_events,
			usage->task_id);
	}
	rc = normalize_usage(
		reservation_id,
		usage,
		event != NULL ? event->consumed.event_time_ns : 0,
		&normalized);
	if (rc != QHW_ADM_OK) {
		goto out;
	}
	if (event != NULL) {
		if (!usage_equal(&entry->reservation, &event->consumed,
		    &normalized)) {
			rc = QHW_ADM_ERR_STATE;
			goto out;
		}
		rc = copy_decision_public(ctx, &event->decision, out_decision);
		goto out;
	}

	memset(&decision, 0, sizeof(decision));
	decision.struct_size = sizeof(decision);
	rc = policy_usage_decision(entry, &normalized, &decision, 1);
	if (rc != QHW_ADM_OK) {
		goto out;
	}
	if (decision.decision == QHW_ADM_DECISION_ACCEPTED) {
		rc = create_usage_event(&normalized, &decision, 1, &event);
		if (rc != QHW_ADM_OK) {
			goto out;
		}
		rc = add_usage_units(&entry->reservation, &normalized);
		if (rc != QHW_ADM_OK) {
			qhw_adm_free_usage_event(event, NULL);
			goto out;
		}
		rc = publish_usage_event(entry, event);
		if (rc != QHW_ADM_OK) {
			(void)rollback_usage_units(
				&entry->reservation,
				&normalized);
			goto out;
		}
		event = NULL;
	} else {
		rc = create_usage_event(&normalized, &decision, 0, &event);
		if (rc != QHW_ADM_OK) {
			goto out;
		}
		rc = publish_usage_event(entry, event);
		if (rc != QHW_ADM_OK) {
			goto out;
		}
		event = NULL;
		entry->reservation.overuse_count++;
	}
	rc = copy_decision_public(ctx, &decision, out_decision);

out:
	if (rc == QHW_ADM_OK) {
		qhw_adm_clear_error(ctx);
	} else {
		qhw_adm_set_error(ctx, "usage consume failed");
	}
	qhw_adm_unlock(ctx);
	return rc;
}

qhw_adm_rc_t qhw_adm_return_usage(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	const qhw_adm_usage_t *usage)
{
	struct qhw_adm_reservation_entry *entry;
	struct qhw_adm_usage_event *event = NULL;
	struct qhw_adm_usage_event *return_event = NULL;
	qhw_adm_usage_t normalized;
	uint64_t credit_units;
	uint64_t rate_units;
	qhw_adm_rc_t rc;

	if (ctx == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	qhw_adm_clear_output(ctx);

	rc = find_active_reservation(ctx, reservation_id, &entry);
	if (rc != QHW_ADM_OK) {
		goto out;
	}
	if (usage != NULL && usage->task_id != 0) {
		event = qhw_hash_table_find(
			&entry->usage_events,
			usage->task_id);
	}
	rc = normalize_usage(
		reservation_id,
		usage,
		event != NULL ? event->consumed.event_time_ns : 0,
		&normalized);
	if (rc != QHW_ADM_OK) {
		goto out;
	}

	if (event != NULL && event->returned_set) {
		if (!usage_equal(&entry->reservation, &event->returned,
		    &normalized)) {
			rc = QHW_ADM_ERR_STATE;
		}
		goto out;
	}
	if (normalized.task_id != 0 &&
	    (event == NULL || !event->consumed_set)) {
		rc = QHW_ADM_ERR_STATE;
		goto out;
	}

	credit_units = usage_credit_units(&entry->reservation, &normalized);
	rate_units = usage_rate_units(&entry->reservation, &normalized);
	if (event != NULL) {
		if (credit_units >
		    usage_credit_units(&entry->reservation, &event->consumed) ||
		    rate_units >
		    usage_rate_units(&entry->reservation, &event->consumed)) {
			rc = QHW_ADM_ERR_INVAL;
			goto out;
		}
	}

	if (event == NULL &&
	    rate_units != 0 &&
	    rate_units > rate_window_consumed(entry, normalized.event_time_ns)) {
		rc = QHW_ADM_ERR_INVAL;
		goto out;
	}

	rc = validate_return_units(&entry->reservation, &normalized);
	if (rc != QHW_ADM_OK) {
		goto out;
	}
	if (event != NULL) {
		rc = store_return_usage(event, &normalized);
		if (rc != QHW_ADM_OK) {
			goto out;
		}
	} else if (rate_units != 0) {
		rc = create_return_event(&normalized, &return_event);
		if (rc != QHW_ADM_OK) {
			goto out;
		}
	}

	if (entry->policy != NULL && entry->policy->return_usage != NULL) {
		rc = entry->policy->return_usage(
			entry->policy_state,
			&entry->reservation,
			&normalized);
		if (rc != QHW_ADM_OK) {
			clear_return_usage(event);
			qhw_adm_free_usage_event(return_event, NULL);
			goto out;
		}
	}
	rc = subtract_usage_units(&entry->reservation, &normalized);
	if (rc != QHW_ADM_OK) {
		clear_return_usage(event);
		qhw_adm_free_usage_event(return_event, NULL);
		goto out;
	}
	if (return_event != NULL) {
		rc = publish_usage_event(entry, return_event);
		if (rc != QHW_ADM_OK) {
			return_event = NULL;
			goto out;
		}
		return_event = NULL;
	}

out:
	if (return_event != NULL) {
		qhw_adm_free_usage_event(return_event, NULL);
	}
	if (rc == QHW_ADM_OK) {
		qhw_adm_clear_error(ctx);
	} else {
		qhw_adm_set_error(ctx, "usage return failed");
	}
	qhw_adm_unlock(ctx);
	return rc;
}

qhw_adm_rc_t qhw_adm_get_usage(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	qhw_adm_usage_state_t *out_usage)
{
	struct qhw_adm_reservation_entry *entry;
	size_t struct_size;
	qhw_adm_rc_t rc;

	if (ctx == NULL ||
	    validate_usage_output(out_usage) != QHW_ADM_OK) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	entry = qhw_hash_table_find(&ctx->reservations, reservation_id);
	if (entry == NULL) {
		qhw_adm_set_error(ctx, "reservation was not found");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOT_FOUND;
	}

	struct_size = out_usage->struct_size;
	memset(out_usage, 0, sizeof(*out_usage));
	out_usage->struct_size = struct_size;
	out_usage->reservation_id = reservation_id;
	out_usage->credits_reserved = entry->reservation.credits_reserved;
	out_usage->credits_consumed = entry->reservation.credits_consumed;
	out_usage->rate_reserved = entry->reservation.rate_reserved;
	if (entry->reservation.rate_reserved != 0) {
		out_usage->rate_consumed = rate_window_consumed(
			entry,
			latest_rate_event_time(entry));
	} else {
		out_usage->rate_consumed = entry->reservation.rate_consumed;
	}
	out_usage->estimated_total_ns = entry->reservation.estimated_total_ns;
	out_usage->actual_total_ns = entry->reservation.actual_total_ns;
	out_usage->remaining_credits = usage_saturating_sub(
		out_usage->credits_reserved,
		out_usage->credits_consumed);
	out_usage->remaining_rate = usage_saturating_sub(
		out_usage->rate_reserved,
		out_usage->rate_consumed);
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_get_compliance(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	qhw_adm_compliance_t *out_compliance)
{
	struct qhw_adm_reservation_entry *entry;
	const char *message = "reservation compliant";
	size_t struct_size;
	qhw_adm_rc_t rc;

	if (ctx == NULL ||
	    validate_compliance_output(out_compliance) != QHW_ADM_OK) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	entry = qhw_hash_table_find(&ctx->reservations, reservation_id);
	if (entry == NULL) {
		qhw_adm_set_error(ctx, "reservation was not found");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOT_FOUND;
	}

	struct_size = out_compliance->struct_size;
	memset(out_compliance, 0, sizeof(*out_compliance));
	out_compliance->struct_size = struct_size;
	out_compliance->reservation_id = reservation_id;
	out_compliance->overuse_count = entry->reservation.overuse_count;
	out_compliance->underuse_score = entry->reservation.underuse_score;
	out_compliance->unused_capacity = entry->reservation.unused_capacity;
	out_compliance->action = QHW_ADM_COMPLIANCE_ALLOW;
	if (out_compliance->overuse_count != 0) {
		out_compliance->action = QHW_ADM_COMPLIANCE_REJECT;
		message = "reservation exceeded admitted usage";
	} else if (out_compliance->unused_capacity != 0) {
		message = "reservation has unused capacity";
	}

	rc = qhw_adm_copy_output_message(
		ctx,
		message,
		&out_compliance->message);
	if (rc == QHW_ADM_OK) {
		qhw_adm_clear_error(ctx);
	} else {
		qhw_adm_set_error(ctx, "failed to copy compliance message");
	}
	qhw_adm_unlock(ctx);
	return rc;
}

qhw_adm_rc_t qhw_adm_record_actual(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	const qhw_adm_actual_usage_t *actual)
{
	struct qhw_adm_reservation_entry *entry;
	struct qhw_adm_actual_event *event = NULL;
	qhw_adm_actual_usage_t normalized;
	qhw_adm_rc_t rc;

	if (ctx == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	qhw_adm_clear_output(ctx);

	rc = find_active_reservation(ctx, reservation_id, &entry);
	if (rc != QHW_ADM_OK) {
		goto out;
	}
	rc = normalize_actual(reservation_id, actual, &normalized);
	if (rc != QHW_ADM_OK) {
		goto out;
	}
	if (normalized.task_id != 0) {
		event = qhw_hash_table_find(
			&entry->actual_events,
			normalized.task_id);
	}
	if (event != NULL) {
		if (!actual_equal(&event->actual, &normalized)) {
			rc = QHW_ADM_ERR_STATE;
			goto out;
		}
		if (!event->feedback_pending) {
			goto out;
		}
		rc = call_feedback(ctx, entry, &event->actual);
		if (rc == QHW_ADM_OK) {
			event->feedback_pending = 0;
		}
		goto out;
	}

	if (normalized.task_id != 0) {
		event = calloc(1, sizeof(*event));
		if (event == NULL) {
			rc = QHW_ADM_ERR_NOMEM;
			goto out;
		}
		rc = copy_actual(
			&normalized,
			&event->actual,
			&event->metadata);
		if (rc != QHW_ADM_OK) {
			qhw_adm_free_actual_event(event, NULL);
			goto out;
		}
		if (qhw_hash_table_insert(
			&entry->actual_events,
			normalized.task_id,
			event) != 0) {
			qhw_adm_free_actual_event(event, NULL);
			rc = QHW_ADM_ERR_NOMEM;
			goto out;
		}
	}

	rc = add_actual_units(&entry->reservation, &normalized);
	if (rc != QHW_ADM_OK) {
		if (event != NULL) {
			qhw_adm_free_actual_event(
				qhw_hash_table_remove(
					&entry->actual_events,
					normalized.task_id),
				NULL);
		}
		goto out;
	}

	rc = call_feedback(ctx, entry, &normalized);
	if (event != NULL && rc != QHW_ADM_OK) {
		event->feedback_pending = 1;
	}

out:
	if (rc == QHW_ADM_OK) {
		qhw_adm_clear_error(ctx);
	} else {
		qhw_adm_set_error(ctx, "actual usage recording failed");
	}
	qhw_adm_unlock(ctx);
	return rc;
}

qhw_adm_rc_t qhw_adm_finalize_unused_capacity(
	struct qhw_adm_reservation_entry *entry)
{
	qhw_adm_reservation_t *reservation;
	uint64_t unused;

	if (entry == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	reservation = &entry->reservation;
	unused = usage_saturating_sub(
		usage_capacity_units(reservation),
		usage_consumed_units(reservation));
	if (unused > reservation->unused_capacity) {
		reservation->unused_capacity = unused;
	}
	reservation->underuse_score = reservation->unused_capacity;
	return QHW_ADM_OK;
}

void qhw_adm_free_usage_event(void *value, void *user_data)
{
	struct qhw_adm_usage_event *event = value;

	(void)user_data;

	if (event == NULL) {
		return;
	}

	qhw_adm_free_metadata_count(
		event->consumed_metadata,
		event->consumed.metadata_count);
	qhw_adm_free_metadata_count(
		event->returned_metadata,
		event->returned.metadata_count);
	free_decision_copy(&event->decision);
	free(event);
}

void qhw_adm_free_usage_events(struct qhw_adm_reservation_entry *entry)
{
	struct qhw_list_node *node;

	if (entry == NULL || !entry->usage_event_list_initialized) {
		return;
	}

	while (!qhw_list_empty(&entry->usage_event_list)) {
		struct qhw_adm_usage_event *event;

		node = qhw_list_pop_front(&entry->usage_event_list);
		event = qhw_container_of(
			node,
			struct qhw_adm_usage_event,
			node);
		qhw_adm_free_usage_event(event, NULL);
	}
}

void qhw_adm_free_actual_event(void *value, void *user_data)
{
	struct qhw_adm_actual_event *event = value;

	(void)user_data;

	if (event == NULL) {
		return;
	}

	qhw_adm_free_metadata_count(
		event->metadata,
		event->actual.metadata_count);
	free(event);
}
