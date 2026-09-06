/// A single accelerometer reading from the live BLE stream.
///
/// [elapsedMicros] comes from the *device*: the firmware stamps every sample
/// with its own `micros()` at capture time, and this is that value re-based to
/// the first sample of the stream. It is not measured by the app, and BLE
/// delivery latency does not enter it.
///
/// This matters because BLE hands samples over in bursts - the 2026-08-31
/// capture showed groups of 1-4 arriving together with 29-31 ms gaps between
/// groups - so an arrival-time stamp carries +/-15-30 ms of error. See
/// playground_IMU/README.md.
class AccelSample {
  final int elapsedMicros;
  final double x;
  final double y;
  final double z;

  const AccelSample({
    required this.elapsedMicros,
    required this.x,
    required this.y,
    required this.z,
  });

  double get elapsedSeconds => elapsedMicros / 1e6;

  double axis(AccelAxis a) => switch (a) {
        AccelAxis.x => x,
        AccelAxis.y => y,
        AccelAxis.z => z,
      };
}

enum AccelAxis {
  x('X'),
  y('Y'),
  z('Z');

  final String label;
  const AccelAxis(this.label);
}
