import 'accel_sample.dart';

/// Min/max/mean of one axis over a finished recording.
class AxisStats {
  final AccelAxis axis;
  final double min;
  final double max;
  final double average;

  const AxisStats({
    required this.axis,
    required this.min,
    required this.max,
    required this.average,
  });

  factory AxisStats.fromSamples(AccelAxis axis, List<AccelSample> samples) {
    if (samples.isEmpty) {
      return AxisStats(axis: axis, min: 0, max: 0, average: 0);
    }
    var min = double.infinity;
    var max = double.negativeInfinity;
    var sum = 0.0;
    for (final sample in samples) {
      final v = sample.axis(axis);
      if (v < min) min = v;
      if (v > max) max = v;
      sum += v;
    }
    return AxisStats(axis: axis, min: min, max: max, average: sum / samples.length);
  }
}

/// An in-memory recording of the live accelerometer stream, captured between
/// two taps of the Record button.
class RecordingSession {
  /// Wall-clock time the recording started, used for the CSV's absolute
  /// timestamp column and the export file name.
  final DateTime startedAt;
  final List<AccelSample> samples;

  RecordingSession({required this.startedAt, required this.samples});

  bool get isEmpty => samples.isEmpty;

  /// Span between the first and last captured sample.
  Duration get duration => samples.length < 2
      ? Duration.zero
      : Duration(microseconds: samples.last.elapsedMicros - samples.first.elapsedMicros);

  /// Actual achieved rate, which can drift from the firmware's nominal 50 Hz.
  double get sampleRateHz {
    final seconds = duration.inMicroseconds / 1e6;
    if (seconds <= 0) return 0;
    return (samples.length - 1) / seconds;
  }

  AxisStats statsFor(AccelAxis axis) => AxisStats.fromSamples(axis, samples);

  String get fileName {
    final t = startedAt;
    String two(int v) => v.toString().padLeft(2, '0');
    return 'prostart_imu_${t.year}${two(t.month)}${two(t.day)}_'
        '${two(t.hour)}${two(t.minute)}${two(t.second)}.csv';
  }

  /// One row per sample: an absolute ISO-8601 timestamp, the elapsed time
  /// relative to the first sample, and the three axes in g.
  String toCsv() {
    final firstMicros = samples.isEmpty ? 0 : samples.first.elapsedMicros;
    final buffer = StringBuffer('timestamp_iso,elapsed_s,x_g,y_g,z_g\n');
    for (final sample in samples) {
      final offset = sample.elapsedMicros - firstMicros;
      final absolute = startedAt.add(Duration(microseconds: offset));
      buffer
        ..write(absolute.toIso8601String())
        ..write(',')
        ..write((offset / 1e6).toStringAsFixed(6))
        ..write(',')
        ..write(sample.x.toStringAsFixed(6))
        ..write(',')
        ..write(sample.y.toStringAsFixed(6))
        ..write(',')
        ..write(sample.z.toStringAsFixed(6))
        ..write('\n');
    }
    return buffer.toString();
  }
}
