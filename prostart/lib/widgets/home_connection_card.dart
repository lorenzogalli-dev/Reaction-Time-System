import 'package:flutter/material.dart';

import '../theme/app_theme.dart';

/// Home-screen prompt shown while no device is connected. Connecting itself
/// now lives in the Settings tab, so this only points the user there.
class ConnectPromptCard extends StatelessWidget {
  final VoidCallback onGoToSettings;

  const ConnectPromptCard({super.key, required this.onGoToSettings});

  @override
  Widget build(BuildContext context) {
    return _HomeCard(
      icon: Icons.bluetooth_disabled_rounded,
      iconColor: AppColors.textMuted,
      title: 'No device connected',
      subtitle: 'Head to Settings to pair your Prostart device.',
      actionLabel: 'Go to Settings',
      actionIcon: Icons.settings_rounded,
      onAction: onGoToSettings,
    );
  }
}

/// Home-screen entry point into the live accelerometer view.
class LiveDataCard extends StatelessWidget {
  final String deviceName;
  final VoidCallback onSeeLiveData;

  const LiveDataCard({super.key, required this.deviceName, required this.onSeeLiveData});

  @override
  Widget build(BuildContext context) {
    return _HomeCard(
      icon: Icons.bluetooth_connected_rounded,
      iconColor: AppColors.success,
      title: deviceName,
      subtitle: 'Connected — stream accelerometer data in real time.',
      actionLabel: 'See Live Data',
      actionIcon: Icons.show_chart_rounded,
      onAction: onSeeLiveData,
    );
  }
}

class _HomeCard extends StatelessWidget {
  final IconData icon;
  final Color iconColor;
  final String title;
  final String subtitle;
  final String actionLabel;
  final IconData actionIcon;
  final VoidCallback onAction;

  const _HomeCard({
    required this.icon,
    required this.iconColor,
    required this.title,
    required this.subtitle,
    required this.actionLabel,
    required this.actionIcon,
    required this.onAction,
  });

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
          Row(
            children: [
              Container(
                width: 48,
                height: 48,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: iconColor.withValues(alpha: 0.12),
                  borderRadius: BorderRadius.circular(AppRadius.sm),
                ),
                child: Icon(icon, color: iconColor),
              ),
              const SizedBox(width: AppSpacing.md),
              Expanded(
                child: Text(
                  title,
                  style: const TextStyle(
                    fontSize: 18,
                    fontWeight: FontWeight.w700,
                    color: AppColors.textPrimary,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: AppSpacing.md),
          Text(
            subtitle,
            style: const TextStyle(fontSize: 14, color: AppColors.textSecondary, height: 1.4),
          ),
          const SizedBox(height: AppSpacing.lg),
          SizedBox(
            width: double.infinity,
            child: ElevatedButton.icon(
              onPressed: onAction,
              icon: Icon(actionIcon),
              label: Text(actionLabel),
            ),
          ),
        ],
      ),
    );
  }
}
