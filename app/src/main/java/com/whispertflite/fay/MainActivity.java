package com.whispertflite.fay;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.Manifest;
import android.app.ActivityManager;
import android.app.PendingIntent;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.View;
import android.widget.Button;
import android.widget.CompoundButton;
import android.widget.EditText;
import android.widget.Switch;
import android.widget.TextView;
import android.widget.Toast;

import com.google.android.material.snackbar.Snackbar;
import com.whispertflite.R;

import java.util.ArrayList;
import java.util.List;

public class MainActivity extends AppCompatActivity {
    private TextView tv = null;
    private EditText httpServerAddress = null;
    private EditText socketServerAddress = null;
    private Switch microphoneSwitch = null;
    private boolean running = false;
    private Intent serviceIntent = null;
    // 定义请求权限的请求码
    private static final int REQUEST_PERMISSIONS = 100;

    @Override
    protected void onResume() {
        super.onResume();
        String serverAddressStr = KVUtils.readData(getApplicationContext(), "SocketServerAddress");
        socketServerAddress.setText(serverAddressStr == null ? "192.168.1.101:10001" :  KVUtils.readData(getApplicationContext(), "SocketServerAddress"));
        microphoneSwitch.setChecked(Boolean.parseBoolean(KVUtils.readData(getApplicationContext(), "IsMic")));
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.fay_activity_main);

        tv = this.findViewById(R.id.tv);
        socketServerAddress = this.findViewById(R.id.socket_server_address);
        httpServerAddress = this.findViewById(R.id.http_server_address);
        microphoneSwitch = this.findViewById(R.id.microphone_switch);

        String socketServerAddressStr = KVUtils.readData(getApplicationContext(), "SocketServerAddress");
        socketServerAddress.setText(socketServerAddressStr == null ? "192.168.1.101:10001" : socketServerAddressStr);
        String httpServerAddressStr = KVUtils.readData(getApplicationContext(), "HttpServerAddress");
        httpServerAddress.setText(socketServerAddressStr == null ? "http://192.168.1.101:5000" : httpServerAddressStr);
        microphoneSwitch.setChecked(Boolean.parseBoolean(KVUtils.readData(getApplicationContext(), "IsMic")));

        serviceIntent = new Intent(this, FayConnectorService.class);

        // 返回按钮
        Button btnBack = findViewById(R.id.btn_back);
        btnBack.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                finish();
            }
        });

        // 按钮点击事件
        tv.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                Log.d("fay", "onclick");
                String address = socketServerAddress.getText().toString();
                if (address == null || address.isEmpty()) {
                    Toast.makeText(MainActivity.this, "请输入服务器地址", Toast.LENGTH_SHORT).show();
                    return;
                }
                // 记录服务器信息
                KVUtils.writeData(getApplicationContext(), "SocketServerAddress", address);

                running = FayConnectorService.running;
                if (!running) { // 启动服务
                    // 构建需要请求的权限列表
                    List<String> permissionsNeeded = new ArrayList<>();

                    // 检查 RECORD_AUDIO 权限
                    if (ContextCompat.checkSelfPermission(MainActivity.this, Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
                        permissionsNeeded.add(Manifest.permission.RECORD_AUDIO);
                    }

                    // 检查 FOREGROUND_SERVICE_MICROPHONE 权限（Android 10 及以上）
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                        if (ContextCompat.checkSelfPermission(MainActivity.this, Manifest.permission.FOREGROUND_SERVICE) != PackageManager.PERMISSION_GRANTED) {
                            permissionsNeeded.add(Manifest.permission.FOREGROUND_SERVICE);
                        }
                    }

                    // 检查 POST_NOTIFICATIONS 权限（Android 13 及以上）
                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        if (ContextCompat.checkSelfPermission(MainActivity.this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
                            permissionsNeeded.add(Manifest.permission.POST_NOTIFICATIONS);
                        }
                    }

                    if (!permissionsNeeded.isEmpty()) {
                        // 请求权限
                        ActivityCompat.requestPermissions(MainActivity.this, permissionsNeeded.toArray(new String[0]), REQUEST_PERMISSIONS);
                    } else {
                        // 所有权限已被授予，启动服务
                        startFayConnectorService(view);
                    }
                } else { // 停止服务
                    stopService(serviceIntent);
                    Snackbar.make(view, "已经断开 fay 控制器", Snackbar.LENGTH_SHORT)
                            .setAction("Action", null).show();
                    running = false;
                }
            }
        });



        // 开关状态更改事件
        microphoneSwitch.setOnCheckedChangeListener(new CompoundButton.OnCheckedChangeListener() {
            @Override
            public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                Intent intent = new Intent(FayConnectorService.ACTION_CONTROL_MIC);
                intent.putExtra("mic", isChecked);
                sendBroadcast(intent);//通知service麦克风状态改变
                KVUtils.writeData(getApplicationContext(), "IsMic", isChecked + "");//记录状态

            }
        });

        socketServerAddress.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence charSequence, int i, int i1, int i2) {

            }

            @Override
            public void onTextChanged(CharSequence charSequence, int i, int i1, int i2) {
                String address = socketServerAddress.getText().toString();
                if (address == null) {
                    return;
                }
                //记录服务器信息
                KVUtils.writeData(getApplicationContext(), "SocketServerAddress", address);
            }

            @Override
            public void afterTextChanged(Editable editable) {

            }
        });

        httpServerAddress.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence charSequence, int i, int i1, int i2) {

            }

            @Override
            public void onTextChanged(CharSequence charSequence, int i, int i1, int i2) {
                String address = httpServerAddress.getText().toString();
                if (address == null) {
                    return;
                }
                //记录服务器信息
                KVUtils.writeData(getApplicationContext(), "HttpServerAddress", address);
            }

            @Override
            public void afterTextChanged(Editable editable) {

            }
        });
    }

    // 启动服务的方法
    private void startFayConnectorService(View view) {
        Log.d("fay", "权限 ok");
        Snackbar.make(view, "正在连接 fay 控制器", Snackbar.LENGTH_SHORT)
                .setAction("Action", null).show();
        serviceIntent.putExtra("mic", microphoneSwitch.isChecked());
        ContextCompat.startForegroundService(MainActivity.this, serviceIntent);
        running = true;
    }

    // 处理权限请求结果
    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_PERMISSIONS) {
            boolean allGranted = true;
            for (int result : grantResults) {
                if (result != PackageManager.PERMISSION_GRANTED) {
                    allGranted = false;
                    break;
                }
            }
            if (allGranted) {
                // 所有权限已被授予，启动服务
                startFayConnectorService(tv); // 传入按钮视图
            } else {
                // 权限被拒绝，显示提示或处理逻辑
                Toast.makeText(MainActivity.this, "需要授予所有权限才能正常运行", Toast.LENGTH_SHORT).show();
            }
        }
    }

    public static String bytesToHexString(byte[] data){
        String result="";
        for (int i = 0; i < data.length; i++) {
            result+=Integer.toHexString((data[i] & 0xFF) | 0x100).toUpperCase().substring(1, 3);
        }
        return result;
    }


    public static byte[] decodeHexBytes(char[] data) {
        int len = data.length;
        if ((len & 0x01) != 0) {
            throw new RuntimeException("未知的字符");
        }
        byte[] out = new byte[len >> 1];
        for (int i = 0, j = 0; j < len; i++) {
            int f = toDigit(data[j], j) << 4;
            j++;
            f = f | toDigit(data[j], j);
            j++;
            out[i] = (byte) (f & 0xFF);
        }
        return out;
    }

    protected static int toDigit(char ch, int index) {
        int digit = Character.digit(ch, 16);
        if (digit == -1) {
            throw new RuntimeException("非法16进制字符 " + ch
                    + " 在索引 " + index);
        }
        return digit;
    }

    private boolean isServiceRunning() {
        ActivityManager activityManager = (ActivityManager) this.getApplicationContext()
                .getSystemService(Context.ACTIVITY_SERVICE);
        ComponentName serviceName = new ComponentName("com.yaheen.fayconnectordemo", ".FayConnectorService");
        PendingIntent intent = activityManager.getRunningServiceControlPanel(serviceName);
        if (intent == null){
            return false;
        }
        return true;

    }
}