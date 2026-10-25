package de.ktbos.nfckey;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.Typeface;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;
import java.security.SecureRandom;

public class MainActivity extends Activity {
    private static final String PREFS = "ktb_key_prefs";
    private TextView status;
    private TextView keyView;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(32, 48, 32, 32);

        TextView title = new TextView(this);
        title.setText("KTB NFC Key 0.1\n\nHCE-Testdienst installiert.\n\nAID: F04B54424E464331\n\nDie App muss beim späteren Vorhalten nicht geöffnet werden.\n\nNur Testbetrieb – noch keine Security-Freigabe.");
        title.setTextSize(19);
        layout.addView(title);

        status = new TextView(this);
        status.setPadding(0, 30, 0, 20);
        layout.addView(status);

        keyView = new TextView(this);
        keyView.setTypeface(Typeface.MONOSPACE);
        keyView.setTextSize(15);
        keyView.setTextIsSelectable(true);
        keyView.setPadding(0, 0, 0, 20);
        layout.addView(keyView);

        Button setup = new Button(this);
        setup.setText("Testschlüssel einmalig erzeugen");
        layout.addView(setup);
        setup.setOnClickListener(v -> {
            if (readKey(this) == null) {
                byte[] key = new byte[32];
                new SecureRandom().nextBytes(key);
                getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString("key", toHex(key)).apply();
            }
            refresh();
        });

        Button copy = new Button(this);
        copy.setText("Schlüssel kopieren (für ESPHome secrets.yaml)");
        layout.addView(copy);
        copy.setOnClickListener(v -> {
            byte[] key = readKey(this);
            if (key == null) {
                Toast.makeText(this, "Noch kein Schlüssel vorhanden", Toast.LENGTH_SHORT).show();
                return;
            }
            ClipboardManager cm = (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            cm.setPrimaryClip(ClipData.newPlainText("KTB NFC Key", toHex(key)));
            Toast.makeText(this, "Schlüssel kopiert", Toast.LENGTH_SHORT).show();
        });

        refresh();
        ScrollView scroll = new ScrollView(this);
        scroll.addView(layout);
        setContentView(scroll);
    }

    private void refresh() {
        byte[] key = readKey(this);
        if (key == null) {
            status.setText("Noch kein Testschlüssel angelegt.");
            keyView.setText("");
        } else {
            status.setText("Testschlüssel vorhanden (32 Byte, 64 Hex-Zeichen). Nur für den Labortest: Er gehört in die ESPHome-secrets.yaml und sonst nirgendwohin.");
            keyView.setText(toHex(key));
        }
    }

    static byte[] readKey(Context ctx) {
        String hex = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString("key", null);
        if (hex == null || hex.length() != 64) return null;
        byte[] out = new byte[32];
        try { for (int i = 0; i < 32; i++) out[i] = (byte) Integer.parseInt(hex.substring(i * 2, i * 2 + 2), 16); }
        catch (Exception ex) { return null; }
        return out;
    }

    private static String toHex(byte[] bytes) {
        StringBuilder sb = new StringBuilder();
        for (byte b : bytes) sb.append(String.format(java.util.Locale.ROOT, "%02X", b & 255));
        return sb.toString();
    }
}
