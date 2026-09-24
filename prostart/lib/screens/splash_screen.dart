import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../theme/app_theme.dart';
import '../widgets/glass.dart';
import 'home_shell.dart';

/// Opening screen: the logo inside pulsing rings, the wordmark and a glass
/// "Get started" button.
class SplashScreen extends StatefulWidget {
  const SplashScreen({super.key});

  @override
  State<SplashScreen> createState() => _SplashScreenState();
}

class _SplashScreenState extends State<SplashScreen>
    with TickerProviderStateMixin {
  late final _pulse = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 2400),
  )..repeat();

  late final _intro = AnimationController(
    vsync: this,
    duration: const Duration(milliseconds: 900),
  )..forward();

  @override
  void dispose() {
    _pulse.dispose();
    _intro.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final fade = CurvedAnimation(parent: _intro, curve: Curves.easeOutCubic);
    return Scaffold(
      body: GlowBackground(
        child: SafeArea(
          child: Padding(
            padding: const EdgeInsets.symmetric(horizontal: AppSpacing.xxl),
            child: Column(
              children: [
                const Spacer(flex: 3),
                SizedBox(
                  width: 260,
                  height: 260,
                  child: AnimatedBuilder(
                    animation: _pulse,
                    builder: (context, child) => CustomPaint(
                      painter: _RingsPainter(_pulse.value),
                      child: child,
                    ),
                    child: Center(child: _logoMark()),
                  ),
                ),
                const SizedBox(height: AppSpacing.xl),
                FadeTransition(
                  opacity: fade,
                  child: SlideTransition(
                    position: Tween(
                      begin: const Offset(0, 0.2),
                      end: Offset.zero,
                    ).animate(fade),
                    child: Column(
                      children: [
                        const Text.rich(
                          TextSpan(
                            children: [
                              TextSpan(text: 'PRO'),
                              TextSpan(
                                text: 'START',
                                style: TextStyle(color: AppColors.accent),
                              ),
                            ],
                          ),
                          style: TextStyle(
                            fontSize: 44,
                            fontWeight: FontWeight.w900,
                            letterSpacing: 2,
                            fontStyle: FontStyle.italic,
                          ),
                        ),
                        const SizedBox(height: AppSpacing.sm),
                        const Text(
                          'Every millisecond counts.',
                          style: TextStyle(
                            fontSize: 16,
                            color: AppColors.textSecondary,
                          ),
                        ),
                        const SizedBox(height: AppSpacing.xl),
                        Row(
                          mainAxisAlignment: MainAxisAlignment.center,
                          children: [
                            _feature(Icons.bolt_rounded, 'Reaction time'),
                            const SizedBox(width: AppSpacing.md),
                            _feature(Icons.sports_score_rounded, 'Photofinish'),
                          ],
                        ),
                      ],
                    ),
                  ),
                ),
                const Spacer(flex: 4),
                FadeTransition(
                  opacity: fade,
                  child: GestureDetector(
                    onTap: () => Navigator.of(context).pushReplacement(
                      PageRouteBuilder(
                        transitionDuration: const Duration(milliseconds: 500),
                        pageBuilder: (_, _, _) => const HomeShell(),
                        transitionsBuilder: (_, a, _, child) =>
                            FadeTransition(opacity: a, child: child),
                      ),
                    ),
                    child: Container(
                      height: 60,
                      decoration: BoxDecoration(
                        color: AppColors.accent,
                        borderRadius: BorderRadius.circular(AppRadius.pill),
                        boxShadow: [
                          BoxShadow(
                            color: AppColors.accent.withValues(alpha: 0.45),
                            blurRadius: 30,
                            offset: const Offset(0, 8),
                          ),
                        ],
                      ),
                      child: const Row(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Text(
                            'Get started',
                            style: TextStyle(
                              fontSize: 17,
                              fontWeight: FontWeight.w800,
                              color: AppColors.onAccent,
                            ),
                          ),
                          SizedBox(width: AppSpacing.sm),
                          Icon(
                            Icons.arrow_forward_rounded,
                            color: AppColors.onAccent,
                          ),
                        ],
                      ),
                    ),
                  ),
                ),
                const SizedBox(height: AppSpacing.lg),
                const Text(
                  'Pair your ProStart device in seconds',
                  style: TextStyle(fontSize: 13, color: AppColors.textFaint),
                ),
                const SizedBox(height: AppSpacing.lg),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _logoMark() {
    return GlassPanel(
      borderRadius: AppRadius.pill,
      tint: AppColors.accent,
      strength: 1.4,
      child: SizedBox(
        width: 124,
        height: 124,
        child: Center(
          child: ShaderMask(
            shaderCallback: (r) => const LinearGradient(
              begin: Alignment.topLeft,
              end: Alignment.bottomRight,
              colors: [Color(0xFFEFFFC2), AppColors.accent],
            ).createShader(r),
            child: const Icon(
              Icons.bolt_rounded,
              size: 76,
              color: Colors.white,
            ),
          ),
        ),
      ),
    );
  }

  Widget _feature(IconData icon, String label) {
    return GlassPanel(
      borderRadius: AppRadius.pill,
      padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
      child: Row(
        children: [
          Icon(icon, size: 16, color: AppColors.accent),
          const SizedBox(width: 6),
          Text(
            label,
            style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w600),
          ),
        ],
      ),
    );
  }
}

/// Concentric rings expanding and fading out from the logo, like a sound
/// wave from the gun.
class _RingsPainter extends CustomPainter {
  final double t;
  _RingsPainter(this.t);

  @override
  void paint(Canvas canvas, Size size) {
    final c = size.center(Offset.zero);
    final maxR = size.width / 2;
    for (var i = 0; i < 3; i++) {
      final p = (t + i / 3) % 1;
      final r = 62 + (maxR - 62) * p;
      canvas.drawCircle(
        c,
        r,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.5
          ..color = AppColors.accent.withValues(alpha: 0.5 * (1 - p)),
      );
    }
    // A static tick ring, like a stopwatch bezel.
    final tick = Paint()
      ..color = Colors.white.withValues(alpha: 0.18)
      ..strokeWidth = 1.5;
    for (var i = 0; i < 60; i++) {
      final a = i * 2 * math.pi / 60;
      final len = i % 5 == 0 ? 8.0 : 4.0;
      final r1 = maxR - 2;
      canvas.drawLine(
        c + Offset(math.cos(a), math.sin(a)) * (r1 - len),
        c + Offset(math.cos(a), math.sin(a)) * r1,
        tick,
      );
    }
  }

  @override
  bool shouldRepaint(_RingsPainter old) => old.t != t;
}
