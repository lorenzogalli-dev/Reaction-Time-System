import 'dart:ui';

import 'package:flutter/material.dart';

import '../theme/app_theme.dart';

/// "Liquid glass": blurs whatever sits behind it, washes it with a faint
/// white gradient (brighter at the top-left, where the light comes from) and
/// traces the edge with a specular rim that fades around the shape.
class GlassPanel extends StatelessWidget {
  final double borderRadius;
  final EdgeInsetsGeometry? padding;
  final Widget child;

  /// Optional hue mixed into the wash, so a card can carry its mode colour.
  final Color? tint;
  final double blur;
  final double strength;

  const GlassPanel({
    super.key,
    required this.child,
    this.borderRadius = AppRadius.lg,
    this.padding,
    this.tint,
    this.blur = AppGlass.blur,
    this.strength = 1,
  });

  @override
  Widget build(BuildContext context) {
    final radius = BorderRadius.circular(borderRadius);
    final hue = tint ?? Colors.white;

    return ClipRRect(
      borderRadius: radius,
      child: BackdropFilter(
        filter: ImageFilter.blur(sigmaX: blur, sigmaY: blur),
        child: CustomPaint(
          foregroundPainter: _RimPainter(borderRadius),
          child: Container(
            padding: padding,
            decoration: BoxDecoration(
              borderRadius: radius,
              gradient: LinearGradient(
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
                colors: [
                  hue.withValues(alpha: 0.16 * strength),
                  Colors.white.withValues(alpha: 0.05 * strength),
                  hue.withValues(alpha: 0.08 * strength),
                ],
                stops: const [0, 0.55, 1],
              ),
            ),
            child: child,
          ),
        ),
      ),
    );
  }
}

class _RimPainter extends CustomPainter {
  final double radius;
  _RimPainter(this.radius);

  @override
  void paint(Canvas canvas, Size size) {
    final rect = Offset.zero & size;
    final rrect = RRect.fromRectAndRadius(
      rect.deflate(0.6),
      Radius.circular(radius),
    );
    final paint = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.2
      ..shader = LinearGradient(
        begin: Alignment.topLeft,
        end: Alignment.bottomRight,
        colors: [
          Colors.white.withValues(alpha: 0.55),
          Colors.white.withValues(alpha: 0.08),
          Colors.white.withValues(alpha: 0.04),
          Colors.white.withValues(alpha: 0.30),
        ],
        stops: const [0, 0.35, 0.65, 1],
      ).createShader(rect);
    canvas.drawRRect(rrect, paint);
  }

  @override
  bool shouldRepaint(_RimPainter old) => old.radius != radius;
}

/// Page backdrop: near-black with a few large, soft colour glows. The glass
/// needs something behind it to bend, otherwise it just reads as grey.
class GlowBackground extends StatelessWidget {
  final Widget child;
  final Color primaryGlow;
  final Color secondaryGlow;

  const GlowBackground({
    super.key,
    required this.child,
    this.primaryGlow = AppColors.accent,
    this.secondaryGlow = AppColors.cyan,
  });

  @override
  Widget build(BuildContext context) {
    final size = MediaQuery.of(context).size;
    return Stack(
      children: [
        const Positioned.fill(child: ColoredBox(color: AppColors.background)),
        _glow(
          color: primaryGlow.withValues(alpha: 0.30),
          diameter: size.width * 1.1,
          left: size.width * 0.35,
          top: -size.width * 0.45,
        ),
        _glow(
          color: secondaryGlow.withValues(alpha: 0.22),
          diameter: size.width * 1.2,
          left: -size.width * 0.55,
          top: size.height * 0.30,
        ),
        _glow(
          color: const Color(0xFF7B5CFF).withValues(alpha: 0.18),
          diameter: size.width,
          left: size.width * 0.45,
          top: size.height * 0.68,
        ),
        Positioned.fill(child: child),
      ],
    );
  }

  Widget _glow({
    required Color color,
    required double diameter,
    required double left,
    required double top,
  }) {
    return Positioned(
      left: left,
      top: top,
      child: IgnorePointer(
        child: Container(
          width: diameter,
          height: diameter,
          decoration: BoxDecoration(
            shape: BoxShape.circle,
            gradient: RadialGradient(
              colors: [color, color.withValues(alpha: 0)],
            ),
          ),
        ),
      ),
    );
  }
}

/// Circular glass button with a single glyph (back, share, ...).
class GlassIconButton extends StatelessWidget {
  final IconData icon;
  final VoidCallback? onTap;
  final double size;

  const GlassIconButton({
    super.key,
    required this.icon,
    this.onTap,
    this.size = 44,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: GlassPanel(
        borderRadius: AppRadius.pill,
        child: SizedBox(
          width: size,
          height: size,
          child: Icon(icon, size: 18, color: AppColors.textPrimary),
        ),
      ),
    );
  }
}

/// Back button + title + optional trailing action: the header of every
/// pushed screen, in place of an app bar.
class GlassHeader extends StatelessWidget {
  final String title;
  final String? subtitle;
  final IconData? trailing;

  const GlassHeader({
    super.key,
    required this.title,
    this.subtitle,
    this.trailing,
  });

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        GlassIconButton(
          icon: Icons.arrow_back_ios_new_rounded,
          onTap: () => Navigator.of(context).maybePop(),
        ),
        const SizedBox(width: AppSpacing.lg),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                title,
                style: const TextStyle(
                  fontSize: 22,
                  fontWeight: FontWeight.w800,
                  letterSpacing: -0.3,
                ),
              ),
              if (subtitle != null)
                Text(
                  subtitle!,
                  style: const TextStyle(
                    fontSize: 13,
                    color: AppColors.textFaint,
                  ),
                ),
            ],
          ),
        ),
        if (trailing != null) GlassIconButton(icon: trailing!, onTap: () {}),
      ],
    );
  }
}
