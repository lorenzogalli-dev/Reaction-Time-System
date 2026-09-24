import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';
import 'package:provider/provider.dart';

import '../services/ble_service.dart';
import '../state/ble_connection_controller.dart';
import '../theme/app_theme.dart';

enum _SheetView { choices, wifiComingSoon, ble }

/// Bottom sheet offering the two Prostart device connection paths.
/// Wi-Fi is a "coming soon" placeholder; Bluetooth drives the app-wide
/// [BleConnectionController] so the connect attempt (and its result)
/// survives even if this sheet is dismissed mid-flight.
class ConnectionSheet extends StatefulWidget {
  const ConnectionSheet({super.key});

  static Future<void> show(BuildContext context) {
    return showModalBottomSheet(
      context: context,
      isScrollControlled: true,
      backgroundColor: Colors.transparent,
      builder: (_) => const ConnectionSheet(),
    );
  }

  @override
  State<ConnectionSheet> createState() => _ConnectionSheetState();
}

class _ConnectionSheetState extends State<ConnectionSheet> {
  late _SheetView _view;

  @override
  void initState() {
    super.initState();
    // Resume showing the in-flight/last BLE state if a connect attempt is
    // already underway (or just finished) from a previous time this sheet
    // was open, rather than resetting back to the choice screen.
    final status = context.read<BleConnectionController>().status;
    _view = status == BleAppStatus.disconnected ? _SheetView.choices : _SheetView.ble;
  }

  void _startBleFlow() {
    setState(() => _view = _SheetView.ble);
    context.read<BleConnectionController>().connect();
  }

  @override
  Widget build(BuildContext context) {
    final controller = context.watch<BleConnectionController>();

    return SafeArea(
      child: Padding(
        padding: const EdgeInsets.fromLTRB(AppSpacing.lg, AppSpacing.sm, AppSpacing.lg, AppSpacing.lg),
        child: Container(
          decoration: BoxDecoration(
            color: AppColors.surface,
            borderRadius: BorderRadius.circular(AppRadius.xl),
            border: Border.all(color: AppColors.divider),
          ),
          padding: const EdgeInsets.all(AppSpacing.lg),
          child: AnimatedSize(
            duration: const Duration(milliseconds: 220),
            curve: Curves.easeOut,
            child: AnimatedSwitcher(
              duration: const Duration(milliseconds: 200),
              child: _buildForView(controller),
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildForView(BleConnectionController controller) {
    switch (_view) {
      case _SheetView.choices:
        return _ChoicesView(
          key: const ValueKey('choices'),
          onWifiTap: () => setState(() => _view = _SheetView.wifiComingSoon),
          onBluetoothTap: _startBleFlow,
        );
      case _SheetView.wifiComingSoon:
        return _ComingSoonView(
          key: const ValueKey('wifi'),
          onBack: () => setState(() => _view = _SheetView.choices),
        );
      case _SheetView.ble:
        switch (controller.status) {
          case BleAppStatus.connected:
            return _BleConnectedView(
              key: const ValueKey('connected'),
              deviceName: controller.deviceName ?? 'Prostart device',
              onDone: () => Navigator.of(context).pop(),
            );
          case BleAppStatus.error:
            return _BleFailedView(
              key: const ValueKey('failed'),
              reason: controller.failureReason,
              message: controller.errorMessage,
              onRetry: _startBleFlow,
              onBack: () => setState(() => _view = _SheetView.choices),
            );
          case BleAppStatus.scanning:
          case BleAppStatus.connecting:
          case BleAppStatus.disconnected:
            return _BleScanningView(key: const ValueKey('scanning'), stage: controller.stage);
        }
    }
  }
}

class _SheetHeader extends StatelessWidget {
  final String title;
  const _SheetHeader({required this.title});

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Center(
          child: Container(
            width: 40,
            height: 4,
            decoration: BoxDecoration(
              color: AppColors.divider,
              borderRadius: BorderRadius.circular(AppRadius.pill),
            ),
          ),
        ),
        const SizedBox(height: AppSpacing.lg),
        Text(
          title,
          style: const TextStyle(fontSize: 18, fontWeight: FontWeight.w700, color: AppColors.textPrimary),
        ),
        const SizedBox(height: AppSpacing.md),
      ],
    );
  }
}

class _ChoicesView extends StatelessWidget {
  final VoidCallback onWifiTap;
  final VoidCallback onBluetoothTap;

  const _ChoicesView({super.key, required this.onWifiTap, required this.onBluetoothTap});

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const _SheetHeader(title: 'Choose a connection method'),
        _ConnectionOptionTile(
          icon: Icons.wifi_rounded,
          title: 'Wi-Fi',
          subtitle: 'Connect over your local network',
          onTap: onWifiTap,
        ),
        const SizedBox(height: AppSpacing.sm),
        _ConnectionOptionTile(
          icon: Icons.bluetooth_rounded,
          title: 'Bluetooth',
          subtitle: 'Scan for a nearby Prostart device',
          onTap: onBluetoothTap,
        ),
      ],
    );
  }
}

class _ConnectionOptionTile extends StatelessWidget {
  final IconData icon;
  final String title;
  final String subtitle;
  final VoidCallback onTap;

  const _ConnectionOptionTile({
    required this.icon,
    required this.title,
    required this.subtitle,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return Material(
      color: AppColors.surfaceElevated,
      borderRadius: BorderRadius.circular(AppRadius.md),
      child: InkWell(
        borderRadius: BorderRadius.circular(AppRadius.md),
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.all(AppSpacing.md),
          child: Row(
            children: [
              Container(
                width: 44,
                height: 44,
                alignment: Alignment.center,
                decoration: BoxDecoration(
                  color: AppColors.accent.withValues(alpha: 0.12),
                  borderRadius: BorderRadius.circular(AppRadius.sm),
                ),
                child: Icon(icon, color: AppColors.accent),
              ),
              const SizedBox(width: AppSpacing.md),
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(title,
                        style: const TextStyle(
                            fontSize: 16, fontWeight: FontWeight.w600, color: AppColors.textPrimary)),
                    const SizedBox(height: 2),
                    Text(subtitle, style: const TextStyle(fontSize: 13, color: AppColors.textSecondary)),
                  ],
                ),
              ),
              const Icon(Icons.chevron_right_rounded, color: AppColors.textMuted),
            ],
          ),
        ),
      ),
    );
  }
}

class _ComingSoonView extends StatelessWidget {
  final VoidCallback onBack;
  const _ComingSoonView({super.key, required this.onBack});

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const _SheetHeader(title: 'Wi-Fi connection'),
        Padding(
          padding: const EdgeInsets.symmetric(vertical: AppSpacing.lg),
          child: Column(
            children: [
              Icon(Icons.wifi_tethering_rounded, color: AppColors.textMuted, size: 48),
              const SizedBox(height: AppSpacing.md),
              const Text(
                'Coming soon',
                style: TextStyle(fontSize: 16, fontWeight: FontWeight.w700, color: AppColors.textPrimary),
              ),
              const SizedBox(height: AppSpacing.xs),
              const Text(
                'Wi-Fi setup for your Prostart device is on the way.',
                textAlign: TextAlign.center,
                style: TextStyle(fontSize: 14, color: AppColors.textSecondary),
              ),
            ],
          ),
        ),
        SizedBox(
          width: double.infinity,
          child: OutlinedButton(
            onPressed: onBack,
            style: OutlinedButton.styleFrom(
              foregroundColor: AppColors.textPrimary,
              side: const BorderSide(color: AppColors.divider),
              padding: const EdgeInsets.symmetric(vertical: AppSpacing.md),
              shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(AppRadius.pill)),
            ),
            child: const Text('Back'),
          ),
        ),
      ],
    );
  }
}

class _BleScanningView extends StatefulWidget {
  final BleConnectStage? stage;
  const _BleScanningView({super.key, required this.stage});

  @override
  State<_BleScanningView> createState() => _BleScanningViewState();
}

class _BleScanningViewState extends State<_BleScanningView> with SingleTickerProviderStateMixin {
  late final AnimationController _pulseController;

  @override
  void initState() {
    super.initState();
    _pulseController = AnimationController(vsync: this, duration: const Duration(milliseconds: 900))
      ..repeat(reverse: true);
  }

  @override
  void dispose() {
    _pulseController.dispose();
    super.dispose();
  }

  String get _label {
    switch (widget.stage) {
      case BleConnectStage.requestingPermission:
        return 'Requesting Bluetooth permission…';
      case BleConnectStage.scanning:
        return 'Scanning for Prostart device…';
      case BleConnectStage.connecting:
        return 'Connecting…';
      case BleConnectStage.verifyingServices:
        return 'Verifying device…';
      case null:
        return 'Starting…';
    }
  }

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        const SizedBox(height: AppSpacing.sm),
        SizedBox(
          height: 96,
          width: 96,
          child: Stack(
            alignment: Alignment.center,
            children: [
              AnimatedBuilder(
                animation: _pulseController,
                builder: (context, child) {
                  final scale = 1 + (_pulseController.value * 0.35);
                  final opacity = 1 - _pulseController.value;
                  return Opacity(
                    opacity: opacity.clamp(0.0, 1.0),
                    child: Transform.scale(
                      scale: scale,
                      child: Container(
                        decoration: const BoxDecoration(
                          color: AppColors.accent,
                          shape: BoxShape.circle,
                        ),
                      ),
                    ),
                  );
                },
              ),
              Container(
                width: 64,
                height: 64,
                alignment: Alignment.center,
                decoration: const BoxDecoration(
                  color: AppColors.accent,
                  shape: BoxShape.circle,
                ),
                child: const Icon(Icons.bluetooth_searching_rounded, color: AppColors.accentOn, size: 30),
              ),
            ],
          ),
        ),
        const SizedBox(height: AppSpacing.lg),
        Text(
          _label,
          textAlign: TextAlign.center,
          style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: AppColors.textPrimary),
        ),
        const SizedBox(height: AppSpacing.xs),
        const Text(
          'Keep the device powered on and nearby',
          style: TextStyle(fontSize: 13, color: AppColors.textSecondary),
        ),
        const SizedBox(height: AppSpacing.lg),
      ],
    );
  }
}

class _BleConnectedView extends StatelessWidget {
  final String deviceName;
  final VoidCallback onDone;

  const _BleConnectedView({super.key, required this.deviceName, required this.onDone});

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        const SizedBox(height: AppSpacing.sm),
        Container(
          width: 72,
          height: 72,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: AppColors.success.withValues(alpha: 0.12),
            shape: BoxShape.circle,
          ),
          child: const Icon(Icons.check_circle_rounded, color: AppColors.success, size: 40),
        ),
        const SizedBox(height: AppSpacing.lg),
        Text(
          'Connected to $deviceName',
          textAlign: TextAlign.center,
          style: const TextStyle(fontSize: 16, fontWeight: FontWeight.w700, color: AppColors.textPrimary),
        ),
        const SizedBox(height: AppSpacing.lg),
        SizedBox(
          width: double.infinity,
          child: ElevatedButton(onPressed: onDone, child: const Text('Done')),
        ),
      ],
    );
  }
}

class _BleFailedView extends StatelessWidget {
  final BleFailureReason? reason;
  final String? message;
  final VoidCallback onRetry;
  final VoidCallback onBack;

  const _BleFailedView({
    super.key,
    required this.reason,
    required this.message,
    required this.onRetry,
    required this.onBack,
  });

  @override
  Widget build(BuildContext context) {
    final displayMessage = message ?? 'Something went wrong — please try again';
    final isPermissionDenied = reason == BleFailureReason.permissionDenied;

    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        const SizedBox(height: AppSpacing.sm),
        Container(
          width: 72,
          height: 72,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: AppColors.error.withValues(alpha: 0.12),
            shape: BoxShape.circle,
          ),
          child: const Icon(Icons.bluetooth_disabled_rounded, color: AppColors.error, size: 36),
        ),
        const SizedBox(height: AppSpacing.lg),
        Text(
          displayMessage,
          textAlign: TextAlign.center,
          style: const TextStyle(fontSize: 15, fontWeight: FontWeight.w600, color: AppColors.textPrimary, height: 1.4),
        ),
        const SizedBox(height: AppSpacing.lg),
        SizedBox(
          width: double.infinity,
          child: ElevatedButton(
            onPressed: isPermissionDenied ? openAppSettings : onRetry,
            child: Text(isPermissionDenied ? 'Open Settings' : 'Try Again'),
          ),
        ),
        const SizedBox(height: AppSpacing.sm),
        SizedBox(
          width: double.infinity,
          child: TextButton(
            onPressed: onBack,
            style: TextButton.styleFrom(foregroundColor: AppColors.textSecondary),
            child: const Text('Back'),
          ),
        ),
      ],
    );
  }
}
