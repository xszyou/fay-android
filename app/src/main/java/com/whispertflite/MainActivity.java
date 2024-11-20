package com.whispertflite;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.floatingactionbutton.FloatingActionButton;
import com.whispertflite.asr.Player;
import com.whispertflite.entity.ChatResponse;
import com.whispertflite.entity.Message;
import com.whispertflite.utils.ApiService;
import com.whispertflite.utils.WaveUtil;
import com.whispertflite.asr.Recorder;
import com.whispertflite.asr.Whisper;
import com.whispertflite.view.MessageAdapter;
import com.whispertflite.fay.KVUtils;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.DateFormat;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Timer;
import java.util.TimerTask;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import okhttp3.MediaType;
import androidx.recyclerview.widget.LinearLayoutManager;



public class MainActivity extends AppCompatActivity {
    private final String TAG = "WhisperMainActivity";

    private TextView tvStatus;
    private FloatingActionButton fabCopy;
    private FloatingActionButton btnRecord;
    private FloatingActionButton sendButton;
    private FloatingActionButton btnConnect;
    private FloatingActionButton btnFay;

    private EditText etSendText;

    private RecyclerView recyclerView;
    private MessageAdapter adapter;
    private List<Message> messageList = new ArrayList<>();

    private Recorder mRecorder = null;
    private Whisper mWhisper = null;

    private File selectedWaveFile = null;
    private File selectedTfliteFile = null;
    private File sdcardDataFolder = null;

    public static final String ACTION_UPDATE_MESSAGES = "com.whispertflite.ACTION_UPDATE_MESSAGES";
    private final BroadcastReceiver updateMessagesReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            // 调用 updateMessages() 方法
            updateMessages();
        }
    };


    private final Handler handler = new Handler(Looper.getMainLooper());

    @SuppressLint("ClickableViewAccessibility")
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // 注册广播接收器
        IntentFilter filter = new IntentFilter(ACTION_UPDATE_MESSAGES);
        registerReceiver(updateMessagesReceiver, filter, Context.RECEIVER_NOT_EXPORTED);

        // Call the method to copy specific file types from assets to data folder
        sdcardDataFolder = this.getExternalFilesDir(null);
        String[] extensionsToCopy = {"tflite", "bin", "wav", "pcm"};
        copyAssetsToSdcard(this, sdcardDataFolder, extensionsToCopy);

        // 指定默认的 tflite 文件和 wav 文件
        selectedTfliteFile = new File(sdcardDataFolder, "whisper-tiny.tflite"); // 替换为你的默认模型文件名
        selectedWaveFile = new File(sdcardDataFolder, "MicInput.wav"); // 替换为你的默认音频文件名
        // 初始化模型
        deinitModel();
        initModel(selectedTfliteFile);


        // Implementation of record button functionality
        btnRecord = findViewById(R.id.btnRecord);
        btnRecord.setOnTouchListener(new View.OnTouchListener() {
            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        // 按下时触发的逻辑
                        Log.d(TAG, "Start recording...");
                        startRecording();
                        return true; // 表示事件被处理
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        // 松开或取消时触发的逻辑
                        Log.d(TAG, "Recording is in progress... stopping...");
                        stopRecording();
                        startTranscription(selectedWaveFile.getAbsolutePath());
                        return true; // 表示事件被处理
                }
                return false; // 其他情况不处理
            }
        });

        tvStatus = findViewById(R.id.tvStatus);

        // Audio recording functionality
        mRecorder = new Recorder(this);
        mRecorder.setListener(new Recorder.RecorderListener() {
            @Override
            public void onUpdateReceived(String message) {
                Log.d(TAG, "Update is received, Message: " + message);
                handler.post(() -> tvStatus.setText(message));
                if (message.equals(Recorder.MSG_RECORDING)) {

                } else if (message.equals(Recorder.MSG_RECORDING_DONE)) {
                }
            }

            @Override
            public void onDataReceived(float[] samples) {
//                mWhisper.writeBuffer(samples);
            }
        });

        // Assume this Activity is the current activity, check record permission
        checkPermissions();

        // for debugging
//        testParallelProcessing();
        etSendText = findViewById(R.id.sendText);
        sendButton = findViewById(R.id.sendButton);
        sendButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                String text = etSendText.getText().toString();
                if (text != null && !text.trim().equals("")) {
                    // Clear the input field
                    etSendText.setText("");
                    etSendText.postDelayed(() -> etSendText.clearFocus(), 100); // 延迟清空焦点以确保 UI 正确更新


                    // Create a message object for the sent message and add it to the list
                    Message sentMessage = new Message();
                    sentMessage.setType("member");
                    sentMessage.setWay("speak");
                    sentMessage.setContent(text);
                    sentMessage.setCreatetime(System.currentTimeMillis() / 1000.0);
                    sentMessage.setTimetext(new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss").format(new java.util.Date()));
                    sentMessage.setUsername("User"); // Assuming the username is "User"
                    sentMessage.setId(messageList.size() + 1); // Assign a unique ID
                    sentMessage.setIsAdopted(0);

                    // Update the UI to show the sent message
//                    runOnUiThread(new Runnable() {
//                        @Override
//                        public void run() {
//                            messageList.add(sentMessage);  // 将数据的添加也放在主线程中
//                            recyclerView.getAdapter().notifyItemInserted(messageList.size() - 1);
//                            recyclerView.scrollToPosition(messageList.size() - 1);
//                        }
//                    });
                    updateMessages();

                    // Send message to the API
                    try {
                        ApiService.chatWithGpt(text, KVUtils.readData(getApplicationContext(), "HttpServerAddress") + "/v1/chat/completions", new ApiService.ChatCallback() {
                            @Override
                            public void onSuccess(ChatResponse response) {
                                String replyContent = response.getChoices().get(0).getMessage().getContent();

                                // Create a message object for the received message and add it to the list
                                Message receivedMessage = new Message();
                                receivedMessage.setType("fay");
                                receivedMessage.setWay("speak");
                                receivedMessage.setContent(replyContent);
                                receivedMessage.setCreatetime(System.currentTimeMillis() / 1000.0);
                                receivedMessage.setTimetext(new java.text.SimpleDateFormat("yyyy-MM-dd HH:mm:ss").format(new java.util.Date()));
                                receivedMessage.setUsername("Fay"); // Assuming the bot's username is "Fay"
                                receivedMessage.setId(messageList.size() + 1); // Assign a unique ID
                                receivedMessage.setIsAdopted(0);

                                // Update the UI to show the received message
//                                runOnUiThread(new Runnable() {
//                                    @Override
//                                    public void run() {
//                                        messageList.add(receivedMessage);  // 将数据的添加也放在主线程中
//                                        recyclerView.getAdapter().notifyItemInserted(messageList.size() - 1);
//                                        recyclerView.scrollToPosition(messageList.size() - 1);
//                                    }
//                                });
                                updateMessages();
                            }

                            @Override
                            public void onFailure(Exception e) {
                                runOnUiThread(new Runnable() {
                                    @Override
                                    public void run() {
                                        Toast.makeText(MainActivity.this, "请求失败: " + e.getMessage(), Toast.LENGTH_SHORT).show();
                                    }
                                });
                            }
                        });
                    }catch (Exception e){

                    }
                }
            }
        });


        recyclerView = findViewById(R.id.recycler_view);
        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        adapter = new MessageAdapter(messageList);
        recyclerView.setAdapter(adapter);

        btnConnect = findViewById(R.id.btnConnect);
        btnConnect.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                updateMessages();
            }
        });
        btnFay = findViewById(R.id.btnFay);
        btnFay.setOnClickListener(new View.OnClickListener(){

            @Override
            public void onClick(View view) {
                Intent intent = new Intent(MainActivity.this, com.whispertflite.fay.MainActivity.class);
                intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(intent);
            }
        });


    }

    private void updateMessages(){
        // Fetch messages
        try {
            ApiService.fetchMessages("User", KVUtils.readData(getApplicationContext(), "HttpServerAddress") + "/api/get-msg", new ApiService.MessageCallback() {
                @Override
                public void onSuccess(List<Message> messages) {
                    messageList.clear();
                    messageList.addAll(messages);
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            // 执行 UI 更新操作，例如 notifyDataSetChanged()
                            recyclerView.getAdapter().notifyDataSetChanged();
                            recyclerView.scrollToPosition(messageList.size() - 1);
                        }
                    });

                    //30秒后的递归调用
                    TimerTask task = new TimerTask() {
                        @Override
                        public void run() {
                            updateMessages();
                        }
                    };
                    new Timer().schedule(task, 30000);

                    // 设置屏幕常亮
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
                        }
                    });

                }

                @Override
                public void onError(Exception e) {
                    runOnUiThread(new Runnable() {
                        @Override
                        public void run() {
                            Toast.makeText(MainActivity.this, "请求失败: " + e.getMessage(), Toast.LENGTH_SHORT).show();
                        }
                    });
                }
            });
        }catch (Exception e){

        }

    }

    // Model initialization
    private void initModel(File tfliteFile) {
        File vocabFile;
        boolean isMultilingualModel;
        if (tfliteFile.getName().endsWith("en.tflite")) {
            // English-only model
            isMultilingualModel = false;
            vocabFile = new File(sdcardDataFolder, "filters_vocab_en.bin");
        } else {
            // Multilingual model
            isMultilingualModel = true;
            vocabFile = new File(sdcardDataFolder, "filters_vocab_multilingual.bin");
        }

        mWhisper = new Whisper(this);
        mWhisper.loadModel(tfliteFile, vocabFile, isMultilingualModel);
        mWhisper.setListener(new Whisper.WhisperListener() {
            @Override
            public void onUpdateReceived(String message) {
                Log.d(TAG, "Update is received, Message: " + message);
                handler.post(() -> tvStatus.setText(message));

                if (message.equals(Whisper.MSG_PROCESSING)) {
                } else if (message.equals(Whisper.MSG_FILE_NOT_FOUND)) {
                    // write code as per need to handled this error
                    Log.d(TAG, "File not found error...!");
                }
            }

            @Override
            public void onResultReceived(String result) {
                Log.d(TAG, "Result: " + result);
                handler.post(() -> etSendText.setText(result));
            }
        });
    }

    private void deinitModel() {
        if (mWhisper != null) {
            //mWhisper.unload();
            mWhisper = null;
        }
    }

    private @NonNull ArrayAdapter<File> getFileArrayAdapter(ArrayList<File> waveFiles) {
        ArrayAdapter<File> adapter = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, waveFiles) {
            @Override
            public View getView(int position, View convertView, ViewGroup parent) {
                View view = super.getView(position, convertView, parent);
                TextView textView = view.findViewById(android.R.id.text1);
                textView.setText(getItem(position).getName());  // Show only the file name
                return view;
            }

            @Override
            public View getDropDownView(int position, View convertView, ViewGroup parent) {
                View view = super.getDropDownView(position, convertView, parent);
                TextView textView = view.findViewById(android.R.id.text1);
                textView.setText(getItem(position).getName());  // Show only the file name
                return view;
            }
        };
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        return adapter;
    }

    private void checkPermissions() {
        String[] permissions = {
                Manifest.permission.RECORD_AUDIO,
                Manifest.permission.WRITE_EXTERNAL_STORAGE,
                Manifest.permission.INTERNET
        };

        ArrayList<String> missingPermissions = new ArrayList<>();
        for (String permission : permissions) {
            if (ContextCompat.checkSelfPermission(this, permission) != PackageManager.PERMISSION_GRANTED) {
                missingPermissions.add(permission);
            }
        }

        if (!missingPermissions.isEmpty()) {
            requestPermissions(missingPermissions.toArray(new String[0]), 0);
        } else {
            Log.d(TAG, "All permissions granted.");
        }
    }


    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            Log.d(TAG, "Record permission is granted");
        } else {
            Log.d(TAG, "Record permission is not granted");
        }
    }

    // Recording calls
    private void startRecording() {
        checkPermissions();

        File waveFile= new File(sdcardDataFolder, WaveUtil.RECORDING_FILE);
        mRecorder.setFilePath(waveFile.getAbsolutePath());
        mRecorder.start();
    }

    private void stopRecording() {
        mRecorder.stop();
    }

    // Transcription calls
    private void startTranscription(String waveFilePath) {
        mWhisper.setFilePath(waveFilePath);
        mWhisper.setAction(Whisper.ACTION_TRANSCRIBE);
        mWhisper.start();
    }

    // Copy assets with specified extensions to destination folder
    private static void copyAssetsToSdcard(Context context, File destFolder, String[] extensions) {
        AssetManager assetManager = context.getAssets();

        try {
            // List all files in the assets folder once
            String[] assetFiles = assetManager.list("");
            if (assetFiles == null) return;

            for (String assetFileName : assetFiles) {
                // Check if file matches any of the provided extensions
                for (String extension : extensions) {
                    if (assetFileName.endsWith("." + extension)) {
                        File outFile = new File(destFolder, assetFileName);

                        // Skip if file already exists
                        if (outFile.exists()) break;

                        // Copy the file from assets to the destination folder
                        try (InputStream inputStream = assetManager.open(assetFileName);
                             OutputStream outputStream = new FileOutputStream(outFile)) {

                            byte[] buffer = new byte[1024];
                            int bytesRead;
                            while ((bytesRead = inputStream.read(buffer)) != -1) {
                                outputStream.write(buffer, 0, bytesRead);
                            }
                        }
                        break; // No need to check further extensions
                    }
                }
            }
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    public ArrayList<File> getFilesWithExtension(File directory, String extension) {
        ArrayList<File> filteredFiles = new ArrayList<>();

        // Check if the directory is accessible
        if (directory != null && directory.exists()) {
            File[] files = directory.listFiles();

            // Filter files by the provided extension
            if (files != null) {
                for (File file : files) {
                    if (file.isFile() && file.getName().endsWith(extension)) {
                        filteredFiles.add(file);
                    }
                }
            }
        }

        return filteredFiles;
    }


    public void addMessage(String message, boolean isSentByUser) {
        Message mObject = new Message();
        mObject.setContent(message);
        mObject.setCreatetime((long)(new Date().getTime() / 1000));
        LocalDateTime currentTime = LocalDateTime.now();
        DateTimeFormatter formatter = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");
        String formattedTime = currentTime.format(formatter);
        mObject.setTimetext(formattedTime);

        // 根据消息来源设置不同样式
        if (isSentByUser) {
            mObject.setType("User");
        } else {
            mObject.setType("fay");
        }
        messageList.add(mObject);
        // 在主线程更新UI
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                recyclerView.getAdapter().notifyDataSetChanged();
                // 滚动到最后一条消息
                recyclerView.scrollToPosition(messageList.size() - 1);
            }
        });
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // 注销广播接收器
        unregisterReceiver(updateMessagesReceiver);
    }


}