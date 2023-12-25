package com.yaheen.fayconnectordemo;

import android.content.Context;
import java.io.BufferedReader;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileReader;
import java.io.FileWriter;
import java.io.IOException;
import java.util.HashMap;
import java.util.Map;

public class KVUtils {

    private static final String FILE_NAME = "kv_data.txt";

    // 写入数据
    public static void writeData(Context context, String key, String value) {
        File file = new File(context.getFilesDir(), FILE_NAME);
        Map<String, String> dataMap = new HashMap<>();

        // 先读取现有的内容到 Map
        BufferedReader reader = null;
        try {
            reader = new BufferedReader(new FileReader(file));
            String line;
            while ((line = reader.readLine()) != null) {
                String[] parts = line.split("=", 2);
                if (parts.length >= 2) {
                    dataMap.put(parts[0], parts[1]);
                }
            }
        } catch (IOException e) {
            e.printStackTrace();
        } finally {
            if (reader != null) {
                try {
                    reader.close();
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }
        }

        // 更新或添加新的键值对
        dataMap.put(key, value);

        // 将更新后的数据写回文件
        BufferedWriter writer = null;
        try {
            writer = new BufferedWriter(new FileWriter(file, false)); // false 以覆盖方式写入
            for (Map.Entry<String, String> entry : dataMap.entrySet()) {
                writer.write(entry.getKey() + "=" + entry.getValue());
                writer.newLine();
            }
        } catch (IOException e) {
            e.printStackTrace();
        } finally {
            if (writer != null) {
                try {
                    writer.close();
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }
        }
    }


    // 读取数据
    public static String readData(Context context, String key) {
        File file = new File(context.getFilesDir(), FILE_NAME);
        BufferedReader reader = null;
        try {
            reader = new BufferedReader(new FileReader(file));
            String line;
            while ((line = reader.readLine()) != null) {
                String[] parts = line.split("=", 2);
                if (parts.length >= 2 && parts[0].equals(key)) {
                    return parts[1];
                }
            }
        } catch (IOException e) {
            e.printStackTrace();
        } finally {
            if (reader != null) {
                try {
                    reader.close();
                } catch (IOException e) {
                    e.printStackTrace();
                }
            }
        }
        return null;
    }
}
