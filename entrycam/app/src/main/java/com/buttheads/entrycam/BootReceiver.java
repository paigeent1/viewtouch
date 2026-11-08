package com.buttheads.entrycam;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** Starts the camera when the TV boots, and again after the app is updated. */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        WatchService.start(context);
        WatchService.showCamera(context);
    }
}
