// GenericAppのメインレポジトリ。
// Copyright (c) 2023-2025 Katayama Hirofumi MZ. All Rights Reserved.

// レポジトリ、すなわち情報の格納庫。現在、電池最適化案内の既読状態を管理している。

package com.katahiromz.hkclock

import android.content.Context
import android.content.SharedPreferences

class MainRepository {
    companion object {
        private const val MainPrefFileKey = "MAIN_PREF_FILE"
        private const val BatteryOptimizationAskedKey = "BATTERY_OPTIMIZATION_ASKED"
        private const val BatteryOptimizationLastAskedKey = "BATTERY_OPTIMIZATION_LAST_ASKED"
        private const val NeedBatteryPromptKey = "NEED_BATTERY_PROMPT"
        private const val LastWidgetUpdateKey = "LAST_WIDGET_UPDATE"

        // 再案内を抑制する最短間隔（ミリ秒）。ここでは 12 時間。
        private const val REASK_INTERVAL_MS = 12 * 60 * 60 * 1000L

        private fun getPrefs(context: Context): SharedPreferences {
            return context.getSharedPreferences(MainPrefFileKey, Context.MODE_PRIVATE)
        }

        // 電池最適化の除外をユーザーに一度でも案内したかどうかを取得する。
        // ※ 「除外を実際に許可したか」ではなく「ダイアログを見せたか」を表すフラグ。
        fun hasAskedIgnoreBatteryOptimization(context: Context): Boolean {
            return getPrefs(context).getBoolean(BatteryOptimizationAskedKey, false)
        }

        // 電池最適化の除外を案内済みであることを記録する。
        fun setAskedIgnoreBatteryOptimization(context: Context, asked: Boolean) {
            getPrefs(context)
                .edit()
                .putBoolean(BatteryOptimizationAskedKey, asked)
                .apply()
        }

        // 最後に案内した時刻を記録
        fun setLastAskedIgnoreBatteryOptimization(context: Context, timeMs: Long) {
            getPrefs(context)
                .edit()
                .putLong(BatteryOptimizationLastAskedKey, timeMs)
                .apply()
        }

        fun getLastAskedIgnoreBatteryOptimization(context: Context): Long {
            return getPrefs(context).getLong(BatteryOptimizationLastAskedKey, 0L)
        }

        // ウィジェット側から「再誘導が必要」とマークする
        fun setNeedBatteryPrompt(context: Context, need: Boolean) {
            getPrefs(context)
                .edit()
                .putBoolean(NeedBatteryPromptKey, need)
                .apply()
        }

        fun needBatteryPrompt(context: Context): Boolean {
            return getPrefs(context).getBoolean(NeedBatteryPromptKey, false)
        }

        // 最後にウィジェットが更新された時刻を保存
        fun setLastWidgetUpdateTime(context: Context, timeMs: Long) {
            getPrefs(context)
                .edit()
                .putLong(LastWidgetUpdateKey, timeMs)
                .apply()
        }

        fun getLastWidgetUpdateTime(context: Context): Long {
            return getPrefs(context).getLong(LastWidgetUpdateKey, 0L)
        }

        /**
         * 今すぐ再案内してよいかどうか。
         * - まだ除外されていない
         * - かつ（一度も聞いていない / 再誘導フラグが立っている / 前回から一定時間経過）
         */
        fun shouldShowBatteryPrompt(context: Context): Boolean {
            val pm = context.getSystemService(Context.POWER_SERVICE) as? android.os.PowerManager
                ?: return false
            if (pm.isIgnoringBatteryOptimizations(context.packageName)) {
                // 既に除外済みならフラグをクリアして終了
                setNeedBatteryPrompt(context, false)
                setAskedIgnoreBatteryOptimization(context, false)
                return false
            }

            // 再誘導フラグが立っている場合は優先して表示
            if (needBatteryPrompt(context)) {
                return true
            }

            // 通常の「一度聞いた」抑制ロジック
            if (!hasAskedIgnoreBatteryOptimization(context)) {
                return true
            }

            // 前回案内から一定時間経過していれば再表示を許可
            val last = getLastAskedIgnoreBatteryOptimization(context)
            return (System.currentTimeMillis() - last) > REASK_INTERVAL_MS
        }
    }
}
