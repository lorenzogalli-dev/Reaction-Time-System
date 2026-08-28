import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../state/ble_connection_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/connect_device_card.dart';
import '../widgets/connection_badge.dart';
import '../widgets/connection_sheet.dart';
import '../widgets/device_status_card.dart';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key});

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
              DeviceStatusCard(
                deviceName: controller.deviceName ?? 'Prostart device',
                lastGoTimestamp: controller.lastGoTimestamp,
              )
            else
              ConnectDeviceCard(onConnectPressed: () => ConnectionSheet.show(context)),
          ],
        ),
      ),
    );
  }
}
