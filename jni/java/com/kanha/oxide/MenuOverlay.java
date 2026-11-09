// java/com/kanha/oxide/MenuOverlay.java
package com.kanha.oxide;

import android.app.Activity;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

public class MenuOverlay {
    static {
        System.loadLibrary("kanha_oxide");
    }

    public static native void nativeSetFeature(int featureId, boolean state, float value);
    public static native float[] nativeFetchEspData();

    private static boolean isInitialized = false;
    private static boolean drawEspLines = false;
    private static boolean drawEspBoxes = false;
    private static boolean drawEspDistance = false;
    private static boolean drawAimbotFov = false;
    private static float currentFov = 120.0f;

    public static void start(final Activity activity) {
        if (isInitialized || activity == null) return;
        isInitialized = true;

        new Handler(Looper.getMainLooper()).postDelayed(new Runnable() {
            @Override
            public void run() {
                buildInterface(activity);
            }
        }, 1500);
    }

    private static int dp(Context ctx, int val) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, val, ctx.getResources().getDisplayMetrics());
    }

    private static void buildInterface(final Activity activity) {
        final FrameLayout rootDecor = (FrameLayout) activity.getWindow().getDecorView();

        // Camada 1: Canvas Transparente para Desenhar ESP e Círculo FOV em 60 FPS
        final View espCanvasView = new View(activity) {
            private final Paint linePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
            private final Paint boxPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
            private final Paint textPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
            private final Paint fovPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

            {
                linePaint.setColor(Color.parseColor("#00FF88"));
                linePaint.setStrokeWidth(2.5f);

                boxPaint.setColor(Color.parseColor("#FF2255"));
                boxPaint.setStyle(Paint.Style.STROKE);
                boxPaint.setStrokeWidth(3.0f);

                textPaint.setColor(Color.WHITE);
                textPaint.setTextSize(28.0f);

                fovPaint.setColor(Color.parseColor("#8800FFFF"));
                fovPaint.setStyle(Paint.Style.STROKE);
                fovPaint.setStrokeWidth(2.0f);
            }

            @Override
            protected void onDraw(Canvas canvas) {
                super.onDraw(canvas);
                int w = getWidth();
                int h = getHeight();

                if (drawAimbotFov) {
                    canvas.drawCircle(w / 2.0f, h / 2.0f, currentFov, fovPaint);
                }

                if (drawEspLines || drawEspBoxes || drawEspDistance) {
                    float[] raw = nativeFetchEspData();
                    if (raw != null && raw.length >= 5) {
                        for (int i = 0; i <= raw.length - 5; i += 5) {
                            float sx = raw[i];
                            float sy = h - raw[i + 1]; // Inverte eixo Y da Unity para Android Canvas
                            float dist = raw[i + 2];
                            int hp = (int) raw[i + 3];

                            if (drawEspLines) {
                                canvas.drawLine(w / 2.0f, 0, sx, sy, linePaint);
                            }
                            if (drawEspBoxes) {
                                float boxW = Math.max(24.0f, 900.0f / (dist + 1.0f));
                                float boxH = boxW * 1.8f;
                                canvas.drawRect(sx - boxW / 2, sy - boxH, sx + boxW / 2, sy, boxPaint);
                            }
                            if (drawEspDistance) {
                                canvas.drawText((int) dist + "m [" + hp + " HP]", sx - 40, sy + 26, textPaint);
                            }
                        }
                    }
                }
                postInvalidateDelayed(16);
            }
        };
        espCanvasView.setClickable(false);
        espCanvasView.setFocusable(false);
        rootDecor.addView(espCanvasView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        // Camada 2: Container Flutuante Arrastável (Ícone Minimizado + Painel Completo)
        final FrameLayout floatingContainer = new FrameLayout(activity);
        final FrameLayout.LayoutParams containerParams = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        containerParams.gravity = Gravity.TOP | Gravity.START;
        containerParams.leftMargin = dp(activity, 25);
        containerParams.topMargin = dp(activity, 60);

        // Botão Flutuante (Aparece quando o menu está minimizado)
        final Button floatingBubble = new Button(activity);
        floatingBubble.setText("KANHA");
        floatingBubble.setTextColor(Color.BLACK);
        floatingBubble.setTextSize(11);
        GradientDrawable bubbleBg = new GradientDrawable();
        bubbleBg.setColor(Color.parseColor("#00FF88"));
        bubbleBg.setCornerRadius(dp(activity, 30));
        bubbleBg.setStroke(dp(activity, 2), Color.BLACK);
        floatingBubble.setBackground(bubbleBg);
        floatingBubble.setVisibility(View.GONE);

        // Painel Principal do Mod Menu
        final LinearLayout menuPanel = new LinearLayout(activity);
        menuPanel.setOrientation(LinearLayout.VERTICAL);
        menuPanel.setPadding(dp(activity, 12), dp(activity, 10), dp(activity, 12), dp(activity, 12));
        GradientDrawable panelBg = new GradientDrawable();
        panelBg.setColor(Color.parseColor("#E6111318"));
        panelBg.setCornerRadius(dp(activity, 12));
        panelBg.setStroke(dp(activity, 2), Color.parseColor("#00FF88"));
        menuPanel.setBackground(panelBg);

        // Barra de Título (Arrastável + Botão Minimizar)
        LinearLayout headerBar = new LinearLayout(activity);
        headerBar.setOrientation(LinearLayout.HORIZONTAL);
        headerBar.setGravity(Gravity.CENTER_VERTICAL);

        TextView titleText = new TextView(activity);
        titleText.setText("OXIDE HACK // KANHA 3@10");
        titleText.setTextColor(Color.parseColor("#00FF88"));
        titleText.setTextSize(13);
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1.0f);
        headerBar.addView(titleText, titleLp);

        Button minBtn = new Button(activity);
        minBtn.setText("—");
        minBtn.setTextColor(Color.WHITE);
        minBtn.setBackgroundColor(Color.TRANSPARENT);
        minBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                menuPanel.setVisibility(View.GONE);
                floatingBubble.setVisibility(View.VISIBLE);
            }
        });
        headerBar.addView(minBtn, new LinearLayout.LayoutParams(dp(activity, 38), dp(activity, 34)));
        menuPanel.addView(headerBar);

        floatingBubble.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                floatingBubble.setVisibility(View.GONE);
                menuPanel.setVisibility(View.VISIBLE);
            }
        });

        // Área de Rolagem com Todas as Funções
        ScrollView scrollArea = new ScrollView(activity);
        LinearLayout listItems = new LinearLayout(activity);
        listItems.setOrientation(LinearLayout.VERTICAL);

        addToggle(activity, listItems, "Bypass Anti-Ban (Ativo)", 13, true);
        addToggle(activity, listItems, "ESP Linhas (Inimigos)", 1, false);
        addToggle(activity, listItems, "ESP Caixas 2D", 2, false);
        addToggle(activity, listItems, "ESP Distância + Vida", 3, false);
        addToggle(activity, listItems, "Aimbot Automático (Cabeça)", 5, false);
        addFovSlider(activity, listItems);
        addToggle(activity, listItems, "AutoFarm (1-Hit Pedra/Árvore)", 8, false);
        addToggle(activity, listItems, "Sem Recuo (No Recoil 100%)", 10, false);
        addToggle(activity, listItems, "Speed Hack (1.6x Seguro)", 11, false);
        addToggle(activity, listItems, "Sempre Dia (Visão Noturna)", 12, false);

        scrollArea.addView(listItems);
        menuPanel.addView(scrollArea, new LinearLayout.LayoutParams(dp(activity, 240), dp(activity, 260)));

        // Lógica de Arrastar na Tela pelo Cabeçalho ou Bolha Flutuante
        View.OnTouchListener dragListener = new View.OnTouchListener() {
            private float dX, dY;
            private long touchStartTime;

            @Override
            public boolean onTouch(View view, MotionEvent event) {
                switch (event.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        dX = containerParams.leftMargin - event.getRawX();
                        dY = containerParams.topMargin - event.getRawY();
                        touchStartTime = System.currentTimeMillis();
                        return true;
                    case MotionEvent.ACTION_MOVE:
                        containerParams.leftMargin = (int) (event.getRawX() + dX);
                        containerParams.topMargin = (int) (event.getRawY() + dY);
                        rootDecor.updateViewLayout(floatingContainer, containerParams);
                        return true;
                    case MotionEvent.ACTION_UP:
                        if (System.currentTimeMillis() - touchStartTime < 180) {
                            view.performClick();
                        }
                        return true;
                }
                return false;
            }
        };

        headerBar.setOnTouchListener(dragListener);
        floatingBubble.setOnTouchListener(dragListener);

        floatingContainer.addView(menuPanel);
        floatingContainer.addView(floatingBubble);
        rootDecor.addView(floatingContainer, containerParams);
    }

    private static void addToggle(Context ctx, LinearLayout parent, String title, final int featureId, boolean defState) {
        CheckBox cb = new CheckBox(ctx);
        cb.setText(title);
        cb.setTextColor(Color.WHITE);
        cb.setTextSize(12);
        cb.setChecked(defState);
        cb.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                if (featureId == 1) drawEspLines = isChecked;
                if (featureId == 2) drawEspBoxes = isChecked;
                if (featureId == 3) drawEspDistance = isChecked;
                if (featureId == 5) drawAimbotFov = isChecked;
                float val = (featureId == 11) ? 1.6f : 1.0f;
                nativeSetFeature(featureId, isChecked, val);
            }
        });
        parent.addView(cb);
    }

    private static void addFovSlider(Context ctx, LinearLayout parent) {
        final TextView label = new TextView(ctx);
        label.setText("Tamanho do FOV Aimbot: 120px");
        label.setTextColor(Color.LTGRAY);
        label.setTextSize(11);
        parent.addView(label);

        SeekBar sb = new SeekBar(ctx);
        sb.setMax(300);
        sb.setProgress(120);
        sb.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                currentFov = Math.max(30, progress);
                label.setText("Tamanho do FOV Aimbot: " + (int) currentFov + "px");
                nativeSetFeature(6, true, currentFov);
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        parent.addView(sb);
    }
}
