import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../models/accel_sample.dart';
import '../state/ble_connection_controller.dart';
import '../state/live_data_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/live_accel_chart.dart';
import '../widgets/recording_summary_card.dart';

/// Live accelerometer view, opened from the home screen while a device is
/// connected. Owns its own [LiveDataController] so BLE notifications are only
/// enabled for as long as this route is on screen.
class LiveDataScreen extends StatelessWidget {
  const LiveDataScreen({super.key});

  static Route<void> route() =>
      MaterialPageRoute(builder: (_) => const LiveDataScreen());

  @override
  Widget build(BuildContext context) {
    return ChangeNotifierProvider(
      create: (_) => LiveDataController(context.read<BleConnectionController>())..start(),
      child: const _LiveDataView(),
    );
  }
}

class _LiveDataView extends StatelessWidget {
  const _LiveDataView();

  @override
  Widget build(BuildContext context) {
    final controller = context.watch<LiveDataController>();
    final session = controller.lastSession;

    return Scaffold(
      backgroundColor: AppColors.background,
      appBar: AppBar(
        backgroundColor: AppColors.background,
        surfaceTintColor: Colors.transparent,
        title: const Text(
          'Live Data',
          style: TextStyle(fontSize: 18, fontWeight: FontWeight.w700, color: AppColors.textPrimary),
        ),
      ),
      body: SafeArea(
        top: false,
        child: switch (controller.status) {
          LiveStreamStatus.starting => const Center(
              child: CircularProgressIndicator(color: AppColors.accent),
            ),
          LiveStreamStatus.unavailable ||
          LiveStreamStatus.disconnected =>
            _StreamProblem(message: controller.errorMessage),
          LiveStreamStatus.streaming => ListView(
              padding: const EdgeInsets.fromLTRB(AppSpacing.lg, 0, AppSpacing.lg, AppSpacing.lg),
              children: [
                const _AxisLegend(),
                const SizedBox(height: AppSpacing.md),
                _ChartCard(controller: controller),
                const SizedBox(height: AppSpacing.md),
                _LiveReadout(controller: controller),
                const SizedBox(height: AppSpacing.lg),
                _RecordButton(controller: controller),
                if (session != null) ...[
                  const SizedBox(height: AppSpacing.lg),
                  Builder(
                    builder: (buttonContext) => RecordingSummaryCard(
                      session: session,
                      isExporting: controller.isExporting,
                      onDismiss: controller.dismissLastSession,
                      onExport: () => _export(buttonContext, controller),
                    ),
                  ),
                ],
              ],
            ),
        },
      ),
    );
  }

  /// iPad's share sheet is a popover and needs an anchor rect in screen
  /// coordinates; deriving it from the summary card keeps it near the button.
  Future<void> _export(BuildContext context, LiveDataController controller) async {
    final box = context.findRenderObject() as RenderBox?;
    final origin = box == null
        ? null
        : box.localToGlobal(Offset.zero) & box.size;
    final messenger = ScaffoldMessenger.of(context);
    final error = await controller.exportLastSession(shareOrigin: origin);
    if (error != null) {
      messenger.showSnackBar(SnackBar(content: Text(error)));
    }
  }
}

class _ChartCard extends StatelessWidget {
  final LiveDataController controller;
  const _ChartCard({required this.controller});

  @override
  Widget build(BuildContext context) {
    return Container(
      height: 260,
      padding: const EdgeInsets.symmetric(vertical: AppSpacing.sm),
      decoration: BoxDecoration(
        color: AppColors.surface,
        borderRadius: BorderRadius.circular(AppRadius.lg),
        border: Border.all(color: AppColors.divider),
      ),
      clipBehavior: Clip.antiAlias,
      child: LiveAccelChart(controller: controller),
    );
  }
}

class _AxisLegend extends StatelessWidget {
  const _AxisLegend();

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        for (final axis in AccelAxis.values) ...[
          Container(
            width: 8,
            height: 8,
            decoration: BoxDecoration(color: accelAxisColors[axis], shape: BoxShape.circle),
          ),
          const SizedBox(width: AppSpacing.sm),
          Text(
            axis.label,
            style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600, color: AppColors.textSecondary),
          ),
          const SizedBox(width: AppSpacing.md),
        ],
        const Spacer(),
        Text(
          'last ${LiveDataController.chartWindow.inSeconds}s',
          style: const TextStyle(fontSize: 12, color: AppColors.textMuted),
        ),
      ],
    );
  }
}

/// Current value per axis, rebuilt off the sample notifier so it doesn't drag
/// the rest of the screen into a 50 Hz rebuild.
class _LiveReadout extends StatelessWidget {
  final LiveDataController controller;
  const _LiveReadout({required this.controller});

  @override
  Widget build(BuildContext context) {
    return ValueListenableBuilder<AccelSample?>(
      valueListenable: controller.latest,
      builder: (context, sample, _) {
        return Row(
          children: [
            for (final axis in AccelAxis.values) ...[
              Expanded(
                child: Container(
                  padding: const EdgeInsets.symmetric(
                      vertical: AppSpacing.md, horizontal: AppSpacing.sm),
                  decoration: BoxDecoration(
                    color: AppColors.surface,
                    borderRadius: BorderRadius.circular(AppRadius.md),
                    border: Border.all(color: AppColors.divider),
                  ),
                  child: Column(
                    children: [
                      Text(
                        axis.label,
                        style: TextStyle(
                          fontSize: 12,
                          fontWeight: FontWeight.w700,
                          color: accelAxisColors[axis],
                        ),
                      ),
                      const SizedBox(height: AppSpacing.xs),
                      Text(
                        sample == null ? '—' : sample.axis(axis).toStringAsFixed(2),
                        style: const TextStyle(
                          fontSize: 20,
                          fontWeight: FontWeight.w800,
                          color: AppColors.textPrimary,
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              if (axis != AccelAxis.values.last) const SizedBox(width: AppSpacing.sm),
            ],
          ],
        );
      },
    );
  }
}

class _RecordButton extends StatelessWidget {
  final LiveDataController controller;
  const _RecordButton({required this.controller});

  @override
  Widget build(BuildContext context) {
    final recording = controller.isRecording;

    return Column(
      children: [
        SizedBox(
          width: double.infinity,
          child: ElevatedButton.icon(
            onPressed: controller.toggleRecording,
            icon: Icon(recording ? Icons.stop_rounded : Icons.fiber_manual_record_rounded),
            label: Text(recording ? 'Stop Recording' : 'Record'),
            style: ElevatedButton.styleFrom(
              backgroundColor: recording ? AppColors.error : AppColors.accent,
              foregroundColor: recording ? AppColors.textPrimary : AppColors.accentOn,
            ),
          ),
        ),
        if (recording) ...[
          const SizedBox(height: AppSpacing.sm),
          ValueListenableBuilder<Duration>(
            valueListenable: controller.recordingElapsed,
            builder: (context, elapsed, _) {
              final minutes = elapsed.inMinutes.toString().padLeft(2, '0');
              final seconds = (elapsed.inSeconds % 60).toString().padLeft(2, '0');
              return Text(
                'Recording… $minutes:$seconds',
                style: const TextStyle(fontSize: 13, color: AppColors.textSecondary),
              );
            },
          ),
        ],
      ],
    );
  }
}

class _StreamProblem extends StatelessWidget {
  final String? message;
  const _StreamProblem({required this.message});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: Padding(
        padding: const EdgeInsets.all(AppSpacing.xl),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            const Icon(Icons.sensors_off_rounded, size: 40, color: AppColors.textMuted),
            const SizedBox(height: AppSpacing.md),
            Text(
              message ?? 'The accelerometer stream is unavailable.',
              textAlign: TextAlign.center,
              style: const TextStyle(fontSize: 15, color: AppColors.textSecondary, height: 1.4),
            ),
            const SizedBox(height: AppSpacing.lg),
            OutlinedButton(
              onPressed: () => Navigator.of(context).pop(),
              style: OutlinedButton.styleFrom(
                foregroundColor: AppColors.textPrimary,
                side: const BorderSide(color: AppColors.divider),
                padding: const EdgeInsets.symmetric(
                    vertical: AppSpacing.md, horizontal: AppSpacing.xl),
                shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(AppRadius.pill)),
              ),
              child: const Text('Back'),
            ),
          ],
        ),
      ),
    );
  }
}
