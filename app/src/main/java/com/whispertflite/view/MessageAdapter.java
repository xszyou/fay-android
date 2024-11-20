package com.whispertflite.view;

import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import com.whispertflite.R;
import com.whispertflite.entity.Message;

import java.util.List;

public class MessageAdapter extends RecyclerView.Adapter<MessageAdapter.MessageViewHolder> {
    private List<Message> messageList;

    public MessageAdapter(List<Message> messageList) {
        this.messageList = messageList;
    }

    @NonNull
    @Override
    public MessageViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext()).inflate(R.layout.item_message, parent, false);
        return new MessageViewHolder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull MessageViewHolder holder, int position) {
        Message message = messageList.get(position);
        holder.contentTextView.setText(message.getContent());
        holder.timeTextView.setText(message.getTimetext());

        // 根据消息的类型设置对齐方式
        if ("fay".equals(message.getType())) {
            // 左对齐
            holder.contentTextView.setTextAlignment(View.TEXT_ALIGNMENT_TEXT_START);
            holder.timeTextView.setTextAlignment(View.TEXT_ALIGNMENT_TEXT_START);

            // 设置左侧的 margin
            ViewGroup.MarginLayoutParams layoutParams = (ViewGroup.MarginLayoutParams) holder.itemView.getLayoutParams();
            layoutParams.setMarginStart(16); // 左侧的 margin
            layoutParams.setMarginEnd(0); // 右侧的 margin
            holder.itemView.setLayoutParams(layoutParams);
        } else if ("member".equals(message.getType())) {
            // 右对齐
            holder.contentTextView.setTextAlignment(View.TEXT_ALIGNMENT_TEXT_END);
            holder.timeTextView.setTextAlignment(View.TEXT_ALIGNMENT_TEXT_END);

            // 设置右侧的 margin
            ViewGroup.MarginLayoutParams layoutParams = (ViewGroup.MarginLayoutParams) holder.itemView.getLayoutParams();
            layoutParams.setMarginStart(0); // 左侧的 margin
            layoutParams.setMarginEnd(16); // 右侧的 margin
            holder.itemView.setLayoutParams(layoutParams);
        }

    }

    @Override
    public int getItemCount() {
        return messageList.size();
    }

    public static class MessageViewHolder extends RecyclerView.ViewHolder {
        TextView contentTextView;
        TextView timeTextView;

        public MessageViewHolder(@NonNull View itemView) {
            super(itemView);
            contentTextView = itemView.findViewById(R.id.content);
            timeTextView = itemView.findViewById(R.id.time);
        }
    }
}
