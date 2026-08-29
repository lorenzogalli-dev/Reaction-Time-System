import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../state/ble_connection_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/connection_badge.dart';
import '../widgets/home_connection_card.dart';
import 'live_data_screen.dart';

class HomeScreen extends StatelessWidget {
  /// Switches the shell to the Settings tab, where device pairing lives.
  final VoidCallback onGoToSettings;

  const HomeScreen({super.key, required this.onGoToSettings});

  @override
  Widget build(BuildContext context) {
    final controller = context.watch<BleConnectionController>();

    return SafeArea(
      bottom: false,
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(AppSpacing.lg, AppSpacing.lg, AppSpacing.lg, 120),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                const Text(
                  'Prostart',
                  style: TextStyle(fontSize: 22, fontWeight: FontWeight.w800, color: AppColors.textPrimary),
                ),
                const ConnectionBadge(),
              ],
            ),
            const SizedBox(height: AppSpacing.xs),
            const Text(
              'Reaction time, trained.',
              style: TextStyle(fontSize: 14, color: AppColors.textSecondary),
            ),
            const SizedBox(height: AppSpacing.xl),
            if (controller.status == BleAppStatus.connected)
              LiveDataCard(
                deviceName: controller.deviceName ?? 'Prostart device',
                onSeeLiveData: () => Navigator.of(context).push(LiveDataScreen.route()),
              )
            else
              ConnectPromptCard(onGoToSettings: onGoToSettings),
          ],
        ),
      ),
    );
  }
}
