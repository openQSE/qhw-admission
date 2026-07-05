import os
import unittest

from qhw_admission import (
    AdmissionContext,
    AdmissionError,
    AdmissionRequest,
    Baseline,
    COMPLIANCE_ALLOW,
    DECISION_ACCEPTED,
    DeviceProfile,
    QtaskClass,
    RESERVATION_ACTIVE,
    RESERVATION_EXPIRED,
    RESERVATION_RELEASED,
    VALUE_STRING,
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
        total_credits=100,
        device_rate=50,
        concurrent_jobs=4,
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


def make_request(task, ttl_ns=1_000_000):
    return AdmissionRequest(
        request_id=42,
        device_id=7,
        user_id=1000,
        job_id=2000,
        scope_id=3,
        workload_kind=WORKLOAD_HYBRID_JOB,
        walltime_ns=2_000_000_000,
        ttl_ns=ttl_ns,
        task_class=task,
    )


class PolicyTests(unittest.TestCase):
    def setup_context(self):
        ctx = AdmissionContext()
        ctx.register_device(make_profile())
        ctx.add_policy_path(os.environ["QHW_ADM_TEST_POLICY_DIR"])
        ctx.set_policy(7, "mock")
        return ctx

    def test_evaluate_and_reserve(self):
        task = make_task()
        request = make_request(task)

        with self.setup_context() as ctx:
            decision = ctx.evaluate(request)
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            self.assertEqual(decision.estimated_total_ns, 683160)
            self.assertEqual(decision.capacity_granted, 3)
            self.assertEqual(decision.reservation_id, 0)
            self.assertEqual(decision.compliance_action, COMPLIANCE_ALLOW)
            self.assertEqual(decision.retry_after_ns, 0)
            self.assertEqual(decision.message, "mock accepted")
            self.assertEqual(
                decision.metadata,
                ((1001, VALUE_STRING, "decision-metadata"),),
            )

            decision = ctx.reserve(request)
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            self.assertNotEqual(decision.reservation_id, 0)
            self.assertEqual(decision.message, "mock accepted")

            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_ACTIVE)
            self.assertEqual(reservation.credits_reserved, 3)
            self.assertEqual(reservation.credits_consumed, 0)
            self.assertEqual(reservation.rate_reserved, 3)
            self.assertEqual(reservation.rate_consumed, 0)
            self.assertEqual(reservation.actual_total_ns, 0)
            self.assertEqual(reservation.unused_capacity, 0)
            self.assertEqual(reservation.overuse_count, 0)
            self.assertEqual(reservation.underuse_score, 0)
            self.assertGreater(reservation.device_profile_version, 0)
            self.assertGreater(reservation.policy_version, 0)
            self.assertGreater(reservation.estimator_version, 0)
            self.assertEqual(
                reservation.metadata,
                ((1002, VALUE_STRING, "grant-metadata"),),
            )

            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.credits_reserved, 3)
            self.assertEqual(capacity.credits_consumed, 0)
            self.assertEqual(capacity.credits_returned, 0)
            self.assertEqual(capacity.scoped_reserved_credits, 3)
            self.assertEqual(capacity.scheduler_policy_id, 1234)
            self.assertEqual(capacity.confidence_ppm, 1_000_000)
            self.assertEqual(
                capacity.metadata,
                ((1003, VALUE_STRING, "capacity-metadata"),),
            )

            with self.assertRaises(AdmissionError):
                ctx.set_policy(7, "mock")

            ctx.release(decision.reservation_id)
            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_RELEASED)

    def test_renew_and_expire(self):
        task = make_task()
        request = make_request(task)

        with self.setup_context() as ctx:
            decision = ctx.reserve(request)
            ctx.renew(decision.reservation_id, 1000, 1000)
            self.assertEqual(ctx.expire(1999), 0)
            self.assertEqual(ctx.expire(2000), 1)
            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_EXPIRED)


if __name__ == "__main__":
    unittest.main()
