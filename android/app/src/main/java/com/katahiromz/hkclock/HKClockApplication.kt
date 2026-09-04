// HK時計ウィジェット用のアプリケーション。
// Copyright (c) 2026 Katayama Hirofumi MZ. All Rights Reserved.

package com.katahiromz.hkclock

import android.app.Application
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter

// 画面ON時に即座にウィジェットを更新するためのApplicationクラス。
// ※ プロセスが生きている間しか効かないおまけ的な対策であり、
//   本質的なDoze対策は HKClockWidgetProvider の forceResumeTick() 側にある。
class HKClockApplication : Application() {
    private val screenOnReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (intent.action == Intent.ACTION_SCREEN_ON) {
                HKClockWidgetProvider.updateAllWidgets(context)
                HKClockWidgetProvider.scheduleNextTick(context)
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        registerReceiver(screenOnReceiver, IntentFilter(Intent.ACTION_SCREEN_ON))
    }
}
