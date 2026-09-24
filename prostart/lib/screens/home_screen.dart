import 'package:flutter/material.dart';

import '../theme/app_theme.dart';
import '../widgets/glass.dart';
import 'photofinish_result_screen.dart';
import 'reaction_result_screen.dart';

class HomeScreen extends StatelessWidget {
  const HomeScreen({super.key});

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      bottom: false,
      child: ListView(
        padding: const EdgeInsets.fromLTRB(
          AppSpacing.xl,
          AppSpacing.md,
          AppSpacing.xl,
          130,
        ),
        children: [
          const _Greeting(),
          const SizedBox(height: AppSpacing.xl),
          const _DeviceCard(),
          const SizedBox(height: AppSpacing.xxl),
          const _SectionTitle('Choose your session'),
          const SizedBox(height: AppSpacing.md),
          _ModeCard(
            icon: Icons.bolt_rounded,
            color: AppColors.accent,
            title: 'Reaction Time',
            subtitle: 'Block start reaction, measured from the gun',
            stat: 'Last  0.138 s',
            decoration: const _SignalDecoration(),
            onTap: () =>
                Navigator.of(context).push(ReactionResultScreen.route()),
          ),
          const SizedBox(height: AppSpacing.lg),
          _ModeCard(
            icon: Icons.sports_score_rounded,
            color: AppColors.cyan,
            title: 'Photofinish',
            subtitle: 'Reaction + total time, gun to finish line',
            stat: 'Last  10"93 · 100 m',
            decoration: const _FinishLineDecoration(),
            onTap: () =>
                Navigator.of(context).push(PhotofinishResultScreen.route()),
          ),
          const SizedBox(height: AppSpacing.xxl),
          const _SectionTitle('This week'),
          const SizedBox(height: AppSpacing.md),
          const _WeekStats(),
        ],
      ),
    );
  }
}

class _Greeting extends StatelessWidget {
  const _Greeting();

  @override
  Widget build(BuildContext context) {
    return Row(
      children: [
        const Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Hi, Lorenzo 👋',
                style: TextStyle(
                  fontSize: 32,
                  fontWeight: FontWeight.w800,
                  letterSpacing: -0.8,
                ),
              ),
              SizedBox(height: 2),
              Text(
                'Ready for your next start?',
                style: TextStyle(fontSize: 15, color: AppColors.textSecondary),
              ),
            ],
          ),
        ),
        // Photo inside a liquid-glass ring.
        GlassPanel(
          borderRadius: AppRadius.pill,
          padding: const EdgeInsets.all(3),
          child: ClipOval(
            child: Image.asset(
              'assets/images/avatar.jpg',
              width: 50,
              height: 50,
              fit: BoxFit.cover,
            ),
          ),
        ),
      ],
    );
  }
}

class _DeviceCard extends StatelessWidget {
  const _DeviceCard();

  @override
  Widget build(BuildContext context) {
    return GlassPanel(
      tint: AppColors.bluetooth,
      padding: const EdgeInsets.all(AppSpacing.lg),
      child: Row(
        children: [
          Container(
            width: 48,
            height: 48,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              gradient: const LinearGradient(
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
                colors: [Color(0xFF5B9BFF), AppColors.bluetooth],
              ),
              boxShadow: [
                BoxShadow(
                  color: AppColors.bluetooth.withValues(alpha: 0.5),
                  blurRadius: 18,
                ),
              ],
            ),
            child: const Icon(Icons.bluetooth_rounded, color: Colors.white),
          ),
          const SizedBox(width: AppSpacing.lg),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Row(
                  children: [
                    Text(
                      'MyProstart',
                      style: TextStyle(
                        fontSize: 17,
                        fontWeight: FontWeight.w700,
                      ),
                    ),
                    SizedBox(width: 6),
                    Icon(
                      Icons.check_circle_rounded,
                      size: 18,
                      color: AppColors.success,
                    ),
                  ],
                ),
                const SizedBox(height: 4),
                Row(
                  children: [
                    Container(
                      width: 7,
                      height: 7,
                      decoration: BoxDecoration(
                        color: AppColors.success,
                        shape: BoxShape.circle,
                        boxShadow: [
                          BoxShadow(
                            color: AppColors.success.withValues(alpha: 0.8),
                            blurRadius: 6,
                          ),
                        ],
                      ),
                    ),
                    const SizedBox(width: 6),
                    const Text(
                      'Connected',
                      style: TextStyle(
                        fontSize: 13,
                        fontWeight: FontWeight.w600,
                        color: AppColors.success,
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
          const _BatteryChip(),
        ],
      ),
    );
  }
}

class _BatteryChip extends StatelessWidget {
  const _BatteryChip();

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: Colors.white.withValues(alpha: 0.08),
        borderRadius: BorderRadius.circular(AppRadius.pill),
      ),
      child: const Row(
        children: [
          Icon(
            Icons.battery_5_bar_rounded,
            size: 16,
            color: AppColors.textSecondary,
          ),
          SizedBox(width: 2),
          Text(
            '87%',
            style: TextStyle(
              fontSize: 13,
              fontWeight: FontWeight.w600,
              color: AppColors.textSecondary,
              fontFeatures: tabular,
            ),
          ),
        ],
      ),
    );
  }
}

class _SectionTitle extends StatelessWidget {
  final String text;
  const _SectionTitle(this.text);

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: const TextStyle(
        fontSize: 19,
        fontWeight: FontWeight.w700,
        letterSpacing: -0.2,
      ),
    );
  }
}

class _ModeCard extends StatelessWidget {
  final IconData icon;
  final Color color;
  final String title;
  final String subtitle;
  final String stat;
  final Widget decoration;
  final VoidCallback onTap;

  const _ModeCard({
    required this.icon,
    required this.color,
    required this.title,
    required this.subtitle,
    required this.stat,
    required this.decoration,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    return GestureDetector(
      onTap: onTap,
      child: GlassPanel(
        tint: color,
        child: SizedBox(
          height: 216,
          child: Stack(
            children: [
              Positioned(
                right: 0,
                top: 0,
                bottom: 0,
                width: 170,
                child: decoration,
              ),
              Padding(
                padding: const EdgeInsets.all(AppSpacing.xl),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Container(
                      width: 52,
                      height: 52,
                      decoration: BoxDecoration(
                        borderRadius: BorderRadius.circular(16),
                        gradient: LinearGradient(
                          begin: Alignment.topLeft,
                          end: Alignment.bottomRight,
                          colors: [
                            Color.lerp(color, Colors.white, 0.35)!,
                            color,
                          ],
                        ),
                        boxShadow: [
                          BoxShadow(
                            color: color.withValues(alpha: 0.45),
                            blurRadius: 22,
                            offset: const Offset(0, 6),
                          ),
                        ],
                      ),
                      child: Icon(icon, size: 30, color: AppColors.onAccent),
                    ),
                    const Spacer(),
                    Text(
                      title,
                      style: const TextStyle(
                        fontSize: 21,
                        fontWeight: FontWeight.w800,
                        letterSpacing: -0.3,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      subtitle,
                      style: const TextStyle(
                        fontSize: 13,
                        color: AppColors.textSecondary,
                      ),
                    ),
                    const SizedBox(height: AppSpacing.md),
                    Row(
                      children: [
                        Container(
                          padding: const EdgeInsets.symmetric(
                            horizontal: 12,
                            vertical: 7,
                          ),
                          decoration: BoxDecoration(
                            color: color.withValues(alpha: 0.22),
                            borderRadius: BorderRadius.circular(AppRadius.pill),
                            border: Border.all(
                              color: color.withValues(alpha: 0.6),
                            ),
                          ),
                          child: Text(
                            stat,
                            style: TextStyle(
                              fontSize: 13.5,
                              fontWeight: FontWeight.w800,
                              color: color,
                              fontFeatures: tabular,
                            ),
                          ),
                        ),
                        const Spacer(),
                        Container(
                          width: 44,
                          height: 44,
                          decoration: BoxDecoration(
                            shape: BoxShape.circle,
                            color: color,
                            boxShadow: [
                              BoxShadow(
                                color: color.withValues(alpha: 0.5),
                                blurRadius: 16,
                              ),
                            ],
                          ),
                          child: const Icon(
                            Icons.arrow_forward_rounded,
                            size: 22,
                            color: AppColors.onAccent,
                          ),
                        ),
                      ],
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }
}

/// Faint accelerometer trace: flat while set, then the burst at the start.
class _SignalDecoration extends StatelessWidget {
  const _SignalDecoration();

  @override
  Widget build(BuildContext context) => CustomPaint(painter: _SignalPainter());
}

class _SignalPainter extends CustomPainter {
  static const _trace = [
    0.0,
    0.02,
    -0.01,
    0.01,
    0.0,
    -0.02,
    0.01,
    0.0,
    0.02,
    0.0,
    0.05,
    -0.15,
    0.35,
    -0.55,
    0.8,
    -0.3,
    0.45,
    -0.7,
    0.25,
    -0.2,
    0.5,
    -0.35,
    0.15,
    -0.1,
  ];

  @override
  void paint(Canvas canvas, Size size) {
    final midY = size.height * 0.38;
    final amp = size.height * 0.26;
    final path = Path();
    for (var i = 0; i < _trace.length; i++) {
      final x = size.width * 0.05 + size.width * 0.95 * i / (_trace.length - 1);
      final y = midY - _trace[i] * amp;
      i == 0 ? path.moveTo(x, y) : path.lineTo(x, y);
    }
    final shader = LinearGradient(
      colors: [
        AppColors.accent.withValues(alpha: 0.0),
        AppColors.accent.withValues(alpha: 0.55),
      ],
    ).createShader(Offset.zero & size);
    canvas.drawPath(
      path,
      Paint()
        ..style = PaintingStyle.stroke
        ..strokeWidth = 2.2
        ..strokeJoin = StrokeJoin.round
        ..shader = shader,
    );
    // The GO marker.
    final goX =
        size.width * 0.05 + size.width * 0.95 * 10 / (_trace.length - 1);
    canvas.drawLine(
      Offset(goX, size.height * 0.08),
      Offset(goX, size.height * 0.68),
      Paint()
        ..color = AppColors.accent.withValues(alpha: 0.35)
        ..strokeWidth = 1,
    );
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}

/// A slanted chequered finish strip.
class _FinishLineDecoration extends StatelessWidget {
  const _FinishLineDecoration();

  @override
  Widget build(BuildContext context) => CustomPaint(painter: _FinishPainter());
}

class _FinishPainter extends CustomPainter {
  @override
  void paint(Canvas canvas, Size size) {
    const cell = 14.0;
    canvas.save();
    canvas.translate(size.width * 0.62, -10);
    canvas.rotate(0.22);
    for (var row = 0; row < (size.height + 40) / cell; row++) {
      for (var col = 0; col < 3; col++) {
        if ((row + col).isEven) continue;
        final t = row * cell / (size.height + 40);
        canvas.drawRect(
          Rect.fromLTWH(col * cell, row * cell, cell, cell),
          Paint()
            ..color = AppColors.cyan.withValues(alpha: 0.10 + 0.30 * (1 - t)),
        );
      }
    }
    canvas.restore();
  }

  @override
  bool shouldRepaint(covariant CustomPainter oldDelegate) => false;
}

class _WeekStats extends StatelessWidget {
  const _WeekStats();

  @override
  Widget build(BuildContext context) {
    return const Column(
      children: [
        Row(
          children: [
            Expanded(
              child: _StatTile(label: 'Best RT', value: '0.121', unit: 's'),
            ),
            SizedBox(width: AppSpacing.md),
            Expanded(
              child: _StatTile(label: 'Avg RT', value: '0.150', unit: 's'),
            ),
          ],
        ),
        SizedBox(height: AppSpacing.md),
        Row(
          children: [
            Expanded(
              child: _StatTile(label: 'Starts', value: '48', unit: ''),
            ),
            SizedBox(width: AppSpacing.md),
            Expanded(
              child: _StatTile(label: 'Best 100 m', value: '10"93', unit: ''),
            ),
          ],
        ),
      ],
    );
  }
}

class _StatTile extends StatelessWidget {
  final String label;
  final String value;
  final String unit;

  const _StatTile({
    required this.label,
    required this.value,
    required this.unit,
  });

  @override
  Widget build(BuildContext context) {
    return GlassPanel(
      borderRadius: AppRadius.md,
      padding: const EdgeInsets.symmetric(
        horizontal: AppSpacing.lg,
        vertical: AppSpacing.lg,
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            label,
            style: const TextStyle(fontSize: 12, color: AppColors.textFaint),
          ),
          const SizedBox(height: 6),
          Text.rich(
            TextSpan(
              children: [
                TextSpan(
                  text: value,
                  style: const TextStyle(
                    fontSize: 22,
                    fontWeight: FontWeight.w800,
                  ),
                ),
                if (unit.isNotEmpty)
                  TextSpan(
                    text: ' $unit',
                    style: const TextStyle(
                      fontSize: 13,
                      color: AppColors.textSecondary,
                    ),
                  ),
              ],
            ),
            style: const TextStyle(fontFeatures: tabular),
          ),
        ],
      ),
    );
  }
}
