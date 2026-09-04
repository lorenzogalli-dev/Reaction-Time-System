import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart' show Ticker;

import '../models/accel_sample.dart';
import '../state/live_data_controller.dart';
import '../theme/app_theme.dart';

/// Colours used for the three axes, here and in the legend/stats cards.
const Map<AccelAxis, Color> accelAxisColors = {
  AccelAxis.x: AppColors.accent,
  AccelAxis.y: Color(0xFF4CC9FF),
  AccelAxis.z: Color(0xFFFF8A5C),
};

/// Scrolling strip chart of the live accelerometer stream.
///
/// Painted directly rather than through a charting package: at ~50 Hz a
/// widget-tree-based chart rebuilds far too often, whereas a [CustomPainter]
/// driven by the vsync ticker repaints once per frame and maps each sample to
/// x by its own timestamp - so the trace scrolls smoothly even when samples
/// arrive slightly early or late.
class LiveAccelChart extends StatefulWidget {
  final LiveDataController controller;

  const LiveAccelChart({super.key, required this.controller});

  @override
  State<LiveAccelChart> createState() => _LiveAccelChartState();
}

class _LiveAccelChartState extends State<LiveAccelChart> with SingleTickerProviderStateMixin {
  late final Ticker _ticker;
  final _ChartFrame _frame = _ChartFrame();

  @override
  void initState() {
    super.initState();
    _ticker = createTicker(_onTick)..start();
  }

  void _onTick(Duration _) {
    final controller = widget.controller;
    _frame.nowMicros = controller.elapsedMicros;

    // Ease the vertical scale toward what the current window needs so the
    // trace never clips, without the axis jumping on every spike.
    var peak = 0.0;
    for (final sample in controller.windowSamples) {
      peak = math.max(peak, sample.x.abs());
      peak = math.max(peak, sample.y.abs());
      peak = math.max(peak, sample.z.abs());
    }
    final target = math.max(_minScaleG, (peak * 1.25 / 0.5).ceilToDouble() * 0.5);
    _frame.scaleG += (target - _frame.scaleG) * 0.08;

    _frame.repaint();
  }

  static const double _minScaleG = 2.0;

  @override
  void dispose() {
    _ticker.dispose();
    _frame.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return RepaintBoundary(
      child: CustomPaint(
        painter: _AccelChartPainter(
          samples: widget.controller.windowSamples,
          windowMicros: LiveDataController.chartWindow.inMicroseconds,
          frame: _frame,
        ),
        size: Size.infinite,
      ),
    );
  }
}

/// Mutable per-frame state shared between the ticker and the painter, and the
/// [Listenable] that drives repaints.
class _ChartFrame extends ChangeNotifier {
  int nowMicros = 0;
  double scaleG = 2.0;

  void repaint() => notifyListeners();
}

class _AccelChartPainter extends CustomPainter {
  final List<AccelSample> samples;
  final int windowMicros;
  final _ChartFrame frame;

  _AccelChartPainter({
    required this.samples,
    required this.windowMicros,
    required this.frame,
  }) : super(repaint: frame);

  @override
  void paint(Canvas canvas, Size size) {
    final scale = frame.scaleG;
    final now = frame.nowMicros;
    final start = now - windowMicros;

    double yFor(double g) => size.height / 2 - (g / scale) * (size.height / 2);

    _paintGrid(canvas, size, scale, yFor);

    if (samples.length < 2) return;

    final paint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 2
      ..strokeCap = StrokeCap.round
      ..strokeJoin = StrokeJoin.round
      ..isAntiAlias = true;

    for (final axis in AccelAxis.values) {
      final path = Path();
      var started = false;
      for (final sample in samples) {
        final x = (sample.elapsedMicros - start) / windowMicros * size.width;
        final y = yFor(sample.axis(axis));
        if (started) {
          path.lineTo(x, y);
        } else {
          path.moveTo(x, y);
          started = true;
        }
      }
      canvas.drawPath(path, paint..color = accelAxisColors[axis]!);
    }
  }

  void _paintGrid(Canvas canvas, Size size, double scale, double Function(double) yFor) {
    final gridPaint = Paint()
      ..color = AppColors.divider
      ..strokeWidth = 1;
    final zeroPaint = Paint()
      ..color = AppColors.divider.withValues(alpha: 0.9)
      ..strokeWidth = 1.5;

    // A line every 1 g, labelled at the left edge.
    final step = scale <= 4 ? 1.0 : (scale / 4).ceilToDouble();
    for (var g = -scale; g <= scale + 0.001; g += step) {
      final y = yFor(g);
      if (y < 0 || y > size.height) continue;
      canvas.drawLine(Offset(0, y), Offset(size.width, y), g.abs() < 0.001 ? zeroPaint : gridPaint);
      if (g.abs() < 0.001) continue;
      _label(canvas, '${g > 0 ? '+' : ''}${g.toStringAsFixed(0)}g', Offset(4, y + 2));
    }
  }

  void _label(Canvas canvas, String text, Offset at) {
    final painter = TextPainter(
      text: TextSpan(
        text: text,
        style: const TextStyle(fontSize: 10, color: AppColors.textMuted),
      ),
      textDirection: TextDirection.ltr,
    )..layout();
    painter.paint(canvas, at);
  }

  @override
  bool shouldRepaint(covariant _AccelChartPainter oldDelegate) =>
      oldDelegate.samples != samples || oldDelegate.windowMicros != windowMicros;
}
