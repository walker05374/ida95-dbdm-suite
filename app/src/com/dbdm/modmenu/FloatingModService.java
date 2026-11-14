package com.dbdm.modmenu;

import android.app.ActivityManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileReader;
import java.io.InputStream;
import java.util.List;
import java.util.Locale;

public final class FloatingModService extends Service {
    private static final String TARGET_GAME_PACKAGE = "com.netease.dbdena";
    static {
        try {
            System.loadLibrary("instrument_core");
        } catch (Throwable ignored) {
        }
    }

    public static native void nativeSyncConfig(
        boolean hudOpen,
        boolean drawMarkers,
        float maxDist,
        String labelFilter,
        boolean overrideName,
        String playerName,
        boolean applyKinematics,
        float walkSpeed,
        float maxAccel,
        float jumpVel,
        float timeScale
    );

    public static native String nativeGetStatus();

    public static native int nativePollOverlayMarkers(
        float screenWidth,
        float screenHeight,
        float[] outPacked
    );

    public static native String nativeStartCrashLogger(String preferredDir, String fallbackDir);
    public static native void nativeStopCrashLogger();
    public static native void nativeLogModAction(String tag, String func, long offset, String detail);

    // Paleta de Cores Wdbd (Obsidiana Profunda, Carmesim Neon, Âmbar, Aço Escovado)
    private static final int COL_BG_SURFACE    = Color.argb(242, 11, 13, 20);   // #F20B0D14
    private static final int COL_BG_INNER      = Color.argb(225, 20, 24, 36);   // #E1141824
    private static final int COL_BORDER_ACCENT = Color.argb(200, 235, 52, 76);  // #C8EB344C
    private static final int COL_BORDER_SUBTLE = Color.argb(150, 58, 68, 92);   // #963A445C
    private static final int COL_CRIMSON       = Color.rgb(238, 42, 72);        // #EE2A48
    private static final int COL_EMBER         = Color.rgb(255, 132, 48);       // #FF8430
    private static final int COL_TEXT_PRIMARY  = Color.rgb(240, 243, 250);      // #F0F3FA
    private static final int COL_TEXT_MUTED    = Color.rgb(148, 160, 182);      // #94A0B6

    private WindowManager windowManager;
    private AuraEspOverlayView espOverlayView;
    private View rootFloatingView;
    private WindowManager.LayoutParams espLayoutParams;
    private WindowManager.LayoutParams windowParams;
    private LinearLayout expandedPanel;
    private ImageView collapsedBubbleView;
    private TextView statusBannerView;
    private Bitmap logoCircleBitmap;
    private boolean  crashLogActive = false;
    private String   crashLogPath   = "";
    private String   lastLoggedGpPayload = "";
    private Button   crashLogButton;
    private TextView crashLogPathNotice;
    private TextView authGateNoticeView;
    private CheckBox authAutoHealCb;
    private CheckBox authTrapImmunityCb;
    private Button   authEscapeBtn;
    private Button   authImmobilizeBtn;
    private Button   authRoleBtn;

    private final Handler uiHandler = new Handler(Looper.getMainLooper());
    private boolean serviceActive = false;

    // Estado de Visualização 3D e Categorias (PT-BR, espelhado com SceneTelemetryData)
    private boolean hudOpen          = true;
    private boolean drawMarkers      = false;
    private float   maxDist          = 250.0f;
    private boolean showAntagonist   = false; // Assassino
    private boolean showTeammates    = false; // Sobrevivente
    private boolean showPowerNodes   = false; // Gerador
    private boolean showShrines      = false; // Totem
    private boolean showContainers   = false; // Baú
    private boolean showBarriers     = false; // Pallet / Janela
    private boolean showPortals      = false; // Hatch / Portão
    private boolean showSpecialProps = false; // Objetos

    // Estado de Cinemática em Porcentagem (%) (PT-BR, espelhado com SceneTelemetryData)
    private boolean applyKinematics = false;
    private float   walkSpeedPct    = 100.0f; // 100% a 250% ("Velocidade de Movimento (%)")
    private float   accelPct        = 100.0f; // 100% a 300% ("Aceleração Máxima (%)")
    private float   jumpPct         = 100.0f; // 100% a 250% ("Força do Pulo (%)")
    private float   timeScalePct    = 100.0f; // 50% a 200%  ("Escala de Tempo (%)")

    // Valores físicos derivados das porcentagens para sincronização IPC
    private float walkSpeed = 600.0f;
    private float maxAccel  = 2048.0f;
    private float jumpVel   = 420.0f;
    private float timeScale = 1.0f;

    // Identidade do Jogador e Manipulação Avançada de Gameplay
    private boolean overrideName    = false;
    private String  playerName      = "";
    private boolean applyFov        = false;
    private float   fovValue        = 90.0f;
    private boolean autoSkillcheck  = false;
    private boolean autoHeal        = false;
    private boolean trapImmunity    = false;
    private boolean flyCheat        = false;
    private float   flyVertSpeed    = 0.0f;  // -600.0 (desce) .. 0.0 (segura) .. +600.0 (sobe)
    private boolean forceEscape     = false;
    private String  immobilizeState = "off";
    private String  playerRole      = "none";

    // Camada de Overlay Minimalista (Caixa Delimitadora 1.5px Contorno + Texto, PT-BR)
    private final class AuraEspOverlayView extends View {
        private static final int MARKER_STRIDE = 5;
        private final float[] packedMarkers = new float[256 * MARKER_STRIDE];
        private int activeMarkerCount = 0;

        private final Paint boxPaint         = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint textOutlinePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint textPaint        = new Paint(Paint.ANTI_ALIAS_FLAG);

        AuraEspOverlayView(Context ctx) {
            super(ctx);
            boxPaint.setStyle(Paint.Style.STROKE);
            boxPaint.setStrokeWidth(1.5f);

            textOutlinePaint.setStyle(Paint.Style.STROKE);
            textOutlinePaint.setStrokeWidth(2.0f);
            textOutlinePaint.setStrokeJoin(Paint.Join.ROUND);
            textOutlinePaint.setColor(Color.argb(235, 0, 0, 0));
            textOutlinePaint.setTextSize(dpF(9.0f));
            textOutlinePaint.setTypeface(Typeface.DEFAULT_BOLD);
            textOutlinePaint.setTextAlign(Paint.Align.LEFT);

            textPaint.setStyle(Paint.Style.FILL);
            textPaint.setTextSize(dpF(9.0f));
            textPaint.setTypeface(Typeface.DEFAULT_BOLD);
            textPaint.setTextAlign(Paint.Align.LEFT);
        }

        void pollAndInvalidate() {
            if (!drawMarkers) {
                if (activeMarkerCount != 0) {
                    activeMarkerCount = 0;
                    invalidate();
                }
                return;
            }
            final int w = getWidth();
            final int h = getHeight();
            if (w <= 1 || h <= 1) {
                return;
            }
            try {
                activeMarkerCount = nativePollOverlayMarkers((float) w, (float) h, packedMarkers);
            } catch (Throwable ignored) {
                activeMarkerCount = 0;
            }
            invalidate();
        }

        private boolean isCategoryVisible(int catId) {
            switch (catId) {
                case 1:  return showAntagonist;   // Assassino
                case 2:  return showTeammates;    // Sobreviventes
                case 3:  return showPowerNodes;   // Geradores
                case 4:  return showPortals;      // Alçapão
                case 5:  return showContainers;   // Baús
                case 6:  return showShrines;      // Totens
                case 7:  return showBarriers;     // Pallets (Sem janelas)
                case 8:  return showPortals;      // Portão de Saída
                case 9:  return showSpecialProps; // Ganchos
                default: return false;
            }
        }

        // Desenha um único retângulo por ator (apenas contorno de 1.5px, cor da categoria, sem preenchimento/gradiente/sombra)
        // dimensionado pela caixa delimitadora projetada e limitado ao mínimo de 14x28px, e o rótulo compacto
        // fora do canto superior direito da caixa (offset 2px, contorno escuro de 1px).
        private void drawActorBoxAndLabel(
            Canvas canvas,
            float footX,
            float footY,
            float projHeightPx,
            int color,
            String tag
        ) {
            final float boxH   = Math.max(28.0f, Math.min(450.0f, projHeightPx));
            final float boxW   = Math.max(14.0f, boxH * 0.5f);
            final float halfW  = boxW * 0.5f;
            final float leftX  = footX - halfW;
            final float rightX = footX + halfW;
            final float topY   = footY - boxH;

            boxPaint.setColor(color);
            canvas.drawRect(leftX, topY, rightX, footY, boxPaint);

            final float labelX = rightX + 2.0f;
            final float labelY = topY - textPaint.ascent();
            canvas.drawText(tag, labelX, labelY, textOutlinePaint);
            textPaint.setColor(color);
            canvas.drawText(tag, labelX, labelY, textPaint);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            if (!drawMarkers || activeMarkerCount <= 0) {
                return;
            }

            final float canvasW = (float) getWidth();
            boolean killerRendered = false;

            for (int i = 0; i < activeMarkerCount; ++i) {
                final int base       = i * MARKER_STRIDE;
                final float sx       = packedMarkers[base];
                final float sy       = packedMarkers[base + 1];
                final float distM    = packedMarkers[base + 2];
                final int catId      = Math.round(packedMarkers[base + 3]);
                final float extra    = packedMarkers[base + 4];

                if (distM > maxDist || !isCategoryVisible(catId)) {
                    continue;
                }

                final int distInt = Math.max(0, Math.round(distM));
                final float fallbackHeightPx = (canvasW * 0.45f) / Math.max(1.0f, distM);
                final float projHeightPx = (catId != 3 && extra >= 10.0f && extra <= 500.0f)
                    ? extra
                    : fallbackHeightPx;

                String tag;
                int colPrimary;

                switch (catId) {
                    case 1: { // Assassino (Vermelho #FF2A2A)
                        if (killerRendered) {
                            continue;
                        }
                        killerRendered = true;
                        colPrimary = Color.rgb(255, 42, 42);
                        tag = String.format(Locale.US, "%dm", distInt);
                        break;
                    }

                    case 2: { // Sobrevivente (Ciano #00E5FF)
                        colPrimary = Color.rgb(0, 229, 255);
                        tag = String.format(Locale.US, "%dm", distInt);
                        break;
                    }

                    case 3: { // Gerador (Verde #2ECC71)
                        colPrimary = Color.rgb(46, 204, 113);
                        final int genPct = Math.max(0, Math.min(100, Math.round(extra)));
                        tag = String.format(Locale.US, "%d%% %dm", genPct, distInt);
                        break;
                    }

                    case 4: // Alçapão (Roxo #E040FB)
                        colPrimary = Color.rgb(224, 64, 251);
                        tag = String.format(Locale.US, "%dm", distInt);
                        break;

                    case 5: // Baú (Laranja #FF9800)
                        colPrimary = Color.rgb(255, 152, 0);
                        tag = String.format(Locale.US, "%dm", distInt);
                        break;

                    case 6: // Totem (Amarelo #F1C40F)
                        colPrimary = Color.rgb(241, 196, 15);
                        tag = String.format(Locale.US, "%dm", distInt);
                        break;

                    case 7: // Pallet (Amarelo Claro #FFF176)
                        colPrimary = Color.rgb(255, 241, 118);
                        tag = String.format(Locale.US, "%dm", distInt);
                        break;

                    case 8: // Portão de Saída (Roxo #E040FB)
                        colPrimary = Color.rgb(224, 64, 251);
                        tag = String.format(Locale.US, "%dm", distInt);
                        break;

                    case 9: // Gancho (#78C8FF)
                        colPrimary = Color.rgb(120, 200, 255);
                        tag = String.format(Locale.US, "%dm", distInt);
                        break;

                    default:
                        continue;
                }

                drawActorBoxAndLabel(canvas, sx, sy, projHeightPx, colPrimary, tag);
            }
        }
    }

    private final Runnable frameLoopRunnable = new Runnable() {
        private int statusTick = 0;

        @Override
        public void run() {
            if (!serviceActive) {
                return;
            }
            if (espOverlayView != null) {
                espOverlayView.pollAndInvalidate();
            }
            if ((++statusTick & 0x0F) == 0 && statusBannerView != null) {
                try {
                    String st = nativeGetStatus();
                    if (st != null) {
                        String ptStatus = st
                            .replace("IPC: ONLINE", "IPC: CONECTADO")
                            .replace("IN MATCH (ACTIVE)", "EM PARTIDA (ATIVO)")
                            .replace("LOBBY / STANDBY", "LOBBY / AGUARDANDO")
                            .replace("Nodes:", "Nós:")
                            .replace("Gens:", "Geradores:")
                            .replace("Auth=1", "Host")
                            .replace("Auth=0", "Cliente")
                            .replace("Player:", "Jogador:")
                            .replace("(In-Game Default)", "(Padrão do Jogo)")
                            .replace("IPC: WAITING FOR GAME (com.netease.dbdena)", "IPC: AGUARDANDO JOGO (com.netease.dbdena)");
                        statusBannerView.setText(ptStatus);
                        updateAuthorityGateUi(st);
                    } else {
                        statusBannerView.setText("IPC: DESCONECTADO | Nós: 0 | Geradores: 0/0");
                        updateAuthorityGateUi(null);
                    }
                } catch (Throwable ignored) {
                    statusBannerView.setText("IPC: DESCONECTADO | Nós: 0 | Geradores: 0/0");
                }
            }
            uiHandler.postDelayed(this, 16L); // ~60 Hz
        }
    };

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        windowManager = (WindowManager) getSystemService(Context.WINDOW_SERVICE);
        clearStaleConfigOnStartup();
        loadLogoBitmap();
        buildEspOverlayLayer();
        buildFloatingMenuUi();
        pushConfigToNative();
        serviceActive = true;
        uiHandler.post(frameLoopRunnable);
        ensureTargetGameLaunched();
    }

    private void clearStaleConfigOnStartup() {
        overrideName   = false;
        playerName     = "";
        applyKinematics = false;
        flyCheat       = false;
        flyVertSpeed   = 0.0f;
        forceEscape    = false;
        try {
            File cfg = new File("/data/local/tmp/dbdm_mod_config.cfg");
            if (cfg.exists()) {
                // Remove stale config from previous sessions so toggles never auto-enable on startup
                cfg.delete();
            }
        } catch (Throwable ignored) {
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        ensureTargetGameLaunched();
        return START_STICKY;
    }

    private boolean isTargetGameRunning() {
        try {
            String st = nativeGetStatus();
            if (st != null && st.contains("IPC: ONLINE")) {
                return true;
            }
        } catch (Throwable ignored) {
        }
        try {
            ActivityManager am = (ActivityManager) getSystemService(Context.ACTIVITY_SERVICE);
            if (am != null) {
                List<ActivityManager.RunningAppProcessInfo> procs = am.getRunningAppProcesses();
                if (procs != null) {
                    for (ActivityManager.RunningAppProcessInfo info : procs) {
                        if (info != null && info.processName != null &&
                            info.processName.startsWith(TARGET_GAME_PACKAGE) &&
                            !info.processName.endsWith(":PushService")) {
                            return true;
                        }
                    }
                }
            }
        } catch (Throwable ignored) {
        }
        return false;
    }

    private void ensureTargetGameLaunched() {
        try {
            if (isTargetGameRunning()) {
                return;
            }
            Intent launchIntent = getPackageManager().getLaunchIntentForPackage(TARGET_GAME_PACKAGE);
            if (launchIntent != null) {
                launchIntent.addFlags(
                    Intent.FLAG_ACTIVITY_NEW_TASK |
                    Intent.FLAG_ACTIVITY_RESET_TASK_IF_NEEDED
                );
                startActivity(launchIntent);
            }
        } catch (Throwable ignored) {
        }
    }

    private void loadLogoBitmap() {
        try (InputStream is = getAssets().open("wdbd_logo_circle.png")) {
            logoCircleBitmap = BitmapFactory.decodeStream(is);
        } catch (Throwable ignored) {
            try {
                int id = getResources().getIdentifier("ic_launcher_round", "mipmap", getPackageName());
                if (id != 0) {
                    logoCircleBitmap = BitmapFactory.decodeResource(getResources(), id);
                }
            } catch (Throwable ignored2) {
            }
        }
    }

    private int dp(int v) {
        return (int) TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP,
            v,
            getResources().getDisplayMetrics()
        );
    }

    private float dpF(float v) {
        return TypedValue.applyDimension(
            TypedValue.COMPLEX_UNIT_DIP,
            v,
            getResources().getDisplayMetrics()
        );
    }

    private void styleMinimalCheckBox(CheckBox cb, String label, boolean checked) {
        cb.setText(label);
        cb.setTextColor(COL_TEXT_PRIMARY);
        cb.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.0f);
        cb.setChecked(checked);
        cb.setMinHeight(dp(23));
        cb.setMinimumHeight(dp(23));
        cb.setPadding(dp(2), 0, dp(2), 0);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            int[][] states = new int[][]{
                new int[]{android.R.attr.state_checked},
                new int[]{-android.R.attr.state_checked}
            };
            int[] colors = new int[]{COL_CRIMSON, COL_TEXT_MUTED};
            cb.setButtonTintList(new ColorStateList(states, colors));
        }
    }

    private void styleMinimalSeekBar(SeekBar sb) {
        sb.setPadding(dp(6), dp(2), dp(6), dp(3));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            sb.setProgressTintList(ColorStateList.valueOf(COL_CRIMSON));
            sb.setThumbTintList(ColorStateList.valueOf(COL_EMBER));
            sb.setProgressBackgroundTintList(ColorStateList.valueOf(Color.rgb(45, 50, 62)));
        }
    }

    private TextView buildMinimalSliderLabel(String text) {
        TextView tv = new TextView(this);
        tv.setText(text);
        tv.setTextColor(COL_TEXT_MUTED);
        tv.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9.5f);
        tv.setPadding(dp(2), dp(2), dp(2), 0);
        return tv;
    }

    private void styleMinimalEditText(EditText et, String hint) {
        et.setHint(hint);
        et.setTextColor(COL_TEXT_PRIMARY);
        et.setHintTextColor(Color.rgb(105, 112, 125));
        et.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.0f);
        et.setSingleLine(true);
        et.setMinHeight(dp(26));
        et.setMinimumHeight(dp(26));
        et.setPadding(dp(6), dp(3), dp(6), dp(3));
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(COL_BG_INNER);
        bg.setCornerRadius(dp(4));
        bg.setStroke(1, COL_BORDER_SUBTLE);
        et.setBackground(bg);
    }

    private Button buildTabButton(String title, boolean active) {
        Button btn = new Button(this);
        btn.setText(title);
        btn.setAllCaps(false);
        btn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9.5f);
        btn.setTypeface(Typeface.DEFAULT_BOLD);
        btn.setPadding(dp(4), dp(2), dp(4), dp(2));
        btn.setMinHeight(0);
        btn.setMinimumHeight(0);
        btn.setMinWidth(0);
        btn.setMinimumWidth(0);
        styleTabButton(btn, active);
        return btn;
    }

    private void styleTabButton(Button btn, boolean active) {
        btn.setTextColor(active ? COL_TEXT_PRIMARY : COL_TEXT_MUTED);
        GradientDrawable bg;
        if (active) {
            bg = new GradientDrawable(
                GradientDrawable.Orientation.TOP_BOTTOM,
                new int[]{Color.argb(230, 224, 38, 66), Color.argb(230, 155, 20, 42)}
            );
            bg.setStroke(1, COL_EMBER);
        } else {
            bg = new GradientDrawable();
            bg.setColor(COL_BG_INNER);
            bg.setStroke(1, COL_BORDER_SUBTLE);
        }
        bg.setCornerRadius(dp(6));
        btn.setBackground(bg);
    }

    private void setWindowFocusableForKeyboard(boolean focusable) {
        if (windowParams == null || rootFloatingView == null) {
            return;
        }
        if (focusable) {
            windowParams.flags &= ~WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
            windowParams.softInputMode = WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE
                | WindowManager.LayoutParams.SOFT_INPUT_ADJUST_PAN;
        } else {
            windowParams.flags |= WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE;
        }
        try {
            windowManager.updateViewLayout(rootFloatingView, windowParams);
        } catch (Throwable ignored) {
        }
    }

    private int computeCategoryVisibilityMask() {
        int catMask = 0;
        if (showAntagonist)   catMask |= 0x01;
        if (showTeammates)    catMask |= 0x02;
        if (showPowerNodes)   catMask |= 0x04;
        if (showShrines)      catMask |= 0x08;
        if (showContainers)   catMask |= 0x10;
        if (showBarriers)     catMask |= 0x20;
        if (showPortals)      catMask |= 0x40;
        if (showSpecialProps) catMask |= 0x80;
        return catMask;
    }

    private String buildCompactGpPayload() {
        final int catMask = computeCategoryVisibilityMask();
        final float clampedFov = Math.max(80.0f, Math.min(120.0f, fovValue));
        final String immToken  = (immobilizeState != null && !immobilizeState.isEmpty()) ? immobilizeState : "off";
        final String roleToken = (playerRole != null && !playerRole.isEmpty()) ? playerRole : "none";
        // Compact payload (< 56 bytes) so it never truncates inside ControlPayload::label_filter[64]
        return String.format(
            Locale.US,
            "@gp:f=%d:%.0f;s=%d;ah=%d;t=%d;y=%d:%.0f;e=%d;i=%s;r=%s;c=%02X",
            applyFov ? 1 : 0,
            clampedFov,
            autoSkillcheck ? 1 : 0,
            autoHeal ? 1 : 0,
            trapImmunity ? 1 : 0,
            flyCheat ? 1 : 0,
            flyVertSpeed,
            forceEscape ? 1 : 0,
            immToken,
            roleToken,
            catMask
        );
    }

    private void pushConfigToNative() {
        walkSpeed = 600.0f  * (walkSpeedPct / 100.0f);
        maxAccel  = 2048.0f * (accelPct / 100.0f);
        jumpVel   = 420.0f  * (jumpPct / 100.0f);
        timeScale = timeScalePct / 100.0f;

        final String gpPayload = buildCompactGpPayload();

        try {
            nativeSyncConfig(
                hudOpen,
                drawMarkers,
                maxDist,
                gpPayload,
                overrideName,
                playerName != null ? playerName : "",
                applyKinematics,
                walkSpeedPct,
                accelPct,
                jumpPct,
                timeScale
            );
            if (crashLogActive && !gpPayload.equals(lastLoggedGpPayload)) {
                lastLoggedGpPayload = gpPayload;
                nativeLogModAction("TOGGLE", "MenuUI", 0L, gpPayload);
            }
        } catch (Throwable ignored) {
        }
        if (forceEscape) {
            forceEscape = false;
        }
    }

    private void buildEspOverlayLayer() {
        final int overlayType = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
            : WindowManager.LayoutParams.TYPE_PHONE;

        espLayoutParams = new WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.MATCH_PARENT,
            overlayType,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
            PixelFormat.TRANSLUCENT
        );
        espLayoutParams.gravity = Gravity.TOP | Gravity.START;
        espOverlayView = new AuraEspOverlayView(this);
        windowManager.addView(espOverlayView, espLayoutParams);
    }

    private void buildFloatingMenuUi() {
        final int overlayType = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O)
            ? WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
            : WindowManager.LayoutParams.TYPE_PHONE;

        windowParams = new WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            overlayType,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
            PixelFormat.TRANSLUCENT
        );
        windowParams.gravity = Gravity.TOP | Gravity.START;
        windowParams.x = dp(16);
        windowParams.y = dp(32);

        LinearLayout container = new LinearLayout(this);
        container.setOrientation(LinearLayout.VERTICAL);

        // 1. Ícone Flutuante Minimizado Discreto (34dp x 34dp)
        collapsedBubbleView = new ImageView(this);
        collapsedBubbleView.setScaleType(ImageView.ScaleType.FIT_CENTER);
        if (logoCircleBitmap != null) {
            collapsedBubbleView.setImageBitmap(logoCircleBitmap);
        }
        GradientDrawable bubbleBg = new GradientDrawable();
        bubbleBg.setShape(GradientDrawable.OVAL);
        bubbleBg.setColor(Color.argb(235, 14, 15, 20));
        bubbleBg.setStroke(dp(1), COL_CRIMSON);
        collapsedBubbleView.setBackground(bubbleBg);
        collapsedBubbleView.setPadding(dp(2), dp(2), dp(2), dp(2));
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            collapsedBubbleView.setElevation(dp(5));
        }
        LinearLayout.LayoutParams bubbleLp = new LinearLayout.LayoutParams(dp(34), dp(34));
        collapsedBubbleView.setLayoutParams(bubbleLp);
        collapsedBubbleView.setVisibility(View.GONE);

        // 2. Painel Expandido Compacto (Totalmente em Português - PT-BR)
        expandedPanel = new LinearLayout(this);
        expandedPanel.setOrientation(LinearLayout.VERTICAL);
        expandedPanel.setPadding(dp(8), dp(6), dp(8), dp(8));
        GradientDrawable cardBg = new GradientDrawable();
        cardBg.setColor(COL_BG_SURFACE);
        cardBg.setCornerRadius(dp(10));
        cardBg.setStroke(dp(1), COL_BORDER_ACCENT);
        expandedPanel.setBackground(cardBg);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            expandedPanel.setElevation(dp(8));
        }

        // Cabeçalho Compacto (Logo + Título "Wdbd" + Botão Minimizar)
        LinearLayout headerBar = new LinearLayout(this);
        headerBar.setOrientation(LinearLayout.HORIZONTAL);
        headerBar.setGravity(Gravity.CENTER_VERTICAL);
        headerBar.setPadding(0, 0, 0, dp(3));

        if (logoCircleBitmap != null) {
            ImageView headerLogo = new ImageView(this);
            headerLogo.setImageBitmap(logoCircleBitmap);
            headerLogo.setScaleType(ImageView.ScaleType.FIT_CENTER);
            LinearLayout.LayoutParams hLogoLp = new LinearLayout.LayoutParams(dp(18), dp(18));
            hLogoLp.setMarginEnd(dp(6));
            headerBar.addView(headerLogo, hLogoLp);
        }

        TextView titleView = new TextView(this);
        titleView.setText("WDBD");
        titleView.setTextColor(COL_TEXT_PRIMARY);
        titleView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11.5f);
        titleView.setTypeface(Typeface.DEFAULT_BOLD);
        LinearLayout.LayoutParams titleLp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
        headerBar.addView(titleView, titleLp);

        Button minBtn = new Button(this);
        minBtn.setText("—");
        minBtn.setTextColor(COL_TEXT_PRIMARY);
        minBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10.0f);
        minBtn.setTypeface(Typeface.DEFAULT_BOLD);
        minBtn.setPadding(0, 0, 0, 0);
        minBtn.setMinHeight(0);
        minBtn.setMinimumHeight(0);
        minBtn.setMinWidth(0);
        minBtn.setMinimumWidth(0);
        GradientDrawable minBtnBg = new GradientDrawable();
        minBtnBg.setColor(COL_BG_INNER);
        minBtnBg.setCornerRadius(dp(4));
        minBtnBg.setStroke(1, COL_BORDER_ACCENT);
        minBtn.setBackground(minBtnBg);
        LinearLayout.LayoutParams minBtnLp = new LinearLayout.LayoutParams(dp(24), dp(20));
        minBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                setWindowFocusableForKeyboard(false);
                expandedPanel.setVisibility(View.GONE);
                collapsedBubbleView.setVisibility(View.VISIBLE);
            }
        });
        headerBar.addView(minBtn, minBtnLp);
        expandedPanel.addView(headerBar);

        View divider = new View(this);
        divider.setBackgroundColor(COL_BORDER_SUBTLE);
        expandedPanel.addView(divider, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 1));

        statusBannerView = new TextView(this);
        statusBannerView.setText("IPC: Conectando... | Nós: 0 | Geradores: 0/0");
        statusBannerView.setTextColor(COL_EMBER);
        statusBannerView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9.0f);
        statusBannerView.setPadding(0, dp(3), 0, dp(3));
        expandedPanel.addView(statusBannerView);

        crashLogPathNotice = new TextView(this);
        crashLogPathNotice.setTextColor(COL_TEXT_MUTED);
        crashLogPathNotice.setTextSize(TypedValue.COMPLEX_UNIT_SP, 8.0f);
        crashLogPathNotice.setPadding(dp(2), 0, dp(2), dp(4));
        crashLogPathNotice.setVisibility(View.GONE);
        expandedPanel.addView(crashLogPathNotice);

        authGateNoticeView = new TextView(this);
        authGateNoticeView.setTextColor(COL_EMBER);
        authGateNoticeView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 8.0f);
        authGateNoticeView.setPadding(dp(2), 0, dp(2), dp(4));
        authGateNoticeView.setVisibility(View.GONE);
        expandedPanel.addView(authGateNoticeView);

        // Barra de Abas (PT-BR): Visual | Física | Identidade | LOG
        LinearLayout tabBar = new LinearLayout(this);
        tabBar.setOrientation(LinearLayout.HORIZONTAL);
        tabBar.setPadding(0, dp(2), 0, dp(5));

        final Button tabVisualsBtn    = buildTabButton("Visual", true);
        final Button tabKinematicsBtn = buildTabButton("Física", false);
        final Button tabIdentityBtn   = buildTabButton("Identidade", false);

        crashLogButton = new Button(this);
        crashLogButton.setText("LOG");
        crashLogButton.setTextColor(COL_TEXT_PRIMARY);
        crashLogButton.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9.0f);
        crashLogButton.setTypeface(Typeface.DEFAULT_BOLD);
        crashLogButton.setAllCaps(false);
        crashLogButton.setSingleLine(true);
        crashLogButton.setGravity(Gravity.CENTER);
        crashLogButton.setMinWidth(0);
        crashLogButton.setMinimumWidth(0);
        crashLogButton.setMinHeight(0);
        crashLogButton.setMinimumHeight(0);
        GradientDrawable logIdleBg = new GradientDrawable();
        logIdleBg.setColor(Color.argb(215, 185, 24, 38));
        logIdleBg.setCornerRadius(dp(5));
        logIdleBg.setStroke(1, COL_BORDER_ACCENT);
        crashLogButton.setBackground(logIdleBg);
        crashLogButton.setPadding(dp(2), dp(2), dp(2), dp(2));
        crashLogButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                toggleCrashLogging();
            }
        });

        LinearLayout.LayoutParams tabLp1 = new LinearLayout.LayoutParams(0, dp(24), 1.0f);
        tabLp1.setMarginEnd(dp(3));
        LinearLayout.LayoutParams tabLp2 = new LinearLayout.LayoutParams(0, dp(24), 1.0f);
        tabLp2.setMarginEnd(dp(3));
        LinearLayout.LayoutParams tabLp3 = new LinearLayout.LayoutParams(0, dp(24), 1.0f);
        tabLp3.setMarginEnd(dp(3));
        LinearLayout.LayoutParams logLp = new LinearLayout.LayoutParams(dp(36), dp(24));

        tabBar.addView(tabVisualsBtn, tabLp1);
        tabBar.addView(tabKinematicsBtn, tabLp2);
        tabBar.addView(tabIdentityBtn, tabLp3);
        tabBar.addView(crashLogButton, logLp);
        expandedPanel.addView(tabBar);

        final ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(true);
        scrollView.setClipToPadding(false);
        scrollView.setVerticalScrollBarEnabled(true);
        scrollView.setScrollbarFadingEnabled(false);
        scrollView.setOverScrollMode(View.OVER_SCROLL_ALWAYS);

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setClipToPadding(false);
        content.setPadding(dp(2), dp(4), dp(4), dp(28));

        final LinearLayout sectionVisuals    = new LinearLayout(this);
        final LinearLayout sectionKinematics = new LinearLayout(this);
        final LinearLayout sectionIdentity   = new LinearLayout(this);
        sectionVisuals.setOrientation(LinearLayout.VERTICAL);
        sectionKinematics.setOrientation(LinearLayout.VERTICAL);
        sectionIdentity.setOrientation(LinearLayout.VERTICAL);

        sectionVisuals.setVisibility(View.VISIBLE);
        sectionKinematics.setVisibility(View.GONE);
        sectionIdentity.setVisibility(View.GONE);

        tabVisualsBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                setWindowFocusableForKeyboard(false);
                sectionVisuals.setVisibility(View.VISIBLE);
                sectionKinematics.setVisibility(View.GONE);
                sectionIdentity.setVisibility(View.GONE);
                styleTabButton(tabVisualsBtn, true);
                styleTabButton(tabKinematicsBtn, false);
                styleTabButton(tabIdentityBtn, false);
                scrollView.scrollTo(0, 0);
            }
        });
        tabKinematicsBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                setWindowFocusableForKeyboard(false);
                sectionVisuals.setVisibility(View.GONE);
                sectionKinematics.setVisibility(View.VISIBLE);
                sectionIdentity.setVisibility(View.GONE);
                styleTabButton(tabVisualsBtn, false);
                styleTabButton(tabKinematicsBtn, true);
                styleTabButton(tabIdentityBtn, false);
                scrollView.scrollTo(0, 0);
            }
        });
        tabIdentityBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                sectionVisuals.setVisibility(View.GONE);
                sectionKinematics.setVisibility(View.GONE);
                sectionIdentity.setVisibility(View.VISIBLE);
                styleTabButton(tabVisualsBtn, false);
                styleTabButton(tabKinematicsBtn, false);
                styleTabButton(tabIdentityBtn, true);
                scrollView.scrollTo(0, 0);
            }
        });

        // ====================================================================
        // ABA 1: VISUAL (Indicadores, FOV e Categorias)
        // ====================================================================
        CheckBox cbDrawMarkers = new CheckBox(this);
        styleMinimalCheckBox(cbDrawMarkers, "Ativar Indicadores", drawMarkers);
        cbDrawMarkers.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                drawMarkers = isChecked;
                pushConfigToNative();
                if (espOverlayView != null) {
                    espOverlayView.invalidate();
                }
            }
        });
        sectionVisuals.addView(cbDrawMarkers);

        final TextView tvMaxDist = buildMinimalSliderLabel("Distância Máxima: " + (int) maxDist + " m");
        sectionVisuals.addView(tvMaxDist);

        SeekBar sbMaxDist = new SeekBar(this);
        sbMaxDist.setMax(990);
        sbMaxDist.setProgress(Math.max(0, Math.min(990, (int) (maxDist - 10.0f))));
        styleMinimalSeekBar(sbMaxDist);
        sbMaxDist.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                maxDist = 10.0f + progress;
                tvMaxDist.setText("Distância Máxima: " + (int) maxDist + " m");
                pushConfigToNative();
                if (espOverlayView != null) {
                    espOverlayView.invalidate();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sectionVisuals.addView(sbMaxDist);

        // FOV Changer (80° a 120°)
        CheckBox cbFov = new CheckBox(this);
        styleMinimalCheckBox(cbFov, "Ativar FOV", applyFov);
        cbFov.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                applyFov = isChecked;
                pushConfigToNative();
            }
        });
        sectionVisuals.addView(cbFov);

        final TextView tvFov = buildMinimalSliderLabel("FOV: " + (int) fovValue + "°");
        sectionVisuals.addView(tvFov);

        SeekBar sbFov = new SeekBar(this);
        sbFov.setMax(40); // 80 + [0..40] = 80°..120°
        sbFov.setProgress(Math.max(0, Math.min(40, (int) (fovValue - 80.0f))));
        styleMinimalSeekBar(sbFov);
        sbFov.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                fovValue = 80.0f + progress;
                tvFov.setText("FOV: " + (int) fovValue + "°");
                pushConfigToNative();
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        sectionVisuals.addView(sbFov);

        CheckBox cbAntagonist = new CheckBox(this);
        styleMinimalCheckBox(cbAntagonist, "Assassino", showAntagonist);
        cbAntagonist.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                showAntagonist = isChecked;
                pushConfigToNative();
                if (espOverlayView != null) espOverlayView.invalidate();
            }
        });
        sectionVisuals.addView(cbAntagonist);

        CheckBox cbTeammates = new CheckBox(this);
        styleMinimalCheckBox(cbTeammates, "Sobreviventes", showTeammates);
        cbTeammates.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                showTeammates = isChecked;
                pushConfigToNative();
                if (espOverlayView != null) espOverlayView.invalidate();
            }
        });
        sectionVisuals.addView(cbTeammates);

        CheckBox cbPowerNodes = new CheckBox(this);
        styleMinimalCheckBox(cbPowerNodes, "Geradores", showPowerNodes);
        cbPowerNodes.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                showPowerNodes = isChecked;
                pushConfigToNative();
                if (espOverlayView != null) espOverlayView.invalidate();
            }
        });
        sectionVisuals.addView(cbPowerNodes);

        CheckBox cbShrines = new CheckBox(this);
        styleMinimalCheckBox(cbShrines, "Totens", showShrines);
        cbShrines.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                showShrines = isChecked;
                pushConfigToNative();
                if (espOverlayView != null) espOverlayView.invalidate();
            }
        });
        sectionVisuals.addView(cbShrines);

        CheckBox cbContainers = new CheckBox(this);
        styleMinimalCheckBox(cbContainers, "Baús", showContainers);
        cbContainers.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                showContainers = isChecked;
                pushConfigToNative();
                if (espOverlayView != null) espOverlayView.invalidate();
            }
        });
        sectionVisuals.addView(cbContainers);

        CheckBox cbBarriers = new CheckBox(this);
        styleMinimalCheckBox(cbBarriers, "Pallets", showBarriers);
        cbBarriers.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                showBarriers = isChecked;
                pushConfigToNative();
                if (espOverlayView != null) espOverlayView.invalidate();
            }
        });
        sectionVisuals.addView(cbBarriers);

        CheckBox cbPortals = new CheckBox(this);
        styleMinimalCheckBox(cbPortals, "Alçapão / Portões", showPortals);
        cbPortals.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                showPortals = isChecked;
                pushConfigToNative();
                if (espOverlayView != null) espOverlayView.invalidate();
            }
        });
        sectionVisuals.addView(cbPortals);

        CheckBox cbSpecialProps = new CheckBox(this);
        styleMinimalCheckBox(cbSpecialProps, "Objetos", showSpecialProps);
        cbSpecialProps.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                showSpecialProps = isChecked;
                pushConfigToNative();
                if (espOverlayView != null) espOverlayView.invalidate();
            }
        });
        sectionVisuals.addView(cbSpecialProps);

        // ====================================================================
        // ABA 2: FÍSICA E CINEMÁTICA
        // ====================================================================
        final TextView hdrAssist = buildMinimalSliderLabel("Assistência");
        hdrAssist.setTextColor(COL_EMBER);
        hdrAssist.setTypeface(Typeface.DEFAULT_BOLD);
        hdrAssist.setPadding(dp(2), dp(2), dp(2), dp(2));
        sectionKinematics.addView(hdrAssist);

        CheckBox cbAutoSkillcheck = new CheckBox(this);
        styleMinimalCheckBox(cbAutoSkillcheck, "Auto Skillcheck", autoSkillcheck);
        cbAutoSkillcheck.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                autoSkillcheck = isChecked;
                pushConfigToNative();
            }
        });
        sectionKinematics.addView(cbAutoSkillcheck);

        CheckBox cbAutoHeal = new CheckBox(this);
        authAutoHealCb = cbAutoHeal;
        styleMinimalCheckBox(cbAutoHeal, "Auto Heal", autoHeal);
        cbAutoHeal.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                autoHeal = isChecked;
                pushConfigToNative();
            }
        });
        sectionKinematics.addView(cbAutoHeal);

        CheckBox cbTrapImmunity = new CheckBox(this);
        authTrapImmunityCb = cbTrapImmunity;
        styleMinimalCheckBox(cbTrapImmunity, "Imunidade a Traps", trapImmunity);
        cbTrapImmunity.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                trapImmunity = isChecked;
                pushConfigToNative();
            }
        });
        sectionKinematics.addView(cbTrapImmunity);

        final int initSignedFlyPct = Math.max(-100, Math.min(100, Math.round(flyVertSpeed / 6.0f)));
        final String initFlyLabel = (initSignedFlyPct > 0)
            ? String.format(Locale.US, "Voo Vertical: +%d%% (Subir)", initSignedFlyPct)
            : (initSignedFlyPct < 0)
                ? String.format(Locale.US, "Voo Vertical: %d%% (Descer)", initSignedFlyPct)
                : "Voo Vertical: 0% (Segurar)";
        final TextView tvJumpVel = buildMinimalSliderLabel(initFlyLabel);
        final SeekBar sbJumpVel = new SeekBar(this);
        sbJumpVel.setMax(200); // 0..-100%, 100..0%, 200..+100%
        sbJumpVel.setProgress(100 + initSignedFlyPct);
        styleMinimalSeekBar(sbJumpVel);

        final CheckBox cbFlyCheat = new CheckBox(this);
        styleMinimalCheckBox(cbFlyCheat, "Modo Voo (Fly)", flyCheat);
        cbFlyCheat.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                flyCheat = isChecked;
                if (isChecked && Math.abs(flyVertSpeed) < 1.0f) {
                    flyVertSpeed = 300.0f; // +50% (Subir) default so checking Fly has immediate vertical lift
                    jumpPct = 175.0f;
                    sbJumpVel.setProgress(150);
                    tvJumpVel.setText("Voo Vertical: +50% (Subir)");
                }
                pushConfigToNative();
            }
        });
        sectionKinematics.addView(cbFlyCheat);

        Button btnEscape = new Button(this);
        authEscapeBtn = btnEscape;
        btnEscape.setText("Sair Instantâneo");
        btnEscape.setTextColor(COL_TEXT_PRIMARY);
        btnEscape.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9.5f);
        btnEscape.setTypeface(Typeface.DEFAULT_BOLD);
        btnEscape.setAllCaps(false);
        btnEscape.setMinHeight(0);
        btnEscape.setMinimumHeight(0);
        btnEscape.setMinWidth(0);
        btnEscape.setMinimumWidth(0);
        GradientDrawable escBg = new GradientDrawable(
            GradientDrawable.Orientation.TOP_BOTTOM,
            new int[]{Color.argb(230, 224, 38, 66), Color.argb(230, 155, 20, 42)}
        );
        escBg.setCornerRadius(dp(6));
        escBg.setStroke(1, COL_EMBER);
        btnEscape.setBackground(escBg);
        btnEscape.setPadding(dp(12), dp(4), dp(12), dp(4));
        btnEscape.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (crashLogActive) {
                    try {
                        nativeLogModAction("ACTION", "SairInstantaneo", 0L, "clicked");
                    } catch (Throwable ignored) {
                    }
                }
                forceEscape = true;
                pushConfigToNative();
            }
        });
        LinearLayout.LayoutParams escLp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(26));
        escLp.setMargins(dp(2), dp(4), dp(2), dp(6));
        sectionKinematics.addView(btnEscape, escLp);

        View physDivider = new View(this);
        physDivider.setBackgroundColor(COL_BORDER_SUBTLE);
        sectionKinematics.addView(physDivider, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 1));

        final TextView hdrCinematics = buildMinimalSliderLabel("Cinemática");
        hdrCinematics.setTextColor(COL_EMBER);
        hdrCinematics.setTypeface(Typeface.DEFAULT_BOLD);
        hdrCinematics.setPadding(dp(2), dp(4), dp(2), dp(2));
        sectionKinematics.addView(hdrCinematics);

        final CheckBox cbKinematics = new CheckBox(this);
        styleMinimalCheckBox(cbKinematics, "Ativar Multiplicadores", applyKinematics);
        cbKinematics.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                applyKinematics = isChecked;
                pushConfigToNative();
            }
        });
        sectionKinematics.addView(cbKinematics);

        // 1. Velocidade (%): 100% a 250%
        final TextView tvWalkSpeed = buildMinimalSliderLabel(String.format(Locale.US, "Velocidade: %.0f%%", walkSpeedPct));
        sectionKinematics.addView(tvWalkSpeed);

        SeekBar sbWalkSpeed = new SeekBar(this);
        sbWalkSpeed.setMax(150); // 100 + [0..150] = 100%..250%
        sbWalkSpeed.setProgress((int) (walkSpeedPct - 100.0f));
        styleMinimalSeekBar(sbWalkSpeed);
        sbWalkSpeed.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                walkSpeedPct = 100.0f + progress;
                tvWalkSpeed.setText(String.format(Locale.US, "Velocidade: %.0f%%", walkSpeedPct));
                if (fromUser) {
                    if (!applyKinematics) {
                        applyKinematics = true;
                        cbKinematics.setChecked(true);
                    }
                    pushConfigToNative();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) { pushConfigToNative(); }
        });
        sectionKinematics.addView(sbWalkSpeed);

        // 2. Aceleração (%): 100% a 300%
        final TextView tvMaxAccel = buildMinimalSliderLabel(String.format(Locale.US, "Aceleração: %.0f%%", accelPct));
        sectionKinematics.addView(tvMaxAccel);

        SeekBar sbMaxAccel = new SeekBar(this);
        sbMaxAccel.setMax(200); // 100 + [0..200] = 100%..300%
        sbMaxAccel.setProgress((int) (accelPct - 100.0f));
        styleMinimalSeekBar(sbMaxAccel);
        sbMaxAccel.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                accelPct = 100.0f + progress;
                tvMaxAccel.setText(String.format(Locale.US, "Aceleração: %.0f%%", accelPct));
                if (fromUser) {
                    if (!applyKinematics) {
                        applyKinematics = true;
                        cbKinematics.setChecked(true);
                    }
                    pushConfigToNative();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) { pushConfigToNative(); }
        });
        sectionKinematics.addView(sbMaxAccel);

        // 3. Eixo Vertical com Sinal (Voo / Salto): -100% (Descer) .. 0% (Segurar) .. +100% (Subir)
        LinearLayout flyHeaderRow = new LinearLayout(this);
        flyHeaderRow.setOrientation(LinearLayout.HORIZONTAL);
        flyHeaderRow.setGravity(Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams tvFlyLp = new LinearLayout.LayoutParams(
            0, LinearLayout.LayoutParams.WRAP_CONTENT, 1.0f);
        flyHeaderRow.addView(tvJumpVel, tvFlyLp);

        Button btnPauseFly = new Button(this);
        btnPauseFly.setText("Pausar (0%)");
        btnPauseFly.setTextColor(COL_TEXT_PRIMARY);
        btnPauseFly.setTextSize(TypedValue.COMPLEX_UNIT_SP, 8.5f);
        btnPauseFly.setTypeface(Typeface.DEFAULT_BOLD);
        btnPauseFly.setAllCaps(false);
        btnPauseFly.setMinHeight(0);
        btnPauseFly.setMinimumHeight(0);
        btnPauseFly.setMinWidth(0);
        btnPauseFly.setMinimumWidth(0);
        GradientDrawable pauseFlyBg = new GradientDrawable();
        pauseFlyBg.setColor(COL_BG_INNER);
        pauseFlyBg.setCornerRadius(dp(4));
        pauseFlyBg.setStroke(1, COL_BORDER_SUBTLE);
        btnPauseFly.setBackground(pauseFlyBg);
        btnPauseFly.setPadding(dp(6), dp(2), dp(6), dp(2));
        btnPauseFly.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                flyVertSpeed = 0.0f;
                jumpPct = 100.0f;
                sbJumpVel.setProgress(100);
                tvJumpVel.setText("Voo Vertical: 0% (Segurar)");
                pushConfigToNative();
            }
        });
        flyHeaderRow.addView(btnPauseFly);
        sectionKinematics.addView(flyHeaderRow);
        sbJumpVel.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                int signedPct = progress - 100; // -100 .. +100
                if (Math.abs(signedPct) <= 4) {
                    signedPct = 0;
                }
                flyVertSpeed = signedPct * 6.0f; // -600.0 .. +600.0 cm/s
                jumpPct = 100.0f + Math.max(0.0f, signedPct * 1.5f);
                if (signedPct > 0) {
                    tvJumpVel.setText(String.format(Locale.US, "Voo Vertical: +%d%% (Subir)", signedPct));
                } else if (signedPct < 0) {
                    tvJumpVel.setText(String.format(Locale.US, "Voo Vertical: %d%% (Descer)", signedPct));
                } else {
                    tvJumpVel.setText("Voo Vertical: 0% (Segurar)");
                }
                if (fromUser) {
                    if (!flyCheat) {
                        flyCheat = true;
                        cbFlyCheat.setChecked(true);
                    }
                    pushConfigToNative();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) { pushConfigToNative(); }
        });
        sectionKinematics.addView(sbJumpVel);

        // 4. Escala de Tempo (%): 50% a 200%
        final TextView tvTimeScale = buildMinimalSliderLabel(
            String.format(Locale.US, "Escala de Tempo: %.0f%% (%.2fx)", timeScalePct, timeScalePct / 100.0f));
        sectionKinematics.addView(tvTimeScale);

        SeekBar sbTimeScale = new SeekBar(this);
        sbTimeScale.setMax(150); // 50 + [0..150] = 50%..200%
        sbTimeScale.setProgress((int) timeScalePct - 50);
        styleMinimalSeekBar(sbTimeScale);
        sbTimeScale.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                timeScalePct = 50.0f + progress;
                tvTimeScale.setText(String.format(Locale.US, "Escala de Tempo: %.0f%% (%.2fx)", timeScalePct, timeScalePct / 100.0f));
                if (fromUser) {
                    if (!applyKinematics) {
                        applyKinematics = true;
                        cbKinematics.setChecked(true);
                    }
                    pushConfigToNative();
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) { pushConfigToNative(); }
        });
        sectionKinematics.addView(sbTimeScale);

        // ====================================================================
        // ABA 3: IDENTIDADE DO JOGADOR, PAPEL (ROLE) E ESTADO DE IMOBILIZAÇÃO
        // ====================================================================
        CheckBox cbOverrideName = new CheckBox(this);
        styleMinimalCheckBox(cbOverrideName, "Substituir Nome do Jogador", overrideName);
        cbOverrideName.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                overrideName = isChecked;
                pushConfigToNative();
            }
        });
        sectionIdentity.addView(cbOverrideName);

        TextView lblNameHint = buildMinimalSliderLabel("Nome de Exibição Personalizado:");
        sectionIdentity.addView(lblNameHint);

        final EditText etPlayerName = new EditText(this);
        styleMinimalEditText(etPlayerName, "Digite o nome do jogador...");
        if (playerName != null && !playerName.isEmpty()) {
            etPlayerName.setText(playerName);
        }
        etPlayerName.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                if (event.getAction() == MotionEvent.ACTION_DOWN) {
                    setWindowFocusableForKeyboard(true);
                    etPlayerName.requestFocus();
                    InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                    if (imm != null) {
                        imm.showSoftInput(etPlayerName, InputMethodManager.SHOW_FORCED);
                    }
                }
                return false;
            }
        });
        etPlayerName.addTextChangedListener(new TextWatcher() {
            @Override public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
            @Override public void onTextChanged(CharSequence s, int start, int before, int count) {}
            @Override
            public void afterTextChanged(Editable s) {
                playerName = s.toString();
                pushConfigToNative();
            }
        });
        LinearLayout.LayoutParams nameLp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT
        );
        nameLp.topMargin = dp(2);
        nameLp.bottomMargin = dp(4);
        sectionIdentity.addView(etPlayerName, nameLp);

        Button doneTypingBtn = new Button(this);
        doneTypingBtn.setText("Aplicar e Ocultar Teclado");
        doneTypingBtn.setTextColor(COL_TEXT_PRIMARY);
        doneTypingBtn.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9.5f);
        doneTypingBtn.setAllCaps(false);
        doneTypingBtn.setMinHeight(0);
        doneTypingBtn.setMinimumHeight(0);
        doneTypingBtn.setPadding(dp(8), dp(3), dp(8), dp(3));
        GradientDrawable btnBg = new GradientDrawable();
        btnBg.setColor(COL_BG_INNER);
        btnBg.setCornerRadius(dp(4));
        btnBg.setStroke(1, COL_BORDER_ACCENT);
        doneTypingBtn.setBackground(btnBg);
        LinearLayout.LayoutParams btnLp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            dp(26)
        );
        btnLp.bottomMargin = dp(4);
        doneTypingBtn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                if (imm != null) {
                    imm.hideSoftInputFromWindow(rootFloatingView.getWindowToken(), 0);
                }
                setWindowFocusableForKeyboard(false);
                pushConfigToNative();
            }
        });
        sectionIdentity.addView(doneTypingBtn, btnLp);

        // Seletor de Estado de Imobilização (EImmobilizeState)
        final String[] immKeys   = new String[]{"off", "none", "hooked", "trapped", "hiding", "dead", "caged"};
        final String[] immLabels = new String[]{
            "Imobilização: Desativado",
            "Imobilização: Livre (none)",
            "Imobilização: Gancho (hooked)",
            "Imobilização: Armadilha (trapped)",
            "Imobilização: Armário (hiding)",
            "Imobilização: Morto (dead)",
            "Imobilização: Jaula (caged)"
        };
        final Button btnImmobilize = new Button(this);
        authImmobilizeBtn = btnImmobilize;
        btnImmobilize.setText(immLabels[0]);
        btnImmobilize.setTextColor(COL_TEXT_PRIMARY);
        btnImmobilize.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9.5f);
        btnImmobilize.setAllCaps(false);
        btnImmobilize.setMinHeight(0);
        btnImmobilize.setMinimumHeight(0);
        btnImmobilize.setPadding(dp(8), dp(3), dp(8), dp(3));
        GradientDrawable immBg = new GradientDrawable();
        immBg.setColor(COL_BG_INNER);
        immBg.setCornerRadius(dp(4));
        immBg.setStroke(1, COL_BORDER_SUBTLE);
        btnImmobilize.setBackground(immBg);
        btnImmobilize.setOnClickListener(new View.OnClickListener() {
            private int idx = 0;
            @Override
            public void onClick(View v) {
                idx = (idx + 1) % immKeys.length;
                immobilizeState = immKeys[idx];
                btnImmobilize.setText(immLabels[idx]);
                pushConfigToNative();
            }
        });
        LinearLayout.LayoutParams immLp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            dp(26)
        );
        immLp.bottomMargin = dp(4);
        sectionIdentity.addView(btnImmobilize, immLp);

        // Seletor de Papel do Jogador (EPlayerRole)
        final String[] roleKeys   = new String[]{"none", "camper", "slasher", "observer"};
        final String[] roleLabels = new String[]{
            "Papel (Role): Padrão",
            "Papel: Sobrevivente (camper)",
            "Papel: Assassino (slasher)",
            "Papel: Espectador (observer)"
        };
        final Button btnRole = new Button(this);
        authRoleBtn = btnRole;
        btnRole.setText(roleLabels[0]);
        btnRole.setTextColor(COL_TEXT_PRIMARY);
        btnRole.setTextSize(TypedValue.COMPLEX_UNIT_SP, 9.5f);
        btnRole.setAllCaps(false);
        btnRole.setMinHeight(0);
        btnRole.setMinimumHeight(0);
        btnRole.setPadding(dp(8), dp(3), dp(8), dp(3));
        GradientDrawable roleBg = new GradientDrawable();
        roleBg.setColor(COL_BG_INNER);
        roleBg.setCornerRadius(dp(4));
        roleBg.setStroke(1, COL_BORDER_SUBTLE);
        btnRole.setBackground(roleBg);
        btnRole.setOnClickListener(new View.OnClickListener() {
            private int idx = 0;
            @Override
            public void onClick(View v) {
                idx = (idx + 1) % roleKeys.length;
                playerRole = roleKeys[idx];
                btnRole.setText(roleLabels[idx]);
                pushConfigToNative();
            }
        });
        LinearLayout.LayoutParams roleLp = new LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT,
            dp(26)
        );
        roleLp.bottomMargin = dp(4);
        sectionIdentity.addView(btnRole, roleLp);

        content.addView(sectionVisuals);
        content.addView(sectionKinematics);
        content.addView(sectionIdentity);

        scrollView.addView(
            content,
            new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT
            )
        );
        final int screenH = getResources().getDisplayMetrics().heightPixels;
        final int maxScrollHeight = Math.min(dp(220), Math.max(dp(150), (int) (screenH * 0.50f)));
        expandedPanel.addView(scrollView, new LinearLayout.LayoutParams(dp(250), maxScrollHeight));

        // Arrastar janela pelo cabeçalho e pelo ícone flutuante 34dp x 34dp
        headerBar.setOnTouchListener(new View.OnTouchListener() {
            private int initialX;
            private int initialY;
            private float touchX;
            private float touchY;

            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        initialX = windowParams.x;
                        initialY = windowParams.y;
                        touchX = event.getRawX();
                        touchY = event.getRawY();
                        return true;
                    case MotionEvent.ACTION_MOVE:
                        windowParams.x = initialX + (int) (event.getRawX() - touchX);
                        windowParams.y = initialY + (int) (event.getRawY() - touchY);
                        windowManager.updateViewLayout(rootFloatingView, windowParams);
                        return true;
                    default:
                        return false;
                }
            }
        });

        collapsedBubbleView.setOnTouchListener(new View.OnTouchListener() {
            private int initialX;
            private int initialY;
            private float touchX;
            private float touchY;
            private boolean moved;

            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        initialX = windowParams.x;
                        initialY = windowParams.y;
                        touchX = event.getRawX();
                        touchY = event.getRawY();
                        moved = false;
                        return true;
                    case MotionEvent.ACTION_MOVE:
                        final int dx = (int) (event.getRawX() - touchX);
                        final int dy = (int) (event.getRawY() - touchY);
                        if (Math.abs(dx) > dp(5) || Math.abs(dy) > dp(5)) {
                            moved = true;
                        }
                        windowParams.x = initialX + dx;
                        windowParams.y = initialY + dy;
                        windowManager.updateViewLayout(rootFloatingView, windowParams);
                        return true;
                    case MotionEvent.ACTION_UP:
                        if (!moved) {
                            collapsedBubbleView.setVisibility(View.GONE);
                            expandedPanel.setVisibility(View.VISIBLE);
                        }
                        return true;
                    default:
                        return false;
                }
            }
        });

        container.addView(collapsedBubbleView);
        container.addView(expandedPanel);
        rootFloatingView = container;

        windowManager.addView(rootFloatingView, windowParams);
    }

    private void toggleCrashLogging() {
        if (!crashLogActive) {
            final String prefDir = "/sdcard/DBDMod/logs";
            final File extLogs = getExternalFilesDir("logs");
            final String fallbackDir = (extLogs != null) ? extLogs.getAbsolutePath() : "";
            String resultPath;
            try {
                resultPath = nativeStartCrashLogger(prefDir, fallbackDir);
            } catch (Throwable t) {
                resultPath = "ERR:" + t.getMessage();
            }

            if (resultPath == null || resultPath.startsWith("ERR:") || resultPath.isEmpty()) {
                final String msg = "Erro ao iniciar logs: " + (resultPath != null ? resultPath : "desconhecido");
                crashLogPathNotice.setText(msg);
                crashLogPathNotice.setTextColor(COL_EMBER);
                crashLogPathNotice.setVisibility(View.VISIBLE);
                Toast.makeText(this, msg, Toast.LENGTH_LONG).show();
                return;
            }

            crashLogActive = true;
            crashLogPath = resultPath;
            lastLoggedGpPayload = "";
            crashLogButton.setText("REC");
            GradientDrawable recBg = new GradientDrawable();
            recBg.setColor(Color.argb(215, 34, 118, 56));
            recBg.setCornerRadius(dp(5));
            recBg.setStroke(1, COL_BORDER_SUBTLE);
            crashLogButton.setBackground(recBg);

            final boolean usedFallback = !resultPath.startsWith(prefDir);
            final String notice = usedFallback
                ? ("Gravando (alternativo): " + resultPath)
                : ("Gravando logs em: " + resultPath);
            crashLogPathNotice.setText(notice);
            crashLogPathNotice.setTextColor(Color.rgb(120, 230, 145));
            crashLogPathNotice.setVisibility(View.VISIBLE);
            Toast.makeText(this, notice, Toast.LENGTH_LONG).show();
            pushConfigToNative();
        } else {
            crashLogActive = false;
            lastLoggedGpPayload = "";
            crashLogButton.setText("LOG");
            GradientDrawable idleBg = new GradientDrawable();
            idleBg.setColor(Color.argb(215, 185, 24, 38));
            idleBg.setCornerRadius(dp(5));
            idleBg.setStroke(1, COL_BORDER_ACCENT);
            crashLogButton.setBackground(idleBg);

            final String stoppedMsg = "Logs encerrados: " + crashLogPath;
            crashLogPathNotice.setText(stoppedMsg);
            crashLogPathNotice.setTextColor(COL_TEXT_MUTED);
            crashLogPathNotice.setVisibility(View.VISIBLE);
            Toast.makeText(this, stoppedMsg, Toast.LENGTH_SHORT).show();
            new Thread(new Runnable() {
                @Override
                public void run() {
                    try {
                        nativeStopCrashLogger();
                    } catch (Throwable ignored) {
                    }
                    uiHandler.post(new Runnable() {
                        @Override
                        public void run() {
                            pushConfigToNative();
                        }
                    });
                }
            }).start();
        }
    }

    private void updateAuthorityGateUi(String rawStatus) {
        final boolean inMatch = rawStatus != null && rawStatus.contains("IN MATCH (ACTIVE)");
        final boolean isClientNoAuth = inMatch && rawStatus.contains("Auth=0");
        final boolean allowAuth = !isClientNoAuth;
        final float alpha = allowAuth ? 1.0f : 0.45f;

        if (authAutoHealCb != null) {
            authAutoHealCb.setEnabled(allowAuth);
            authAutoHealCb.setAlpha(alpha);
        }
        if (authTrapImmunityCb != null) {
            authTrapImmunityCb.setEnabled(allowAuth);
            authTrapImmunityCb.setAlpha(alpha);
        }
        if (authEscapeBtn != null) {
            authEscapeBtn.setEnabled(allowAuth);
            authEscapeBtn.setAlpha(alpha);
        }
        if (authImmobilizeBtn != null) {
            authImmobilizeBtn.setEnabled(allowAuth);
            authImmobilizeBtn.setAlpha(alpha);
        }
        if (authRoleBtn != null) {
            authRoleBtn.setEnabled(allowAuth);
            authRoleBtn.setAlpha(alpha);
        }
        if (authGateNoticeView != null) {
            if (isClientNoAuth) {
                authGateNoticeView.setText(
                    "Modo Cliente (Auth=0 no GameState): escritas de estado desativadas; apenas predição (Aura/FOV/Velocidade) ativa."
                );
                authGateNoticeView.setVisibility(View.VISIBLE);
            } else {
                authGateNoticeView.setVisibility(View.GONE);
            }
        }
    }

    @Override
    public void onDestroy() {
        serviceActive = false;
        uiHandler.removeCallbacksAndMessages(null);
        super.onDestroy();
        if (windowManager != null) {
            if (espOverlayView != null) {
                try {
                    windowManager.removeView(espOverlayView);
                } catch (Throwable ignored) {
                }
            }
            if (rootFloatingView != null) {
                try {
                    windowManager.removeView(rootFloatingView);
                } catch (Throwable ignored) {
                }
            }
        }
    }
}
