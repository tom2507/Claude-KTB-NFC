package de.ktbos.nfckey;

import android.app.Activity;
import android.os.Bundle;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Button;
import android.content.Context;
import android.content.SharedPreferences;
import java.security.SecureRandom;

public class MainActivity extends Activity {
    private static final String PREFS="ktb_key_prefs";
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout=new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(32,48,32,32);
        TextView title=new TextView(this);
        title.setText("KTB NFC Key 0.1\n\nHCE-Testdienst installiert.\n\nAID: F04B54424E464331\n\nDie App muss beim späteren Vorhalten nicht geöffnet werden.\n\nNur Testbetrieb – noch keine Security-Freigabe.");
        title.setTextSize(19);
        layout.addView(title);
        TextView status=new TextView(this);
        status.setPadding(0,30,0,20);
        layout.addView(status);
        Button setup=new Button(this);
        setup.setText("Testschlüssel einmalig erzeugen");
        layout.addView(setup);
        setup.setOnClickListener(v -> {
            byte[] key=readKey(this);
            if(key==null) {
                key=new byte[32]; new SecureRandom().nextBytes(key);
                getSharedPreferences(PREFS,MODE_PRIVATE).edit().putString("key",toHex(key)).apply();
            }
            status.setText("Testschlüssel vorhanden (32 Byte). Für die spätere Kopplung mit dem ESP32 brauchen wir einen sicheren Provisionierungsweg.");
        });
        status.setText(readKey(this)==null ? "Noch kein Testschlüssel angelegt." : "Testschlüssel vorhanden.");
        setContentView(layout);
    }
    static byte[] readKey(Context ctx) {
        String hex=ctx.getSharedPreferences(PREFS,Context.MODE_PRIVATE).getString("key",null);
        if(hex==null || hex.length()!=64) return null;
        byte[] out=new byte[32];
        try { for(int i=0;i<32;i++) out[i]=(byte)Integer.parseInt(hex.substring(i*2,i*2+2),16); }
        catch(Exception ex) {return null;}
        return out;
    }
    private static String toHex(byte[] bytes) {
        StringBuilder sb=new StringBuilder();
        for(byte b:bytes) sb.append(String.format(java.util.Locale.ROOT,"%02X",b&255));
        return sb.toString();
    }
}
