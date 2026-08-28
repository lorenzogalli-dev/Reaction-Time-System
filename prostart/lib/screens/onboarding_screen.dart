import 'package:flutter/material.dart';

import '../theme/app_theme.dart';
import 'home_shell.dart';

/// First-launch screen: a starting gun "fires" with a flash, then a runner
/// sprints across in a continuous loop — exiting off the right edge and
/// re-entering from the left — while the app name and CTA fade in.
/// Built entirely with AnimationController/AnimatedBuilder — no external
/// animation assets.
class OnboardingScreen extends StatefulWidget {
  const OnboardingScreen({super.key});

  @override
  State<OnboardingScreen> createState() => _OnboardingScreenState();
}

class _OnboardingScreenState extends State<OnboardingScreen> with TickerProviderStateMixin {
  static const double _runnerIconSize = 64;

  late final AnimationController _gunController;
  late final AnimationController _runnerLoopController;
  late final AnimationController _contentController;

  @override
  void initState() {
    super.initState();
    _gunController = AnimationController(vsync: this, duration: const Duration(milliseconds: 750));
    _runnerLoopController = AnimationController(vsync: this, duration: const Duration(milliseconds: 1400));
    _contentController = AnimationController(vsync: this, duration: const Duration(milliseconds: 500));

    _gunController.addStatusListener((status) {
      if (status == AnimationStatus.completed) {
        _runnerLoopController.repeat();
        _contentController.forward();
      }
    });

    _gunController.forward();
  }

  @override
  void dispose() {
    _gunController.dispose();
    _runnerLoopController.dispose();
    _contentController.dispose();
    super.dispose();
  }

  static double _progress(double t, double start, double end, [Curve curve = Curves.linear]) {
    if (end <= start) return t >= start ? 1 : 0;
    if (t <= start) return 0;
    if (t >= end) return 1;
    return curve.transform((t - start) / (end - start));
  }

  @override
  Widget build(BuildContext context) {
    final screenWidth = MediaQuery.of(context).size.width;
    // Half the lane width the runner travels, measured from center, plus
    // its own size so it is fully hidden beyond each edge before looping.
    final laneHalfSpan = screenWidth / 2 + _runnerIconSize;

    return Scaffold(
      backgroundColor: AppColors.background,
      body: SafeArea(
        child: Column(
          children: [
            const Spacer(flex: 3),
            SizedBox(
              height: 160,
              width: double.infinity,
              child: Stack(
                alignment: Alignment.center,
                clipBehavior: Clip.hardEdge,
                children: [
                  AnimatedBuilder(
                    animation: _gunController,
                    builder: (context, child) {
                      final t = _gunController.value;

                      final gunScale = 1.0 + 0.4 * _progress(t, 0.0, 0.3, Curves.easeOut);
                      final gunOpacity = 1.0 - _progress(t, 0.55, 1.0, Curves.easeIn);

                      final flashRise = _progress(t, 0.2, 0.4, Curves.easeOut);
                      final flashFall = _progress(t, 0.4, 0.75, Curves.easeIn);
                      final flashOpacity = (flashRise - flashFall).clamp(0.0, 1.0);
                      final flashScale = 0.5 + 1.8 * _progress(t, 0.2, 0.75, Curves.easeOut);

                      return Stack(
                        alignment: Alignment.center,
                        children: [
                          Opacity(
                            opacity: flashOpacity,
                            child: Transform.scale(
                              scale: flashScale,
                              child: Container(
                                width: 70,
                                height: 70,
                                decoration: BoxDecoration(
                                  shape: BoxShape.circle,
                                  color: AppColors.accent.withValues(alpha: 0.5),
                                  boxShadow: [
                                    BoxShadow(
                                      color: AppColors.accent.withValues(alpha: 0.6),
                                      blurRadius: 40,
                                      spreadRadius: 10,
                                    ),
                                  ],
                                ),
                              ),
                            ),
                          ),
                          Opacity(
                            opacity: gunOpacity.clamp(0.0, 1.0),
                            child: Transform.scale(
                              scale: gunScale,
                              child: const Icon(
                                Icons.sports_score_rounded,
                                size: 56,
                                color: AppColors.textPrimary,
                              ),
                            ),
                          ),
                        ],
                      );
                    },
                  ),
                  AnimatedBuilder(
                    animation: _runnerLoopController,
                    builder: (context, child) {
                      // Constant left-to-right sprint: enters from the left
                      // edge, exits past the right edge, then instantly
                      // (while both ends are off-screen) loops back to
                      // re-enter from the left again.
                      final dx = -laneHalfSpan + (2 * laneHalfSpan) * _runnerLoopController.value;
                      return Transform.translate(offset: Offset(dx, 0), child: child);
                    },
                    child: const Icon(
                      Icons.directions_run_rounded,
                      size: _runnerIconSize,
                      color: AppColors.accent,
                    ),
                  ),
                ],
              ),
            ),
            const Spacer(flex: 2),
            FadeTransition(
              opacity: _contentController,
              child: SlideTransition(
                position: Tween<Offset>(begin: const Offset(0, 0.15), end: Offset.zero)
                    .animate(CurvedAnimation(parent: _contentController, curve: Curves.easeOut)),
                child: Column(
                  children: [
                    const Text(
                      'Prostart',
                      style: TextStyle(
                        fontSize: 40,
                        fontWeight: FontWeight.w800,
                        letterSpacing: -0.5,
                        color: AppColors.textPrimary,
                      ),
                    ),
                    const SizedBox(height: AppSpacing.sm),
                    const Text(
                      'Train your reaction time.',
                      style: TextStyle(fontSize: 15, color: AppColors.textSecondary),
                    ),
                    const SizedBox(height: AppSpacing.xl),
                    Padding(
                      padding: const EdgeInsets.symmetric(horizontal: AppSpacing.xl),
                      child: SizedBox(
                        width: double.infinity,
                        child: ElevatedButton(
                          onPressed: () {
                            Navigator.of(context).pushReplacement(
                              MaterialPageRoute(builder: (_) => const HomeShell()),
                            );
                          },
                          child: const Text('Get Started'),
                        ),
                      ),
                    ),
                  ],
                ),
              ),
            ),
            const Spacer(flex: 3),
          ],
        ),
      ),
    );
  }
}
