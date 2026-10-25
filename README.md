# KTB NFC Key 0.1 – Android HCE prototype

Experimental Android HCE app for Samsung S24 Ultra + PN532. No Android Studio needed: upload project files to a GitHub repository and run **Actions → Android APK → Run workflow**. Download the resulting **ktb-nfc-key-debug-apk** artifact; unzip it to obtain `app-debug.apk`. Android will ask permission to install from your selected file manager. The APK is signed with a disposable debug key; do not use it as a production access credential.

## Prototype APDU protocol

- AID: `F04B54424E464331`
- SELECT APDU: `00 A4 04 00 08 F0 4B 54 42 4E 46 43 31 00`
- Successful SELECT response: `90 00`
- Challenge APDU: `80 10 00 00 10 <16 random bytes> 00`
- Response: `HMAC-SHA256(key, ASCII("KTB-NFC-v1") || challenge) || 90 00` (34 bytes)
- A new unpredictable challenge is required for each authentication; the verifier must compare HMAC in constant time and rate-limit attempts.

## Important limitations

- Prototype only; **DO NOT use to disarm security**. There is no ESP32 APDU reader integration, secure key enrollment, device binding, replay protection on the verifier, or end-to-end testing yet.
- The generated key is currently held in private app preferences (not Android Keystore); this is intentionally a lab prototype, **not production-grade secret storage**. A secure provisioning flow and Keystore-backed architecture are required before real use.
- Screen-on / locked-screen HCE behavior depends on Samsung/Android OS and must be verified on the actual device. `requireDeviceUnlock=false` is a request, not a guarantee.
- The PN532 ESPHome `pn532_spi` YAML currently reports UIDs only; APDU exchange requires an additional reader-side implementation.
- No guarantee of build success until GitHub Actions runs; if build fails, inspect the workflow logs.
