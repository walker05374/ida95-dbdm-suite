package com.dbdm.modmenu;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.widget.Toast;

public final class MainActivity extends Activity {
    private static final int REQ_OVERLAY_PERMISSION = 1001;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M && !Settings.canDrawOverlays(this)) {
            Intent intent = new Intent(
                Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:" + getPackageName())
            );
            startActivityForResult(intent, REQ_OVERLAY_PERMISSION);
            Toast.makeText(
                this,
                "Conceda a permissão 'Sobrepor a outros apps' para o Wdbd",
                Toast.LENGTH_LONG
            ).show();
            return;
        }
        launchOverlayAndGame();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_OVERLAY_PERMISSION) {
            if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M || Settings.canDrawOverlays(this)) {
                launchOverlayAndGame();
            } else {
                Toast.makeText(
                    this,
                    "A permissão de sobreposição é necessária para o Wdbd",
                    Toast.LENGTH_LONG
                ).show();
                finish();
            }
        }
    }

    private void launchOverlayAndGame() {
        Intent svc = new Intent(this, FloatingModService.class);
        startService(svc);

        Intent launchIntent = getPackageManager().getLaunchIntentForPackage("com.netease.dbdena");
        if (launchIntent != null) {
            launchIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(launchIntent);
        } else {
            Toast.makeText(this, "Wdbd ativo", Toast.LENGTH_SHORT).show();
        }
        finish();
    }
}
