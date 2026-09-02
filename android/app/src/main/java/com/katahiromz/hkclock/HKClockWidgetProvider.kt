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

        // 次の30秒境界（:00 または :30）に合わせてアラームを1回だけセットする。
        // これにより時刻表示の誤差を最大30秒以内に抑える。
        // 呼ばれるたびに次の境界を予約し直す。
        //
        // 【重要】setAndAllowWhileIdle等の「不正確」なアラームは、Doze/App Standby中に
        // OSがまとめて発火させる「メンテナンスウィンドウ」まで遅延することがあり、
        // その間隔は端末がアイドルな時間が長いほど数分〜十数分に広がっていく。
        // 本ウィジェットは「30秒ごとに次の1回を予約し直す」自己再帰的な仕組みのため、
        // 不正確アラームだとこの遅延がそのまま「分針が全く動かない」症状に直結してしまう。
        // そのため可能な限りsetExactAndAllowWhileIdleを使い、権限が無い場合のみ
        // フォールバックとしてsetAndAllowWhileIdleを使う。
        fun scheduleNextTick(context: Context) {
            val alarmManager = context.getSystemService(Context.ALARM_SERVICE) as AlarmManager
            val cal = Calendar.getInstance()
            val currentMsInMinute = cal.get(Calendar.SECOND) * 1000L + cal.get(Calendar.MILLISECOND)
            // 次の30秒境界までのミリ秒を計算（0または30秒のタイミング）
            val msToNextTime = if (currentMsInMinute < 30_000L) {
                30_000L - currentMsInMinute
            } else {
                60_000L - currentMsInMinute
            }
            // ちょうど境界上にいる場合は次の境界へ（0にならないように）
            val delay = if (msToNextTime <= 0L) 30_000L else msToNextTime
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
                        // 正確なアラームの許可が無い端末向けフォールバック。
                        // Doze中はある程度ずれ得るが、次回のtickで自動的に補正される。
                        alarmManager.setAndAllowWhileIdle(
                            AlarmManager.ELAPSED_REALTIME_WAKEUP, triggerAt, pendingIntent
                        )
                    }
                    Build.VERSION.SDK_INT >= Build.VERSION_CODES.KITKAT -> {
                        // KitKat以降はsetも不正確化されているため、明示的にsetExactを使う。
                        alarmManager.setExact(AlarmManager.ELAPSED_REALTIME_WAKEUP, triggerAt, pendingIntent)
                    }
                    else -> {
                        alarmManager.set(AlarmManager.ELAPSED_REALTIME_WAKEUP, triggerAt, pendingIntent)
                    }
                }
            } catch (e: SecurityException) {
                // 端末/OEMの制限等でSecurityExceptionが飛んでも、ここで再帰が
                // 完全に止まってしまわないようフォールバックしておく。
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
    }

    override fun onReceive(context: Context, intent: Intent) {
        super.onReceive(context, intent)
        if (intent.action == ACTION_TICK) {
            updateAllWidgets(context)
            scheduleNextTick(context)
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
