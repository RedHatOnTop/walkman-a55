package org.walkmanui.browser;

import android.app.Activity;
import android.app.DownloadManager;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.text.InputType;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup.LayoutParams;
import android.view.Window;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.webkit.CookieManager;
import android.webkit.URLUtil;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.Toast;

/**
 * One tab on the system WebView. On this 467 MB player Firefox's Gecko ran three
 * processes and pushed the kernel into swap; the WebView renders inside this one
 * (devlog/2026-10-22-webview-browser.md).
 */
public class BrowserActivity extends Activity {
    private static final String SEARCH = "https://html.duckduckgo.com/html/?q=";
    private static final int BAR = 0xFF15171B, FIELD = 0xFF262A31, TEXT = 0xFFECEDEE, HINT = 0xFF8A9099;
    private static final int ORANGE = 0xFFFF6600;
    private static final int PICK_FILE = 1;

    private WebView web;
    private EditText address;
    private ProgressLine progress;
    private ReloadButton reload;
    private ValueCallback<Uri[]> picking;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        Window window = getWindow();
        window.setStatusBarColor(BAR);
        window.setNavigationBarColor(0xFF000000);

        GradientDrawable pill = new GradientDrawable();
        pill.setColor(FIELD);
        pill.setCornerRadius(dp(18));
        address = new EditText(this);
        address.setBackground(pill);
        address.setPadding(dp(14), 0, dp(14), 0);
        address.setSingleLine(true);
        address.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_URI);
        address.setImeOptions(EditorInfo.IME_ACTION_GO);
        address.setSelectAllOnFocus(true);
        address.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        address.setTextColor(TEXT);
        address.setHintTextColor(HINT);
        address.setHint(R.string.address_hint);
        address.setOnEditorActionListener((v, action, event) -> {
            load(v.getText().toString());
            return true;
        });

        reload = new ReloadButton(this);
        reload.setOnClickListener(v -> {
            if (reload.loading)
                web.stopLoading();
            else
                web.reload();
        });

        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        bar.setBackgroundColor(BAR);
        bar.setPadding(dp(8), dp(8), dp(4), dp(8));
        bar.addView(address, new LinearLayout.LayoutParams(0, dp(36), 1));
        LinearLayout.LayoutParams button = new LinearLayout.LayoutParams(dp(40), dp(40));
        button.leftMargin = dp(4);
        bar.addView(reload, button);

        progress = new ProgressLine(this);
        progress.setBackgroundColor(BAR);

        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setUseWideViewPort(true);
        s.setLoadWithOverviewMode(true);
        s.setBuiltInZoomControls(true);
        s.setDisplayZoomControls(false);
        web.setWebViewClient(new WebViewClient() {
            @Override
            public void onPageStarted(WebView view, String url, Bitmap favicon) {
                if (!address.hasFocus())
                    address.setText(url);
                reload.setLoading(true);
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                reload.setLoading(false);
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onProgressChanged(WebView view, int p) {
                progress.set(p);
            }

            @Override
            public boolean onShowFileChooser(WebView view, ValueCallback<Uri[]> callback, FileChooserParams params) {
                if (picking != null)
                    picking.onReceiveValue(null);
                picking = callback;
                try {
                    startActivityForResult(params.createIntent(), PICK_FILE);
                } catch (ActivityNotFoundException e) {
                    picking = null;
                    Toast.makeText(BrowserActivity.this, R.string.no_file_picker, Toast.LENGTH_SHORT).show();
                    return false;
                }
                return true;
            }
        });
        web.setDownloadListener((url, agent, disposition, mime, length) -> {
            String name = URLUtil.guessFileName(url, disposition, mime);
            DownloadManager.Request r = new DownloadManager.Request(Uri.parse(url));
            r.setMimeType(mime);
            r.addRequestHeader("User-Agent", agent);
            String cookies = CookieManager.getInstance().getCookie(url);
            if (cookies != null)
                r.addRequestHeader("Cookie", cookies);
            r.setNotificationVisibility(DownloadManager.Request.VISIBILITY_VISIBLE_NOTIFY_COMPLETED);
            r.setDestinationInExternalPublicDir(Environment.DIRECTORY_DOWNLOADS, name);
            ((DownloadManager) getSystemService(DOWNLOAD_SERVICE)).enqueue(r);
            Toast.makeText(this, getString(R.string.downloading, name), Toast.LENGTH_SHORT).show();
        });

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.addView(bar, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
        root.addView(progress, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, dp(2)));
        root.addView(web, new LinearLayout.LayoutParams(LayoutParams.MATCH_PARENT, 0, 1));
        setContentView(root);

        if (state != null)
            web.restoreState(state);
        else if (!open(getIntent()))
            address.requestFocus();
    }

    private int dp(float v) {
        return Math.round(TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, getResources().getDisplayMetrics()));
    }

    private boolean open(Intent intent) {
        Uri data = intent == null ? null : intent.getData();
        if (data == null)
            return false;
        load(data.toString());
        return true;
    }

    private void load(String text) {
        String t = text.trim();
        if (t.isEmpty())
            return;
        String url;
        if (t.matches("^[a-zA-Z][a-zA-Z0-9+.-]*://.*") || t.startsWith("about:"))
            url = t;
        else if (!t.contains(" ") && t.contains("."))
            url = "https://" + t;
        else
            url = SEARCH + Uri.encode(t);
        address.setText(url);
        address.clearFocus();
        web.requestFocus();
        ((InputMethodManager) getSystemService(INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(address.getWindowToken(), 0);
        web.loadUrl(url);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request == PICK_FILE && picking != null) {
            picking.onReceiveValue(WebChromeClient.FileChooserParams.parseResult(result, data));
            picking = null;
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        open(intent);
    }

    @Override
    public void onBackPressed() {
        if (web.canGoBack())
            web.goBack();
        else
            super.onBackPressed();
    }

    @Override
    protected void onSaveInstanceState(Bundle state) {
        super.onSaveInstanceState(state);
        web.saveState(state);
    }

    @Override
    protected void onPause() {
        web.onPause();
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        web.onResume();
    }

    @Override
    protected void onDestroy() {
        web.destroy();
        super.onDestroy();
    }

    /** The page's load as an orange line under the bar, gone when the page is in. */
    private static final class ProgressLine extends View {
        private final Paint paint = new Paint();
        private int percent = 100;

        ProgressLine(Context context) {
            super(context);
            paint.setColor(ORANGE);
        }

        void set(int p) {
            percent = p;
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            if (percent < 100)
                canvas.drawRect(0, 0, getWidth() * Math.max(percent, 5) / 100f, getHeight(), paint);
        }
    }

    /** Reload, drawn as an open circle with its arrowhead; a cross while a page loads. */
    private final class ReloadButton extends View {
        private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Path head = new Path();
        private final RectF arc = new RectF();
        boolean loading;

        ReloadButton(Context context) {
            super(context);
            stroke.setStyle(Paint.Style.STROKE);
            stroke.setStrokeWidth(dp(2));
            stroke.setStrokeCap(Paint.Cap.ROUND);
            stroke.setColor(TEXT);
            fill.setColor(TEXT);
            setBackground(new RippleDrawable(ColorStateList.valueOf(0x33FFFFFF), null, null));
            setLoading(false);
        }

        void setLoading(boolean on) {
            loading = on;
            setContentDescription(getString(on ? R.string.stop : R.string.reload));
            invalidate();
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float cx = getWidth() / 2f, cy = getHeight() / 2f, r = dp(7);
            if (loading) {
                canvas.drawLine(cx - r, cy - r, cx + r, cy + r, stroke);
                canvas.drawLine(cx - r, cy + r, cx + r, cy - r, stroke);
                return;
            }
            arc.set(cx - r, cy - r, cx + r, cy + r);
            canvas.drawArc(arc, -60, 300, false, stroke);
            float tipX = cx + r * 0.5f, tipY = cy - r * 0.866f, a = dp(4);
            head.reset();
            head.moveTo(tipX + a, tipY);
            head.lineTo(tipX - a * 0.5f, tipY - a * 0.9f);
            head.lineTo(tipX - a * 0.5f, tipY + a * 0.9f);
            head.close();
            canvas.drawPath(head, fill);
        }
    }
}
