// HK時計のホーム画面ウィジェット。
// Copyright (c) 2023-2025 Katayama Hirofumi MZ. All Rights Reserved.

package com.katahiromz.hkclock

import android.app.AlarmManager
import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.os.SystemClock
import android.util.TypedValue
import android.widget.RemoteViews
import java.util.Calendar

class HKClockWidgetProvider : AppWidgetProvider() {

    companion object {
        private const val ACTION_TICK = "com.katahiromz.hkclock.ACTION_WIDGET_TICK"

        private fun dp2px(context: Context, dp: Float): Int {
            return TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, dp, context.resources.displayMetrics
            ).toInt()
        }

        // 配置されているすべてのウィジェットを再描画する。
        fun updateAllWidgets(context: Context) {
            val manager = AppWidgetManager.getInstance(context)
            val ids = manager.getAppWidgetIds(ComponentName(context, HKClockWidgetProvider::class.java))
            for (id in ids) {
                updateWidget(context, manager, id)
            }

            // 電池最適化がまだ有効なら、次回アプリ起動時に再誘導するフラグを立てる
            checkAndMarkBatteryPromptIfNeeded(context)
        }

        private fun checkAndMarkBatteryPromptIfNeeded(context: Context) {
            val now = System.currentTimeMillis()
            val lastUpdate = MainRepository.getLastWidgetUpdateTime(context)

            // 今回の更新時刻を記録
            MainRepository.setLastWidgetUpdateTime(context, now)

            val DELAY_THRESHOLD_MS = 2 * 60 * 1000L   // 2分以上遅延したら「止まっていた」と判定

            val wasDelayed = lastUpdate > 0L && (now - lastUpdate) > DELAY_THRESHOLD_MS

            if (wasDelayed) {
                // 実際に分針が止まっていたので再誘導フラグを立てる
                MainRepository.setNeedBatteryPrompt(context, true)
                android.util.Log.w(
                    "HKClockWidget",
                    "Widget update delayed by ${(now - lastUpdate) / 1000}s → force resume + mark battery prompt"
                )

                // 強制的に更新チェーンを再開させる
                forceResumeTick(context)
            }

            // 電池最適化が除外済みならフラグをクリア
            val pm = context.getSystemService(Context.POWER_SERVICE) as? PowerManager
            if (pm != null && pm.isIgnoringBatteryOptimizations(context.packageName)) {
                MainRepository.setNeedBatteryPrompt(context, false)
            }
        }

        /**
         * 分針が止まっていた場合に、アラームを強制再スケジュールして動きを再開させる。
         * setAlarmClock を併用することで Doze 中でもより確実に発火しやすくする。
         */
        private fun forceResumeTick(context: Context) {
            val alarmManager = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
            val pendingIntent = tickPendingIntent(context)

            // 既存のアラームをキャンセル
            alarmManager.cancel(pendingIntent)

            // 次の分境界を計算
            val cal = Calendar.getInstance()
            val currentMsInMinute = cal.get(Calendar.SECOND) * 1000L + cal.get(Calendar.MILLISECOND)
            val msToNextMinute = 60_000L - currentMsInMinute
            val delay = if (msToNextMinute <= 0L) 60_000L else msToNextMinute
            val triggerAtElapsed = SystemClock.elapsedRealtime() + delay
            val triggerAtRtc = System.currentTimeMillis() + delay

            try {
                // 1. Doze 耐性が高い setAlarmClock を優先（時計アプリとして適切）
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    val showIntent = PendingIntent.getActivity(
                        context, 1,
                        context.packageManager.getLaunchIntentForPackage(context.packageName) ?: Intent(),
                        PendingIntent.FLAG_UPDATE_CURRENT or
                                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0
                    )
                    val info = AlarmManager.AlarmClockInfo(triggerAtRtc, showIntent)
                    alarmManager.setAlarmClock(info, pendingIntent)
                } else {
                    // 古い端末向けフォールバック
                    alarmManager.setExact(AlarmManager.ELAPSED_REALTIME_WAKEUP, triggerAtElapsed, pendingIntent)
                }

                // 2. バックアップ（90秒後）— 別の requestCode を使う
                val backupTrigger = SystemClock.elapsedRealtime() + 90_000L
                val backupIntent = PendingIntent.getBroadcast(
                    context, 2,   // requestCode = 2
                    Intent(context, HKClockWidgetProvider::class.java).apply { action = ACTION_TICK },
                    PendingIntent.FLAG_UPDATE_CURRENT or
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0
                )
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                    alarmManager.setAndAllowWhileIdle(
                        AlarmManager.ELAPSED_REALTIME_WAKEUP,
                        backupTrigger,
                        backupIntent
                    )
                }

                android.util.Log.i("HKClockWidget", "Forced resume scheduled (AlarmClock + backup)")
            } catch (e: SecurityException) {
                android.util.Log.e("HKClockWidget", "Failed to force resume", e)
                scheduleNextTick(context)   // 最低限のフォールバック
            }
        }

        // 1個のウィジェットを再描画する。
        fun updateWidget(context: Context, manager: AppWidgetManager, appWidgetId: Int) {
            val options = manager.getAppWidgetOptions(appWidgetId)

            val sizes: List<android.util.SizeF>? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    // API 33+
                    options.getParcelableArrayList(AppWidgetManager.OPTION_APPWIDGET_SIZES, android.util.SizeF::class.java)
                } else {
                    // API 31–32
                    @Suppress("DEPRECATION")
                    options.getParcelableArrayList(AppWidgetManager.OPTION_APPWIDGET_SIZES)
                }
            } else {
                null
            }

            val (widthDp, heightDp) = if (!sizes.isNullOrEmpty()) {
                // 現在の向きに近いサイズを選ぶ（先頭か、画面向きに合わせて選んでも可）
                val size = sizes[0]
                size.width to size.height
            } else {
                // 従来の min/max から向きを考慮して取得
                val minW = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_WIDTH, 40)
                val maxW = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MAX_WIDTH, minW)
                val minH = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MIN_HEIGHT, 40)
                val maxH = options.getInt(AppWidgetManager.OPTION_APPWIDGET_MAX_HEIGHT, minH)

                val isPortrait = context.resources.configuration.orientation ==
                        android.content.res.Configuration.ORIENTATION_PORTRAIT

                if (isPortrait) {
                    // ポートレート: 狭い幅 × 高い高さ
                    minW to maxH
                } else {
                    // ランドスケープ: 広い幅 × 低い高さ
                    maxW to minH
                }
            }

            val scale = 2
            val w = (dp2px(context, widthDp.toFloat()) * scale).coerceAtLeast(64)
            val h = (dp2px(context, heightDp.toFloat()) * scale).coerceAtLeast(64)
            val bitmap = ClockFaceRenderer.render(w, h)

            val views = RemoteViews(context.packageName, R.layout.widget_hk_clock)
            views.setImageViewBitmap(R.id.widgetClockImage, bitmap)

            // タップでアプリ本体を開く。
            context.packageManager.getLaunchIntentForPackage(context.packageName)?.let { launchIntent ->
                val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0
                val pendingIntent = PendingIntent.getActivity(context, 0, launchIntent, flags)
                views.setOnClickPendingIntent(R.id.widgetClockImage, pendingIntent)
            }

            manager.updateAppWidget(appWidgetId, views)
        }

        private fun tickPendingIntent(context: Context): PendingIntent {
            val intent = Intent(context, HKClockWidgetProvider::class.java).apply {
                action = ACTION_TICK
            }
            val flags = PendingIntent.FLAG_UPDATE_CURRENT or
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) PendingIntent.FLAG_IMMUTABLE else 0
            return PendingIntent.getBroadcast(context, 0, intent, flags)
        }

        // Android 12(S)以降で「正確なアラーム」を実際に発行できるかどうか。
        // 本アプリは時計ウィジェットなのでAndroidManifest.xmlに
        //   <uses-permission android:name="android.permission.USE_EXACT_ALARM" />
        // を追加すれば、この許可はインストール時に自動付与され常にtrueになる。
        // (Google Playの「時計・アラーム」用途ポリシーに該当するため使用可)
        // 何らかの理由でUSE_EXACT_ALARMが使えない端末/審査環境でも動くよう、
        // SCHEDULE_EXACT_ALARMの実行時許可があるかを併せて確認しておく。
        private fun canScheduleExact(context: Context): Boolean {
            return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val alarmManager = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
                alarmManager.canScheduleExactAlarms()
            } else {
                // Android 11以下は特別な許可なしに正確なアラームを予約できる。
                true
            }
        }

        // 次の「分」境界（:00）に合わせてアラームを1回だけセットする。
        // 30秒間隔だと Doze の while-idle 制限（深いIdle時は約15分）に抵触しやすく、
        // 分針が止まる原因になるため、1分間隔に変更。
        fun scheduleNextTick(context: Context) {
            val alarmManager = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
            val cal = Calendar.getInstance()
            val currentMsInMinute = cal.get(Calendar.SECOND) * 1000L + cal.get(Calendar.MILLISECOND)
            // 次の分境界までのミリ秒
            val msToNextMinute = 60_000L - currentMsInMinute
            val delay = if (msToNextMinute <= 0L) 60_000L else msToNextMinute
            val triggerAt = SystemClock.elapsedRealtime() + delay
            val pendingIntent = tickPendingIntent(context)

            try {
                when {
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && canScheduleExact(context) -> {
                        alarmManager.setExactAndAllowWhileIdle(
                            AlarmManager.ELAPSED_REALTIME_WAKEUP, triggerAt, pendingIntent
                        )
                    }
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.M -> {
                        alarmManager.setAndAllowWhileIdle(
                            AlarmManager.ELAPSED_REALTIME_WAKEUP, triggerAt, pendingIntent
                        )
                    }
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT -> {
                        alarmManager.setExact(AlarmManager.ELAPSED_REALTIME_WAKEUP, triggerAt, pendingIntent)
                    }
                    else -> {
                        alarmManager.set(AlarmManager.ELAPSED_REALTIME_WAKEUP, triggerAt, pendingIntent)
                    }
                }
            } catch (e: SecurityException) {
                // フォールバック
                alarmManager.setAndAllowWhileIdle(AlarmManager.ELAPSED_REALTIME_WAKEUP, triggerAt, pendingIntent)
            }
        }

        fun cancelTick(context: Context) {
            val alarmManager = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
            alarmManager.cancel(tickPendingIntent(context))
        }
    }

    override fun onUpdate(context: Context, appWidgetManager: AppWidgetManager, appWidgetIds: IntArray) {
        for (id in appWidgetIds) {
            updateWidget(context, appWidgetManager, id)
        }
        scheduleNextTick(context)
    }

    // ウィジェットのサイズが変更された時にも再描画する。
    override fun onAppWidgetOptionsChanged(
        context: Context, appWidgetManager: AppWidgetManager,
        appWidgetId: Int, newOptions: Bundle?
    ) {
        updateWidget(context, appWidgetManager, appWidgetId)

        // リサイズ中でも更新チェーンが切れないよう、必ず次の分境界を再スケジュールする
        scheduleNextTick(context)
    }

    override fun onReceive(context: Context, intent: Intent) {
        super.onReceive(context, intent)
        when (intent.action) {
            ACTION_TICK,
            Intent.ACTION_BOOT_COMPLETED,
            Intent.ACTION_MY_PACKAGE_REPLACED,
            Intent.ACTION_TIME_CHANGED,
            Intent.ACTION_TIMEZONE_CHANGED -> {
                updateAllWidgets(context)
                scheduleNextTick(context)
            }
        }
    }

    // 1個目のウィジェットが置かれた時。
    override fun onEnabled(context: Context) {
        scheduleNextTick(context)
    }

    // 最後の1個のウィジェットが取り除かれた時。
    override fun onDisabled(context: Context) {
        cancelTick(context)
    }
}
