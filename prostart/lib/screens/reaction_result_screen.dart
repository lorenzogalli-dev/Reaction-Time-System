import 'package:flutter/material.dart';

import '../mock_data.dart';
import '../theme/app_theme.dart';
import '../widgets/glass.dart';
import '../widgets/result_widgets.dart';

/// Result of a "Reaction Time Only" start. Every value here is mock data.
class ReactionResultScreen extends StatelessWidget {
  const ReactionResultScreen({super.key});

  static Route<void> route() =>
      MaterialPageRoute(builder: (_) => const ReactionResultScreen());

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: GlowBackground(
        child: SafeArea(
          bottom: false,
          child: ListView(
            padding: const EdgeInsets.fromLTRB(
              AppSpacing.xl,
              AppSpacing.md,
              AppSpacing.xl,
              48,
            ),
            children: [
              const GlassHeader(
                title: 'Reaction Time',
                subtitle: 'Start #12 · Today, 18:34',
                trailing: Icons.ios_share_rounded,
              ),
              const SizedBox(height: AppSpacing.xl),
              const _ReactionHero(),
              const SizedBox(height: AppSpacing.lg),
              const RecentStartsChart(
                values: MockData.recentReactions,
                average: 0.150,
              ),
              const SizedBox(height: AppSpacing.lg),
              const InsightsCard(
                insights: [
                  InsightRow(
                    icon: Icons.trending_down_rounded,
                    color: AppColors.success,
                    title: '0.012 s faster than your average',
                    detail: 'Your average over the last 20 starts is 0.150 s',
                  ),
                  InsightRow(
                    icon: Icons.emoji_events_rounded,
                    color: AppColors.gold,
                    title: '2nd best start this week',
                    detail: 'Only 0.017 s off your personal best (0.121 s)',
                  ),
                  InsightRow(
                    icon: Icons.verified_rounded,
                    color: AppColors.cyan,
                    title: 'Clean set position',
                    detail: 'No movement detected before the gun',
                  ),
                ],
              ),
              const SizedBox(height: AppSpacing.lg),
              const ImageCard(
                asset: MockData.startChart,
                label: 'START SIGNAL',
                icon: Icons.graphic_eq_rounded,
                badge: 'GO → move  138 ms',
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _ReactionHero extends StatelessWidget {
  const _ReactionHero();

  @override
  Widget build(BuildContext context) {
    return GlassPanel(
      tint: AppColors.accent,
      padding: const EdgeInsets.all(AppSpacing.xl),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Row(
            children: [
              Eyebrow('Reaction time'),
              Spacer(),
              Tag('Legal start', icon: Icons.check_rounded),
            ],
          ),
          const SizedBox(height: AppSpacing.lg),
          const HeroNumber(value: '0.138', unit: 's', size: 72),
          const SizedBox(height: AppSpacing.lg),
          _GaugeBar(value: 0.138),
          const SizedBox(height: 6),
          const Row(
            children: [
              Text('0.100', style: _gaugeLabel),
              Spacer(),
              Text('elite  < 0.130', style: _gaugeLabel),
              Spacer(),
              Text('0.200', style: _gaugeLabel),
            ],
          ),
        ],
      ),
    );
  }

  static const _gaugeLabel = TextStyle(
    fontSize: 11,
    color: AppColors.textFaint,
    fontFeatures: tabular,
  );
}

/// Where this reaction sits between the false-start limit and a slow start.
class _GaugeBar extends StatelessWidget {
  final double value;
  const _GaugeBar({required this.value});

  @override
  Widget build(BuildContext context) {
    const lo = 0.100, hi = 0.200;
    final t = ((value - lo) / (hi - lo)).clamp(0.0, 1.0);
    return LayoutBuilder(
      builder: (context, c) {
        return SizedBox(
          height: 18,
          child: Stack(
            clipBehavior: Clip.none,
            alignment: Alignment.centerLeft,
            children: [
              Container(
                height: 8,
                decoration: BoxDecoration(
                  borderRadius: BorderRadius.circular(AppRadius.pill),
                  gradient: const LinearGradient(
                    colors: [
                      AppColors.accent,
                      AppColors.gold,
                      Color(0xFFFF6B5C),
                    ],
                  ),
                ),
              ),
              Positioned(
                left: c.maxWidth * t - 9,
                child: Container(
                  width: 18,
                  height: 18,
                  decoration: BoxDecoration(
                    shape: BoxShape.circle,
                    color: Colors.white,
                    border: Border.all(color: AppColors.background, width: 3),
                    boxShadow: const [
                      BoxShadow(color: Colors.black54, blurRadius: 8),
                    ],
                  ),
                ),
              ),
            ],
          ),
        );
      },
    );
  }
}
