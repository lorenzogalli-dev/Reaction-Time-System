import 'dart:ui';

import 'package:flutter/material.dart';

import '../theme/app_theme.dart';

/// Floating frosted-glass bottom navigation bar — the same bar as KinePose:
/// a glass pill with four evenly shared slots, the selected one on a filled
/// accent disc with a black glyph, plus a solid accent circle beside the pill
/// that is not a tab but a shortcut (here: start a new session).
class AppBottomNavBar extends StatelessWidget {
  /// Unselected glyphs: a fixed mid-grey so "off" reads as hollow against
  /// the glass, not just dimmer.
  static const _kInactive = Color(0xFFB0B0B0);

  static const _pillHeight = 72.0;
  static const _slotSize = 52.0;
  static const _actionSize = 72.0;
  static const _maxWidth = 420.0;

  final int activeIndex;
  final ValueChanged<int> onTap;
  final VoidCallback onTapAction;

  const AppBottomNavBar({
    super.key,
    required this.activeIndex,
    required this.onTap,
    required this.onTapAction,
  });

  @override
  Widget build(BuildContext context) {
    final bottomInset = MediaQuery.of(context).viewPadding.bottom;
    final bottomGap = bottomInset > 0
        ? (bottomInset - 6).clamp(18.0, 30.0)
        : 20.0;

    return Padding(
      padding: EdgeInsets.only(
        left: 32,
        right: 32,
        top: AppSpacing.sm,
        bottom: bottomGap,
      ),
      child: Align(
        alignment: Alignment.bottomCenter,
        heightFactor: 1,
        child: ConstrainedBox(
          constraints: const BoxConstraints(maxWidth: _maxWidth),
          child: Row(
            children: [
              Expanded(child: _pill()),
              const SizedBox(width: 16),
              _actionButton(),
            ],
          ),
        ),
      ),
    );
  }

  Widget _pill() {
    final radius = BorderRadius.circular(AppRadius.pill);
    return ClipRRect(
      borderRadius: radius,
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: AppGlass.blur, sigmaY: AppGlass.blur),
        child: Container(
          height: _pillHeight,
          padding: const EdgeInsets.symmetric(horizontal: AppSpacing.sm),
          decoration: BoxDecoration(
            color: const Color(0x661C1E21),
            borderRadius: radius,
            border: Border.all(color: const Color(0x33FFFFFF), width: 1),
          ),
          child: Row(
            children: [
              _slot(Icons.home_outlined, Icons.home_rounded, 0),
              _slot(Icons.history_rounded, Icons.history_rounded, 1),
              _slot(Icons.insights_outlined, Icons.insights_rounded, 2),
              _slot(Icons.person_outline_rounded, Icons.person_rounded, 3),
            ],
          ),
        ),
      ),
    );
  }

  Widget _slot(IconData icon, IconData activeIcon, int index) {
    final active = activeIndex == index;
    return Expanded(
      child: GestureDetector(
        onTap: () => onTap(index),
        behavior: HitTestBehavior.opaque,
        child: Center(
          child: AnimatedContainer(
            duration: AppMotion.normal,
            curve: Curves.easeOut,
            width: _slotSize,
            height: _slotSize,
            decoration: BoxDecoration(
              // Fade to a transparent *accent*, not transparent black, so the
              // disc never flashes grey mid-animation.
              color: active
                  ? AppColors.accent
                  : AppColors.accent.withValues(alpha: 0),
              shape: BoxShape.circle,
            ),
            child: Icon(
              active ? activeIcon : icon,
              size: 28,
              color: active ? Colors.black : _kInactive,
            ),
          ),
        ),
      ),
    );
  }

  Widget _actionButton() {
    return GestureDetector(
      onTap: onTapAction,
      child: Container(
        width: _actionSize,
        height: _actionSize,
        decoration: const BoxDecoration(
          color: AppColors.accent,
          shape: BoxShape.circle,
        ),
        child: const Icon(Icons.bolt_rounded, size: 32, color: Colors.black),
      ),
    );
  }
}
