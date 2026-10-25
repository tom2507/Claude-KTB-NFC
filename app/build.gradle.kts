plugins { id("com.android.application") }
android {
    namespace = "de.ktbos.nfckey"
    compileSdk = 35
    defaultConfig {
        applicationId = "de.ktbos.nfckey"
        minSdk = 28
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"
    }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
}
