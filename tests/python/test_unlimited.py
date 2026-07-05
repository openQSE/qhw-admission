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
    RESERVATION_CANCELLED,
    RESERVATION_EXPIRED,
    RESERVATION_RELEASED,
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


def make_request(task, request_id=42, ttl_ns=1_000_000):
    return AdmissionRequest(
        request_id=request_id,
        device_id=7,
        user_id=1000,
        job_id=2000,
        scope_id=3,
        workload_kind=WORKLOAD_HYBRID_JOB,
        walltime_ns=2_000_000_000,
        ttl_ns=ttl_ns,
        task_class=task,
    )


class UnlimitedPolicyTests(unittest.TestCase):
    def setup_context(self):
        ctx = AdmissionContext()
        ctx.register_device(make_profile())
        ctx.add_policy_path(os.environ["QHW_ADM_TEST_UNLIMITED_DIR"])
        ctx.set_policy(7, "unlimited")
        return ctx

    def test_evaluate_and_reserve(self):
        task = make_task()
        request = make_request(task)

        with self.setup_context() as ctx:
            decision = ctx.evaluate(request)
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            self.assertEqual(decision.estimated_total_ns, 683160)
            self.assertEqual(decision.credits_required, 0)
            self.assertEqual(decision.rate_required, 0)
            self.assertEqual(decision.capacity_granted, 3)
            self.assertEqual(decision.compliance_action, COMPLIANCE_ALLOW)
            self.assertEqual(decision.message, "accepted by unlimited policy")
            self.assertEqual(decision.metadata, ())

            decision = ctx.reserve(request)
            self.assertEqual(decision.decision, DECISION_ACCEPTED)
            self.assertNotEqual(decision.reservation_id, 0)

            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_ACTIVE)
            self.assertEqual(reservation.credits_reserved, 0)
            self.assertEqual(reservation.rate_reserved, 0)

            capacity = ctx.get_capacity(7, 3)
            self.assertEqual(capacity.active_reservation_count, 1)
            self.assertEqual(capacity.credits_reserved, 0)
            self.assertEqual(capacity.rate_reserved, 0)

            ctx.release(decision.reservation_id)
            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_RELEASED)

    def test_cancel_and_expire(self):
        task = make_task()

        with self.setup_context() as ctx:
            request = make_request(task)
            decision = ctx.reserve(request)
            ctx.cancel(decision.reservation_id)
            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_CANCELLED)

            request = make_request(task, request_id=43)
            decision = ctx.reserve(request)
            ctx.renew(decision.reservation_id, 1000, 1000)
            self.assertEqual(ctx.expire(1999), 0)
            self.assertEqual(ctx.expire(2000), 1)
            reservation = ctx.get_reservation(decision.reservation_id)
            self.assertEqual(reservation.state, RESERVATION_EXPIRED)

    def test_invalid_request(self):
        task = make_task()
        request = make_request(task)
        request._native.task_class_count = 0

        with self.setup_context() as ctx:
            with self.assertRaises(AdmissionError):
                ctx.evaluate(request)
            with self.assertRaises(AdmissionError):
                ctx.reserve(request)


if __name__ == "__main__":
    unittest.main()
