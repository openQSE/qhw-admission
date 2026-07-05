#include "qhw_admission_internal.h"

#include <stdlib.h>
#include <string.h>

static qhw_adm_rc_t validate_decision_output(
	const qhw_adm_decision_t *out_decision)
{
	if (out_decision == NULL ||
	    out_decision->struct_size < sizeof(*out_decision)) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t validate_reservation_output(
	const qhw_adm_reservation_t *out_reservation)
{
	if (out_reservation == NULL ||
	    out_reservation->struct_size < sizeof(*out_reservation)) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t validate_capacity_output(
	const qhw_adm_capacity_view_t *out_capacity)
{
	if (out_capacity == NULL ||
	    out_capacity->struct_size < sizeof(*out_capacity)) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static int reservation_is_active(const qhw_adm_reservation_t *reservation)
{
	return reservation->state == QHW_ADM_RESERVATION_ACTIVE;
}

int qhw_adm_device_has_active_reservation(
	qhw_adm_t *ctx,
	uint64_t device_id)
{
	size_t i;

	if (ctx == NULL || device_id == 0) {
		return 0;
	}

	for (i = 0; i < ctx->reservations.bucket_count; i++) {
		struct qhw_hash_entry *hash_entry;

		hash_entry = ctx->reservations.buckets[i];
		while (hash_entry != NULL) {
			struct qhw_adm_reservation_entry *entry;

			entry = hash_entry->value;
			hash_entry = hash_entry->next;
			if (entry->reservation.device_id == device_id &&
			    reservation_is_active(&entry->reservation)) {
				return 1;
			}
		}
	}

	return 0;
}

static qhw_adm_rc_t validate_request(
	const qhw_adm_request_t *request)
{
	size_t i;

	if (request == NULL ||
	    request->struct_size < sizeof(*request) ||
	    request->device_id == 0 ||
	    request->walltime_ns == 0 ||
	    request->task_class_count == 0 ||
	    request->task_classes == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	if (request->workload_kind != QHW_ADM_WORKLOAD_QUANTUM_JOB &&
	    request->workload_kind != QHW_ADM_WORKLOAD_HYBRID_JOB) {
		return QHW_ADM_ERR_INVAL;
	}

	for (i = 0; i < request->task_class_count; i++) {
		const qhw_adm_qtask_class_t *task;

		task = &request->task_classes[i];
		if (task->struct_size < sizeof(*task) ||
		    task->count == 0 ||
		    task->qubit_count == 0 ||
		    task->shots == 0 ||
		    (task->metadata_count > 0 && task->metadata == NULL)) {
			return QHW_ADM_ERR_INVAL;
		}
	}

	if (request->metadata_count > 0 && request->metadata == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t compute_quantum_budget(
	const qhw_adm_request_t *request,
	uint64_t *out_budget)
{
	uint64_t reserved_time;
	qhw_adm_rc_t rc;

	rc = qhw_adm_add_u64(
		request->classical_runtime_ns,
		request->overhead_ns,
		&reserved_time);
	if (rc != QHW_ADM_OK || request->walltime_ns <= reserved_time) {
		return QHW_ADM_ERR_INVAL;
	}

	*out_budget = request->walltime_ns - reserved_time;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t add_estimate(
	qhw_adm_estimate_t *total,
	const qhw_adm_estimate_t *part)
{
	qhw_adm_rc_t rc;

	rc = qhw_adm_add_u64(total->execution_ns, part->execution_ns,
		&total->execution_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(total->measurement_ns, part->measurement_ns,
		&total->measurement_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(total->compile_ns, part->compile_ns,
		&total->compile_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(total->transfer_ns, part->transfer_ns,
		&total->transfer_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(
		total->control_overhead_ns,
		part->control_overhead_ns,
		&total->control_overhead_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	rc = qhw_adm_add_u64(total->total_ns, part->total_ns,
		&total->total_ns);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	return qhw_adm_add_u64(
		total->baseline_units,
		part->baseline_units,
		&total->baseline_units);
}

static qhw_adm_rc_t estimate_request(
	struct qhw_adm_device_entry *entry,
	const qhw_adm_request_t *request,
	qhw_adm_estimate_t *out_estimate)
{
	size_t i;
	qhw_adm_rc_t rc;

	memset(out_estimate, 0, sizeof(*out_estimate));
	out_estimate->struct_size = sizeof(*out_estimate);

	if (entry->estimator->validate_request != NULL) {
		rc = entry->estimator->validate_request(
			entry->estimator_state,
			&entry->profile,
			request);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
	}

	if (entry->estimator->estimate_request != NULL) {
		return entry->estimator->estimate_request(
			entry->estimator_state,
			&entry->profile,
			request,
			out_estimate);
	}

	out_estimate->confidence_ppm = 1000000U;
	for (i = 0; i < request->task_class_count; i++) {
		qhw_adm_estimate_t part;

		memset(&part, 0, sizeof(part));
		part.struct_size = sizeof(part);
		rc = entry->estimator->estimate_task(
			entry->estimator_state,
			&entry->profile,
			&request->task_classes[i],
			&part);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
		rc = add_estimate(out_estimate, &part);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
		if (part.confidence_ppm < out_estimate->confidence_ppm) {
			out_estimate->confidence_ppm = part.confidence_ppm;
		}
	}

	return QHW_ADM_OK;
}

static uint64_t min_nonzero(uint64_t a, uint64_t b)
{
	if (a == 0) {
		return b;
	}
	if (b == 0) {
		return a;
	}
	if (a < b) {
		return a;
	}
	return b;
}

static uint64_t saturating_sub(uint64_t a, uint64_t b)
{
	if (a < b) {
		return 0;
	}
	return a - b;
}

static uint64_t saturating_add(uint64_t a, uint64_t b)
{
	uint64_t value;

	if (qhw_adm_add_u64(a, b, &value) != QHW_ADM_OK) {
		return UINT64_MAX;
	}
	return value;
}

static void accumulate_active_capacity(
	qhw_adm_t *ctx,
	uint64_t device_id,
	uint64_t scope_id,
	qhw_adm_capacity_view_t *view)
{
	size_t i;

	for (i = 0; i < ctx->reservations.bucket_count; i++) {
		struct qhw_hash_entry *hash_entry;

		hash_entry = ctx->reservations.buckets[i];
		while (hash_entry != NULL) {
			struct qhw_adm_reservation_entry *entry;
			qhw_adm_reservation_t *reservation;

			entry = hash_entry->value;
			reservation = &entry->reservation;
			hash_entry = hash_entry->next;
			if (!reservation_is_active(reservation) ||
			    reservation->device_id != device_id) {
				continue;
			}
			view->active_reservation_count = saturating_add(
				view->active_reservation_count,
				1);
			view->credits_reserved = saturating_add(
				view->credits_reserved,
				reservation->credits_reserved);
			view->rate_reserved = saturating_add(
				view->rate_reserved,
				reservation->rate_reserved);
			if (reservation->scope_id == scope_id) {
				view->scoped_reserved_credits = saturating_add(
					view->scoped_reserved_credits,
					reservation->credits_reserved);
				view->scoped_reserved_rate = saturating_add(
					view->scoped_reserved_rate,
					reservation->rate_reserved);
			}
		}
	}
}

static qhw_adm_rc_t derive_total_rate(
	struct qhw_adm_device_entry *entry,
	uint64_t *out_rate)
{
	qhw_adm_estimate_t estimate;
	qhw_adm_rc_t rc;

	if (entry->profile.device_rate != 0) {
		*out_rate = entry->profile.device_rate;
		return QHW_ADM_OK;
	}
	if (entry->profile.time_span_ns == 0) {
		*out_rate = 0;
		return QHW_ADM_OK;
	}

	memset(&estimate, 0, sizeof(estimate));
	estimate.struct_size = sizeof(estimate);
	rc = entry->estimator->estimate_baseline(
		entry->estimator_state,
		&entry->profile,
		&entry->profile.baseline,
		&estimate);
	if (rc != QHW_ADM_OK || estimate.total_ns == 0) {
		return QHW_ADM_ERR_ESTIMATOR;
	}

	return qhw_adm_ceil_div_u64(
		entry->profile.time_span_ns,
		estimate.total_ns,
		out_rate);
}

static qhw_adm_rc_t build_capacity_view(
	qhw_adm_t *ctx,
	struct qhw_adm_device_entry *entry,
	uint64_t scope_id,
	qhw_adm_capacity_view_t *out_capacity)
{
	qhw_adm_capacity_snapshot_t snapshot;
	qhw_adm_capacity_view_t view;
	qhw_adm_rc_t rc;

	memset(&snapshot, 0, sizeof(snapshot));
	snapshot.struct_size = sizeof(snapshot);
	snapshot.device_id = entry->profile.device_id;
	snapshot.scope_id = scope_id;
	snapshot.device_state = QHW_ADM_DEVICE_AVAILABLE;
	snapshot.now_ns = qhw_adm_now_ns();
	snapshot.confidence_ppm = 1000000U;

	if (ctx->capacity_provider.get_snapshot != NULL) {
		rc = ctx->capacity_provider.get_snapshot(
			entry->profile.device_id,
			scope_id,
			&snapshot,
			ctx->capacity_provider.user_data);
		if (rc != QHW_ADM_OK) {
			return rc;
		}
		if (snapshot.struct_size < sizeof(snapshot)) {
			return QHW_ADM_ERR_INVAL;
		}
	}

	memset(&view, 0, sizeof(view));
	view.struct_size = sizeof(view);
	view.device_id = entry->profile.device_id;
	view.scope_id = scope_id;
	view.device_state = snapshot.device_state;
	view.now_ns = snapshot.now_ns;
	if (view.now_ns == 0) {
		view.now_ns = qhw_adm_now_ns();
	}
	view.next_available_ns = snapshot.next_available_ns;
	view.queued_baseline_units = snapshot.queued_baseline_units;
	view.queued_estimated_ns = snapshot.queued_estimated_ns;
	view.total_credits = entry->profile.total_credits;
	view.external_credit_limit = snapshot.external_credit_limit;
	view.external_rate_limit = snapshot.external_rate_limit;
	view.scheduler_policy_id = snapshot.scheduler_policy_id;
	view.confidence_ppm = snapshot.confidence_ppm;
	view.metadata = snapshot.metadata;
	view.metadata_count = snapshot.metadata_count;
	rc = qhw_adm_copy_output_metadata(
		ctx,
		view.metadata,
		view.metadata_count,
		&view.metadata,
		&view.metadata_count);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = derive_total_rate(entry, &view.total_rate);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	accumulate_active_capacity(ctx, entry->profile.device_id, scope_id, &view);
	view.active_reservation_count = saturating_add(
		view.active_reservation_count,
		snapshot.active_reservation_count);
	view.core_available_credits = saturating_sub(
		view.total_credits,
		view.credits_reserved);
	view.core_available_rate = saturating_sub(
		view.total_rate,
		view.rate_reserved);
	view.effective_available_credits = min_nonzero(
		view.core_available_credits,
		saturating_sub(
			view.external_credit_limit,
			view.scoped_reserved_credits));
	view.effective_available_rate = min_nonzero(
		view.core_available_rate,
		saturating_sub(
			view.external_rate_limit,
			view.scoped_reserved_rate));

	*out_capacity = view;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t apply_policy_capacity(
	struct qhw_adm_device_entry *entry,
	const qhw_adm_capacity_view_t *core_view,
	qhw_adm_capacity_view_t *out_capacity)
{
	qhw_adm_capacity_view_t decorated;

	if (entry == NULL || core_view == NULL || out_capacity == NULL) {
		return QHW_ADM_ERR_INVAL;
	}
	if (entry->policy == NULL || entry->policy->capacity == NULL) {
		*out_capacity = *core_view;
		return QHW_ADM_OK;
	}

	decorated = *core_view;
	if (entry->policy->capacity(
		entry->policy_state,
		&entry->profile,
		core_view,
		&decorated) != QHW_ADM_OK) {
		return QHW_ADM_ERR_POLICY;
	}

	*out_capacity = decorated;
	return QHW_ADM_OK;
}

static void fill_rejected_decision(
	qhw_adm_decision_t *decision,
	const qhw_adm_request_t *request,
	uint64_t reason_code)
{
	size_t struct_size = decision->struct_size;

	memset(decision, 0, sizeof(*decision));
	decision->struct_size = struct_size;
	decision->decision = QHW_ADM_DECISION_REJECTED;
	decision->reason_code = reason_code;
	if (request != NULL) {
		decision->request_id = request->request_id;
		decision->device_id = request->device_id;
		decision->scope_id = request->scope_id;
	}
}

static qhw_adm_rc_t copy_decision_outputs(
	qhw_adm_t *ctx,
	qhw_adm_decision_t *decision)
{
	const qhw_adm_kv_t *metadata = decision->metadata;
	size_t metadata_count = decision->metadata_count;
	const char *message = decision->message;
	qhw_adm_rc_t rc;

	rc = qhw_adm_copy_output_metadata(
		ctx,
		metadata,
		metadata_count,
		&decision->metadata,
		&decision->metadata_count);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	return qhw_adm_copy_output_message(
		ctx,
		message,
		&decision->message);
}

static void clear_decision_pointer_outputs(qhw_adm_decision_t *decision)
{
	if (decision == NULL) {
		return;
	}

	decision->message = NULL;
	decision->metadata = NULL;
	decision->metadata_count = 0;
}

static qhw_adm_rc_t copy_capacity_outputs(
	qhw_adm_t *ctx,
	qhw_adm_capacity_view_t *capacity)
{
	const qhw_adm_kv_t *metadata = capacity->metadata;
	size_t metadata_count = capacity->metadata_count;

	return qhw_adm_copy_output_metadata(
		ctx,
		metadata,
		metadata_count,
		&capacity->metadata,
		&capacity->metadata_count);
}

static qhw_adm_rc_t prepare_policy_call(
	qhw_adm_t *ctx,
	const qhw_adm_request_t *request,
	qhw_adm_decision_t *out_decision,
	struct qhw_adm_device_entry **out_entry,
	qhw_adm_estimate_t *out_estimate,
	qhw_adm_capacity_view_t *out_capacity,
	uint64_t *out_budget)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_rc_t rc;

	rc = validate_request(request);
	if (rc != QHW_ADM_OK) {
		fill_rejected_decision(
			out_decision,
			request,
			QHW_ADM_REASON_INVALID_REQUEST);
		return rc;
	}

	entry = qhw_hash_table_find(&ctx->devices, request->device_id);
	if (entry == NULL) {
		fill_rejected_decision(
			out_decision,
			request,
			QHW_ADM_REASON_DEVICE_NOT_FOUND);
		return QHW_ADM_ERR_NOT_FOUND;
	}
	if (entry->policy == NULL) {
		fill_rejected_decision(
			out_decision,
			request,
			QHW_ADM_REASON_POLICY_FAILED);
		return QHW_ADM_ERR_NOT_FOUND;
	}

	rc = compute_quantum_budget(request, out_budget);
	if (rc != QHW_ADM_OK) {
		fill_rejected_decision(
			out_decision,
			request,
			QHW_ADM_REASON_WALLTIME_INFEASIBLE);
		return rc;
	}

	rc = estimate_request(entry, request, out_estimate);
	if (rc != QHW_ADM_OK) {
		fill_rejected_decision(
			out_decision,
			request,
			QHW_ADM_REASON_ESTIMATOR_FAILED);
		return rc;
	}

	rc = build_capacity_view(
		ctx,
		entry,
		request->scope_id,
		out_capacity);
	if (rc != QHW_ADM_OK) {
		fill_rejected_decision(
			out_decision,
			request,
			QHW_ADM_REASON_POLICY_FAILED);
		return rc;
	}
	rc = apply_policy_capacity(entry, out_capacity, out_capacity);
	if (rc != QHW_ADM_OK) {
		fill_rejected_decision(
			out_decision,
			request,
			QHW_ADM_REASON_POLICY_FAILED);
		return rc;
	}

	*out_entry = entry;
	return QHW_ADM_OK;
}

static void decorate_decision(
	qhw_adm_decision_t *decision,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	uint64_t quantum_budget)
{
	decision->request_id = request->request_id;
	decision->device_id = request->device_id;
	decision->scope_id = request->scope_id;
	decision->estimated_total_ns = estimate->total_ns;
	decision->estimated_start_ns = capacity->next_available_ns;
	decision->estimated_finish_ns = saturating_add(
		capacity->next_available_ns,
		estimate->total_ns);
	decision->quantum_budget_ns = quantum_budget;
	decision->confidence_ppm = estimate->confidence_ppm;
}

qhw_adm_rc_t qhw_adm_evaluate(
	qhw_adm_t *ctx,
	const qhw_adm_request_t *request,
	qhw_adm_decision_t *out_decision)
{
	struct qhw_adm_device_entry *entry = NULL;
	qhw_adm_capacity_view_t capacity;
	qhw_adm_estimate_t estimate;
	uint64_t quantum_budget = 0;
	qhw_adm_rc_t rc;

	if (ctx == NULL ||
	    validate_decision_output(out_decision) != QHW_ADM_OK) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}

	rc = prepare_policy_call(
		ctx,
		request,
		out_decision,
		&entry,
		&estimate,
		&capacity,
		&quantum_budget);
	if (rc == QHW_ADM_OK) {
		memset(out_decision, 0, sizeof(*out_decision));
		out_decision->struct_size = sizeof(*out_decision);
		rc = entry->policy->evaluate(
			entry->policy_state,
			&entry->profile,
			request,
			&estimate,
			&capacity,
			out_decision);
		if (rc == QHW_ADM_OK) {
			decorate_decision(
				out_decision,
				request,
				&estimate,
				&capacity,
				quantum_budget);
			rc = copy_decision_outputs(ctx, out_decision);
			if (rc == QHW_ADM_OK) {
				qhw_adm_clear_error(ctx);
			} else {
				qhw_adm_set_error(ctx, "failed to copy decision");
			}
		} else {
			clear_decision_pointer_outputs(out_decision);
			qhw_adm_set_error(ctx, "policy evaluation failed");
		}
	} else {
		qhw_adm_set_error(ctx, "admission evaluation failed");
	}

	qhw_adm_unlock(ctx);
	return rc;
}

static qhw_adm_rc_t allocate_reservation_id(
	qhw_adm_t *ctx,
	uint64_t requested_id,
	uint64_t *out_id)
{
	uint64_t candidate;

	if (requested_id != 0) {
		if (qhw_hash_table_find(&ctx->reservations, requested_id) != NULL) {
			return QHW_ADM_ERR_EXISTS;
		}
		*out_id = requested_id;
		return QHW_ADM_OK;
	}

	candidate = ctx->next_reservation_id;
	while (candidate != 0) {
		if (qhw_hash_table_find(&ctx->reservations, candidate) == NULL) {
			ctx->next_reservation_id = candidate + 1;
			*out_id = candidate;
			return QHW_ADM_OK;
		}
		candidate++;
	}

	return QHW_ADM_ERR_STATE;
}

static qhw_adm_rc_t select_ttl(
	const qhw_adm_request_t *request,
	const qhw_adm_device_profile_t *profile,
	const qhw_adm_policy_grant_t *grant,
	uint64_t *out_ttl)
{
	if (grant->ttl_ns != 0) {
		*out_ttl = grant->ttl_ns;
		return QHW_ADM_OK;
	}
	if (request->ttl_ns != 0) {
		*out_ttl = request->ttl_ns;
		return QHW_ADM_OK;
	}
	if (request->walltime_ns != 0) {
		*out_ttl = request->walltime_ns;
		return QHW_ADM_OK;
	}

	*out_ttl = profile->default_ttl_ns;
	return QHW_ADM_OK;
}

static qhw_adm_rc_t validate_policy_grant(
	const qhw_adm_request_t *request,
	const qhw_adm_capacity_view_t *capacity,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_policy_grant_t *grant)
{
	if (grant->struct_size < sizeof(*grant) ||
	    grant->device_id != request->device_id ||
	    grant->scope_id != request->scope_id ||
	    grant->baseline_units_granted != estimate->baseline_units ||
	    (grant->metadata_count > 0 && grant->metadata == NULL)) {
		return QHW_ADM_ERR_POLICY;
	}

	if ((capacity->total_credits != 0 ||
	     capacity->external_credit_limit != 0) &&
	    grant->credits_granted > capacity->effective_available_credits) {
		return QHW_ADM_ERR_POLICY;
	}

	if ((capacity->total_rate != 0 ||
	     capacity->external_rate_limit != 0) &&
	    grant->rate_granted > capacity->effective_available_rate) {
		return QHW_ADM_ERR_POLICY;
	}

	return QHW_ADM_OK;
}

static qhw_adm_rc_t create_reservation_entry(
	struct qhw_adm_device_entry *device,
	const qhw_adm_request_t *request,
	const qhw_adm_estimate_t *estimate,
	const qhw_adm_capacity_view_t *capacity,
	const qhw_adm_policy_grant_t *grant,
	uint64_t reservation_id,
	uint64_t quantum_budget,
	struct qhw_adm_reservation_entry **out_entry)
{
	struct qhw_adm_reservation_entry *entry;
	qhw_adm_reservation_t *reservation;
	uint64_t ttl_ns;
	qhw_adm_rc_t rc;

	*out_entry = NULL;
	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		return QHW_ADM_ERR_NOMEM;
	}

	rc = qhw_adm_copy_metadata(
		grant->metadata,
		grant->metadata_count,
		&entry->metadata);
	if (rc != QHW_ADM_OK) {
		free(entry);
		return rc;
	}

	reservation = &entry->reservation;
	reservation->struct_size = sizeof(*reservation);
	reservation->reservation_id = reservation_id;
	reservation->request_id = request->request_id;
	reservation->device_id = request->device_id;
	reservation->scope_id = request->scope_id;
	reservation->user_id = request->user_id;
	reservation->job_id = request->job_id;
	reservation->workload_kind = request->workload_kind;
	reservation->state = QHW_ADM_RESERVATION_ACTIVE;
	reservation->credits_reserved = grant->credits_granted;
	reservation->rate_reserved = grant->rate_granted;
	reservation->quantum_budget_ns = quantum_budget;
	reservation->device_profile_version = device->profile_version;
	reservation->policy_version = device->policy_version;
	reservation->estimator_version = device->estimator_version;
	reservation->estimated_total_ns = estimate->total_ns;
	reservation->created_at_ns = capacity->now_ns;
	reservation->metadata = entry->metadata;
	reservation->metadata_count = grant->metadata_count;
	entry->policy = device->policy;
	entry->policy_state = device->policy_state;

	rc = select_ttl(request, &device->profile, grant, &ttl_ns);
	if (rc != QHW_ADM_OK) {
		qhw_adm_free_reservation_entry(entry, NULL);
		return rc;
	}
	if (ttl_ns != 0) {
		rc = qhw_adm_add_u64(
			reservation->created_at_ns,
			ttl_ns,
			&reservation->expires_at_ns);
		if (rc != QHW_ADM_OK) {
			qhw_adm_free_reservation_entry(entry, NULL);
			return rc;
		}
	}

	*out_entry = entry;
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_reserve(
	qhw_adm_t *ctx,
	const qhw_adm_request_t *request,
	qhw_adm_decision_t *out_decision)
{
	struct qhw_adm_reservation_entry *reservation = NULL;
	struct qhw_adm_device_entry *entry = NULL;
	qhw_adm_policy_grant_t grant;
	qhw_adm_capacity_view_t capacity;
	qhw_adm_estimate_t estimate;
	uint64_t quantum_budget = 0;
	uint64_t reservation_id = 0;
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

	rc = prepare_policy_call(
		ctx,
		request,
		out_decision,
		&entry,
		&estimate,
		&capacity,
		&quantum_budget);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "admission reservation failed");
		qhw_adm_unlock(ctx);
		return rc;
	}

	rc = allocate_reservation_id(
		ctx,
		request->reservation_id,
		&reservation_id);
	if (rc != QHW_ADM_OK) {
		fill_rejected_decision(
			out_decision,
			request,
			QHW_ADM_REASON_OBJECT_EXISTS);
		qhw_adm_set_error(ctx, "reservation id already exists");
		qhw_adm_unlock(ctx);
		return rc;
	}

	memset(&grant, 0, sizeof(grant));
	grant.struct_size = sizeof(grant);
	memset(out_decision, 0, sizeof(*out_decision));
	out_decision->struct_size = sizeof(*out_decision);
	rc = entry->policy->reserve(
		entry->policy_state,
		&entry->profile,
		request,
		&estimate,
		&capacity,
		&grant,
		out_decision);
	if (rc != QHW_ADM_OK) {
		decorate_decision(
			out_decision,
			request,
			&estimate,
			&capacity,
			quantum_budget);
		clear_decision_pointer_outputs(out_decision);
		qhw_adm_set_error(ctx, "policy reservation failed");
		qhw_adm_unlock(ctx);
		return rc;
	}
	if (out_decision->decision != QHW_ADM_DECISION_ACCEPTED) {
		decorate_decision(
			out_decision,
			request,
			&estimate,
			&capacity,
			quantum_budget);
		rc = copy_decision_outputs(ctx, out_decision);
		if (rc == QHW_ADM_OK) {
			qhw_adm_clear_error(ctx);
		} else {
			qhw_adm_set_error(ctx, "failed to copy decision");
		}
		qhw_adm_unlock(ctx);
		return rc;
	}

	rc = validate_policy_grant(request, &capacity, &estimate, &grant);
	if (rc != QHW_ADM_OK) {
		fill_rejected_decision(
			out_decision,
			request,
			QHW_ADM_REASON_POLICY_FAILED);
		qhw_adm_set_error(ctx, "invalid policy grant");
		qhw_adm_unlock(ctx);
		return rc;
	}

	rc = create_reservation_entry(
		entry,
		request,
		&estimate,
		&capacity,
		&grant,
		reservation_id,
		quantum_budget,
		&reservation);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "failed to create reservation");
		qhw_adm_unlock(ctx);
		return rc;
	}

	if (qhw_hash_table_insert(
		&ctx->reservations,
		reservation_id,
		reservation) != 0) {
		qhw_adm_free_reservation_entry(reservation, NULL);
		qhw_adm_set_error(ctx, "failed to publish reservation");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOMEM;
	}

	out_decision->reservation_id = reservation_id;
	out_decision->capacity_granted = grant.baseline_units_granted;
	decorate_decision(
		out_decision,
		request,
		&estimate,
		&capacity,
		quantum_budget);
	rc = copy_decision_outputs(ctx, out_decision);
	if (rc != QHW_ADM_OK) {
		qhw_hash_table_remove(&ctx->reservations, reservation_id);
		qhw_adm_free_reservation_entry(reservation, NULL);
		qhw_adm_set_error(ctx, "failed to copy decision");
		qhw_adm_unlock(ctx);
		return rc;
	}
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_get_capacity(
	qhw_adm_t *ctx,
	uint64_t device_id,
	uint64_t scope_id,
	qhw_adm_capacity_view_t *out_capacity)
{
	struct qhw_adm_device_entry *entry;
	qhw_adm_capacity_view_t capacity;
	qhw_adm_rc_t rc;

	if (ctx == NULL ||
	    validate_capacity_output(out_capacity) != QHW_ADM_OK) {
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

	rc = build_capacity_view(ctx, entry, scope_id, &capacity);
	if (rc == QHW_ADM_OK) {
		rc = apply_policy_capacity(entry, &capacity, &capacity);
	}
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "failed to build capacity view");
		qhw_adm_unlock(ctx);
		return rc;
	}

	*out_capacity = capacity;
	rc = copy_capacity_outputs(ctx, out_capacity);
	if (rc != QHW_ADM_OK) {
		qhw_adm_set_error(ctx, "failed to copy capacity view");
		qhw_adm_unlock(ctx);
		return rc;
	}
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_set_capacity_provider(
	qhw_adm_t *ctx,
	const qhw_adm_capacity_provider_t *provider)
{
	qhw_adm_rc_t rc;

	if (ctx == NULL ||
	    (provider != NULL &&
	     provider->struct_size < sizeof(*provider))) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	qhw_adm_clear_output(ctx);

	memset(&ctx->capacity_provider, 0, sizeof(ctx->capacity_provider));
	if (provider != NULL) {
		ctx->capacity_provider = *provider;
	}
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_get_reservation(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	qhw_adm_reservation_t *out_reservation)
{
	struct qhw_adm_reservation_entry *entry;
	qhw_adm_rc_t rc;

	if (ctx == NULL || reservation_id == 0 ||
	    validate_reservation_output(out_reservation) != QHW_ADM_OK) {
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

	*out_reservation = entry->reservation;
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

static qhw_adm_rc_t transition_reservation(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	qhw_adm_reservation_state_t state,
	uint64_t reason_code)
{
	struct qhw_adm_reservation_entry *entry;
	qhw_adm_reservation_t *reservation;
	qhw_adm_rc_t rc;

	entry = qhw_hash_table_find(&ctx->reservations, reservation_id);
	if (entry == NULL) {
		return QHW_ADM_ERR_NOT_FOUND;
	}

	reservation = &entry->reservation;
	if (reservation->state == state) {
		return QHW_ADM_OK;
	}
	if (reservation->state != QHW_ADM_RESERVATION_ACTIVE) {
		return QHW_ADM_ERR_STATE;
	}

	if (entry->policy != NULL && entry->policy->release != NULL) {
		qhw_adm_reservation_state_t old_state = reservation->state;

		reservation->state = state;
		rc = entry->policy->release(
			entry->policy_state,
			reservation,
			reason_code);
		if (rc != QHW_ADM_OK) {
			reservation->state = old_state;
			return rc;
		}
	} else {
		reservation->state = state;
	}

	return QHW_ADM_OK;
}

qhw_adm_rc_t qhw_adm_release(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	uint64_t reason_code)
{
	qhw_adm_rc_t rc;

	if (ctx == NULL || reservation_id == 0) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc == QHW_ADM_OK) {
		qhw_adm_clear_output(ctx);
		rc = transition_reservation(
			ctx,
			reservation_id,
			QHW_ADM_RESERVATION_RELEASED,
			reason_code);
		if (rc == QHW_ADM_OK) {
			qhw_adm_clear_error(ctx);
		} else {
			qhw_adm_set_error(ctx, "failed to release reservation");
		}
		qhw_adm_unlock(ctx);
	}
	return rc;
}

qhw_adm_rc_t qhw_adm_cancel(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	uint64_t reason_code)
{
	qhw_adm_rc_t rc;

	if (ctx == NULL || reservation_id == 0) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc == QHW_ADM_OK) {
		qhw_adm_clear_output(ctx);
		rc = transition_reservation(
			ctx,
			reservation_id,
			QHW_ADM_RESERVATION_CANCELLED,
			reason_code);
		if (rc == QHW_ADM_OK) {
			qhw_adm_clear_error(ctx);
		} else {
			qhw_adm_set_error(ctx, "failed to cancel reservation");
		}
		qhw_adm_unlock(ctx);
	}
	return rc;
}

qhw_adm_rc_t qhw_adm_renew(
	qhw_adm_t *ctx,
	uint64_t reservation_id,
	uint64_t now_ns,
	uint64_t ttl_ns)
{
	struct qhw_adm_reservation_entry *entry;
	qhw_adm_rc_t rc;

	if (ctx == NULL || reservation_id == 0 || ttl_ns == 0) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	qhw_adm_clear_output(ctx);

	entry = qhw_hash_table_find(&ctx->reservations, reservation_id);
	if (entry == NULL) {
		qhw_adm_set_error(ctx, "reservation was not found");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_NOT_FOUND;
	}
	if (entry->reservation.state != QHW_ADM_RESERVATION_ACTIVE) {
		qhw_adm_set_error(ctx, "reservation is terminal");
		qhw_adm_unlock(ctx);
		return QHW_ADM_ERR_STATE;
	}
	if (now_ns == 0) {
		now_ns = qhw_adm_now_ns();
	}
	rc = qhw_adm_add_u64(now_ns, ttl_ns, &entry->reservation.expires_at_ns);
	if (rc == QHW_ADM_OK) {
		qhw_adm_clear_error(ctx);
	} else {
		qhw_adm_set_error(ctx, "failed to renew reservation");
	}
	qhw_adm_unlock(ctx);
	return rc;
}

qhw_adm_rc_t qhw_adm_expire(
	qhw_adm_t *ctx,
	uint64_t now_ns,
	size_t *out_expired_count)
{
	size_t expired_count = 0;
	size_t i;
	qhw_adm_rc_t rc = QHW_ADM_OK;

	if (ctx == NULL || out_expired_count == NULL) {
		return QHW_ADM_ERR_INVAL;
	}

	rc = qhw_adm_lock(ctx);
	if (rc != QHW_ADM_OK) {
		return rc;
	}
	qhw_adm_clear_output(ctx);

	if (now_ns == 0) {
		now_ns = qhw_adm_now_ns();
	}

	for (i = 0; i < ctx->reservations.bucket_count; i++) {
		struct qhw_hash_entry *hash_entry;

		hash_entry = ctx->reservations.buckets[i];
		while (hash_entry != NULL) {
			struct qhw_adm_reservation_entry *entry;
			qhw_adm_reservation_t *reservation;

			entry = hash_entry->value;
			reservation = &entry->reservation;
			hash_entry = hash_entry->next;
			if (reservation->state != QHW_ADM_RESERVATION_ACTIVE ||
			    reservation->expires_at_ns == 0 ||
			    reservation->expires_at_ns > now_ns) {
				continue;
			}
			rc = transition_reservation(
				ctx,
				reservation->reservation_id,
				QHW_ADM_RESERVATION_EXPIRED,
				QHW_ADM_REASON_RESERVATION_TERMINAL);
			if (rc != QHW_ADM_OK) {
				qhw_adm_set_error(ctx, "failed to expire");
				qhw_adm_unlock(ctx);
				return rc;
			}
			expired_count++;
		}
	}

	*out_expired_count = expired_count;
	qhw_adm_clear_error(ctx);
	qhw_adm_unlock(ctx);
	return QHW_ADM_OK;
}

void qhw_adm_free_reservation_entry(void *value, void *user_data)
{
	struct qhw_adm_reservation_entry *entry = value;

	(void)user_data;

	if (entry == NULL) {
		return;
	}

	qhw_adm_free_metadata_count(
		entry->metadata,
		entry->reservation.metadata_count);
	free(entry);
}
