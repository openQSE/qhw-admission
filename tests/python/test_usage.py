import os
import unittest

from qhw_admission import (
    ActualUsage,
    AdmissionContext,
    AdmissionError,
    AdmissionRequest,
    Baseline,
    COMPLIANCE_REJECT,
    DECISION_ACCEPTED,
    DECISION_REJECTED,
    DeviceProfile,
    QtaskClass,
    Usage,
    WORKLOAD_HYBRID_JOB,
)


def make_profile():
    baseline = Baseline(
        qubit_count=4,
        depth=10,
        one_q_gate_count=10,
        two_q_gate_count=5,
        shots=100,
        measurement_count=2,
    )
    return DeviceProfile(
        device_id=7,
        time_span_ns=1_000_000_000,
        baseline=baseline,
        max_qubits=20,
        max_shots=10_000,
        one_q_gate_ns=20,
        two_q_gate_ns=100,
        measurement_ns=1000,
        one_q_gate_transfer_ns=1,
        two_q_gate_transfer_ns=4,
        measurement_transfer_ns=10,
        compile_ns=1000,
        control_overhead_ns=200,
        provider_overhead_ns=300,
        total_credits=5,
        default_ttl_ns=60_000_000_000,
    )


def make_task():
    return QtaskClass(
        class_id=11,
        count=2,
        qubit_count=4,
        depth=12,
        one_q_gate_count=20,
        two_q_gate_count=10,
        shots=100,
        measurement_count=2,
    )


def make_request(task):
    return AdmissionRequest(
        request_id=42,
        device_id=7,
        user_id=1000,
        job_id=2000,
        scope_id=3,
        workload_kind=WORKLOAD_HYBRID_JOB,
        walltime_ns=2_000_000_000,
        ttl_ns=1_000_000,
        task_class=task,
    )


class UsageTests(unittest.TestCase):
    def setup_context(self):
        ctx = AdmissionContext()
        ctx.register_device(make_profile())
        ctx.add_policy_path(os.environ["QHW_ADM_TEST_CREDIT_DIR"])
        ctx.set_policy(7, "credit")
        return ctx

    def test_authorize_consume_return_and_actual(self):
        with self.setup_context() as ctx:
            decision = ctx.reserve(make_request(make_task()))
            reservation_id = decision.reservation_id
            usage = Usage(
                reservation_id=reservation_id,
                task_id=101,
                class_id=11,
                estimated_ns=100,
                baseline_units=1,
                credits=1,
            )

            authorized = ctx.authorize_usage(reservation_id, usage)
            self.assertEqual(authorized.decision, DECISION_ACCEPTED)
            state = ctx.get_usage(reservation_id)
            self.assertEqual(state.credits_consumed, 0)

            consumed = ctx.consume(reservation_id, usage)
            self.assertEqual(consumed.decision, DECISION_ACCEPTED)
            state = ctx.get_usage(reservation_id)
            self.assertEqual(state.credits_consumed, 1)
            self.assertEqual(state.remaining_credits, 2)

            repeated = ctx.consume(reservation_id, usage)
            self.assertEqual(repeated.decision, DECISION_ACCEPTED)
            state = ctx.get_usage(reservation_id)
            self.assertEqual(state.credits_consumed, 1)

            actual = ActualUsage(
                reservation_id=reservation_id,
                task_id=101,
                observed_device_ns=100,
                observed_compile_ns=20,
                observed_transfer_ns=5,
                observed_control_overhead_ns=5,
            )
            ctx.record_actual(reservation_id, actual)
            ctx.record_actual(reservation_id, actual)
            state = ctx.get_usage(reservation_id)
            self.assertEqual(state.actual_total_ns, 130)

            ctx.return_usage(reservation_id, usage)
            state = ctx.get_usage(reservation_id)
            self.assertEqual(state.credits_consumed, 0)
            self.assertEqual(state.remaining_credits, 3)

    def test_overlimit_compliance(self):
        with self.setup_context() as ctx:
            decision = ctx.reserve(make_request(make_task()))
            reservation_id = decision.reservation_id
            usage = Usage(
                reservation_id=reservation_id,
                task_id=102,
                class_id=11,
                baseline_units=4,
                credits=4,
            )

            rejected = ctx.consume(reservation_id, usage)
            self.assertEqual(rejected.decision, DECISION_REJECTED)
            rejected = ctx.consume(reservation_id, usage)
            self.assertEqual(rejected.decision, DECISION_REJECTED)
            compliance = ctx.get_compliance(reservation_id)
            self.assertEqual(compliance.overuse_count, 1)
            self.assertEqual(compliance.action, COMPLIANCE_REJECT)

    def test_return_rejects_unconsumed_task(self):
        with self.setup_context() as ctx:
            decision = ctx.reserve(make_request(make_task()))
            usage = Usage(
                reservation_id=decision.reservation_id,
                task_id=103,
                class_id=11,
                baseline_units=1,
                credits=1,
            )

            with self.assertRaises(AdmissionError):
                ctx.return_usage(decision.reservation_id, usage)


if __name__ == "__main__":
    unittest.main()
