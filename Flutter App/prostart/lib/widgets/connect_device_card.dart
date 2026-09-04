import 'package:flutter/material.dart';

import '../theme/app_theme.dart';

/// The primary MVP card on the home screen inviting the user to connect
/// their Prostart hardware.
class ConnectDeviceCard extends StatelessWidget {
  final VoidCallback onConnectPressed;

  const ConnectDeviceCard({super.key, required this.onConnectPressed});

  @override
  Widget build(BuildContext context) {
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
          Container(
            width: 48,
            height: 48,
            alignment: Alignment.center,
            decoration: BoxDecoration(
              color: AppColors.accent.withValues(alpha: 0.12),
              borderRadius: BorderRadius.circular(AppRadius.sm),
            ),
            child: const Icon(Icons.sensors_rounded, color: AppColors.accent),
          ),
          const SizedBox(height: AppSpacing.md),
          const Text(
            'Connect your Prostart device',
            style: TextStyle(
              fontSize: 20,
              fontWeight: FontWeight.w700,
              color: AppColors.textPrimary,
            ),
          ),
          const SizedBox(height: AppSpacing.xs),
          const Text(
            'Pair via Wi-Fi or Bluetooth to start tracking your reaction time.',
            style: TextStyle(fontSize: 14, color: AppColors.textSecondary, height: 1.4),
          ),
          const SizedBox(height: AppSpacing.lg),
          SizedBox(
            width: double.infinity,
            child: ElevatedButton.icon(
              onPressed: onConnectPressed,
              icon: const Icon(Icons.bolt_rounded),
              label: const Text('Connect Device'),
            ),
          ),
        ],
      ),
    );
  }
}
