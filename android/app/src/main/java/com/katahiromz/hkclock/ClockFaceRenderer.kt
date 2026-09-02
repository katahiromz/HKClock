// HK時計ウィジェット用の文字盤描画。
// index.html の Canvas 描画ロジック(draw関数)を android.graphics.Canvas に移植したもの。
// Copyright (c) 2023-2025 Katayama Hirofumi MZ. All Rights Reserved.

package com.katahiromz.hkclock

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Path
import android.graphics.Shader
import android.graphics.Typeface
import java.util.Calendar
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

// index.html の HK時計と同じデザインの文字盤を Bitmap に描画するオブジェクト。
// ウィジェットは常時アニメーションできないため、呼び出された瞬間の時刻で1枚描画する。
object ClockFaceRenderer {

    // rgb(13, 13, 20) と同値。手計算の16進リテラルは誤りやすいため Color.rgb() を直接使う。
    private val FACE_COLOR = Color.rgb(13, 13, 20)

    private fun deg2rad(deg: Float): Float = (deg * PI / 180.0).toFloat()

    // 12時方向を0度とした時計回りの角度から単位ベクトルを求める(index.htmlのdir関数)。
    private fun dir(angleDeg: Float): FloatArray {
        val rad = deg2rad(angleDeg - 90f)
        return floatArrayOf(cos(rad), sin(rad))
    }

    // 中心・長さ・角度から座標を求める(index.htmlのhandEndpoint関数)。
    private fun point(cx: Float, cy: Float, length: Float, angleDeg: Float): FloatArray {
        val d = dir(angleDeg)
        return floatArrayOf(cx + length * d[0], cy + length * d[1])
    }

    fun render(width: Int, height: Int): Bitmap {
        val bitmap = Bitmap.createBitmap(width.coerceAtLeast(1), height.coerceAtLeast(1), Bitmap.Config.ARGB_8888)
        draw(Canvas(bitmap), width.toFloat(), height.toFloat())
        return bitmap
    }

    private fun draw(canvas: Canvas, clientW: Float, clientH: Float) {
        val paint = Paint(Paint.ANTI_ALIAS_FLAG)

        var diameter = min(clientW, clientH)
        var radius = diameter * 0.5f
        val cx = clientW * 0.5f
        val cy = clientH * 0.5f

        val inset = diameter * 0.02f
        radius -= inset
        diameter = radius * 2f

        // 背景: 縦グラデーション + うっすらした縦ストライプ
        run {
            paint.shader = LinearGradient(
                0f, 0f, 0f, clientH,
                intArrayOf(Color.rgb(30, 50, 120), Color.rgb(80, 190, 230), Color.rgb(30, 50, 120)),
                floatArrayOf(0f, 0.5f, 1f), Shader.TileMode.CLAMP
            )
            canvas.drawRect(0f, 0f, clientW, clientH, paint)

            val stripeStep = 0.06f * radius
            val lineWidth = 0.02f * radius
            var x = 0f
            while (x < clientW) {
                paint.shader = LinearGradient(
                    x, 0f, x, clientH,
                    Color.rgb(94, 200, 255), Color.rgb(94, 100, 255), Shader.TileMode.CLAMP
                )
                canvas.drawRect(x, 0f, x + lineWidth, clientH, paint)
                x += stripeStep
            }
            paint.shader = null
        }

        // リング(縁)の寸法
        val ringH = min(clientW * 0.77f, clientH) * 0.85f
        val ringW = ringH / 0.77f
        val faceR = min(ringW, ringH) / 2f * 0.9f

        // 影 + 縁のグラデーション + 黒い文字盤
        run {
            paint.style = Paint.Style.FILL
            paint.color = Color.argb(Math.round(0.31f * 255f), 0, 0, 0)
            val shadowWidth = radius * 0.03f
            canvas.drawOval(
                cx - ringW / 2f - shadowWidth, cy - ringH / 2f - shadowWidth,
                cx + ringW / 2f + shadowWidth, cy + ringH / 2f + shadowWidth, paint
            )
            // shader使用時もpaint.alphaが乗算されるため、影の半透明値を引きずらないよう明示的に不透明へ戻す
            paint.alpha = 255

            paint.shader = LinearGradient(
                cx, cy - ringH / 2f, cx, cy + ringH / 2f,
                intArrayOf(Color.rgb(106, 125, 135), Color.rgb(190, 200, 200), Color.rgb(106, 125, 135)),
                floatArrayOf(0f, 0.5f, 1f), Shader.TileMode.CLAMP
            )
            canvas.drawOval(cx - ringW / 2f, cy - ringH / 2f, cx + ringW / 2f, cy + ringH / 2f, paint)
            paint.shader = null

            paint.color = FACE_COLOR
            canvas.drawCircle(cx, cy, faceR, paint)
        }

        // 数字(12,3,6,9)と目盛り
        run {
            val fontSize = faceR * 0.20f
            paint.style = Paint.Style.FILL
            paint.color = Color.WHITE
            paint.typeface = Typeface.create(Typeface.DEFAULT_BOLD, Typeface.BOLD)
            paint.textSize = fontSize
            paint.textAlign = Paint.Align.CENTER
            val fm = paint.fontMetrics
            val textOffsetY = -(fm.ascent + fm.descent) / 2f

            val labels = arrayOf("12", "3", "6", "9")
            val labelAngles = floatArrayOf(0f, 90f, 180f, 270f)
            val labelDist = faceR * 0.80f
            for (i in labels.indices) {
                val p = point(cx, cy, labelDist, labelAngles[i])
                canvas.drawText(labels[i], p[0], p[1] + textOffsetY, paint)
            }

            paint.strokeCap = Paint.Cap.ROUND
            paint.strokeWidth = faceR * 0.012f
            paint.color = Color.WHITE

            for (i in 0 until 60) {
                val angle = i * 6f
                if (i % 5 == 0) {
                    val dist = faceR * 0.95f
                    val p = point(cx, cy, dist, angle)
                    paint.style = Paint.Style.FILL
                    canvas.drawCircle(p[0], p[1], faceR * 0.04f, paint)
                } else {
                    val o = point(cx, cy, faceR * 0.98f, angle)
                    val i2 = point(cx, cy, faceR * 0.91f, angle)
                    paint.style = Paint.Style.STROKE
                    canvas.drawLine(o[0], o[1], i2[0], i2[1], paint)
                }
            }
        }

        // 現在時刻
        val cal = Calendar.getInstance()
        val sec = cal.get(Calendar.SECOND)
        val min = cal.get(Calendar.MINUTE)
        val hour = (cal.get(Calendar.HOUR_OF_DAY) % 12) + min / 60f

        val secAngle = sec * 6f
        val minAngle = min * 6f
        val hourAngle = hour * 30f

        // 分針
        drawFatHand(
            canvas, paint, cx, cy, minAngle,
            shaftEnd = faceR * 0.9f, middle = faceR * 0.25f, tail = -faceR * 0.08f,
            midOffset = 6f, wideOffset = 12f, innerShaftFactor = 0.6f,
            bossR1 = faceR * 0.08f, bossR2 = faceR * 0.06f
        )

        // 時針
        drawFatHand(
            canvas, paint, cx, cy, hourAngle,
            shaftEnd = faceR * 0.6f, middle = faceR * 0.3f, tail = -faceR * 0.05f,
            midOffset = 5f, wideOffset = 11f, innerShaftFactor = 0.8f,
            bossR1 = faceR * 0.05f, bossR2 = null
        )

        if (false) { // 秒針は表示しない
            // 秒針(ウィジェットは1分毎更新のため常に0秒付近だが、意匠として描画しておく)
            run {
                val m = dir(secAngle - 0.3f)
                val p = dir(secAngle + 0.3f)
                val mm = dir(secAngle - 2.5f)
                val pp = dir(secAngle + 2.5f)

                val shaftEnd = faceR * 0.9f
                val tail = -faceR * 0.3f

                paint.style = Paint.Style.FILL
                paint.color = Color.WHITE
                val path = Path()
                path.moveTo(cx + shaftEnd * m[0], cy + shaftEnd * m[1])
                path.lineTo(cx + shaftEnd * p[0], cy + shaftEnd * p[1])
                path.lineTo(cx + tail * mm[0], cy + tail * mm[1])
                path.lineTo(cx + tail * pp[0], cy + tail * pp[1])
                path.close()
                canvas.drawPath(path, paint)
                canvas.drawCircle(cx, cy, faceR * 0.048f, paint)
            }
        }

        // "H K" ラベル(右下)
        run {
            val fontSize = faceR * 0.20f
            paint.typeface = Typeface.create(Typeface.DEFAULT_BOLD, Typeface.BOLD_ITALIC)
            paint.textSize = fontSize
            paint.color = Color.WHITE
            paint.textAlign = Paint.Align.RIGHT
            paint.style = Paint.Style.FILL
            val fm = paint.fontMetrics
            canvas.drawText("H K", clientW - fontSize, clientH - fontSize - fm.descent, paint)
        }
    }

    // 分針・時針共通の「太い針」を描く。
    private fun drawFatHand(
        canvas: Canvas, paint: Paint,
        cx: Float, cy: Float, angleDeg: Float,
        shaftEnd: Float, middle: Float, tail: Float,
        midOffset: Float, wideOffset: Float, innerShaftFactor: Float,
        bossR1: Float, bossR2: Float?
    ) {
        val d = dir(angleDeg)
        val mm = dir(angleDeg - midOffset)
        val pp = dir(angleDeg + midOffset)
        val mmm = dir(angleDeg - wideOffset)
        val ppp = dir(angleDeg + wideOffset)
        val mmmm = dir(angleDeg - 5f)
        val pppp = dir(angleDeg + 5f)

        // outer/innerを1つのPathにまとめ、evenoddで本当に「穴」を開ける。
        // これでindex.htmlのfill('evenodd')と同じく、針の内側からは
        // 下に描いた文字盤・数字・目盛りがそのまま透けて見える。
        val handPath = Path()
        handPath.fillType = Path.FillType.EVEN_ODD

        handPath.moveTo(cx + shaftEnd * d[0], cy + shaftEnd * d[1])
        handPath.lineTo(cx + middle * mmm[0], cy + middle * mmm[1])
        handPath.lineTo(cx + 0.8f * middle * mm[0], cy + 0.8f * middle * mm[1])
        handPath.lineTo(cx + tail * ppp[0], cy + tail * ppp[1])
        handPath.lineTo(cx + tail * mmm[0], cy + tail * mmm[1])
        handPath.lineTo(cx + 0.8f * middle * pp[0], cy + 0.8f * middle * pp[1])
        handPath.lineTo(cx + middle * ppp[0], cy + middle * ppp[1])
        handPath.close()

        val innerShaftEnd = shaftEnd * innerShaftFactor
        handPath.moveTo(cx + innerShaftEnd * d[0], cy + innerShaftEnd * d[1])
        handPath.lineTo(cx + middle * mmmm[0], cy + middle * mmmm[1])
        handPath.lineTo(cx + 0.95f * middle * d[0], cy + 0.95f * middle * d[1])
        handPath.lineTo(cx + middle * pppp[0], cy + middle * pppp[1])
        handPath.close()

        paint.style = Paint.Style.FILL
        paint.color = Color.WHITE
        canvas.drawPath(handPath, paint)

        paint.color = Color.WHITE
        canvas.drawCircle(cx, cy, bossR1, paint)
        if (bossR2 != null) {
            paint.color = FACE_COLOR
            canvas.drawCircle(cx, cy, bossR2, paint)
        }
    }
}
