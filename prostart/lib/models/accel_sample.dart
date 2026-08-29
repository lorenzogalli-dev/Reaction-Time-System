/// A single accelerometer reading from the live BLE stream.
///
/// [elapsedMicros] is measured by the app from the moment the live stream was
/// opened, not by the device - the firmware notification carries only the
/// three axis values. That's accurate enough for visualization; the
/// reaction-time path uses the device's own microsecond timestamp instead.
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
