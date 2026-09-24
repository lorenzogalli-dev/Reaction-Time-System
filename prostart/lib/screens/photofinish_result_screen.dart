import 'package:flutter/material.dart';

import '../mock_data.dart';
import '../theme/app_theme.dart';
import '../widgets/glass.dart';
import '../widgets/result_widgets.dart';

/// Result of a photofinish run: gun → finish line. Every value is mock data.
class PhotofinishResultScreen extends StatelessWidget {
  const PhotofinishResultScreen({super.key});

  static Route<void> route() =>
      MaterialPageRoute(builder: (_) => const PhotofinishResultScreen());

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: GlowBackground(
        primaryGlow: AppColors.cyan,
        secondaryGlow: AppColors.accent,
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
                title: 'Photofinish',
                subtitle: '100 m · Today, 18:52',
                trailing: Icons.ios_share_rounded,
              ),
              const SizedBox(height: AppSpacing.xl),
              const _TimeHero(),
              const SizedBox(height: AppSpacing.lg),
              const ImageCard(
                asset: MockData.finishImage,
                label: 'FINISH LINE',
                icon: Icons.sports_score_rounded,
                badge: '10"93',
                color: AppColors.cyan,
                aspectRatio: MockData.finishAspectRatio,
              ),
              const SizedBox(height: AppSpacing.lg),
              const InsightsCard(
                insights: [
                  InsightRow(
                    icon: Icons.emoji_events_rounded,
                    color: AppColors.gold,
                    title: 'Season best on 100 m',
                    detail: '0.09 s faster than your previous best (11"02)',
                  ),
                  InsightRow(
                    icon: Icons.trending_down_rounded,
                    color: AppColors.success,
                    title: 'Reaction 0.012 s faster than average',
                    detail: 'A sharp start: 0.138 s against your 0.150 s',
                  ),
                  InsightRow(
                    icon: Icons.speed_rounded,
                    color: AppColors.cyan,
                    title: 'Average speed 9.15 m/s',
                    detail: '32.9 km/h from gun to finish line',
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}

class _TimeHero extends StatelessWidget {
  const _TimeHero();

  @override
  Widget build(BuildContext context) {
    return GlassPanel(
      tint: AppColors.cyan,
      padding: const EdgeInsets.all(AppSpacing.xl),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Row(
            children: [
              Eyebrow('Total time'),
              Spacer(),
              Tag(
                '100 m',
                icon: Icons.straighten_rounded,
                color: AppColors.cyan,
              ),
              SizedBox(width: 6),
              Tag('SB', icon: Icons.star_rounded, color: AppColors.gold),
            ],
          ),
          const SizedBox(height: AppSpacing.lg),
          const HeroNumber(
            value: '10"93',
            unit: '',
            color: AppColors.textPrimary,
            size: 60,
          ),
          const SizedBox(height: AppSpacing.xl),
          Container(height: 1, color: Colors.white.withValues(alpha: 0.12)),
          const SizedBox(height: AppSpacing.lg),
          const Row(
            children: [
              Expanded(
                child: _Metric(
                  label: 'Reaction',
                  value: '0.138',
                  unit: 's',
                  color: AppColors.accent,
                ),
              ),
              Expanded(
                child: _Metric(label: 'Run', value: '10.792', unit: 's'),
              ),
              Expanded(
                child: _Metric(label: 'Avg speed', value: '9.15', unit: 'm/s'),
              ),
            ],
          ),
        ],
      ),
    );
  }
}

class _Metric extends StatelessWidget {
  final String label;
  final String value;
  final String unit;
  final Color color;

  const _Metric({
    required this.label,
    required this.value,
    required this.unit,
    this.color = AppColors.textPrimary,
  });

  @override
  Widget build(BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Eyebrow(label),
        const SizedBox(height: 6),
        Text.rich(
          TextSpan(
            children: [
              TextSpan(
                text: value,
                style: TextStyle(
                  fontSize: 19,
                  fontWeight: FontWeight.w800,
                  color: color,
                ),
              ),
              TextSpan(
                text: ' $unit',
                style: const TextStyle(
                  fontSize: 12,
                  color: AppColors.textSecondary,
                ),
              ),
            ],
          ),
          style: const TextStyle(fontFeatures: tabular),
        ),
      ],
    );
  }
}
