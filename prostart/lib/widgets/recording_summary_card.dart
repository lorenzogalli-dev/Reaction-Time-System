import 'package:flutter/material.dart';

import '../models/accel_sample.dart';
import '../models/recording_session.dart';
import '../services/ble_service.dart' show prostartAccelerometerSampleRateHz;
import '../theme/app_theme.dart';
import 'live_accel_chart.dart' show accelAxisColors;

/// Post-recording summary: per-axis min/max/average plus the CSV export.
class RecordingSummaryCard extends StatelessWidget {
  final RecordingSession session;
  final bool isExporting;
  final VoidCallback onExport;
  final VoidCallback onDismiss;

  const RecordingSummaryCard({
    super.key,
    required this.session,
    required this.isExporting,
    required this.onExport,
    required this.onDismiss,
  });

  @override
  Widget build(BuildContext context) {
    final seconds = session.duration.inMilliseconds / 1000;

    return Container(
      width: double.infinity,
      padding: const EdgeInsets.all(AppSpacing.lg),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(AppRadius.lg),
        border: Border.all(color: AppColors.divider),
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              const Expanded(
                child: Text(
                  'Recording complete',
                  style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700, color: AppColors.textPrimary),
                ),
              ),
              IconButton(
                onPressed: onDismiss,
                icon: const Icon(Icons.close_rounded, color: AppColors.textMuted),
                tooltip: 'Dismiss',
              ),
            ],
          ),
          Text(
            session.isEmpty
                ? 'No samples were captured.'
                : '${session.samples.length} samples · ${seconds.toStringAsFixed(1)} s · '
                    '${session.sampleRateHz.toStringAsFixed(1)} Hz '
                    '(nominal $prostartAccelerometerSampleRateHz Hz)',
            style: const TextStyle(fontSize: 13, color: AppColors.textSecondary),
          ),
          if (!session.isEmpty) ...[
            const SizedBox(height: AppSpacing.lg),
            const Row(
              children: [
                SizedBox(width: 44),
                Expanded(child: _StatHeader('Min')),
                Expanded(child: _StatHeader('Avg')),
                Expanded(child: _StatHeader('Max')),
              ],
            ),
            const SizedBox(height: AppSpacing.sm),
            for (final axis in AccelAxis.values) ...[
              _AxisStatsRow(stats: session.statsFor(axis)),
              if (axis != AccelAxis.values.last) const SizedBox(height: AppSpacing.sm),
            ],
            const SizedBox(height: AppSpacing.lg),
            SizedBox(
              width: double.infinity,
              child: ElevatedButton.icon(
                onPressed: isExporting ? null : onExport,
                icon: isExporting
                    ? const SizedBox(
                        width: 18,
                        height: 18,
                        child: CircularProgressIndicator(strokeWidth: 2, color: AppColors.accentOn),
                      )
                    : const Icon(Icons.ios_share_rounded),
                label: Text(isExporting ? 'Preparing…' : 'Export CSV'),
              ),
            ),
          ],
        ],
      ),
    );
  }
}

class _StatHeader extends StatelessWidget {
  final String label;
  const _StatHeader(this.label);

  @override
  Widget build(BuildContext context) {
    return Text(
      label,
      textAlign: TextAlign.right,
      style: const TextStyle(fontSize: 11, fontWeight: FontWeight.w600, color: AppColors.textMuted),
    );
  }
}

class _AxisStatsRow extends StatelessWidget {
  final AxisStats stats;
  const _AxisStatsRow({required this.stats});

  @override
  Widget build(BuildContext context) {
    final color = accelAxisColors[stats.axis]!;
    return Row(
      children: [
        SizedBox(
          width: 44,
          child: Row(
            children: [
              Container(
                width: 8,
                height: 8,
                decoration: BoxDecoration(color: color, shape: BoxShape.circle),
              ),
              const SizedBox(width: AppSpacing.sm),
              Text(
                stats.axis.label,
                style: const TextStyle(
                    fontSize: 14, fontWeight: FontWeight.w700, color: AppColors.textPrimary),
              ),
            ],
          ),
        ),
        Expanded(child: _StatValue(stats.min)),
        Expanded(child: _StatValue(stats.average)),
        Expanded(child: _StatValue(stats.max)),
      ],
    );
  }
}

class _StatValue extends StatelessWidget {
  final double value;
  const _StatValue(this.value);

  @override
  Widget build(BuildContext context) {
    return Text(
      '${value.toStringAsFixed(2)} g',
      textAlign: TextAlign.right,
      style: const TextStyle(
        fontSize: 14,
        fontWeight: FontWeight.w600,
        color: AppColors.textPrimary,
        fontFeatures: [FontFeature.tabularFigures()],
      ),
    );
  }
}
