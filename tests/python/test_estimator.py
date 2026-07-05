import unittest

from qhw_admission import (
    AdmissionContext,
    Baseline,
    CONFIG_MERGE,
    CONFIG_REPLACE,
    DeviceProfile,
    QtaskClass,
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
        total_credits=100_000,
        concurrent_jobs=4,
        default_ttl_ns=60_000_000_000,
    )


CONFIG_YAML = """
devices:
  - device_id: 8
    max_qubits: 20
    max_shots: 10000
    time_span_ns: 1000000000
    baseline:
      qubit_count: 4
      depth: 10
      one_q_gate_count: 10
      two_q_gate_count: 5
      measurement_count: 2
      shots: 100
    timing:
      one_q_gate_ns: 20
      two_q_gate_ns: 100
      measurement_ns: 1000
      one_q_gate_transfer_ns: 1
      two_q_gate_transfer_ns: 4
      measurement_transfer_ns: 10
      compile_ns: 1000
      control_overhead_ns: 200
      provider_overhead_ns: 300
    estimator:
      name: baseline
"""


MULTI_DEVICE_CONFIG_YAML = """
devices:
  - device_id: 8
    max_qubits: 20
    max_shots: 10000
    baseline:
      qubit_count: 4
      depth: 10
      one_q_gate_count: 10
      two_q_gate_count: 5
      measurement_count: 2
      shots: 100
    timing:
      one_q_gate_ns: 20
      two_q_gate_ns: 100
      measurement_ns: 1000
    estimator:
      name: baseline
  - device_id: 9
    max_qubits: 20
    max_shots: 10000
    baseline:
      qubit_count: 4
      depth: 10
      one_q_gate_count: 10
      two_q_gate_count: 5
      measurement_count: 2
      shots: 100
    timing:
      one_q_gate_ns: 30
      two_q_gate_ns: 100
      measurement_ns: 1000
    estimator:
      name: baseline
"""


class EstimatorTests(unittest.TestCase):
    def test_baseline_estimate(self):
        profile = make_profile()
        with AdmissionContext() as ctx:
            ctx.register_device(profile)
            estimate = ctx.estimate_baseline(profile._native.device_id)
            self.assertEqual(estimate.execution_ns, 270000)
            self.assertEqual(estimate.measurement_ns, 200000)
            self.assertEqual(estimate.total_ns, 271550)
            self.assertEqual(estimate.baseline_units, 1)

    def test_qtask_estimate(self):
        profile = make_profile()
        task = QtaskClass(
            class_id=11,
            count=2,
            qubit_count=4,
            depth=12,
            one_q_gate_count=20,
            two_q_gate_count=10,
            shots=100,
            measurement_count=2,
        )
        with AdmissionContext() as ctx:
            ctx.register_device(profile)
            ctx.set_estimator(profile._native.device_id)
            estimate = ctx.estimate_qtask_class(
                profile._native.device_id,
                task,
            )
            self.assertEqual(estimate.execution_ns, 680000)
            self.assertEqual(estimate.measurement_ns, 400000)
            self.assertEqual(estimate.total_ns, 683160)
            self.assertEqual(estimate.baseline_units, 3)

    def test_config_string(self):
        with AdmissionContext() as ctx:
            ctx.load_config_string(CONFIG_YAML, CONFIG_MERGE)
            estimate = ctx.estimate_baseline(8)
            self.assertEqual(estimate.total_ns, 271550)
            self.assertEqual(estimate.baseline_units, 1)

    def test_multi_device_config_string(self):
        with AdmissionContext() as ctx:
            ctx.load_config_string(MULTI_DEVICE_CONFIG_YAML, CONFIG_REPLACE)
            estimate = ctx.estimate_baseline(9)
            self.assertEqual(estimate.total_ns, 280000)
            self.assertEqual(estimate.baseline_units, 1)


if __name__ == "__main__":
    unittest.main()
