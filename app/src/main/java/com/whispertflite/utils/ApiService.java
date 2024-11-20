package com.whispertflite.utils;
import android.os.Handler;
import android.os.Looper;

import com.google.gson.Gson;
import com.whispertflite.entity.ChatResponse;
import com.whispertflite.entity.Message;
import com.whispertflite.entity.MessageListResponse;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.IOException;
import java.util.List;
import java.util.concurrent.TimeUnit;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

public class ApiService {
    private static final String HOST = "192.168.3.213";
    private static final String URL = String.format("http://%s:5000/api/get-msg", HOST);
    private static final MediaType JSON = MediaType.parse("application/json; charset=utf-8");

    private static final String OPENAI_API_URL = String.format("http://%s:5000/v1/chat/completions", HOST);
    private static final String OPENAI_API_KEY = "YOUR_OPENAI_API_KEY_HERE";

    public static void chatWithGpt(String prompt, String url, final ChatCallback callback) {
        // 创建 OkHttpClient，设置请求超时
        OkHttpClient client = new OkHttpClient.Builder()
                .connectTimeout(10, TimeUnit.SECONDS)
                .writeTimeout(10, TimeUnit.SECONDS)
                .readTimeout(30, TimeUnit.SECONDS)
                .build();

        // 使用 JSONObject 创建 JSON 请求体
        JSONObject jsonObject = new JSONObject();
        try {
            jsonObject.put("model", "fay");  // 使用的模型

            // 构建 messages 数组
            JSONArray messages = new JSONArray();
            JSONObject message = new JSONObject();
            message.put("role", "user");
            message.put("content", prompt);
            messages.put(message);
            jsonObject.put("messages", messages);

            jsonObject.put("observation", "");  // 视情况填写实际值
        } catch (Exception e) {
            e.printStackTrace();
            callback.onFailure(e);
            return;
        }

        RequestBody body = RequestBody.create(jsonObject.toString(), MediaType.parse("application/json"));

        // 创建请求
        Request request = new Request.Builder()
                .url(url)
                .post(body)
                .addHeader("Authorization", "Bearer " + OPENAI_API_KEY)
                .addHeader("Content-Type", "application/json")
                .build();

        // 异步请求
        client.newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                // 打印错误信息便于调试
                e.printStackTrace();
                // 通过回调传递错误信息，而不是直接抛出异常
                new Handler(Looper.getMainLooper()).post(() -> callback.onFailure(new Exception("Network request failed: " + e.getMessage())));
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                if (response.isSuccessful()) {
                    String jsonResponse = response.body().string();
                    new Handler(Looper.getMainLooper()).post(() -> {
                        try {
                            Gson gson = new Gson();
                            ChatResponse chatResponse = gson.fromJson(jsonResponse, ChatResponse.class);
                            if (chatResponse != null && chatResponse.getChoices() != null && !chatResponse.getChoices().isEmpty()) {
                                callback.onSuccess(chatResponse);
                            } else {
                                callback.onFailure(new Exception("Invalid response from OpenAI"));
                            }
                        } catch (Exception e) {
                            e.printStackTrace();
                            callback.onFailure(e);
                        }
                    });
                } else {
                    new Handler(Looper.getMainLooper()).post(() -> {
                        // 打印失败状态码和消息
                        System.err.println("Failed to fetch GPT response: " + response.code() + " " + response.message());
                        callback.onFailure(new Exception("Failed to fetch GPT response, status code: " + response.code()));
                    });
                }
            }
        });
    }

    public interface ChatCallback {
        void onSuccess(ChatResponse response);
        void onFailure(Exception e);
    }

    public static void fetchMessages(String username, String url, final MessageCallback callback) {
        // 创建 OkHttpClient，设置请求超时
        OkHttpClient client = new OkHttpClient.Builder()
                .connectTimeout(10, TimeUnit.SECONDS)
                .writeTimeout(10, TimeUnit.SECONDS)
                .readTimeout(30, TimeUnit.SECONDS)
                .build();

        // 使用 JSONObject 创建 JSON 请求体
        JSONObject jsonObject = new JSONObject();
        try {
            jsonObject.put("username", username);  // 与 Flask 端对应的键为 "username"
        } catch (Exception e) {
            callback.onError(e);
            return;
        }

        RequestBody body = RequestBody.create(JSON, jsonObject.toString());
        Request request = new Request.Builder()
                .url(url)
                .post(body)
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override
            public void onFailure(Call call, IOException e) {
                // 打印错误信息便于调试
                e.printStackTrace();
                // 通过回调传递错误信息，而不是直接抛出异常
                callback.onError(new Exception("Network request failed: " + e.getMessage()));
            }

            @Override
            public void onResponse(Call call, Response response) throws IOException {
                if (response.isSuccessful()) {
                    String jsonResponse = response.body().string();
                    Gson gson = new Gson();
                    MessageListResponse messageListResponse = gson.fromJson(jsonResponse, MessageListResponse.class);
                    if (messageListResponse != null && messageListResponse.getList() != null) {
                        callback.onSuccess(messageListResponse.getList());
                    } else {
                        callback.onError(new Exception("Invalid response format"));
                    }
                } else {
                    // 打印失败状态码和消息
                    System.err.println("Failed to fetch messages: " + response.code() + " " + response.message());
                    callback.onError(new Exception("Failed to fetch messages, status code: " + response.code()));
                }
            }
        });
    }

    public interface MessageCallback {
        void onSuccess(List<Message> messages);
        void onError(Exception e);
    }
}
