import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import '../state/ble_connection_controller.dart';
import '../theme/app_theme.dart';
import '../widgets/connect_device_card.dart';
import '../widgets/connection_sheet.dart';
import '../widgets/device_status_card.dart';

/// Settings tab. Device connection and management live here - the home screen
/// only reflects the resulting status.
class SettingsScreen extends StatelessWidget {
  const SettingsScreen({super.key});

  @override
  Widget build(BuildContext context) {
    final controller = context.watch<BleConnectionController>();
    final isConnected = controller.status == BleAppStatus.connected;

    return SafeArea(
      bottom: false,
      child: SingleChildScrollView(
        padding: const EdgeInsets.fromLTRB(AppSpacing.lg, AppSpacing.lg, AppSpacing.lg, 120),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Settings',
              style: TextStyle(fontSize: 22, fontWeight: FontWeight.w800, color: AppColors.textPrimary),
            ),
            const SizedBox(height: AppSpacing.xs),
            const Text(
              'Manage your Prostart device.',
              style: TextStyle(fontSize: 14, color: AppColors.textSecondary),
            ),
            const SizedBox(height: AppSpacing.xl),
            const Text(
              'DEVICE',
              style: TextStyle(
                fontSize: 11,
                fontWeight: FontWeight.w700,
                letterSpacing: 1.2,
                color: AppColors.textMuted,
              ),
            ),
            const SizedBox(height: AppSpacing.md),
            if (isConnected) ...[
              DeviceStatusCard(
                deviceName: controller.deviceName ?? 'Prostart device',
                lastGoTimestamp: controller.lastGoTimestamp,
              ),
              const SizedBox(height: AppSpacing.md),
              SizedBox(
                width: double.infinity,
                child: OutlinedButton.icon(
                  onPressed: controller.disconnect,
                  icon: const Icon(Icons.bluetooth_disabled_rounded, color: AppColors.error),
                  label: const Text('Disconnect'),
                  style: OutlinedButton.styleFrom(
                    foregroundColor: AppColors.error,
                    side: const BorderSide(color: AppColors.error),
                    padding: const EdgeInsets.symmetric(vertical: AppSpacing.md),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(AppRadius.pill)),
                  ),
                ),
              ),
            ] else
              ConnectDeviceCard(onConnectPressed: () => ConnectionSheet.show(context)),
          ],
        ),
      ),
    );
  }
}
