import 'package:flutter/material.dart';

import '../theme/app_theme.dart';
import 'glass.dart';

/// A plot image (matplotlib, photofinish frame, ...) on a glass card, with a
/// label chip. Tapping it opens it full screen, pinch-zoomable.
class ImageCard extends StatelessWidget {
  final String asset;
  final String label;
  final IconData icon;
  final String? badge;
  final Color color;
  final double aspectRatio;

  const ImageCard({
    super.key,
    required this.asset,
    required this.label,
    required this.icon,
    this.badge,
    this.color = AppColors.accent,
    this.aspectRatio = 1510 / 1237,
  });

  @override
  Widget build(BuildContext context) {
    return GlassPanel(
      padding: const EdgeInsets.all(AppSpacing.sm),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Padding(
            padding: const EdgeInsets.fromLTRB(8, 6, 8, 10),
            child: Row(
              children: [
                Icon(icon, size: 16, color: color),
                const SizedBox(width: 6),
                Text(
                  label,
                  style: const TextStyle(
                    fontSize: 13,
                    fontWeight: FontWeight.w700,
                    letterSpacing: 0.2,
                  ),
                ),
                const Spacer(),
                if (badge != null)
                  Text(
                    badge!,
                    style: TextStyle(
                      fontSize: 12,
                      fontWeight: FontWeight.w700,
                      color: color,
                      fontFeatures: tabular,
                    ),
                  ),
                const SizedBox(width: 8),
                const Icon(
                  Icons.open_in_full_rounded,
                  size: 14,
                  color: AppColors.textFaint,
                ),
              ],
            ),
          ),
          GestureDetector(
            onTap: () => Navigator.of(context).push(
              PageRouteBuilder(
                opaque: false,
                barrierColor: Colors.black87,
                pageBuilder: (_, _, _) => _FullScreenImage(asset: asset),
              ),
            ),
            child: ClipRRect(
              borderRadius: BorderRadius.circular(AppRadius.md),
              child: ColoredBox(
                color: Colors.white,
                child: AspectRatio(
                  aspectRatio: aspectRatio,
                  child: Image.asset(asset, fit: BoxFit.contain),
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _FullScreenImage extends StatelessWidget {
  final String asset;
  const _FullScreenImage({required this.asset});

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: () => Navigator.of(context).pop(),
      child: Scaffold(
        backgroundColor: Colors.transparent,
        body: Center(
          child: InteractiveViewer(maxScale: 5, child: Image.asset(asset)),
        ),
      ),
    );
  }
}

/// Small uppercase label above a value.
class Eyebrow extends StatelessWidget {
  final String text;
  final Color color;
  const Eyebrow(this.text, {super.key, this.color = AppColors.textFaint});

  @override
  Widget build(BuildContext context) {
    return Text(
      text.toUpperCase(),
      style: TextStyle(
        fontSize: 11,
        fontWeight: FontWeight.w700,
        letterSpacing: 1.4,
        color: color,
      ),
    );
  }
}

/// Coloured pill with an icon: "Legal start", "60 m", ...
class Tag extends StatelessWidget {
  final String text;
  final IconData? icon;
  final Color color;

  const Tag(this.text, {super.key, this.icon, this.color = AppColors.success});

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
      decoration: BoxDecoration(
        color: color.withValues(alpha: 0.14),
        borderRadius: BorderRadius.circular(AppRadius.pill),
        border: Border.all(color: color.withValues(alpha: 0.35)),
      ),
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          if (icon != null) ...[
            Icon(icon, size: 14, color: color),
            const SizedBox(width: 4),
          ],
          Text(
            text,
            style: TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w700,
              color: color,
              fontFeatures: tabular,
            ),
          ),
        ],
      ),
    );
  }
}

/// Big number + unit, with a soft glow in the value's colour.
class HeroNumber extends StatelessWidget {
  final String value;
  final String unit;
  final Color color;
  final double size;

  const HeroNumber({
    super.key,
    required this.value,
    required this.unit,
    this.color = AppColors.accent,
    this.size = 64,
  });

  @override
  Widget build(BuildContext context) {
    return Text.rich(
      TextSpan(
        children: [
          TextSpan(
            text: value,
            style: TextStyle(
              fontSize: size,
              fontWeight: FontWeight.w800,
              letterSpacing: -size * 0.04,
              height: 1,
              color: color,
              shadows: [
                Shadow(color: color.withValues(alpha: 0.55), blurRadius: 28),
              ],
            ),
          ),
          if (unit.isNotEmpty)
            TextSpan(
              text: ' $unit',
              style: TextStyle(
                fontSize: size * 0.34,
                fontWeight: FontWeight.w600,
                color: AppColors.textSecondary,
              ),
            ),
        ],
      ),
      style: const TextStyle(fontFeatures: tabular),
    );
  }
}

/// One insight: icon disc, headline, detail.
class InsightRow extends StatelessWidget {
  final IconData icon;
  final Color color;
  final String title;
  final String detail;

  const InsightRow({
    super.key,
    required this.icon,
    required this.color,
    required this.title,
    required this.detail,
  });

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: AppSpacing.sm),
      child: Row(
        children: [
          Container(
            width: 40,
            height: 40,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: color.withValues(alpha: 0.16),
            ),
            child: Icon(icon, size: 20, color: color),
          ),
          const SizedBox(width: AppSpacing.md),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  title,
                  style: const TextStyle(
                    fontSize: 15,
                    fontWeight: FontWeight.w700,
                    fontFeatures: tabular,
                  ),
                ),
                const SizedBox(height: 1),
                Text(
                  detail,
                  style: const TextStyle(
                    fontSize: 12.5,
                    color: AppColors.textSecondary,
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }
}

/// Glass card holding a titled list of [InsightRow]s.
class InsightsCard extends StatelessWidget {
  final List<InsightRow> insights;
  const InsightsCard({super.key, required this.insights});

  @override
  Widget build(BuildContext context) {
    return GlassPanel(
      padding: const EdgeInsets.fromLTRB(
        AppSpacing.xl,
        AppSpacing.lg,
        AppSpacing.xl,
        AppSpacing.md,
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Row(
            children: [
              Icon(
                Icons.auto_awesome_rounded,
                size: 16,
                color: AppColors.accent,
              ),
              SizedBox(width: 6),
              Eyebrow('Insights', color: AppColors.textSecondary),
            ],
          ),
          const SizedBox(height: AppSpacing.sm),
          ...insights,
        ],
      ),
    );
  }
}

/// Recent reaction times as bars, the latest highlighted, with the average
/// as a dashed line.
class RecentStartsChart extends StatelessWidget {
  final List<double> values;
  final double average;

  const RecentStartsChart({
    super.key,
    required this.values,
    required this.average,
  });

  @override
  Widget build(BuildContext context) {
    return GlassPanel(
      padding: const EdgeInsets.all(AppSpacing.xl),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Eyebrow('Last 8 starts', color: AppColors.textSecondary),
              const Spacer(),
              Container(width: 14, height: 2, color: AppColors.textFaint),
              const SizedBox(width: 6),
              Text(
                'avg ${average.toStringAsFixed(3)} s',
                style: const TextStyle(
                  fontSize: 12,
                  color: AppColors.textFaint,
                  fontFeatures: tabular,
                ),
              ),
            ],
          ),
          const SizedBox(height: AppSpacing.lg),
          SizedBox(
            height: 120,
            child: CustomPaint(
              size: Size.infinite,
              painter: _BarsPainter(values, average),
            ),
          ),
        ],
      ),
    );
  }
}

class _BarsPainter extends CustomPainter {
  final List<double> values;
  final double average;
  _BarsPainter(this.values, this.average);

  static const _floor = 0.100;
  static const _ceil = 0.180;
  static const _labelSpace = 18.0;

  @override
  void paint(Canvas canvas, Size size) {
    final chartH = size.height - _labelSpace;
    double yOf(double v) =>
        chartH * (1 - ((v - _floor) / (_ceil - _floor)).clamp(0.0, 1.0));

    final slot = size.width / values.length;
    final barW = slot * 0.46;
    for (var i = 0; i < values.length; i++) {
      final latest = i == values.length - 1;
      final x = slot * i + (slot - barW) / 2;
      final rect = RRect.fromRectAndRadius(
        Rect.fromLTRB(x, yOf(values[i]), x + barW, chartH),
        const Radius.circular(6),
      );
      canvas.drawRRect(
        rect,
        Paint()
          ..color = latest
              ? AppColors.accent
              : Colors.white.withValues(alpha: 0.16),
      );
      if (latest) {
        canvas.drawRRect(
          rect,
          Paint()
            ..color = AppColors.accent.withValues(alpha: 0.5)
            ..maskFilter = const MaskFilter.blur(BlurStyle.outer, 10),
        );
      }
      final tp = TextPainter(
        text: TextSpan(
          text: (values[i] * 1000).round().toString(),
          style: TextStyle(
            fontSize: 10.5,
            fontWeight: latest ? FontWeight.w800 : FontWeight.w500,
            color: latest ? AppColors.accent : AppColors.textFaint,
            fontFeatures: tabular,
          ),
        ),
        textDirection: TextDirection.ltr,
      )..layout();
      tp.paint(canvas, Offset(x + barW / 2 - tp.width / 2, chartH + 5));
    }

    // Dashed average line.
    final y = yOf(average);
    final dash = Paint()
      ..color = Colors.white.withValues(alpha: 0.45)
      ..strokeWidth = 1.2;
    for (double x = 0; x < size.width; x += 8) {
      canvas.drawLine(Offset(x, y), Offset(x + 4, y), dash);
    }
  }

  @override
  bool shouldRepaint(_BarsPainter old) => false;
}
