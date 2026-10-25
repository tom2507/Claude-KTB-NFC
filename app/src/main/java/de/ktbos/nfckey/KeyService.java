package de.ktbos.nfckey;

import android.nfc.cardemulation.HostApduService;
import android.os.Bundle;
import java.security.SecureRandom;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

/** Prototype protocol: SELECT AID -> 9000; 80 10 00 00 10 <16-byte challenge> 00 -> 32-byte HMAC + 9000. */
public class KeyService extends HostApduService {
    private static final byte[] AID = hex("F04B54424E464331");
    private static final byte[] OK = hex("9000");
    private static final byte[] BAD = hex("6A80");
    private static final byte[] NOT_FOUND = hex("6A82");
    private static final byte[] WRONG_LENGTH = hex("6700");
    private boolean selected = false;

    @Override public byte[] processCommandApdu(byte[] command, Bundle extras) {
        if (command == null || command.length < 4) return BAD;
        // ISO 7816 SELECT by DF name (AID)
        if ((command[0]&255)==0 && (command[1]&255)==0xA4 && (command[2]&255)==4 && (command[3]&255)==0) {
            selected = command.length >= 5 + AID.length && (command[4]&255)==AID.length;
            for (int i=0; selected && i<AID.length; i++) if (command[5+i]!=AID[i]) selected=false;
            return selected ? OK : NOT_FOUND;
        }
        if (!selected) return NOT_FOUND;
        if (command.length != 22 || (command[0]&255)!=0x80 || (command[1]&255)!=0x10 || command[2]!=0 || command[3]!=0 || (command[4]&255)!=16 || command[21]!=0) return WRONG_LENGTH;
        byte[] key = MainActivity.readKey(this);
        if (key == null) return BAD;
        try {
            Mac mac = Mac.getInstance("HmacSHA256");
            mac.init(new SecretKeySpec(key,"HmacSHA256"));
            byte[] payload = new byte["KTB-NFC-v1".length()+16];
            byte[] domain = "KTB-NFC-v1".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
            System.arraycopy(domain,0,payload,0,domain.length);
            System.arraycopy(command,5,payload,domain.length,16);
            byte[] digest = mac.doFinal(payload);
            byte[] response = new byte[34];
            System.arraycopy(digest,0,response,0,32);
            response[32]=(byte)0x90; response[33]=0;
            return response;
        } catch (Exception ex) { return BAD; }
    }
    @Override public void onDeactivated(int reason) { selected=false; }
    private static byte[] hex(String s) {
        byte[] b=new byte[s.length()/2];
        for(int i=0;i<b.length;i++) b[i]=(byte)Integer.parseInt(s.substring(2*i,2*i+2),16);
        return b;
    }
}
